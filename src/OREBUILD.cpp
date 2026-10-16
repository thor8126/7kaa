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

//Filename    : OREBUILD.CPP
//Description : Rebuilding the player's destroyed buildings
//
// When one of the player's buildings is destroyed by an enemy (not sold
// or destructed by the player), it is remembered. Every few days, once
// the site is clear and the player has the cash, the nearest idle
// construction worker of the player on the same land goes to rebuild it.
// Buildings not rebuilt within REBUILD_EXPIRE_DAYS are forgotten. The
// list is not saved with the game (config_adv firm_auto_rebuild).

#include <ALL.h>
#include <OREBUILD.h>
#include <OFIRM.h>
#include <OFIRMRES.h>
#include <OUNIT.h>
#include <OUNITRES.h>
#include <ONATION.h>
#include <OWORLD.h>
#include <OINFO.h>
#include <ONEWS.h>
#include <OAIWAR.h>
#include <OMISC.h>
#include <ConfigAdv.h>

//------- Define constants -------//

#define REBUILD_CHECK_INTERVAL_DAYS		5

//------- Define global variables -------//

RebuildList rebuild_list;

//--------- Begin of function RebuildList::add ---------//
//
// Remember a building of the player that an enemy has destroyed.
//
void RebuildList::add(Firm* firmPtr)
{
	if( !config_adv.firm_auto_rebuild || !nation_array.player_recno ||
		 firmPtr->nation_recno != nation_array.player_recno )
	{
		return;
	}

	if( firmPtr->firm_id == FIRM_BASE || firmPtr->firm_id == FIRM_MONSTER )		// seats of power need a scroll of power and a race
	{
		AiWarState::log( "REBUILD     not remembering the destroyed %s at %d,%d", firm_res[firmPtr->firm_id]->name, firmPtr->loc_x1, firmPtr->loc_y1 );
		return;
	}

	if( rebuild_count == MAX_REBUILD_FIRM )		// forget the oldest one
		del(0);

	RebuildFirm* rebuildFirm = rebuild_array + rebuild_count++;

	rebuildFirm->firm_id = firmPtr->firm_id;
	rebuildFirm->x_loc = firmPtr->loc_x1;
	rebuildFirm->y_loc = firmPtr->loc_y1;
	rebuildFirm->destroyed_date = info.game_date;

	AiWarState::log( "REBUILD     remembering the destroyed %s at %d,%d (%d waiting)",
						  firm_res[firmPtr->firm_id]->name, firmPtr->loc_x1, firmPtr->loc_y1, rebuild_count );
}
//----------- End of function RebuildList::add -----------//


//--------- Begin of function RebuildList::next_day ---------//
//
void RebuildList::next_day()
{
	if( !rebuild_count || info.game_date % REBUILD_CHECK_INTERVAL_DAYS )
		return;

	if( !config_adv.firm_auto_rebuild || !nation_array.player_recno ||
		 nation_array.is_deleted(nation_array.player_recno) )
	{
		rebuild_count = 0;
		return;
	}

	for( int i=rebuild_count-1 ; i>=0 ; i-- )
	{
		RebuildFirm* rebuildFirm = rebuild_array + i;

		if( info.game_date - rebuildFirm->destroyed_date > REBUILD_EXPIRE_DAYS )
		{
			AiWarState::log( "REBUILD     forgetting the %s at %d,%d, not rebuilt within a year",
								  firm_res[rebuildFirm->firm_id]->name, rebuildFirm->x_loc, rebuildFirm->y_loc );
			del(i);
			continue;
		}

		if( rebuild(rebuildFirm) )
			del(i);
	}
}
//----------- End of function RebuildList::next_day -----------//


