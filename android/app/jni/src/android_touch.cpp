// Android: touchscreen controls.
//
// Fingers are translated into the same mouse events the desktop build gets
// from SDL, so all game code stays unchanged:
//
//   tap                      -> left click
//   long press, then lift    -> right click (move / attack orders); the phone
//                               vibrates when the press is long enough
//   two-finger tap           -> right click at the first finger
//   three-finger tap         -> map view back to normal size
//   one-finger drag          -> left button drag (box select, sliders), also
//                               after holding still first
//   two-finger drag          -> pan the map smoothly (it glides on when let
//                               go while moving)
//   pinch                    -> zoom the map view in or out (android_view.cpp)
//   Back button              -> Esc
//   "Right click" on-screen toggle (SevenKaaActivity): taps are right clicks
//
// Placing a building (or any command waiting for a target on the map:
// assign, settle, caravan stop, wall, god power...): touching the map aims
// instead. The outline follows the finger, holding still does nothing, and
// lifting the finger places it there. Lifting it off the map places nothing.
// A two-finger tap (right click) or Esc cancels, as on the desktop.
//
// Every action happens exactly under the finger: the touch point is mapped to
// 800x600 game coordinates, then through the map view's zoom/pan
// (android_view_to_engine). There is no cursor to steer (it is not drawn on
// Android, see MouseCursor in src/OMOUSECR.cpp). The window is never grabbed
// on Android (Vga::is_input_grabbed), so nothing clamps the position and
// there is no edge scrolling.
//
// When zoomed out, a touch can land on a part of the map that is outside the
// game's own 18x17-location view. The touch then asks the view module to move
// the game's view there (the picture does not move), and the click is sent
// once that has happened, a frame or two later.
//
// Touches that start in the black bars beside the game screen are ignored
// (that is where the on-screen buttons live).
//
// A real mouse (Bluetooth, USB, DeX, Chromebook) keeps working through the
// normal SDL mouse events in Vga::handle_messages().

#include <jni.h>
#include <math.h>

#include <android/log.h>

#include <OMOUSE.h>
#include <OPOWER.h>
#include <OWORLDMT.h>
#include "android_glue.h"
#include "android_view.h"

#define LOG_TAG "7kaa-touch"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

enum TouchState
{
	TOUCH_IDLE,      // no finger down
	TOUCH_PENDING,   // one finger down, not yet a tap, drag or long press
	TOUCH_DRAG,      // one finger dragging with the left button held
	TOUCH_TWO,       // two fingers: tap = right click, move = pan/zoom
	TOUCH_AIM,       // one finger aiming a command (building placement...)
	TOUCH_IGNORE,    // gesture finished, wait until all fingers are up
};

static const Uint32 LONG_PRESS_MS = 450;
static const Uint32 TWO_FINGER_TAP_MS = 400;
static const Uint32 THREE_FINGER_TAP_MS = 600;
static const Uint32 PENDING_CLICK_MS = 700;   // give up on a deferred click after this
static const Uint32 CLICK_HOLD_MS = 300;      // keep the game's view still while it handles a click

static TouchState state = TOUCH_IDLE;
static int fingers_down;
static SDL_FingerID finger1, finger2;
static Uint32 down_time;
static float start_gx, start_gy;      // finger1 start, display pixels
static int start_in_view;             // finger1 went down in the map view
static float f1_gx, f1_gy, f2_gx, f2_gy;  // current finger positions, display pixels
static float two_last_x, two_last_y, two_last_dist;  // two-finger midpoint/distance
static float two_total;               // how much the two fingers moved
static int panned;                    // two-finger gesture moved (no right click)
static int view_gesture;              // two-finger gesture pans/zooms the map
static int three_fingers;             // a third finger came down
static int long_press_armed;          // held still long enough: lift = right click
static volatile int right_click_mode;   // set from the Java UI thread

static int pend_type;                 // deferred click: 0 none, 1 left, 2 right
static float pend_gx, pend_gy;
static Uint32 pend_time;
static Uint32 hold_until;             // keep the game's view still until then

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

