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
// - Messages, buttons and labels the game draws over the map view (news at
//   the bottom, the Army/Road/Defend/Idle buttons, the tutorial, the frame
//   rate) are not map: they stay at their normal size and place. The map is
//   captured right after it is drawn, before those go on top
//   (android_view_map_drawn, called in Sys::disp_zoom after
//   info.draw_selected); whatever differs from it on screen is drawn
//   unzoomed over the zoomed map, and taps there go to it unmapped.
//
// - The selection box is drawn here, in screen coordinates, from the game's
//   own frame (MouseCursor no longer draws it on Android).
//
// - The window never shows anything beyond the edges of the map.
//
// - Touches in the map view are mapped back through the same transform.
//   A touch on a part outside the game's view first moves the game's view to
//   include it (android_view_request; the picture stays put), so taps,
//   orders and building placement act on what is under the finger.

#include <android/log.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <COLOR.h>
#include <OMOUSECR.h>
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

static float map_w_px, map_h_px;        // size of the map (world pixels)
static int map_pending;                 // big8 prepared, waiting for the map snapshot
static Uint32 map_gen;                  // incremented with every map snapshot

// texture upload state of big32/bigtex
static Uint32 up_gen = (Uint32) -1, up_pal = (Uint32) -1;
static SDL_Rect up_rect;

// overlays: what the game drew over the map view (messages, buttons...)
static SDL_Surface *ovl32;              // ARGB, transparent where the map shows
static SDL_Texture *ovltex;
static SDL_Renderer *ovltex_renderer;
static Uint8 ovl_mask[ZOOM_WIDTH * ZOOM_HEIGHT];   // 1 = overlay pixel
static int ovl_valid;                   // ovl_mask matches what is on screen
static Uint32 ovl_gen = (Uint32) -1, ovl_time;

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

// One axis of clamp_origin().
//  - The origin may drift up to a location from its rest position before the
//    hook scrolls the game's view after it. Not at the map's edges, where the
//    game's view cannot scroll further (atLo/atHi).
//  - The window never shows anything beyond the edges of the map (mapSize,
//    viewOrigin = the game's view in world pixels).
static void clamp_axis(float *o, float *v, float rest, float viewOrigin, float winSize,
                       float mapSize, int atLo, int atHi)
{
	float lo = atLo ? -1e9f : rest - LOC;
	float hi = atHi ?  1e9f : rest + LOC;
	if( mapSize > 0.0f )
	{
		float mlo = -viewOrigin;                      // window starts at the map's start
		float mhi = mapSize - winSize - viewOrigin;   // window ends at the map's end
		if( mlo > mhi )
			mlo = mhi = (mlo + mhi) / 2;             // map smaller than the window: centre
		if( lo < mlo ) lo = mlo;
		if( hi > mhi ) hi = mhi;
		if( lo > hi )
		{
			lo = mlo;                                 // the map's edges win
			hi = mhi;
		}
	}
	else if( lo > hi )
		lo = hi = (lo + hi) / 2;
	float n = clampf(*o, lo, hi);
	if( n != *o )
		*v = 0;
	*o = n;
}

