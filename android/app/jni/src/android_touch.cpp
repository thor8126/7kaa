// Android: touchscreen controls.
//
// Fingers are translated into the same mouse events the desktop build gets
// from SDL, so all game code stays unchanged:
//
//   tap                      -> left click
//   long press, then lift    -> right click (move / attack orders); the phone
//                               vibrates when the press is long enough
//   two-finger tap           -> right click at the first finger
//   one-finger drag          -> left button drag (box select, sliders), also
//                               after holding still first
//   two-finger drag          -> pan the map
//   Back button              -> Esc
//   "Right click" on-screen toggle (SevenKaaActivity): taps are right clicks
//
// Every action happens exactly under the finger: the touch point is mapped
// straight to 800x600 game coordinates, and there is no cursor to steer (it is
// not drawn on Android, see MouseCursor in src/OMOUSECR.cpp). The window is
// never grabbed on Android (Vga::is_input_grabbed), so nothing clamps the
// position and there is no edge scrolling.
//
// Touches that start in the black bars beside the game screen are ignored
// (that is where the on-screen buttons live).
//
// A real mouse (Bluetooth, USB, DeX, Chromebook) keeps working through the
// normal SDL mouse events in Vga::handle_messages().

#include <jni.h>
#include <math.h>

#include <OMOUSE.h>
#include "android_glue.h"

enum TouchState
{
	TOUCH_IDLE,      // no finger down
	TOUCH_PENDING,   // one finger down, not yet a tap, drag or long press
	TOUCH_DRAG,      // one finger dragging with the left button held
	TOUCH_TWO,       // two fingers: tap = right click, move = pan
	TOUCH_IGNORE,    // gesture finished, wait until all fingers are up
};

static const Uint32 LONG_PRESS_MS = 450;
static const Uint32 TWO_FINGER_TAP_MS = 400;
static const int    MAP_LOC_PIXELS = 32;      // size of a map location on screen

static TouchState state = TOUCH_IDLE;
static int fingers_down;
static SDL_FingerID finger1, finger2;
static Uint32 down_time;
static float start_wx, start_wy;      // finger1 start, window pixels
static int start_lx, start_ly;        // finger1 start, game (800x600) pixels
static float f1_wx, f1_wy, f2_wx, f2_wy;
static float pan_last_x, pan_last_y;  // centroid, game pixels
static float pan_acc_x, pan_acc_y;
static float pan_total;
static int panned;
static int long_press_armed;          // held still long enough: lift = right click
static volatile int right_click_mode;   // set from the Java UI thread

extern "C" JNIEXPORT void JNICALL
Java_com_thor8126_sevenkaa_SevenKaaActivity_nativeSetRightClickMode(JNIEnv *, jclass, jboolean on)
{
	right_click_mode = on ? 1 : 0;
}

//------------------------------------------------------------------------

static SDL_Window *touch_window(const SDL_TouchFingerEvent &f)
{
	SDL_Window *win = SDL_GetWindowFromID(f.windowID);
	if( !win )
		win = SDL_GetKeyboardFocus();
	return win;
}

// Window pixels of a finger event.
static void finger_to_window(const SDL_TouchFingerEvent &f, float *wx, float *wy)
{
	int w = 1, h = 1;
	SDL_Window *win = touch_window(f);
	if( win )
		SDL_GetWindowSize(win, &w, &h);
	*wx = f.x * w;
	*wy = f.y * h;
}

// Window coordinates -> game screen coordinates (800x600). Uses SDL's own
// conversion, which also accounts for the window size differing from the
// drawable's pixel size (dpi_scale) and for the letterboxing done by
// SDL_RenderSetLogicalSize(): the same math SDL applies to mouse events.
static void window_to_game(SDL_Window *win, float wx, float wy, float *gx, float *gy)
{
	int w = 800, h = 600, lw = 0, lh = 0;
	SDL_Renderer *ren = win ? SDL_GetRenderer(win) : NULL;
	if( win )
		SDL_GetWindowSize(win, &w, &h);
	if( ren )
		SDL_RenderGetLogicalSize(ren, &lw, &lh);

	if( ren && lw > 0 && lh > 0 )
	{
		SDL_RenderWindowToLogical(ren, (int) (wx + 0.5f), (int) (wy + 0.5f), gx, gy);
	}
	else
	{
		*gx = wx * 800.0f / (w > 0 ? w : 1);
		*gy = wy * 600.0f / (h > 0 ? h : 1);
	}
}

