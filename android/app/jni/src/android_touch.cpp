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
//   two-finger drag          -> pan the map smoothly (it glides on when let
//                               go while moving)
//   pinch                    -> zoom the map view in/out (android_view.cpp)
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
#include <OWORLDMT.h>
#include "android_glue.h"
#include "android_view.h"

enum TouchState
{
	TOUCH_IDLE,      // no finger down
	TOUCH_PENDING,   // one finger down, not yet a tap, drag or long press
	TOUCH_DRAG,      // one finger dragging with the left button held
	TOUCH_TWO,       // two fingers: tap = right click, move = pan/zoom
	TOUCH_IGNORE,    // gesture finished, wait until all fingers are up
};

static const Uint32 LONG_PRESS_MS = 450;
static const Uint32 TWO_FINGER_TAP_MS = 400;

static TouchState state = TOUCH_IDLE;
static int fingers_down;
static SDL_FingerID finger1, finger2;
static Uint32 down_time;
static float start_gx, start_gy;      // finger1 start, game pixels (float)
static int start_lx, start_ly;        // finger1 start, engine (800x600) pixels
static int start_in_view;             // finger1 went down in the map view
static float f1_gx, f1_gy, f2_gx, f2_gy;  // current finger positions, display pixels
static float two_last_x, two_last_y, two_last_dist;  // two-finger midpoint/distance
static float two_total;               // how much the two fingers moved
static int panned;                    // two-finger gesture moved (no right click)
static int view_gesture;              // two-finger gesture pans/zooms the map
static int long_press_armed;          // held still long enough: lift = right click
static volatile int right_click_mode;   // set from the Java UI thread

extern "C" JNIEXPORT void JNICALL
Java_com_thor8126_sevenkaa_SevenKaaActivity_nativeSetRightClickMode(JNIEnv *, jclass, jboolean on)
{
	right_click_mode = on ? 1 : 0;
}

//------------------------------------------------------------------------

// Finger position in game screen pixels (800x600).
//
// SDL already did the work: with SDL_RenderSetLogicalSize() active, SDL's
// renderer rewrites every finger event (SDL_RendererEventWatch in
// SDL_render.c) so that tfinger.x/y run from 0 to 1 across the game picture
// itself, letterbox bars excluded and clamped to 0/1 outside it. Without a
// logical size the picture is stretched over the whole window, which is again
// 0..1. Either way the game position is simply x*800, y*600. (Converting
// these values for the letterbox once more, as earlier versions did, threw
// touches off and pushed the right-hand panel out of reach.)
static void finger_game(const SDL_TouchFingerEvent &f, float *gx, float *gy)
{
	*gx = f.x * 800.0f;
	*gy = f.y * 600.0f;
}

static int clamp_x(float gx) { int i = (int) gx; return i < 0 ? 0 : (i > 799 ? 799 : i); }
static int clamp_y(float gy) { int i = (int) gy; return i < 0 ? 0 : (i > 599 ? 599 : i); }

// Position the game acts on for a finger of the current one-finger gesture:
// display coordinates mapped back through the map view's zoom/pan. A drag that
// started in the map view stays in it.
static void engine_pos(const SDL_TouchFingerEvent &f, int *x, int *y)
{
	float gx, gy;
	finger_game(f, &gx, &gy);
	if( start_in_view )
	{
		const float x1 = ZOOM_X1, x2 = ZOOM_X2 + 0.9f, y1 = ZOOM_Y1, y2 = ZOOM_Y2 + 0.9f;
		gx = gx < x1 ? x1 : (gx > x2 ? x2 : gx);
		gy = gy < y1 ? y1 : (gy > y2 ? y2 : gy);
	}
	android_view_to_engine(&gx, &gy);
	*x = clamp_x(gx);
	*y = clamp_y(gy);
}

// Is the finger on the game picture, not in the black bars at its sides?
// SDL clamps touches in the bars to exactly 0 or 1.
static int on_game_screen(const SDL_TouchFingerEvent &f)
{
	return f.x > 0.0f && f.x < 1.0f;
}

// How far (game pixels) a finger must move before a touch becomes a drag.
static const float MOVE_THRESHOLD = 16.0f;

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

static void two_finger_sample(float *mx, float *my, float *dist)
{
	*mx = (f1_gx + f2_gx) / 2;
	*my = (f1_gy + f2_gy) / 2;
	*dist = hypotf(f1_gx - f2_gx, f1_gy - f2_gy);
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
		android_view_stop_fling();
		finger_game(f, &start_gx, &start_gy);
		f1_gx = start_gx;
		f1_gy = start_gy;
		float ex = start_gx, ey = start_gy;
		start_in_view = android_view_contains(ex, ey);
		android_view_to_engine(&ex, &ey);
		start_lx = clamp_x(ex);
		start_ly = clamp_y(ey);
		// a new touch can land anywhere: no button is held, so nothing may
		// confine the cursor
		mouse.reset_boundary();
		move_cursor(start_lx, start_ly);
	}
	else if( state == TOUCH_PENDING && f.fingerId != finger1 )
	{
		state = TOUCH_TWO;
		finger2 = f.fingerId;
		finger_game(f, &f2_gx, &f2_gy);
		two_finger_sample(&two_last_x, &two_last_y, &two_last_dist);
		two_total = 0;
		panned = 0;
		view_gesture = android_view_gesture_begin(two_last_x, two_last_y, two_last_dist);
	}
	// a third finger, or a second one during a drag, is ignored
}

static void finger_motion(const SDL_TouchFingerEvent &f)
{
	if( state == TOUCH_PENDING && f.fingerId == finger1 )
	{
		finger_game(f, &f1_gx, &f1_gy);
		if( hypotf(f1_gx - start_gx, f1_gy - start_gy) > MOVE_THRESHOLD )
		{
			// start a left-button drag from where the finger went down
			move_cursor(start_lx, start_ly);
			mouse.add_event(LEFT_BUTTON);
			int x, y;
			engine_pos(f, &x, &y);
			move_cursor(x, y);
			state = TOUCH_DRAG;
		}
	}
	else if( state == TOUCH_DRAG && f.fingerId == finger1 )
	{
		int x, y;
		engine_pos(f, &x, &y);
		move_cursor(x, y);
	}
	else if( state == TOUCH_TWO && (f.fingerId == finger1 || f.fingerId == finger2) )
	{
		if( f.fingerId == finger1 )
			finger_game(f, &f1_gx, &f1_gy);
		else
			finger_game(f, &f2_gx, &f2_gy);

		float mx, my, dist;
		two_finger_sample(&mx, &my, &dist);
		two_total += fabsf(mx - two_last_x) + fabsf(my - two_last_y) + fabsf(dist - two_last_dist);
		two_last_x = mx;
		two_last_y = my;
		two_last_dist = dist;
		if( !panned && two_total > 8.0f )
			panned = 1;
		if( view_gesture )
			android_view_gesture_move(mx, my, dist);
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
			engine_pos(f, &x, &y);
			move_cursor(x, y);
			release_left();
			park_cursor();
			state = TOUCH_IGNORE;
		}
		break;

	case TOUCH_TWO:
		if( f.fingerId == finger1 || f.fingerId == finger2 )
		{
			if( view_gesture )
				android_view_gesture_end();
			view_gesture = 0;
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
		return 1;          // two-finger pan/zoom is done above

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

	android_view_animate();
}