static void clamp_origin()
{
	clamp_axis(&ox, &vx, rest_x(), cur_tx * LOC, VW / k, map_w_px, at_left, at_right);
	clamp_axis(&oy, &vy, rest_y(), cur_ty * LOC, VH / k, map_h_px, at_top, at_bottom);
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
	map_w_px  = zm->max_x_loc * LOC;
	map_h_px  = zm->max_y_loc * LOC;
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
	map_pending = 0;

	if( !active() || sys.view_mode != MODE_NORMAL )
		return;
	if( !ensure_big() )
		return;

	// the window in world pixels, and the game's view
	float wx0 = cur_tx * LOC + ox, wy0 = cur_ty * LOC + oy;
	float wx1 = wx0 + VW / k,      wy1 = wy0 + VH / k;
	float gx0 = cur_tx * LOC,      gy0 = cur_ty * LOC;
	drawn_wx0 = wx0;
	drawn_wy0 = wy0;
	drawn_k = k;
	drawn_time = SDL_GetTicks();
	int need_passes = !(wx0 >= gx0 && wy0 >= gy0 && wx1 <= gx0 + VW && wy1 <= gy0 + VH);

	// big8 covers the window plus a margin, and the game's view (for the map
	// snapshot); if both do not fit, the window
	float ux0 = fminf(wx0 - PASS_MARGIN, gx0), ux1 = fmaxf(wx1 + PASS_MARGIN, gx0 + VW);
	float uy0 = fminf(wy0 - PASS_MARGIN, gy0), uy1 = fmaxf(wy1 + PASS_MARGIN, gy0 + VH);
	if( ux1 - ux0 + LOC > BIG_W ) ux0 = wx0 - PASS_MARGIN;
	if( uy1 - uy0 + LOC > BIG_H ) uy0 = wy0 - PASS_MARGIN;
	big_x = (int) floorf(ux0 / LOC) * ZOOM_LOC_WIDTH;
	big_y = (int) floorf(uy0 / LOC) * ZOOM_LOC_HEIGHT;
	SDL_FillRect(big8, NULL, V_BLACK);
	map_pending = 1;           // android_view_map_drawn() adds the game's view

	if( !need_passes )
		return;            // the game's own view covers the window

	int maxTx = zm->max_x_loc - zm->disp_x_loc;
	int maxTy = zm->max_y_loc - zm->disp_y_loc;
	int done_x[MAX_PASSES], done_y[MAX_PASSES], passes = 0;
	int px0 = (int) floorf((wx0 - PASS_MARGIN) / LOC), py0 = (int) floorf((wy0 - PASS_MARGIN) / LOC);

	busy = 1;
	for( int py = py0; py * LOC < wy1 + PASS_MARGIN; py += zm->disp_y_loc )
	{
		for( int px = px0; px * LOC < wx1 + PASS_MARGIN; px += zm->disp_x_loc )
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

	static int last_passes = -1;
	if( passes != last_passes )
	{
		LOGI("map view drawn with %d extra pass(es), zoom %.2f", passes, k);
		last_passes = passes;
	}
}

// In Sys::disp_zoom, after the map (terrain, units, buildings, fog, names,
// the selected building's frame) and before the messages and buttons that go
// over it: take the game's own view into big8.
void android_view_map_drawn()
{
	if( !map_pending )
		return;
	map_pending = 0;
	copy_view_to_big(drawn_tx, drawn_ty);
	big_valid = 1;
	map_gen++;
}

int android_view_busy()
{
	return busy;
}

//------------------------------------------------------------------------
// Vga::flip: show the window.

static int ensure_textures(SDL_Renderer *ren)
{
	if( !bigtex || bigtex_renderer != ren )
	{
		if( bigtex )
			SDL_DestroyTexture(bigtex);
		bigtex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB888,
			SDL_TEXTUREACCESS_STREAMING, BIG_W, BIG_H);
		bigtex_renderer = ren;
		up_gen = (Uint32) -1;
		if( !bigtex )
			LOGI("cannot create the zoom texture: %s", SDL_GetError());
	}
	if( !ovl32 )
		ovl32 = SDL_CreateRGBSurfaceWithFormat(0, ZOOM_WIDTH, ZOOM_HEIGHT, 32, SDL_PIXELFORMAT_ARGB8888);
	if( !ovltex || ovltex_renderer != ren )
	{
		if( ovltex )
			SDL_DestroyTexture(ovltex);
		ovltex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
			SDL_TEXTUREACCESS_STREAMING, ZOOM_WIDTH, ZOOM_HEIGHT);
		ovltex_renderer = ren;
		ovl_gen = (Uint32) -1;
		if( ovltex )
			SDL_SetTextureBlendMode(ovltex, SDL_BLENDMODE_BLEND);
		else
			LOGI("cannot create the overlay texture: %s", SDL_GetError());
	}
	return bigtex && ovl32 && ovltex;
}