static void game_pos(const SDL_TouchFingerEvent &f, int *x, int *y)
{
	float wx, wy, gx, gy;
	finger_to_window(f, &wx, &wy);
	window_to_game(touch_window(f), wx, wy, &gx, &gy);
	int ix = (int) gx, iy = (int) gy;
	*x = ix < 0 ? 0 : (ix > 799 ? 799 : ix);
	*y = iy < 0 ? 0 : (iy > 599 ? 599 : iy);
}

// Is the finger on the game screen (not in the letterbox bars)?
static int on_game_screen(const SDL_TouchFingerEvent &f)
{
	float wx, wy, gx, gy;
	finger_to_window(f, &wx, &wy);
	window_to_game(touch_window(f), wx, wy, &gx, &gy);
	return gx >= -2 && gx < 802 && gy >= -2 && gy < 602;
}

static float move_threshold(const SDL_TouchFingerEvent &f)
{
	int w = 0, h = 0;
	SDL_Window *win = touch_window(f);
	if( win )
		SDL_GetWindowSize(win, &w, &h);
	float t = h * 0.03f;
	return t < 12.0f ? 12.0f : t;
}

//------------------------------------------------------------------------
// Temporary debug aid: a red cross where the game thinks the finger is,
// drawn on the final frame (Vga::flip), so one test on a phone shows whether
// touches map to the right spot. Set to 0 to turn it off.
#define TOUCH_DEBUG_CROSS 1

static int dbg_x = -1, dbg_y = -1;
static Uint32 dbg_time;

static void move_cursor(int x, int y)
{
	mouse.process_mouse_motion(x, y);
	dbg_x = x;
	dbg_y = y;
	dbg_time = SDL_GetTicks();
}

void android_touch_draw_debug(SDL_Surface *surface)
{
#if TOUCH_DEBUG_CROSS
	if( dbg_x < 0 || !surface || SDL_GetTicks() - dbg_time > 1500 )
		return;
	Uint32 black = SDL_MapRGB(surface->format, 0, 0, 0);
	Uint32 red   = SDL_MapRGB(surface->format, 255, 32, 32);
	const int len = 12;
	SDL_Rect r;
	r = { dbg_x - len - 1, dbg_y - 2, 2 * len + 3, 5 };  SDL_FillRect(surface, &r, black);
	r = { dbg_x - 2, dbg_y - len - 1, 5, 2 * len + 3 };  SDL_FillRect(surface, &r, black);
	r = { dbg_x - len, dbg_y - 1, 2 * len + 1, 3 };      SDL_FillRect(surface, &r, red);
	r = { dbg_x - 1, dbg_y - len, 3, 2 * len + 1 };      SDL_FillRect(surface, &r, red);
#else
	(void) surface;
#endif
}

// Lifting a finger must not leave the cursor on the screen border, where the
// game would keep edge-scrolling the map.
static void park_cursor()
{
	int x = mouse.cur_x, y = mouse.cur_y;
	if( x < 1 ) x = 1;
	if( x > 798 ) x = 798;
	if( y < 1 ) y = 1;
	if( y > 598 ) y = 598;
	if( x != mouse.cur_x || y != mouse.cur_y )
		move_cursor(x, y);
}

// Left button up. Like the desktop's SDL_MOUSEBUTTONUP handler
// (Vga::handle_messages), this frees the cursor: pressing in the map view
// confines the cursor to it (Power::detect_frame), and without the reset every
// later tap would be pulled back into the map view.
static void release_left()
{
	mouse.add_event(LEFT_BUTTON_RELEASE);
	mouse.reset_boundary();
}

