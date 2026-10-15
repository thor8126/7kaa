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

//Filename    : OAI_ROAD.CPP
//Description : AI functions on building roads

#include <ALL.h>
#include <ONATION.h>
#include <OFIRM.h>
#include <OTOWN.h>
#include <OROAD.h>
#include <OAIWAR.h>
#include <ConfigAdv.h>

//------- Define constants -------//

#define MIN_AI_ROAD_LENGTH		6		// buildings closer than this don't need a road

//--------- Begin of function Nation::think_build_road --------//
//
// Build a road from one of the nation's markets, mines or factories to
// the nearest of its towns, as caravans and units travel between them.
// At most one road is built each time.
//
// return : <int> 1 - a road has been built
//                0 - no road built
//
int Nation::think_build_road()
{
	if( !config_adv.nation_ai_build_roads )
		return 0;

	int curPosture = ai_war_state.get_posture(nation_recno);

	if( curPosture == AI_POSTURE_DEFEND || curPosture == AI_POSTURE_RECOVER )
		return 0;

	if( !ai_should_spend(30 + pref_economic_development/5) )
		return 0;

	//------ consider markets first, then mines and factories ------//

	short* firmArrays[3] = { ai_market_array, ai_mine_array, ai_factory_array };
	short  firmCounts[3] = { ai_market_count, ai_mine_count, ai_factory_count };

	int bestRating=0, bestX1=0, bestY1=0, bestX2=0, bestY2=0;

	for( int type=0 ; type<3 ; type++ )
	{
		for( int i=0 ; i<firmCounts[type] ; i++ )
		{
			Firm* firmPtr = firm_array[ firmArrays[type][i] ];

			if( firmPtr->under_construction )
				continue;

			//------ find the nearest own town in the same region ------//

			Town* nearestTown = NULL;
			int   minDistance = 0x7FFF;

			for( int j=0 ; j<ai_town_count ; j++ )
			{
				Town* townPtr = town_array[ ai_town_array[j] ];

				if( townPtr->region_id != firmPtr->region_id )
					continue;

				int townDistance = MAX( abs(townPtr->center_x - firmPtr->center_x),
												abs(townPtr->center_y - firmPtr->center_y) );

				if( townDistance < minDistance )
				{
					minDistance = townDistance;
					nearestTown = townPtr;
				}
			}

			if( !nearestTown || minDistance < MIN_AI_ROAD_LENGTH || minDistance >= MAX_ROAD_SEGMENT_LENGTH )
				continue;

			//------ skip it if it is already mostly connected ------//

			int roadCount = road_res.road_tile_count( firmPtr->center_x, firmPtr->center_y,
																	nearestTown->center_x, nearestTown->center_y );

			if( roadCount*2 >= minDistance )
				continue;

			int newTileCount;

			if( road_res.check_road( firmPtr->center_x, firmPtr->center_y,
											 nearestTown->center_x, nearestTown->center_y, nation_recno, &newTileCount ) != ROAD_OK )
			{
				continue;
			}

			if( !ai_should_spend(30 + pref_economic_development/5, (float) (newTileCount * ROAD_COST_PER_TILE)) )
				continue;

			//------ busier buildings first, shorter roads first ------//

			int curRating = (3-type) * 100 + MAX_ROAD_SEGMENT_LENGTH - minDistance;

			if( curRating > bestRating )
			{
				bestRating = curRating;
				bestX1 = firmPtr->center_x;
				bestY1 = firmPtr->center_y;
				bestX2 = nearestTown->center_x;
				bestY2 = nearestTown->center_y;
			}
		}
	}

	if( !bestRating )
		return 0;

	return road_res.build_road( bestX1, bestY1, bestX2, bestY2, nation_recno, COMMAND_AI );
}
//---------- End of function Nation::think_build_road --------//
