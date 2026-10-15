/*
 * Seven Kingdoms: Ancient Adversaries
 *
 * Copyright 1997,1998 Enlight Software Ltd.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

//Filename    : OROAD.CPP
//Description : Roads
//
// A road is laid as a straight segment of locations between two points.
// It can only run over land (trees on the way are cleared), through
// unclaimed land or the builder's own territory, and passes under the
// builder's own buildings. Each new road location costs ROAD_COST_PER_TILE.
// Land units move faster on roads, see Unit::process_move().

#include <string.h>
#include <stdlib.h>
#include <ALL.h>
#include <COLCODE.h>
#include <COLOR.h>
#include <OROAD.h>
#include <OWORLD.h>
#include <OWORLDMT.h>
#include <OMATRIX.h>
#include <OVGA.h>
#include <OVGABUF.h>
#include <OFONT.h>
#include <ONATION.h>
#include <OFIRM.h>
#include <OTOWN.h>
#include <OREMOTE.h>
#include <OSYS.h>
#include <OAIWAR.h>
#include "gettext.h"

//------- Define constants -------//

#define ROAD_HALF_WIDTH			6		// half width of the road in pixels
#define RESERVED_COLOR_FIRST	0xE0	// palette colours remapped to nation colours, and the selection outline colour
#define RESERVED_COLOR_LAST	0xEF

// neighbour bit for each direction: N, NE, E, SE, S, SW, W, NW
static const int neighbor_x_array[8] = {  0,  1, 1, 1, 0, -1, -1, -1 };
static const int neighbor_y_array[8] = { -1, -1, 0, 1, 1,  1,  0, -1 };

//--------- Begin of function RoadRes::RoadRes ---------//
//
RoadRes::RoadRes()
{
	init_flag = 0;
	memset( bitmap_array, 0, sizeof(bitmap_array) );
	start_x_loc = start_y_loc = -1;
}
//---------- End of function RoadRes::RoadRes ----------//


//--------- Begin of function RoadRes::~RoadRes ---------//
//
RoadRes::~RoadRes()
{
	deinit();
}
//---------- End of function RoadRes::~RoadRes ----------//


//--------- Begin of function RoadRes::init ---------//
//
// Pick the road colours from the game palette. It must be called after
// the game palette has been loaded.
//
void RoadRes::init()
{
	deinit();

	color_edge  = pick_color( 92,  74,  52);
	color_dark  = pick_color(122,  98,  68);
	color_mid   = pick_color(150, 122,  86);
	color_light = pick_color(176, 150, 110);
	map_color   = color_dark;

	start_x_loc = start_y_loc = -1;

	init_flag = 1;
}
//---------- End of function RoadRes::init ----------//


//--------- Begin of function RoadRes::deinit ---------//
//
void RoadRes::deinit()
{
	for( int i=0 ; i<256 ; i++ )
	{
		if( bitmap_array[i] )
		{
			mem_del( bitmap_array[i] );
			bitmap_array[i] = NULL;
		}
	}

	init_flag = 0;
}
//---------- End of function RoadRes::deinit ----------//


//--------- Begin of function RoadRes::pick_color ---------//
//
// return : <int> the palette colour closest to the given RGB, avoiding
//			  the colours with special meanings.
//
int RoadRes::pick_color(int r, int g, int b)
{
	int bestColor=0, bestDist=0x7FFFFFFF;
	const SDL_Color* gamePal = vga.get_game_pal();

	for( int i=0 ; i<VGA_PALETTE_SIZE ; i++ )
	{
		if( (i >= RESERVED_COLOR_FIRST && i <= RESERVED_COLOR_LAST) || i == TRANSPARENT_CODE )
			continue;

		int dr = gamePal[i].r - r;
		int dg = gamePal[i].g - g;
		int db = gamePal[i].b - b;
		int dist = 2*dr*dr + 4*dg*dg + 3*db*db;

		if( dist < bestDist )
		{
			bestDist  = dist;
			bestColor = i;
		}
	}

	return bestColor;
}
//---------- End of function RoadRes::pick_color ----------//


//--------- Begin of function RoadRes::get_bitmap ---------//
//
// Return the tile bitmap of a road location connected to the neighbours
// in neighborMask, making it when it is first needed.
//
char* RoadRes::get_bitmap(int neighborMask)
{
	if( bitmap_array[neighborMask] )
		return bitmap_array[neighborMask];

	char* bitmapPtr = mem_add( 4 + ROAD_TILE_SIZE*ROAD_TILE_SIZE );

	((short*)bitmapPtr)[0] = ROAD_TILE_SIZE;
	((short*)bitmapPtr)[1] = ROAD_TILE_SIZE;

	char* pixelPtr = bitmapPtr + 4;
	const int center = ROAD_TILE_SIZE/2;

	for( int y=0 ; y<ROAD_TILE_SIZE ; y++ )
	{
		for( int x=0 ; x<ROAD_TILE_SIZE ; x++ )
		{
			//--- the squared distance from the pixel to the road: the centre and a segment towards each connected neighbour ---//

			int px = x-center, py = y-center;
			int minDist = px*px + py*py;

			for( int dir=0 ; dir<8 ; dir++ )
			{
				if( !(neighborMask & (1<<dir)) )
					continue;

				int ex = neighbor_x_array[dir] * ROAD_TILE_SIZE;		// to the centre of the neighbour, clipped by the tile edge
				int ey = neighbor_y_array[dir] * ROAD_TILE_SIZE;
				int lenSq = ex*ex + ey*ey;
				int dot = px*ex + py*ey;

				if( dot <= 0 )
					continue;

				int dist;

				if( dot >= lenSq )
					dist = (px-ex)*(px-ex) + (py-ey)*(py-ey);
				else
				{
					int cross = px*ey - py*ex;
					dist = (int) ((long long) cross * cross / lenSq);
				}

				if( dist < minDist )
					minDist = dist;
			}

			char color;

			if( minDist > ROAD_HALF_WIDTH*ROAD_HALF_WIDTH )
				color = (char) TRANSPARENT_CODE;
			else if( minDist > (ROAD_HALF_WIDTH-1)*(ROAD_HALF_WIDTH-1) )
				color = color_edge;
			else
			{
				//------ a speckled dirt texture ------//

				unsigned int hash = (unsigned int) x * 73856093u ^ (unsigned int) y * 19349663u;		// unsigned, signed overflow is undefined
				hash ^= hash >> 13;

				switch( hash % 7 )
				{
					case 0:
						color = color_dark;
						break;
					case 1:
					case 2:
						color = color_light;
						break;
					default:
						color = color_mid;
				}
			}

			*pixelPtr++ = color;
		}
	}

	bitmap_array[neighborMask] = bitmapPtr;
	return bitmapPtr;
}
//---------- End of function RoadRes::get_bitmap ----------//


//--------- Begin of function RoadRes::draw ---------//
//
// Draw the road on the given location on the zoom view (vga_back).
//
void RoadRes::draw(int x, int y, int xLoc, int yLoc)
{
	if( !init_flag )
		return;

	int neighborMask = 0;

	for( int dir=0 ; dir<8 ; dir++ )
	{
		int nx = xLoc + neighbor_x_array[dir];
		int ny = yLoc + neighbor_y_array[dir];

		if( nx < 0 || ny < 0 || nx >= MAX_WORLD_X_LOC || ny >= MAX_WORLD_Y_LOC )
			continue;

		Location* locPtr = world.get_loc(nx, ny);

		if( locPtr->has_road() || locPtr->is_firm() || locPtr->is_town() )		// roads lead into buildings
			neighborMask |= 1<<dir;
	}

	vga_back.put_bitmap_trans( x, y, get_bitmap(neighborMask) );
}
//---------- End of function RoadRes::draw ----------//


//--------- Begin of function RoadRes::line_locs ---------//
//
// Get the locations of a straight, 8-connected line between two locations.
//
// return : <int> the no. of locations, 0 if the line is longer than MAX_ROAD_SEGMENT_LENGTH
//
int RoadRes::line_locs(int xLoc1, int yLoc1, int xLoc2, int yLoc2, short* xLocArray, short* yLocArray)
{
	int dx = abs(xLoc2-xLoc1), dy = abs(yLoc2-yLoc1);
	int sx = xLoc1 < xLoc2 ? 1 : -1;
	int sy = yLoc1 < yLoc2 ? 1 : -1;
	int count = MAX(dx, dy) + 1;

	if( count > MAX_ROAD_SEGMENT_LENGTH )
		return 0;

	int err = dx - dy;
	int x = xLoc1, y = yLoc1;

	for( int i=0 ; i<count ; i++ )
	{
		xLocArray[i] = x;
		yLocArray[i] = y;

		int err2 = err*2;

		if( err2 > -dy )
		{
			err -= dy;
			x += sx;
		}

		if( err2 < dx )
		{
			err += dx;
			y += sy;
		}
	}

	return count;
}
//---------- End of function RoadRes::line_locs ----------//


//--------- Begin of function RoadRes::check_road ---------//
//
// Check whether a nation can build a road between two locations.
//
// <int*> newTileCount - if given, returns the no. of new road locations
//
// return : <int> ROAD_OK or a ROAD_ERR_ code
//
int RoadRes::check_road(int xLoc1, int yLoc1, int xLoc2, int yLoc2, int nationRecno, int* newTileCount)
{
	short xLocArray[MAX_ROAD_SEGMENT_LENGTH], yLocArray[MAX_ROAD_SEGMENT_LENGTH];

	if( newTileCount )
		*newTileCount = 0;

	if( !nationRecno || nation_array.is_deleted(nationRecno) )
		return ROAD_ERR_BLOCKED;

	if( xLoc1 < 0 || yLoc1 < 0 || xLoc2 < 0 || yLoc2 < 0 ||
		 xLoc1 >= MAX_WORLD_X_LOC || yLoc1 >= MAX_WORLD_Y_LOC ||
		 xLoc2 >= MAX_WORLD_X_LOC || yLoc2 >= MAX_WORLD_Y_LOC )
	{
		return ROAD_ERR_BLOCKED;
	}

	int count = line_locs(xLoc1, yLoc1, xLoc2, yLoc2, xLocArray, yLocArray);

	if( !count )
		return ROAD_ERR_TOO_LONG;

	int newCount = 0;
	int isPlayer = nationRecno == nation_array.player_recno;

	for( int i=0 ; i<count ; i++ )
	{
		Location* locPtr = world.get_loc(xLocArray[i], yLocArray[i]);

		if( isPlayer && !locPtr->explored() )
			return ROAD_ERR_UNEXPLORED;

		//--- the road passes under the nation's own buildings ---//

		if( locPtr->is_firm() )
		{
			if( firm_array[locPtr->firm_recno()]->nation_recno != nationRecno )
				return ROAD_ERR_BLOCKED;
			continue;
		}

		if( locPtr->is_town() )
		{
			if( town_array[locPtr->town_recno()]->nation_recno != nationRecno )
				return ROAD_ERR_BLOCKED;
			continue;
		}

		//--- land only, trees on the way are cleared ---//

		if( !locPtr->is_plant() &&
			 ( !(locPtr->loc_flag & LOCATE_WALK_LAND) || (locPtr->loc_flag & LOCATE_BLOCK_MASK) ) )
		{
			return ROAD_ERR_BLOCKED;
		}

		//--- only in unclaimed land or the nation's own territory ---//

		if( locPtr->power_nation_recno && locPtr->power_nation_recno != nationRecno )
			return ROAD_ERR_TERRITORY;

		if( !locPtr->has_road() )
			newCount++;
	}

	if( newTileCount )
		*newTileCount = newCount;

	if( newCount == 0 )
		return ROAD_ERR_NO_NEW_TILE;

	if( nation_array[nationRecno]->cash < newCount * ROAD_COST_PER_TILE )
		return ROAD_ERR_NOT_ENOUGH_CASH;

	return ROAD_OK;
}
//---------- End of function RoadRes::check_road ----------//


//--------- Begin of function RoadRes::build_road ---------//
//
// Build a road between two locations.
//
// <char> remoteAction - COMMAND_PLAYER, COMMAND_AI or COMMAND_REMOTE
//
// return : <int> 1 - the road has been built (or the request sent in a multiplayer game)
//                0 - the road cannot be built
//
int RoadRes::build_road(int xLoc1, int yLoc1, int xLoc2, int yLoc2, int nationRecno, char remoteAction)
{
	//------ in a multiplayer game, let all players build it at the same frame ------//

	if( !remoteAction && remote.is_enable() )
	{
		if( check_road(xLoc1, yLoc1, xLoc2, yLoc2, nationRecno) != ROAD_OK )
			return 0;

		// packet structure : <nation recno> <xLoc1> <yLoc1> <xLoc2> <yLoc2>
		short *shortPtr = (short *)remote.new_send_queue_msg(MSG_ROAD_BUILD, 5*sizeof(short));
		shortPtr[0] = nationRecno;
		shortPtr[1] = xLoc1;
		shortPtr[2] = yLoc1;
		shortPtr[3] = xLoc2;
		shortPtr[4] = yLoc2;
		return 1;
	}

	//--- check again, as the situation may have changed since the request was sent ---//

	int newTileCount;

	if( check_road(xLoc1, yLoc1, xLoc2, yLoc2, nationRecno, &newTileCount) != ROAD_OK )
		return 0;

	short xLocArray[MAX_ROAD_SEGMENT_LENGTH], yLocArray[MAX_ROAD_SEGMENT_LENGTH];

	int count = line_locs(xLoc1, yLoc1, xLoc2, yLoc2, xLocArray, yLocArray);

	for( int i=0 ; i<count ; i++ )
	{
		Location* locPtr = world.get_loc(xLocArray[i], yLocArray[i]);

		if( locPtr->is_firm() || locPtr->is_town() )
			continue;

		if( locPtr->is_plant() )
		{
			locPtr->remove_plant();
			world.plant_count--;
		}

		locPtr->set_road();
	}

	nation_array[nationRecno]->add_expense( EXPENSE_FIRM, (float) (newTileCount * ROAD_COST_PER_TILE) );

	sys.zoom_need_redraw = 1;
	sys.map_need_redraw = 1;

	AiWarState::log( "ROAD        nation %d builds %d new road tiles from %d,%d to %d,%d",
						  nationRecno, newTileCount, xLoc1, yLoc1, xLoc2, yLoc2 );

	return 1;
}
//---------- End of function RoadRes::build_road ----------//


//--------- Begin of function RoadRes::road_tile_count ---------//
//
// return : <int> the no. of road locations on the straight line between two locations
//
int RoadRes::road_tile_count(int xLoc1, int yLoc1, int xLoc2, int yLoc2)
{
	short xLocArray[MAX_ROAD_SEGMENT_LENGTH], yLocArray[MAX_ROAD_SEGMENT_LENGTH];

	int count = line_locs(xLoc1, yLoc1, xLoc2, yLoc2, xLocArray, yLocArray);
	int roadCount = 0;

	for( int i=0 ; i<count ; i++ )
	{
		if( world.get_loc(xLocArray[i], yLocArray[i])->has_road() )
			roadCount++;
	}

	return roadCount;
}
//---------- End of function RoadRes::road_tile_count ----------//


//--------- Begin of function RoadRes::error_str ---------//
//
const char* RoadRes::error_str(int errorCode)
{
	switch( errorCode )
	{
		case ROAD_ERR_TOO_LONG:
			return _("Too long, build the road in shorter segments");
		case ROAD_ERR_BLOCKED:
			return _("Blocked, roads need clear land");
		case ROAD_ERR_TERRITORY:
			return _("Cannot build in another kingdom's territory");
		case ROAD_ERR_UNEXPLORED:
			return _("Cannot build in unexplored land");
		case ROAD_ERR_NO_NEW_TILE:
			return _("There is already a road here");
		case ROAD_ERR_NOT_ENOUGH_CASH:
			return _("Not enough money");
	}

	return "";
}
//---------- End of function RoadRes::error_str ----------//


//--------- Begin of function RoadRes::draw_build_preview ---------//
//
// Show where the road the player is placing will go, on the zoom view.
//
// <int> curXLoc, curYLoc - the location under the mouse cursor
//
void RoadRes::draw_build_preview(int curXLoc, int curYLoc)
{
	int playerRecno = nation_array.player_recno;

	if( !playerRecno )
		return;

	ZoomMatrix* zoomMatrix = world.zoom_matrix;
	int xLoc1 = start_x_loc >= 0 ? start_x_loc : curXLoc;
	int yLoc1 = start_y_loc >= 0 ? start_y_loc : curYLoc;

	short xLocArray[MAX_ROAD_SEGMENT_LENGTH], yLocArray[MAX_ROAD_SEGMENT_LENGTH];

	int count = line_locs(xLoc1, yLoc1, curXLoc, curYLoc, xLocArray, yLocArray);
	int newTileCount = 0;
	int rc = check_road(xLoc1, yLoc1, curXLoc, curYLoc, playerRecno, &newTileCount);

	if( start_x_loc < 0 && rc == ROAD_ERR_NO_NEW_TILE )		// only the start location, which already has a road, is fine as the start
		rc = ROAD_OK;

	int color = rc == ROAD_OK ? V_LIGHT_GREEN : V_RED;

	//------- outline the road locations --------//

	for( int i=0 ; i<count ; i++ )
	{
		int x = ZOOM_X1 + (xLocArray[i]-zoomMatrix->top_x_loc) * ZOOM_LOC_WIDTH;
		int y = ZOOM_Y1 + (yLocArray[i]-zoomMatrix->top_y_loc) * ZOOM_LOC_HEIGHT;

		if( x < ZOOM_X1 || y < ZOOM_Y1 || x+ZOOM_LOC_WIDTH-1 > ZOOM_X2 || y+ZOOM_LOC_HEIGHT-1 > ZOOM_Y2 )
			continue;

		vga_back.bar( x, y, x+ZOOM_LOC_WIDTH-1, y, color );
		vga_back.bar( x, y+ZOOM_LOC_HEIGHT-1, x+ZOOM_LOC_WIDTH-1, y+ZOOM_LOC_HEIGHT-1, color );
		vga_back.bar( x, y, x, y+ZOOM_LOC_HEIGHT-1, color );
		vga_back.bar( x+ZOOM_LOC_WIDTH-1, y, x+ZOOM_LOC_WIDTH-1, y+ZOOM_LOC_HEIGHT-1, color );
	}

	//------- describe the road --------//

	String str;

	if( start_x_loc < 0 )
	{
		str = _("Road: click where the road starts");
	}
	else if( rc == ROAD_OK )
	{
		str  = _("Road: click where it ends. New tiles: ");
		str += newTileCount;
		str += _(", cost: ");
		str += misc.format( newTileCount * ROAD_COST_PER_TILE, 2 );
	}
	else
	{
		str  = _("Road: ");
		str += error_str(rc);
	}

	VgaBuf* activeBufBackup = Vga::active_buf;
	Vga::active_buf = &vga_back;

	int textY = ZOOM_Y2 - font_san.height() - 6;
	vga_back.bar( ZOOM_X1+4, textY-2, ZOOM_X1+8+font_san.text_width(str), textY+font_san.height()+1, V_BLACK );
	font_san.put( ZOOM_X1+6, textY, str );

	Vga::active_buf = activeBufBackup;
}
//---------- End of function RoadRes::draw_build_preview ----------//