static void click(MouseEventType down, MouseEventType up, int x, int y)
{
	move_cursor(x, y);
	mouse.add_event(down);
	if( up == LEFT_BUTTON_RELEASE )
		release_left();
	else
		mouse.add_event(up);
}

// Short vibration so the player feels when a long press became a right click.
static void haptic_tick()
{
	JNIEnv *env = (JNIEnv*) SDL_AndroidGetJNIEnv();
	if( !env )
		return;
	jclass cls = env->FindClass("com/thor8126/sevenkaa/SevenKaaActivity");
	if( cls )
	{
		jmethodID mid = env->GetStaticMethodID(cls, "hapticTick", "()V");
		if( mid )
			env->CallStaticVoidMethod(cls, mid);
		env->DeleteLocalRef(cls);
	}
	if( env->ExceptionCheck() )
		env->ExceptionClear();
}

static void centroid_game(const SDL_TouchFingerEvent &f, float *gx, float *gy)
{
	window_to_game(touch_window(f), (f1_wx + f2_wx) / 2, (f1_wy + f2_wy) / 2, gx, gy);
}

//------------------------------------------------------------------------

static void finger_down(const SDL_TouchFingerEvent &f)
{
	fingers_down++;

	if( state == TOUCH_IDLE )
	{
		if( !on_game_screen(f) )
			return;
		state = TOUCH_PENDING;
		long_press_armed = 0;
		finger1 = f.fingerId;
		down_time = SDL_GetTicks();
		finger_to_window(f, &start_wx, &start_wy);
		f1_wx = start_wx;
		f1_wy = start_wy;
		game_pos(f, &start_lx, &start_ly);
		// a new touch can land anywhere: no button is held, so nothing may
		// confine the cursor
		mouse.reset_boundary();
		move_cursor(start_lx, start_ly);
	}
	else if( state == TOUCH_PENDING && f.fingerId != finger1 )
	{
		state = TOUCH_TWO;
		finger2 = f.fingerId;
		finger_to_window(f, &f2_wx, &f2_wy);
		centroid_game(f, &pan_last_x, &pan_last_y);
		pan_acc_x = pan_acc_y = 0;
		pan_total = 0;
		panned = 0;
	}
	// a third finger, or a second one during a drag, is ignored
}

static void finger_motion(const SDL_TouchFingerEvent &f)
{
	if( state == TOUCH_PENDING && f.fingerId == finger1 )
	{
		float wx, wy;
		finger_to_window(f, &wx, &wy);
		f1_wx = wx;
		f1_wy = wy;
		if( hypotf(wx - start_wx, wy - start_wy) > move_threshold(f) )
		{
			// start a left-button drag from where the finger went down
			move_cursor(start_lx, start_ly);
			mouse.add_event(LEFT_BUTTON);
			int x, y;
			game_pos(f, &x, &y);
			move_cursor(x, y);
			state = TOUCH_DRAG;
		}
	}
	else if( state == TOUCH_DRAG && f.fingerId == finger1 )
	{
		int x, y;
		game_pos(f, &x, &y);
		move_cursor(x, y);
	}
	else if( state == TOUCH_TWO && (f.fingerId == finger1 || f.fingerId == finger2) )
	{
		if( f.fingerId == finger1 )
			finger_to_window(f, &f1_wx, &f1_wy);
		else
			finger_to_window(f, &f2_wx, &f2_wy);

		float cx, cy;
		centroid_game(f, &cx, &cy);
		float dx = cx - pan_last_x, dy = cy - pan_last_y;
		pan_last_x = cx;
		pan_last_y = cy;
		pan_total += fabsf(dx) + fabsf(dy);
		if( !panned && pan_total > 8.0f )
			panned = 1;
		if( panned )
		{
			// Drag the map with the fingers: moving right shows what is
			// to the left. The game consumes mouse.scroll_x/y in
			// World::detect_scroll() and resets them.
			pan_acc_x -= dx;
			pan_acc_y -= dy;
			int sx = (int) (pan_acc_x / MAP_LOC_PIXELS);
			int sy = (int) (pan_acc_y / MAP_LOC_PIXELS);
			if( sx || sy )
			{
				pan_acc_x -= sx * MAP_LOC_PIXELS;
				pan_acc_y -= sy * MAP_LOC_PIXELS;
				mouse.scroll_x += sx;
				mouse.scroll_y += sy;
			}
		}
	}
}

