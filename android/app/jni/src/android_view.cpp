// Android: smooth panning and zooming (in and out) of the map view.
//
// The game engine draws its map view (ZOOM_X1..ZOOM_X2 x ZOOM_Y1..ZOOM_Y2,
// 576x544 pixels = 18x17 locations), scrolls it only by whole 32-pixel
// locations, and cannot draw a bigger view: that size is fixed throughout the
// game code. On Android:
//
// - The part of the world shown in the map view is a "window": a zoom factor
//   k (0.5 = twice as much map across, up to 3 = magnified) and an origin
//   (ox, oy) in pixels relative to the game's own view (its top-left
//   location cur_tx, cur_ty). Two-finger gestures move and zoom the window
//   continuously, keeping the map point between the fingers under them, so
//   the picture follows the fingers pixel by pixel; letting go while moving
//   makes it glide on (fling).
//
// - android_view_world_hook() (called each frame by World::detect in the
//   normal map view) keeps the game's view near the middle of the window,
//   scrolling it by whole locations and moving the origin back by the same
//   amount, so the picture does not jump.
//
// - Where the window reaches beyond the game's view (zoomed out, or by part of
//   a location while panning), android_view_disp_zoom_begin() (called at the
//   start of Sys::disp_zoom) draws the missing areas (plus a margin) first, as extra passes of
//   the normal map drawing at other positions (terrain, units, buildings, fog
//   and names; no weather and no build marker, which keep state or follow the
//   mouse), into a larger picture. Vga::flip() then lays the game's real view
//   (with selection box and messages) on top and shows the window scaled into
//   the map view (android_view_render). Nothing beyond the map is drawn.
//
// - Touches in the map view are mapped back through the same transform.
//   A touch on a part outside the game's view first moves the game's view to
//   include it (android_view_request; the picture stays put), so taps,
//   orders and building placement act on what is under the finger.

#include <android/log.h>
#include <math.h>
#include <string.h>

#include <COLOR.h>
#include <OSYS.h>
#include <OVGABUF.h>
#include <OWORLD.h>
#include <OWORLDMT.h>

#include "android_glue.h"
#include "android_view.h"

#define LOG_TAG "7kaa-view"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

static const float VX = ZOOM_X1, VY = ZOOM_Y1;            // map view on screen
static const float VW = ZOOM_WIDTH, VH = ZOOM_HEIGHT;
static const float LOC = ZOOM_LOC_WIDTH;                   // 32 pixels
static const float K_MIN = 0.5f, K_MAX = 3.0f;
static const Uint32 ACTIVE_MS = 300;    // view counts as showing this long after the last hook call

// Extra passes cover the window plus this margin, so small moves of the
// window before the next drawing (the game reads input while it draws) show
// map, not black.
static const int PASS_MARGIN = 2 * ZOOM_LOC_WIDTH;
// Larger picture for the extra passes: the biggest window, margins, alignment.
static const int BIG_W = (int) (ZOOM_WIDTH / 0.5f) + 2 * PASS_MARGIN + ZOOM_LOC_WIDTH;
static const int BIG_H = (int) (ZOOM_HEIGHT / 0.5f) + 2 * PASS_MARGIN + ZOOM_LOC_HEIGHT;
static const int MAX_PASSES = 16;
// The game draws a new frame only on game ticks (a few per second), but the
// screen is shown many times in between. While the window moves, ask for a
// redraw at most this often so the extra passes follow it (else newly shown
// edges are black until the next tick: flashing).
static const Uint32 REDRAW_MS = 15;

static float k = 1.0f;                  // zoom
static float ox, oy;                    // window origin relative to the game's view
static float vx, vy;                    // fling velocity, display pixels/second
static Uint32 seen_time;                // last hook call in the normal map view
static int at_left, at_right, at_top, at_bottom;  // game's view cannot scroll further that way
static int cur_tx, cur_ty;              // game's view (locations), cached outside drawing passes
static int drawn_tx, drawn_ty;          // game's view of the last drawn frame
static int hold;                        // one-finger gesture in progress: keep the game's view
static int busy;                        // drawing extra passes

static int req_active;                  // bring a point into the game's view
static float req_wx, req_wy;            // ... in world pixels

static int gesture;                     // a two-finger gesture controls the view
static float g_mid_x, g_mid_y, g_dist;  // last gesture sample
static Uint32 g_time;
static int g_scrolled_x, g_scrolled_y;  // locations scrolled during the gesture (log)

