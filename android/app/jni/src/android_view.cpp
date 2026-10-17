// Android: smooth panning and pinch zoom of the map view.
//
// Everything here happens on the display side; the game engine is unchanged.
//
// - The engine draws the map view (ZOOM_X1..ZOOM_X2 x ZOOM_Y1..ZOOM_Y2,
//   576x544 pixels) and can only scroll it by whole 32-pixel locations.
// - The part of that view shown on screen is described by a zoom factor
//   k >= 1 and an origin (ox, oy): pixels relative to the view's top-left in
//   the engine's frame. android_view_render() (called by Vga::flip) draws the
//   rectangle [o, o + size/k) of the engine's map view, magnified over the
//   whole map view. The rest of the screen (top bar, side panel) is untouched.
// - Two-finger gestures move the origin and the zoom continuously, keeping
//   the map point between the fingers under the fingers, so the picture
//   follows them pixel by pixel. Letting go while moving makes the map glide
//   on (fling) and then ease to rest.
// - android_view_world_hook() (called each frame by World::detect while the
//   normal map view is showing) scrolls the engine by whole locations whenever
//   the origin drifts more than half a location from its rest position, and
//   moves the origin back by the same amount, so the picture does not jump.
// - Touches in the map view are mapped back through the same transform
//   (android_view_to_engine), so taps and selection boxes act on exactly what
//   is displayed under the finger.
//
// The engine always renders the same 18x17 locations, so the map cannot be
// zoomed out beyond the normal view (the mini-map gives the overview).

#include <android/log.h>
#include <math.h>

#include <OSYS.h>
#include <OWORLD.h>
#include <OWORLDMT.h>

#include "android_glue.h"
#include "android_view.h"

#define LOG_TAG "7kaa-view"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

static const float VX = ZOOM_X1, VY = ZOOM_Y1;            // map view on screen
static const float VW = ZOOM_WIDTH, VH = ZOOM_HEIGHT;
static const float LOC = ZOOM_LOC_WIDTH;                   // 32 pixels
static const float K_MAX = 3.0f;
static const Uint32 ACTIVE_MS = 300;    // view counts as showing this long after the last hook call

static float k = 1.0f;                  // zoom
static float ox, oy;                    // origin of the shown part, see above
static float vx, vy;                    // fling velocity, display pixels/second
static Uint32 seen_time;                // last hook call in the normal map view
static int at_left, at_right, at_top, at_bottom;  // engine cannot scroll further that way

static int gesture;                     // a two-finger gesture controls the view
static float g_mid_x, g_mid_y, g_dist;  // last gesture sample
static Uint32 g_time;
static int g_scrolled_x, g_scrolled_y;  // engine locations scrolled during the gesture (log)

//------------------------------------------------------------------------

static int active()
{
	return seen_time && SDL_GetTicks() - seen_time < ACTIVE_MS;
}