static void finger_up(const SDL_TouchFingerEvent &f)
{
	if( fingers_down > 0 )
		fingers_down--;

	switch( state )
	{
	case TOUCH_PENDING:
		if( f.fingerId == finger1 )
		{
			if( right_click_mode || long_press_armed )
				click(RIGHT_BUTTON, RIGHT_BUTTON_RELEASE, start_lx, start_ly);
			else
				click(LEFT_BUTTON, LEFT_BUTTON_RELEASE, start_lx, start_ly);
			park_cursor();
			state = TOUCH_IDLE;
		}
		break;

	case TOUCH_DRAG:
		if( f.fingerId == finger1 )
		{
			int x, y;
			game_pos(f, &x, &y);
			move_cursor(x, y);
			release_left();
			park_cursor();
			state = TOUCH_IGNORE;
		}
		break;

	case TOUCH_TWO:
		if( f.fingerId == finger1 || f.fingerId == finger2 )
		{
			if( !panned && SDL_GetTicks() - down_time < TWO_FINGER_TAP_MS + LONG_PRESS_MS )
			{
				click(RIGHT_BUTTON, RIGHT_BUTTON_RELEASE, start_lx, start_ly);
				park_cursor();
			}
			state = TOUCH_IGNORE;
		}
		break;

	default:
		break;
	}

	if( fingers_down == 0 )
		state = TOUCH_IDLE;
}

//------------------------------------------------------------------------

int android_touch_event(SDL_Event *event)
{
	switch( event->type )
	{
	case SDL_FINGERDOWN:
	case SDL_FINGERMOTION:
	case SDL_FINGERUP:
		if( event->tfinger.touchId == SDL_MOUSE_TOUCHID )
			return 1;      // generated from a real mouse; mouse events handle it
		if( event->type == SDL_FINGERDOWN )
			finger_down(event->tfinger);
		else if( event->type == SDL_FINGERMOTION )
			finger_motion(event->tfinger);
		else
			finger_up(event->tfinger);
		return 1;

	case SDL_MULTIGESTURE:
		return 1;          // two-finger panning is done above

	// Touches act only through the absolute mapping above. Drop any mouse
	// events SDL or the device synthesizes from touches, so nothing can move
	// the cursor relative to its old position like a trackpad.
	case SDL_MOUSEMOTION:
		return event->motion.which == SDL_TOUCH_MOUSEID;
	case SDL_MOUSEBUTTONDOWN:
	case SDL_MOUSEBUTTONUP:
		return event->button.which == SDL_TOUCH_MOUSEID;
	case SDL_MOUSEWHEEL:
		return event->wheel.which == SDL_TOUCH_MOUSEID;

	case SDL_KEYDOWN:
	case SDL_KEYUP:
		if( event->key.keysym.sym == SDLK_AC_BACK )
		{
			event->key.keysym.sym = SDLK_ESCAPE;
			event->key.keysym.scancode = SDL_SCANCODE_ESCAPE;
		}
		return 0;
	}
	return 0;
}

void android_touch_update()
{
	// Holding still only arms the right click (the phone vibrates); it fires
	// when the finger is lifted, so holding and then dragging still drags.
	if( state == TOUCH_PENDING && !long_press_armed &&
		 SDL_GetTicks() - down_time >= LONG_PRESS_MS )
	{
		long_press_armed = 1;
		haptic_tick();
	}
}