static SDL_Surface *big8;               // extra passes, game palette
static SDL_Surface *big32;              // same, converted for the texture
static SDL_Texture *bigtex;
static SDL_Renderer *bigtex_renderer;
static int big_valid;                   // big8 belongs to the last drawn frame
static int big_x, big_y;                // world pixel of big8's top-left corner
static int big_failed;

static float drawn_wx0 = -1e9f, drawn_wy0, drawn_k;   // window when last drawn (world px)
static Uint32 drawn_time;

//------------------------------------------------------------------------

static int active()
{
	return seen_time && SDL_GetTicks() - seen_time < ACTIVE_MS;
}

static float clampf(float v, float lo, float hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

static int clampi(int v, int lo, int hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

// Room between the window and the game's view; negative when zoomed out.
static float margin_x() { return VW - VW / k; }
static float margin_y() { return VH - VH / k; }

// Rest position of the origin: the game's view centred in the window.
static float rest_x() { return margin_x() / 2; }
static float rest_y() { return margin_y() / 2; }

// One axis of clamp_origin(). Normally the origin may drift up to a location
// from its rest position before the hook scrolls the game's view after it.
// When the game's view is at the start of the map (atLo), the window may not
// begin before it (origin >= 0); at the end of the map (atHi) it may not reach
// past it (origin <= margin).
static void clamp_axis(float *o, float *v, float rest, float margin, int atLo, int atHi)
{
	float lo = rest - LOC, hi = rest + LOC;
	if( atLo )
	{
		lo = 0.0f;
		if( hi < 0.0f ) hi = 0.0f;
	}
	if( atHi )
	{
		hi = margin;
		if( lo > margin ) lo = margin;
	}
	if( lo > hi )
		lo = hi = (lo + hi) / 2;
	float n = clampf(*o, lo, hi);
	if( n != *o )
		*v = 0;
	*o = n;
}

static void clamp_origin()
{
	clamp_axis(&ox, &vx, rest_x(), margin_x(), at_left, at_right);
	clamp_axis(&oy, &vy, rest_y(), margin_y(), at_top, at_bottom);
}

//------------------------------------------------------------------------
// Hook in World::detect: move the game's view to follow the window.

static int scroll_game_view(ZoomMatrix *zm, int sx, int sy)
{
	int tx = zm->top_x_loc, ty = zm->top_y_loc;
	zm->scroll(sx, sy);
	int dx = zm->top_x_loc - tx, dy = zm->top_y_loc - ty;
	ox -= dx * LOC;
	oy -= dy * LOC;
	g_scrolled_x += dx;
	g_scrolled_y += dy;
	return dx || dy;
}

void android_view_world_hook()
{
	if( sys.view_mode != MODE_NORMAL || !world.zoom_matrix )
		return;            // a report covers the map view

	ZoomMatrix *zm = world.zoom_matrix;
	seen_time = SDL_GetTicks();
	if( !seen_time )
		seen_time = 1;

	// The game moves its own view too (mini-map, keys, "go to"): the window
	// stays relative to it, so the picture follows.
	cur_tx = zm->top_x_loc;
	cur_ty = zm->top_y_loc;

	int moved = 0;

	if( req_active )
	{
		req_active = 0;
		float qx = req_wx - cur_tx * LOC, qy = req_wy - cur_ty * LOC;
		int sx = 0, sy = 0;
		if( qx < LOC )            sx = (int) floorf((qx - LOC) / LOC);
		else if( qx > VW - LOC )  sx = (int) ceilf((qx - (VW - LOC)) / LOC);
		if( qy < LOC )            sy = (int) floorf((qy - LOC) / LOC);
		else if( qy > VH - LOC )  sy = (int) ceilf((qy - (VH - LOC)) / LOC);
		if( sx || sy )
		{
			int ok = scroll_game_view(zm, sx, sy);
			moved |= ok;
			LOGI("touch outside the game's view (%.0f,%.0f): view moved by (%d,%d), %s",
				qx, qy, sx, sy, ok ? "ok" : "at the map edge");
		}
	}

	if( !hold )
	{
		while( ox - rest_x() > LOC / 2 && scroll_game_view(zm, 1, 0) )  moved = 1;
		while( ox - rest_x() < -LOC / 2 && scroll_game_view(zm, -1, 0) ) moved = 1;
		while( oy - rest_y() > LOC / 2 && scroll_game_view(zm, 0, 1) )  moved = 1;
		while( oy - rest_y() < -LOC / 2 && scroll_game_view(zm, 0, -1) ) moved = 1;
	}

	cur_tx = zm->top_x_loc;
	cur_ty = zm->top_y_loc;
	at_left   = zm->top_x_loc <= 0;
	at_top    = zm->top_y_loc <= 0;
	at_right  = zm->top_x_loc + zm->disp_x_loc >= zm->max_x_loc;
	at_bottom = zm->top_y_loc + zm->disp_y_loc >= zm->max_y_loc;
	if( !hold )
		clamp_origin();

	if( moved )
		sys.zoom_need_redraw = 1;     // as World::detect_scroll does

	// The window moved since the map was last drawn and reaches beyond the
	// game's view: redraw now rather than at the next game tick, so the extra
	// passes follow it.
	float wx0 = cur_tx * LOC + ox, wy0 = cur_ty * LOC + oy;
	float gx0 = cur_tx * LOC, gy0 = cur_ty * LOC;
	int beyond = wx0 < gx0 || wy0 < gy0 || wx0 + VW / k > gx0 + VW || wy0 + VH / k > gy0 + VH;
	if( beyond && (fabsf(wx0 - drawn_wx0) > 0.5f || fabsf(wy0 - drawn_wy0) > 0.5f || k != drawn_k) &&
		 SDL_GetTicks() - drawn_time >= REDRAW_MS )
	{
		sys.zoom_need_redraw = 1;
	}
}

//------------------------------------------------------------------------
// Extra map passes, at the start of Sys::disp_zoom.

static int ensure_big()
{
	if( big8 )
		return 1;
	if( big_failed )
		return 0;
	big8  = SDL_CreateRGBSurfaceWithFormat(0, BIG_W, BIG_H, 8, SDL_PIXELFORMAT_INDEX8);
	big32 = SDL_CreateRGBSurfaceWithFormat(0, BIG_W, BIG_H, 32, SDL_PIXELFORMAT_RGB888);
	if( !big8 || !big32 )
	{
		LOGI("cannot allocate the zoom-out picture (%dx%d): %s", BIG_W, BIG_H, SDL_GetError());
		if( big8 )  SDL_FreeSurface(big8);
		if( big32 ) SDL_FreeSurface(big32);
		big8 = big32 = NULL;
		big_failed = 1;
		return 0;
	}
	LOGI("zoom-out picture allocated: %dx%d", BIG_W, BIG_H);
	return 1;
}

// Copy the game's map view from vga_back into big8, where location (tx, ty)
// is the view's top-left.
static void copy_view_to_big(int tx, int ty)
{
	SDL_Surface *src = vga_back.surface;
	int dx0 = tx * ZOOM_LOC_WIDTH - big_x;
	int dy0 = ty * ZOOM_LOC_HEIGHT - big_y;
	int xa = dx0 < 0 ? -dx0 : 0;
	int xb = ZOOM_WIDTH;
	if( dx0 + xb > BIG_W ) xb = BIG_W - dx0;
	if( xb <= xa )
		return;
	for( int y = 0; y < ZOOM_HEIGHT; y++ )
	{
		int by = dy0 + y;
		if( by < 0 || by >= BIG_H )
			continue;
		memcpy( (Uint8*) big8->pixels + by * big8->pitch + dx0 + xa,
		        (Uint8*) src->pixels + (ZOOM_Y1 + y) * src->pitch + ZOOM_X1 + xa,
		        xb - xa );
	}
}

void android_view_disp_zoom_begin()
{
	ZoomMatrix *zm = world.zoom_matrix;
	if( !zm )
		return;
	cur_tx = drawn_tx = zm->top_x_loc;
	cur_ty = drawn_ty = zm->top_y_loc;
	big_valid = 0;

	if( !active() || sys.view_mode != MODE_NORMAL )
		return;

	// the window in world pixels, and the game's view
	float wx0 = cur_tx * LOC + ox, wy0 = cur_ty * LOC + oy;
	float wx1 = wx0 + VW / k,      wy1 = wy0 + VH / k;
	float gx0 = cur_tx * LOC,      gy0 = cur_ty * LOC;
	drawn_wx0 = wx0;
	drawn_wy0 = wy0;
	drawn_k = k;
	drawn_time = SDL_GetTicks();
	if( wx0 >= gx0 && wy0 >= gy0 && wx1 <= gx0 + VW && wy1 <= gy0 + VH )
		return;            // the game's own view covers the window

	if( !ensure_big() )
		return;

	// cover the window plus a margin
	wx0 -= PASS_MARGIN;
	wy0 -= PASS_MARGIN;
	wx1 += PASS_MARGIN;
	wy1 += PASS_MARGIN;
	big_x = (int) floorf(wx0 / LOC) * ZOOM_LOC_WIDTH;
	big_y = (int) floorf(wy0 / LOC) * ZOOM_LOC_HEIGHT;
	SDL_FillRect(big8, NULL, V_BLACK);

	int maxTx = zm->max_x_loc - zm->disp_x_loc;
	int maxTy = zm->max_y_loc - zm->disp_y_loc;
	int done_x[MAX_PASSES], done_y[MAX_PASSES], passes = 0;

	busy = 1;
	for( int py = big_y / ZOOM_LOC_HEIGHT; py * LOC < wy1; py += zm->disp_y_loc )
	{
		for( int px = big_x / ZOOM_LOC_WIDTH; px * LOC < wx1; px += zm->disp_x_loc )
		{
			int tx = clampi(px, 0, maxTx), ty = clampi(py, 0, maxTy);
			if( tx == cur_tx && ty == cur_ty )
				continue;          // the game's own view is drawn as usual
			int dup = 0;
			for( int i = 0; i < passes; i++ )
				if( done_x[i] == tx && done_y[i] == ty ) dup = 1;
			if( dup || passes >= MAX_PASSES )
				continue;
			done_x[passes] = tx;
			done_y[passes] = ty;
			passes++;

			zm->top_x_loc = tx;
			zm->top_y_loc = ty;
			World::view_top_x = tx * ZOOM_LOC_WIDTH;
			World::view_top_y = ty * ZOOM_LOC_HEIGHT;
			zm->draw_extra_pass();
			copy_view_to_big(tx, ty);
		}
	}
	zm->top_x_loc = cur_tx;
	zm->top_y_loc = cur_ty;
	World::view_top_x = cur_tx * ZOOM_LOC_WIDTH;
	World::view_top_y = cur_ty * ZOOM_LOC_HEIGHT;
	busy = 0;

	if( passes )
		sys.zoom_need_redraw = 1;    // the passes overwrote the saved terrain
	big_valid = 1;

	static int last_passes = -1;
	if( passes != last_passes )
	{
		LOGI("map view drawn with %d extra pass(es), zoom %.2f", passes, k);
		last_passes = passes;
	}
}

int android_view_busy()
{
	return busy;
}

//------------------------------------------------------------------------
// Vga::flip: show the window.

void android_view_render(SDL_Renderer *ren, SDL_Texture *tex, SDL_Surface *front)
{
	if( !active() )
		return;

	// window and the drawn game view, in world pixels
	float wx0 = cur_tx * LOC + ox, wy0 = cur_ty * LOC + oy;
	float ww = VW / k, wh = VH / k;
	float gx0 = drawn_tx * LOC, gy0 = drawn_ty * LOC;

	if( k == 1.0f && wx0 == gx0 && wy0 == gy0 )
		return;            // nothing to change: the frame already shows it

	SDL_Rect view = { ZOOM_X1, ZOOM_Y1, ZOOM_WIDTH, ZOOM_HEIGHT };
	SDL_RenderSetClipRect(ren, &view);
	SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);   // off the map / not drawn: black
	SDL_RenderFillRect(ren, &view);

	int inside = wx0 >= gx0 && wy0 >= gy0 && wx0 + ww <= gx0 + VW && wy0 + wh <= gy0 + VH;

	if( !inside && big_valid && big8 && front )
	{
		//---- composite: extra passes + the game's real view on top ----//

		if( big8->format->palette != front->format->palette )
			SDL_SetSurfacePalette(big8, front->format->palette);

		SDL_Rect fsrc = { ZOOM_X1, ZOOM_Y1, ZOOM_WIDTH, ZOOM_HEIGHT };
		SDL_Rect fdst = { (int) gx0 - big_x, (int) gy0 - big_y, ZOOM_WIDTH, ZOOM_HEIGHT };
		SDL_BlitSurface(front, &fsrc, big8, &fdst);

		float sx = wx0 - big_x, sy = wy0 - big_y;
		int x0 = clampi((int) floorf(sx), 0, BIG_W), x1 = clampi((int) ceilf(sx + ww), 0, BIG_W);
		int y0 = clampi((int) floorf(sy), 0, BIG_H), y1 = clampi((int) ceilf(sy + wh), 0, BIG_H);
		if( x1 > x0 && y1 > y0 )
		{
			if( !bigtex || bigtex_renderer != ren )
			{
				if( bigtex )
					SDL_DestroyTexture(bigtex);
				bigtex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB888,
					SDL_TEXTUREACCESS_STREAMING, BIG_W, BIG_H);
				bigtex_renderer = ren;
				if( !bigtex )
					LOGI("cannot create the zoom-out texture: %s", SDL_GetError());
			}
			if( bigtex )
			{
				SDL_Rect r = { x0, y0, x1 - x0, y1 - y0 };
				SDL_BlitSurface(big8, &r, big32, &r);
				SDL_UpdateTexture(bigtex, &r,
					(Uint8*) big32->pixels + r.y * big32->pitch + r.x * 4, big32->pitch);
				SDL_FRect dst = { VX + (x0 - sx) * k, VY + (y0 - sy) * k,
				                  (x1 - x0) * k, (y1 - y0) * k };
				SDL_RenderCopyF(ren, bigtex, &r, &dst);
			}
		}
	}
	else
	{
		//---- the window lies in the drawn game view (or no passes yet) ----//

		float sx = VX + (wx0 - gx0), sy = VY + (wy0 - gy0);   // in frame pixels
		int x0 = clampi((int) floorf(sx), ZOOM_X1, ZOOM_X1 + ZOOM_WIDTH);
		int x1 = clampi((int) ceilf(sx + ww), ZOOM_X1, ZOOM_X1 + ZOOM_WIDTH);
		int y0 = clampi((int) floorf(sy), ZOOM_Y1, ZOOM_Y1 + ZOOM_HEIGHT);
		int y1 = clampi((int) ceilf(sy + wh), ZOOM_Y1, ZOOM_Y1 + ZOOM_HEIGHT);
		if( x1 > x0 && y1 > y0 )
		{
			SDL_Rect src = { x0, y0, x1 - x0, y1 - y0 };
			SDL_FRect dst = { VX + (x0 - sx) * k, VY + (y0 - sy) * k,
			                  (x1 - x0) * k, (y1 - y0) * k };
			SDL_RenderCopyF(ren, tex, &src, &dst);
		}
	}
	SDL_RenderSetClipRect(ren, NULL);
}