// Engine position for a display point. Returns 0 if the point is on a part of
// the map outside the game's current view (zoomed out): the view module has
// been asked to move the game's view there; try again on a later frame.
static int engine_point(float gx, float gy, int *x, int *y)
{
	float ex = gx, ey = gy;
	if( android_view_to_engine(&ex, &ey) < 0 )
	{
		android_view_request(gx, gy);
		return 0;
	}
	*x = clamp_x(ex);
	*y = clamp_y(ey);
	return 1;
}

// Engine position while dragging a box or aiming: in the map view it stays
// inside the game's view (a box cannot reach beyond it).
static void engine_pos_clamped(const SDL_TouchFingerEvent &f, int *x, int *y)
{
	float gx, gy;
	finger_game(f, &gx, &gy);
	if( start_in_view || android_view_contains(gx, gy) )
		android_view_to_engine_clamped(&gx, &gy);
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
	hold_until = SDL_GetTicks() + CLICK_HOLD_MS;
}

static void click(MouseEventType down, MouseEventType up, int x, int y)
{
	move_cursor(x, y);
	mouse.add_event(down);
	if( up == LEFT_BUTTON_RELEASE )
		release_left();
	else
		mouse.add_event(up);
	hold_until = SDL_GetTicks() + CLICK_HOLD_MS;
}

// Click at a display point; if it is outside the game's view (zoomed out),
// send it once the view has moved there (android_touch_update).
static void click_at(int right, float gx, float gy)
{
	int x, y;
	if( engine_point(gx, gy, &x, &y) )
	{
		if( right )
			click(RIGHT_BUTTON, RIGHT_BUTTON_RELEASE, x, y);
		else
			click(LEFT_BUTTON, LEFT_BUTTON_RELEASE, x, y);
		park_cursor();
		return;
	}
	pend_type = right ? 2 : 1;
	pend_gx = gx;
	pend_gy = gy;
	pend_time = SDL_GetTicks();
	LOGI("%s click at (%.0f,%.0f) waits for the game's view to move there",
		right ? "right" : "left", gx, gy);
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
		three_fingers = 0;
		finger1 = f.fingerId;
		down_time = SDL_GetTicks();
		android_view_stop_fling();
		finger_game(f, &start_gx, &start_gy);
		f1_gx = start_gx;
		f1_gy = start_gy;
		start_in_view = android_view_contains(start_gx, start_gy);

		// a new touch can land anywhere: no button is held, so nothing may
		// confine the cursor
		mouse.reset_boundary();

		// Show the game where the finger is (building outline, highlights).
		// Off the game's view (zoomed out) this also asks for the view to
		// move there, so the tap or order at lift time lands right.
		int x, y;
		if( engine_point(start_gx, start_gy, &x, &y) )
			move_cursor(x, y);

		if( start_in_view && power.command_id && !right_click_mode )
		{
			state = TOUCH_AIM;
			LOGI("aim start: command %d at (%.0f,%.0f)", power.command_id, start_gx, start_gy);
		}
	}
	else if( (state == TOUCH_PENDING || state == TOUCH_AIM) && f.fingerId != finger1 )
	{
		state = TOUCH_TWO;
		finger2 = f.fingerId;
		finger_game(f, &f2_gx, &f2_gy);
		two_finger_sample(&two_last_x, &two_last_y, &two_last_dist);
		two_total = 0;
		panned = 0;
		view_gesture = android_view_gesture_begin(two_last_x, two_last_y, two_last_dist);
	}
	else if( state == TOUCH_TWO && f.fingerId != finger1 && f.fingerId != finger2 )
	{
		// three fingers: a quick tap resets the zoom (at lift), no right click
		three_fingers = 1;
		if( view_gesture )
			android_view_gesture_end();
		view_gesture = 0;
		state = TOUCH_IGNORE;
	}
	// any other extra finger is ignored
}