// Overlays: pixels of the game's view on screen (front) that differ from the
// map snapshot in big8 are what the game drew over the map.
static void build_overlay(SDL_Surface *front)
{
	SDL_Color *pal = front->format->palette->colors;
	int bx = drawn_tx * ZOOM_LOC_WIDTH - big_x, by = drawn_ty * ZOOM_LOC_HEIGHT - big_y;
	int count = 0;
	for( int y = 0; y < ZOOM_HEIGHT; y++ )
	{
		const Uint8 *f = (const Uint8*) front->pixels + (ZOOM_Y1 + y) * front->pitch + ZOOM_X1;
		int my = by + y;
		const Uint8 *m = (my >= 0 && my < BIG_H) ? (const Uint8*) big8->pixels + my * big8->pitch : NULL;
		Uint32 *o = (Uint32*) ((Uint8*) ovl32->pixels + y * ovl32->pitch);
		Uint8 *mk = ovl_mask + y * ZOOM_WIDTH;
		for( int x = 0; x < ZOOM_WIDTH; x++ )
		{
			int mx = bx + x;
			int diff = m && mx >= 0 && mx < BIG_W && f[x] != m[mx];
			mk[x] = (Uint8) diff;
			if( diff )
			{
				SDL_Color c = pal[f[x]];
				o[x] = 0xFF000000u | ((Uint32) c.r << 16) | ((Uint32) c.g << 8) | c.b;
				count++;
			}
			else
				o[x] = 0;
		}
	}
	SDL_UpdateTexture(ovltex, NULL, ovl32->pixels, ovl32->pitch);
	ovl_valid = 1;

	static int last_count = -1;
	if( (count == 0) != (last_count == 0) || abs(count - last_count) > 5000 )
	{
		LOGI("overlay over the map view: %d pixels", count);
		last_count = count;
	}
}

// The selection box, from the game's own frame (engine coordinates).
static void draw_selection_box(SDL_Renderer *ren, SDL_Surface *front)
{
	if( !mouse_cursor.frame_flag || !mouse_cursor.frame_shown )
		return;
	float x1 = VX + (mouse_cursor.frame_x1 - VX - ox) * k;
	float y1 = VY + (mouse_cursor.frame_y1 - VY - oy) * k;
	float x2 = VX + (mouse_cursor.frame_x2 - VX - ox) * k;
	float y2 = VY + (mouse_cursor.frame_y2 - VY - oy) * k;
	x1 = clampf(x1, VX, VX + VW - 1); x2 = clampf(x2, VX, VX + VW - 1);
	y1 = clampf(y1, VY, VY + VH - 1); y2 = clampf(y2, VY, VY + VH - 1);
	SDL_Color c = { 255, 255, 0, 255 };
	if( front && front->format->palette )
		c = front->format->palette->colors[OWN_SELECT_FRAME_COLOR];
	SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, 255);
	SDL_FRect r1 = { x1, y1, x2 - x1 + 1, y2 - y1 + 1 };
	SDL_FRect r2 = { x1 + 1, y1 + 1, x2 - x1 - 1, y2 - y1 - 1 };
	SDL_RenderDrawRectF(ren, &r1);
	if( r2.w > 0 && r2.h > 0 )
		SDL_RenderDrawRectF(ren, &r2);
}