//------------------------------------------------------------------------
// Touch mapping.

int android_view_contains(float x, float y)
{
	return x >= VX && x < VX + VW && y >= VY && y < VY + VH;
}

int android_view_to_engine(float *x, float *y)
{
	if( !active() || !android_view_contains(*x, *y) )
		return 0;
	float qx = ox + (*x - VX) / k, qy = oy + (*y - VY) / k;
	if( qx < 0 || qx >= VW || qy < 0 || qy >= VH )
		return -1;
	*x = VX + qx;
	*y = VY + qy;
	return 1;
}

void android_view_to_engine_clamped(float *x, float *y)
{
	if( !active() )
		return;
	float px = clampf(*x, VX, VX + VW - 0.01f), py = clampf(*y, VY, VY + VH - 0.01f);
	float qx = ox + (px - VX) / k, qy = oy + (py - VY) / k;
	*x = VX + clampf(qx, 0.0f, VW - 1.0f);
	*y = VY + clampf(qy, 0.0f, VH - 1.0f);
}

void android_view_request(float x, float y)
{
	if( !active() || !android_view_contains(x, y) )
		return;
	req_wx = cur_tx * LOC + ox + (x - VX) / k;
	req_wy = cur_ty * LOC + oy + (y - VY) / k;
	req_active = 1;
}