static float clampf(float v, float lo, float hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

// Free room around the shown part when zoomed in.
static float margin_x() { return VW - VW / k; }
static float margin_y() { return VH - VH / k; }

// Rest position of the origin: the shown part centred in the map view.
static float rest_x() { return margin_x() / 2; }
static float rest_y() { return margin_y() / 2; }

// Keep the origin within what the engine has drawn, allowing up to one
// location of drift before the hook scrolls the engine. At the map's edges
// the engine cannot scroll, so nothing beyond the drawn view may show there.
static void clamp_origin()
{
	float lo = at_left   ? 0.0f        : rest_x() - LOC;
	float hi = at_right  ? margin_x()  : rest_x() + LOC;
	float nx = clampf(ox, lo, hi);
	if( nx != ox ) vx = 0;
	ox = nx;

	lo = at_top    ? 0.0f        : rest_y() - LOC;
	hi = at_bottom ? margin_y()  : rest_y() + LOC;
	float ny = clampf(oy, lo, hi);
	if( ny != oy ) vy = 0;
	oy = ny;
}

//------------------------------------------------------------------------

void android_view_world_hook()
{
	if( sys.view_mode != MODE_NORMAL || !world.zoom_matrix )
		return;            // a report covers the map view

	seen_time = SDL_GetTicks();
	if( !seen_time )
		seen_time = 1;

	ZoomMatrix *zm = world.zoom_matrix;
	int moved = 0;

	while( ox - rest_x() > LOC / 2 )
	{
		int t = zm->top_x_loc;
		zm->scroll(1, 0);
		if( zm->top_x_loc == t ) break;
		ox -= LOC; moved = 1; g_scrolled_x++;
	}
	while( ox - rest_x() < -LOC / 2 )
	{
		int t = zm->top_x_loc;
		zm->scroll(-1, 0);
		if( zm->top_x_loc == t ) break;
		ox += LOC; moved = 1; g_scrolled_x--;
	}
	while( oy - rest_y() > LOC / 2 )
	{
		int t = zm->top_y_loc;
		zm->scroll(0, 1);
		if( zm->top_y_loc == t ) break;
		oy -= LOC; moved = 1; g_scrolled_y++;
	}
	while( oy - rest_y() < -LOC / 2 )
	{
		int t = zm->top_y_loc;
		zm->scroll(0, -1);
		if( zm->top_y_loc == t ) break;
		oy += LOC; moved = 1; g_scrolled_y--;
	}

	at_left   = zm->top_x_loc <= 0;
	at_top    = zm->top_y_loc <= 0;
	at_right  = zm->top_x_loc + zm->disp_x_loc >= zm->max_x_loc;
	at_bottom = zm->top_y_loc + zm->disp_y_loc >= zm->max_y_loc;
	clamp_origin();

	if( moved )
		sys.zoom_need_redraw = 1;     // as World::detect_scroll does
}

//------------------------------------------------------------------------

void android_view_render(SDL_Renderer *ren, SDL_Texture *tex)
{
	if( !active() || (k == 1.0f && ox == 0.0f && oy == 0.0f) )
		return;

	// source: the shown part of the engine's map view, in frame pixels
	float sx = VX + ox, sy = VY + oy, sw = VW / k, sh = VH / k;
	int x0 = (int) floorf(sx), x1 = (int) ceilf(sx + sw);
	int y0 = (int) floorf(sy), y1 = (int) ceilf(sy + sh);
	if( x0 < ZOOM_X1 ) x0 = ZOOM_X1;
	if( y0 < ZOOM_Y1 ) y0 = ZOOM_Y1;
	if( x1 > ZOOM_X1 + ZOOM_WIDTH )  x1 = ZOOM_X1 + ZOOM_WIDTH;
	if( y1 > ZOOM_Y1 + ZOOM_HEIGHT ) y1 = ZOOM_Y1 + ZOOM_HEIGHT;

	SDL_Rect view = { ZOOM_X1, ZOOM_Y1, ZOOM_WIDTH, ZOOM_HEIGHT };
	SDL_RenderSetClipRect(ren, &view);
	SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);   // anything not drawn yet stays black
	SDL_RenderFillRect(ren, &view);
	if( x1 > x0 && y1 > y0 )
	{
		SDL_Rect src = { x0, y0, x1 - x0, y1 - y0 };
		SDL_FRect dst = { VX + (x0 - sx) * k, VY + (y0 - sy) * k,
		                  (x1 - x0) * k, (y1 - y0) * k };
		SDL_RenderCopyF(ren, tex, &src, &dst);
	}
	SDL_RenderSetClipRect(ren, NULL);
}

//------------------------------------------------------------------------

int android_view_contains(float x, float y)
{
	return x >= VX && x < VX + VW && y >= VY && y < VY + VH;
}

int android_view_to_engine(float *x, float *y)
{
	if( !active() || !android_view_contains(*x, *y) )
		return 0;
	*x = VX + ox + (*x - VX) / k;
	*y = VY + oy + (*y - VY) / k;
	return 1;
}