void android_view_render(SDL_Renderer *ren, SDL_Texture *tex, SDL_Surface *front)
{
	if( !active() )
	{
		ovl_valid = 0;
		return;
	}

	// window and the drawn game view, in world pixels
	float wx0 = cur_tx * LOC + ox, wy0 = cur_ty * LOC + oy;
	float ww = VW / k, wh = VH / k;
	float gx0 = drawn_tx * LOC, gy0 = drawn_ty * LOC;
	int transformed = !(k == 1.0f && wx0 == gx0 && wy0 == gy0);

	if( transformed )
	{
		SDL_Rect view = { ZOOM_X1, ZOOM_Y1, ZOOM_WIDTH, ZOOM_HEIGHT };
		SDL_RenderSetClipRect(ren, &view);
		SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);   // off the map / not drawn: black
		SDL_RenderFillRect(ren, &view);

		if( big_valid && big8 && front && front->format->palette && ensure_textures(ren) )
		{
			//---- the map, zoomed: from the passes and the snapshot ----//

			if( big8->format->palette != front->format->palette )
				SDL_SetSurfacePalette(big8, front->format->palette);
			Uint32 pal_version = front->format->palette->version;

			float sx = wx0 - big_x, sy = wy0 - big_y;
			int x0 = clampi((int) floorf(sx), 0, BIG_W), x1 = clampi((int) ceilf(sx + ww), 0, BIG_W);
			int y0 = clampi((int) floorf(sy), 0, BIG_H), y1 = clampi((int) ceilf(sy + wh), 0, BIG_H);

			static Uint32 last_gap_log;
			if( (sx < 0 || sy < 0 || sx + ww > BIG_W || sy + wh > BIG_H) &&
				 SDL_GetTicks() - last_gap_log > 2000 )
			{
				LOGI("window reaches beyond the drawn area by (%.0f,%.0f)-(%.0f,%.0f) px: black there",
					-sx, -sy, sx + ww - BIG_W, sy + wh - BIG_H);
				last_gap_log = SDL_GetTicks();
			}

			if( x1 > x0 && y1 > y0 )
			{
				SDL_Rect r = { x0, y0, x1 - x0, y1 - y0 };
				if( up_gen != map_gen || up_pal != pal_version ||
					 r.x != up_rect.x || r.y != up_rect.y || r.w != up_rect.w || r.h != up_rect.h )
				{
					SDL_BlitSurface(big8, &r, big32, &r);
					SDL_UpdateTexture(bigtex, &r,
						(Uint8*) big32->pixels + r.y * big32->pitch + r.x * 4, big32->pitch);
					up_gen = map_gen;
					up_pal = pal_version;
					up_rect = r;
				}
				SDL_FRect dst = { VX + (x0 - sx) * k, VY + (y0 - sy) * k,
				                  (x1 - x0) * k, (y1 - y0) * k };
				SDL_RenderCopyF(ren, bigtex, &r, &dst);
			}

			//---- messages and buttons over the map: at their normal place ----//

			Uint32 now = SDL_GetTicks();
			if( ovl_gen != map_gen || now - ovl_time > 100 )
			{
				build_overlay(front);
				ovl_gen = map_gen;
				ovl_time = now;
			}
			SDL_RenderCopy(ren, ovltex, NULL, &view);
		}
		else
		{
			//---- no prepared picture yet: zoom the frame as it is ----//

			static Uint32 last_fallback_log;
			if( SDL_GetTicks() - last_fallback_log > 2000 )
			{
				LOGI("zoomed view shown without the prepared picture (valid %d)", big_valid);
				last_fallback_log = SDL_GetTicks();
			}
			ovl_valid = 0;
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
	else
		ovl_valid = 0;

	draw_selection_box(ren, front);
}

//------------------------------------------------------------------------
// Touch mapping.

int android_view_contains(float x, float y)
{
	return x >= VX && x < VX + VW && y >= VY && y < VY + VH;
}

// Is a message, button or label the game drew over the map at (or right
// next to) display point (x, y)?
static int on_overlay(float x, float y)
{
	if( !ovl_valid )
		return 0;
	int px = (int) (x - VX), py = (int) (y - VY);
	for( int dy = -4; dy <= 4; dy++ )
	{
		int yy = py + dy;
		if( yy < 0 || yy >= ZOOM_HEIGHT )
			continue;
		for( int dx = -4; dx <= 4; dx++ )
		{
			int xx = px + dx;
			if( xx >= 0 && xx < ZOOM_WIDTH && ovl_mask[yy * ZOOM_WIDTH + xx] )
				return 1;
		}
	}
	return 0;
}

int android_view_to_engine(float *x, float *y)
{
	if( !active() || !android_view_contains(*x, *y) )
		return 0;
	if( on_overlay(*x, *y) )
		return 0;          // a message or button shown at its normal place
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