static void finger_motion(const SDL_TouchFingerEvent &f)
{
	if( state == TOUCH_PENDING && f.fingerId == finger1 )
	{
		finger_game(f, &f1_gx, &f1_gy);
		int sx, sy;
		if( hypotf(f1_gx - start_gx, f1_gy - start_gy) > MOVE_THRESHOLD &&
			 engine_point(start_gx, start_gy, &sx, &sy) )   // (zoomed out: once the view is there)
		{
			// start a left-button drag from where the finger went down
			move_cursor(sx, sy);
			mouse.add_event(LEFT_BUTTON);
			int x, y;
			engine_pos_clamped(f, &x, &y);
			move_cursor(x, y);
			state = TOUCH_DRAG;
		}
	}
	else if( (state == TOUCH_DRAG || state == TOUCH_AIM) && f.fingerId == finger1 )
	{
		// drag: the box follows; aim: the building outline follows
		int x, y;
		finger_game(f, &f1_gx, &f1_gy);
		if( state == TOUCH_AIM && !engine_point(f1_gx, f1_gy, &x, &y) )
			return;        // the game's view is moving there; follow next time
		if( state == TOUCH_DRAG )
			engine_pos_clamped(f, &x, &y);
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
			click_at(right_click_mode || long_press_armed, start_gx, start_gy);
			state = TOUCH_IDLE;
		}
		break;

	case TOUCH_AIM:
		if( f.fingerId == finger1 )
		{
			float gx, gy;
			finger_game(f, &gx, &gy);
			if( android_view_contains(gx, gy) && power.command_id )
			{
				click_at(0, gx, gy);
				LOGI("aim done: command %d placed at (%.0f,%.0f)", power.command_id, gx, gy);
			}
			else
			{
				LOGI("aim aborted: finger lifted off the map (%.0f,%.0f) or command gone (%d)",
					gx, gy, power.command_id);
			}
			park_cursor();
			state = TOUCH_IDLE;
		}
		break;

	case TOUCH_DRAG:
		if( f.fingerId == finger1 )
		{
			int x, y;
			engine_pos_clamped(f, &x, &y);
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
				click_at(1, start_gx, start_gy);
			state = TOUCH_IGNORE;
		}
		break;

	default:
		break;
	}

	if( fingers_down == 0 )
	{
		if( three_fingers && SDL_GetTicks() - down_time < THREE_FINGER_TAP_MS )
			android_view_reset_zoom();
		three_fingers = 0;
		state = TOUCH_IDLE;
	}
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
	Uint32 now = SDL_GetTicks();

	// Holding still only arms the right click (the phone vibrates); it fires
	// when the finger is lifted, so holding and then dragging still drags.
	if( state == TOUCH_PENDING && !long_press_armed && now - down_time >= LONG_PRESS_MS )
	{
		long_press_armed = 1;
		haptic_tick();
	}

	// A click that waited for the game's view to move (zoomed out).
	if( pend_type && !android_view_busy() )
	{
		int x, y;
		if( engine_point(pend_gx, pend_gy, &x, &y) )
		{
			if( pend_type == 2 )
				click(RIGHT_BUTTON, RIGHT_BUTTON_RELEASE, x, y);
			else
				click(LEFT_BUTTON, LEFT_BUTTON_RELEASE, x, y);
			park_cursor();
			LOGI("deferred %s click sent at (%d,%d) after %u ms",
				pend_type == 2 ? "right" : "left", x, y, (unsigned) (now - pend_time));
			pend_type = 0;
		}
		else if( now - pend_time > PENDING_CLICK_MS )
		{
			LOGI("deferred click at (%.0f,%.0f) dropped: the view could not move there (map edge?)",
				pend_gx, pend_gy);
			pend_type = 0;
		}
	}

	// The game's view must stay where it is while a one-finger gesture or a
	// click works with it.
	int hold = state == TOUCH_PENDING || state == TOUCH_DRAG || state == TOUCH_AIM ||
		pend_type || (Sint32) (hold_until - now) > 0;
	android_view_set_hold(hold);

	android_view_animate();
}