//------------------------------------------------------------------------

int android_view_gesture_begin(float midX, float midY, float dist)
{
	vx = vy = 0;
	if( !active() )
	{
		LOGI("two-finger gesture: map view not showing, no pan/zoom");
		return 0;
	}
	if( !android_view_contains(midX, midY) )
	{
		LOGI("two-finger gesture outside the map view (%.0f,%.0f), no pan/zoom", midX, midY);
		return 0;
	}
	gesture = 1;
	g_mid_x = midX;
	g_mid_y = midY;
	g_dist = dist < 1.0f ? 1.0f : dist;
	g_time = SDL_GetTicks();
	g_scrolled_x = g_scrolled_y = 0;
	LOGI("pan/zoom start: zoom %.2f origin (%.1f,%.1f) mid (%.0f,%.0f) dist %.0f",
		k, ox, oy, midX, midY, dist);
	return 1;
}

void android_view_gesture_move(float midX, float midY, float dist)
{
	if( !gesture )
		return;
	if( dist < 1.0f )
		dist = 1.0f;

	// the map point under the previous midpoint...
	float wx = ox + (g_mid_x - VX) / k;
	float wy = oy + (g_mid_y - VY) / k;

	k = clampf(k * dist / g_dist, 1.0f, K_MAX);

	// ...stays under the new midpoint: this pans and zooms in one go
	ox = wx - (midX - VX) / k;
	oy = wy - (midY - VY) / k;
	clamp_origin();

	// finger speed for the fling, smoothed
	Uint32 now = SDL_GetTicks();
	Uint32 dt = now - g_time;
	if( dt > 0 )
	{
		float ix = (midX - g_mid_x) * 1000.0f / dt;
		float iy = (midY - g_mid_y) * 1000.0f / dt;
		vx = vx * 0.5f + ix * 0.5f;
		vy = vy * 0.5f + iy * 0.5f;
	}

	g_mid_x = midX;
	g_mid_y = midY;
	g_dist = dist;
	g_time = now;
}

void android_view_gesture_end()
{
	if( !gesture )
		return;
	gesture = 0;

	if( k < 1.03f )
		k = 1.0f;           // snap back to the normal view
	if( SDL_GetTicks() - g_time > 80 )
		vx = vy = 0;        // fingers had stopped before lifting: no fling
	if( fabsf(vx) < 60.0f && fabsf(vy) < 60.0f )
		vx = vy = 0;
	clamp_origin();

	LOGI("pan/zoom end: zoom %.2f origin (%.1f,%.1f) fling (%.0f,%.0f) px/s, engine scrolled (%d,%d) locations",
		k, ox, oy, vx, vy, g_scrolled_x, g_scrolled_y);
}

void android_view_stop_fling()
{
	vx = vy = 0;
}

void android_view_animate()
{
	static Uint32 last;
	Uint32 now = SDL_GetTicks();
	float dt = (now - last) / 1000.0f;
	last = now;
	if( dt > 0.1f )
		dt = 0.1f;

	if( !active() || gesture )
		return;

	if( vx != 0.0f || vy != 0.0f )
	{
		// keep gliding, slowing down
		ox -= vx * dt / k;
		oy -= vy * dt / k;
		float decay = expf(-4.0f * dt);
		vx *= decay;
		vy *= decay;
		if( hypotf(vx, vy) < 20.0f )
			vx = vy = 0;
		clamp_origin();
	}
	else
	{
		// at rest: ease to where no undrawn (black) edge shows
		float tx = clampf(ox, 0.0f, margin_x());
		float ty = clampf(oy, 0.0f, margin_y());
		float a = dt * 12.0f;
		if( a > 1.0f ) a = 1.0f;
		ox += (tx - ox) * a;
		oy += (ty - oy) * a;
		if( fabsf(tx - ox) < 0.25f ) ox = tx;
		if( fabsf(ty - oy) < 0.25f ) oy = ty;
	}
}