void android_view_set_hold(int h)
{
	hold = h;
}

//------------------------------------------------------------------------
// Two-finger gestures.

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

	k = clampf(k * dist / g_dist, K_MIN, K_MAX);

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

	if( fabsf(k - 1.0f) < 0.04f )
		k = 1.0f;           // snap to the normal view
	if( SDL_GetTicks() - g_time > 80 )
		vx = vy = 0;        // fingers had stopped before lifting: no fling
	if( fabsf(vx) < 60.0f && fabsf(vy) < 60.0f )
		vx = vy = 0;
	clamp_origin();

	LOGI("pan/zoom end: zoom %.2f origin (%.1f,%.1f) fling (%.0f,%.0f) px/s, game view scrolled (%d,%d) locations",
		k, ox, oy, vx, vy, g_scrolled_x, g_scrolled_y);
}

void android_view_reset_zoom()
{
	if( !active() )
		return;
	// zoom back to 1 around the middle of the window
	float cx = ox + VW / k / 2, cy = oy + VH / k / 2;
	k = 1.0f;
	ox = cx - VW / 2;
	oy = cy - VH / 2;
	vx = vy = 0;
	clamp_origin();
	LOGI("zoom reset to normal");
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

	if( !active() || gesture || hold )
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
	else if( k >= 1.0f )
	{
		// at rest: ease to where the game's own view covers the window, so
		// no extra map passes are needed while nothing moves
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