//--------- Begin of function RebuildList::rebuild ---------//
//
// return : <int> 1 if a construction worker has gone to rebuild it
//
int RebuildList::rebuild(RebuildFirm* rebuildFirm)
{
	Nation*   nationPtr = nation_array[nation_array.player_recno];
	FirmInfo* firmInfo  = firm_res[rebuildFirm->firm_id];

	if( nationPtr->cash < firmInfo->setup_cost )
	{
		AiWarState::log( "REBUILD     waiting to rebuild the %s at %d,%d: not enough cash (%.0f < %d)",
							  firmInfo->name, rebuildFirm->x_loc, rebuildFirm->y_loc, nationPtr->cash, firmInfo->setup_cost );
		return 0;
	}

	if( !world.can_build_firm(rebuildFirm->x_loc, rebuildFirm->y_loc, rebuildFirm->firm_id) )
	{
		AiWarState::log( "REBUILD     waiting to rebuild the %s at %d,%d: the site is not clear",
							  firmInfo->name, rebuildFirm->x_loc, rebuildFirm->y_loc );
		return 0;
	}

	//------ the nearest idle construction worker on the same land ------//

	int   regionId = world.get_region_id(rebuildFirm->x_loc, rebuildFirm->y_loc);
	Unit* bestUnit = NULL;
	int   minDistance = 0x7FFF;
	int   busyCount = 0, elsewhereCount = 0;		// construction workers that can't go now, for the log

	for( int i=unit_array.size() ; i>0 ; i-- )
	{
		if( unit_array.is_deleted(i) )
			continue;

		Unit* unitPtr = unit_array[i];

		if( unitPtr->nation_recno != nation_array.player_recno || unitPtr->spy_recno ||
			 unitPtr->skill.skill_id != SKILL_CONSTRUCTION )
		{
			continue;
		}

		if( !unitPtr->is_visible() || unitPtr->unit_mode != 0 || unitPtr->region_id() != regionId )
		{
			elsewhereCount++;		// in a building, on a ship or on other land
			continue;
		}

		if( unitPtr->cur_action != SPRITE_IDLE || unitPtr->action_mode != ACTION_STOP ||
			 unitPtr->action_mode2 != ACTION_STOP || !firmInfo->can_build(i) )
		{
			busyCount++;
			continue;
		}

		int curDistance = misc.points_distance( unitPtr->next_x_loc(), unitPtr->next_y_loc(),
															 rebuildFirm->x_loc, rebuildFirm->y_loc );

		if( curDistance < minDistance )
		{
			minDistance = curDistance;
			bestUnit = unitPtr;
		}
	}

	if( !bestUnit )
	{
		AiWarState::log( "REBUILD     waiting to rebuild the %s at %d,%d: no idle construction worker on that land (%d busy, %d elsewhere)",
							  firmInfo->name, rebuildFirm->x_loc, rebuildFirm->y_loc, busyCount, elsewhereCount );
		return 0;
	}

	bestUnit->build_firm( rebuildFirm->x_loc, rebuildFirm->y_loc, rebuildFirm->firm_id, COMMAND_AUTO );

	if( bestUnit->action_mode2 != ACTION_BUILD_FIRM )
	{
		AiWarState::log( "REBUILD     unit %d couldn't be sent to rebuild the %s at %d,%d",
							  bestUnit->sprite_recno, firmInfo->name, rebuildFirm->x_loc, rebuildFirm->y_loc );
		return 0;
	}

	news_array.rebuilding_firm( rebuildFirm->firm_id, rebuildFirm->x_loc, rebuildFirm->y_loc );

	AiWarState::log( "REBUILD     unit %d goes to rebuild the %s at %d,%d (%d locations away)",
						  bestUnit->sprite_recno, firmInfo->name, rebuildFirm->x_loc, rebuildFirm->y_loc, minDistance );
	return 1;
}
//----------- End of function RebuildList::rebuild -----------//


//--------- Begin of function RebuildList::del ---------//
//
void RebuildList::del(int i)
{
	misc.del_array_rec( rebuild_array, rebuild_count, sizeof(RebuildFirm), i+1 );
	rebuild_count--;
}
//----------- End of function RebuildList::del -----------//
