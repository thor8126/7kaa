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

//Filename    : OROAD.H
//Description : Roads

#ifndef __OROAD_H
#define __OROAD_H

//------------- Define constants --------------//
//
// A road is a straight segment of locations (8-connected), each marked
// with LOCATE_ROAD in Location::loc_flag. Land units move faster on roads.
//

#define ROAD_COST_PER_TILE				5		// cost of each new road location
#define MAX_ROAD_SEGMENT_LENGTH		60		// maximum no. of locations in a road segment
#define ROAD_TILE_SIZE					32		// same as ZOOM_LOC_WIDTH/HEIGHT

enum { ROAD_OK=0,
		 ROAD_ERR_TOO_LONG,
		 ROAD_ERR_BLOCKED,
		 ROAD_ERR_TERRITORY,
		 ROAD_ERR_UNEXPLORED,
		 ROAD_ERR_NO_NEW_TILE,
		 ROAD_ERR_NOT_ENOUGH_CASH };

//------------- Define class RoadRes --------------//

class RoadRes
{
public:
	char	init_flag;

	char	color_edge, color_dark, color_mid, color_light;
	char	map_color;		// colour of road locations on the mini-map

	char*	bitmap_array[256];		// one tile bitmap for each combination of the 8 road neighbours, made when first needed

	short	start_x_loc, start_y_loc;		// start of the road the player is placing, -1 if not set

public:
	RoadRes();
	~RoadRes();

	void	init();
	void	deinit();

	void	draw(int x, int y, int xLoc, int yLoc);
	void	draw_build_preview(int curXLoc, int curYLoc);

	int	check_road(int xLoc1, int yLoc1, int xLoc2, int yLoc2, int nationRecno, int* newTileCount=0);
	int	build_road(int xLoc1, int yLoc1, int xLoc2, int yLoc2, int nationRecno, char remoteAction);
	int	road_tile_count(int xLoc1, int yLoc1, int xLoc2, int yLoc2);

	static const char* error_str(int errorCode);

private:
	int	line_locs(int xLoc1, int yLoc1, int xLoc2, int yLoc2, short* xLocArray, short* yLocArray);
	char*	get_bitmap(int neighborMask);
	int	pick_color(int r, int g, int b);
};

extern RoadRes road_res;

//--------------------------------------------//

#endif
