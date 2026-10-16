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

//Filename    : OUNITSUP.CPP
//Description : Supply of armies in the field
//
// A soldier, general, king or war machine out on the map is supplied when
// it is within SUPPLY_BUILDING_RANGE of one of its own or an allied
// kingdom's towns or buildings (territory itself only reaches 2 locations
// beyond buildings, see World::set_power), near a road that is not in
// enemy territory, or within SUPPLY_WAGON_RANGE of one of its own or an
// allied kingdom's supply wagons. Out of supply it does not recover hit
// points, and it slowly loses them (attrition), but never below
// SUPPLY_MIN_HIT_POINTS_PERCENT, so attrition alone never kills a unit.
//
// Supply wagons are built in war factories without research. They can't
// attack: ordered to attack along with an army, they follow it instead,
// staying SUPPLY_WAGON_FOLLOW_DISTANCE short of the target.
//
// The player's units that stand idle far from home (no own town or firm
// within SUPPLY_BUILDING_RANGE) for AUTO_RETURN_IDLE_DAYS go back on their
// own: soldiers and war machines to their fort or the nearest fort with
// room, generals to a fort without a commander, civilians to the nearest
// town with room, otherwise next to the nearest own building.

#include <ALL.h>
#include <OUNIT.h>
#include <OUNITRES.h>
#include <ONATION.h>
#include <OWORLD.h>
#include <OMATRIX.h>
#include <OTOWN.h>
#include <OFIRM.h>
#include <OINFO.h>
#include <ConfigAdv.h>
#include <OAIWAR.h>
#include <OMISC.h>

//------- Define constants -------//

#define SUPPLY_ROAD_RANGE					3		// a road within this distance supplies a unit
#define SUPPLY_BUILDING_RANGE				15		// an own or allied town or building within this distance supplies a unit
#define SUPPLY_WAGON_FOLLOW_DISTANCE	4		// a supply wagon following an army attack stops this far from the target
#define SUPPLY_CHECK_INTERVAL_DAYS		10
#define AUTO_RETURN_IDLE_DAYS				60		// the player's idle units far from home return after this many days
#define SUPPLY_ATTRITION_PERCENT			3		// hit points lost each SUPPLY_CHECK_INTERVAL_DAYS out of supply
#define SUPPLY_MIN_HIT_POINTS_PERCENT	30		// attrition stops at this level

//--------- Begin of function Unit::is_supply_unit ---------//
//
// return : <int> 1 if supply rules apply to this unit:
//			  a soldier, general, king or war machine out on the map on land.
//
int Unit::is_supply_unit()
{
	if( !config_adv.unit_supply_attrition )
		return 0;

	if( !nation_recno || spy_recno || mobile_type != UNIT_LAND || !is_visible() )
		return 0;

	UnitInfo* unitInfo = unit_res[unit_id];

	if( unitInfo->unit_class == UNIT_CLASS_WEAPON )
		return 1;

	return unitInfo->unit_class == UNIT_CLASS_HUMAN && race_id && !is_civilian();
}
//----------- End of function Unit::is_supply_unit -----------//


//--------- Begin of static function loc_rect_distance ---------//
//
// return : <int> the distance in locations from a location to a rectangle of locations
//
static int loc_rect_distance(int xLoc, int yLoc, int x1, int y1, int x2, int y2)
{
	int dx = xLoc < x1 ? x1-xLoc : (xLoc > x2 ? xLoc-x2 : 0);
	int dy = yLoc < y1 ? y1-yLoc : (yLoc > y2 ? yLoc-y2 : 0);

	return MAX(dx, dy);
}
//----------- End of static function loc_rect_distance -----------//


//--------- Begin of function Unit::is_supplying_nation ---------//
//
// return : <int> 1 if the given nation supplies this unit: its own nation or an ally
//
int Unit::is_supplying_nation(int supplierNationRecno)
{
	if( !supplierNationRecno )
		return 0;

	if( supplierNationRecno == nation_recno )
		return 1;

	return supplierNationRecno <= MAX_NATION && !nation_array.is_deleted(supplierNationRecno) &&
			 nation_array[nation_recno]->get_relation_status(supplierNationRecno) == NATION_ALLIANCE;
}
//----------- End of function Unit::is_supplying_nation -----------//


//--------- Begin of function Unit::is_supplied ---------//
//
// return : <int> 1 if the unit is in its own or an allied kingdom's
//			  territory, near a road not in enemy territory, near its or an
//			  ally's supply wagon, or near its or an ally's town or building.
//
int Unit::is_supplied()
{
	Nation* ownNation = nation_array[nation_recno];

	int xLoc = next_x_loc();
	int yLoc = next_y_loc();

	//------ in its own or an allied kingdom's territory ------//

	int powerNationRecno = world.get_loc(xLoc, yLoc)->power_nation_recno;

	if( powerNationRecno == nation_recno )
		return 1;

	if( powerNationRecno > 0 && powerNationRecno <= MAX_NATION &&		// MAX_NATION+1 means contested by several kingdoms
		 !nation_array.is_deleted(powerNationRecno) &&
		 ownNation->get_relation_status(powerNationRecno) == NATION_ALLIANCE )
	{
		return 1;
	}

	//------ near a road outside enemy territory ------//

	int xLoc1 = MAX(0, xLoc-SUPPLY_ROAD_RANGE), xLoc2 = MIN(MAX_WORLD_X_LOC-1, xLoc+SUPPLY_ROAD_RANGE);
	int yLoc1 = MAX(0, yLoc-SUPPLY_ROAD_RANGE), yLoc2 = MIN(MAX_WORLD_Y_LOC-1, yLoc+SUPPLY_ROAD_RANGE);

	for( int y=yLoc1 ; y<=yLoc2 ; y++ )
	{
		Location* locPtr = world.get_loc(xLoc1, y);

		for( int x=xLoc1 ; x<=xLoc2 ; x++, locPtr++ )
		{
			if( !locPtr->has_road() )
				continue;

			int roadPower = locPtr->power_nation_recno;

			if( roadPower == 0 || roadPower == nation_recno ||
				 ( roadPower <= MAX_NATION && !nation_array.is_deleted(roadPower) &&
					ownNation->get_relation_status(roadPower) == NATION_ALLIANCE ) )
			{
				return 1;
			}
		}
	}

	//------ near an own or allied supply wagon ------//

	xLoc1 = MAX(0, xLoc-SUPPLY_WAGON_RANGE), xLoc2 = MIN(MAX_WORLD_X_LOC-1, xLoc+SUPPLY_WAGON_RANGE);
	yLoc1 = MAX(0, yLoc-SUPPLY_WAGON_RANGE), yLoc2 = MIN(MAX_WORLD_Y_LOC-1, yLoc+SUPPLY_WAGON_RANGE);

	for( int y=yLoc1 ; y<=yLoc2 ; y++ )
	{
		Location* locPtr = world.get_loc(xLoc1, y);

		for( int x=xLoc1 ; x<=xLoc2 ; x++, locPtr++ )
		{
			if( !locPtr->has_unit(UNIT_LAND) )
				continue;

			int unitRecno = locPtr->unit_recno(UNIT_LAND);

			if( unit_array.is_deleted(unitRecno) )
				continue;

			Unit* unitPtr = unit_array[unitRecno];

			if( unitPtr->unit_id == UNIT_SUPPLY_WAGON && is_supplying_nation(unitPtr->nation_recno) )
				return 1;
		}
	}

	//------ near an own or allied town or building ------//

	int i;

	for( i=town_array.size() ; i>0 ; i-- )
	{
		if( town_array.is_deleted(i) )
			continue;

		Town* townPtr = town_array[i];

		if( is_supplying_nation(townPtr->nation_recno) &&
			 loc_rect_distance(xLoc, yLoc, townPtr->loc_x1, townPtr->loc_y1, townPtr->loc_x2, townPtr->loc_y2) <= SUPPLY_BUILDING_RANGE )
		{
			return 1;
		}
	}

	for( i=firm_array.size() ; i>0 ; i-- )
	{
		if( firm_array.is_deleted(i) )
			continue;

		Firm* firmPtr = firm_array[i];

		if( is_supplying_nation(firmPtr->nation_recno) &&
			 loc_rect_distance(xLoc, yLoc, firmPtr->loc_x1, firmPtr->loc_y1, firmPtr->loc_x2, firmPtr->loc_y2) <= SUPPLY_BUILDING_RANGE )
		{
			return 1;
		}
	}

	return 0;
}
//----------- End of function Unit::is_supplied -----------//


//--------- Begin of function Unit::supply_wagon_follow ---------//
//
// A supply wagon can't attack. Ordered to attack along with an army, it
// follows the army instead, stopping SUPPLY_WAGON_FOLLOW_DISTANCE short of
// the target, on its way there.
//
// <int> targetXLoc, targetYLoc - location of the target
//
// return : <int> 1 if this is a supply wagon and it has been given the order
//
int Unit::supply_wagon_follow(int targetXLoc, int targetYLoc)
{
	if( unit_id != UNIT_SUPPLY_WAGON || is_unit_dead() )
		return 0;

	int curXLoc = next_x_loc();
	int curYLoc = next_y_loc();
	int curDistance = misc.points_distance(curXLoc, curYLoc, targetXLoc, targetYLoc);

	if( curDistance <= SUPPLY_WAGON_FOLLOW_DISTANCE )
	{
		stop2(KEEP_DEFENSE_MODE);
		return 1;
	}

	int destXLoc = targetXLoc + (curXLoc-targetXLoc) * SUPPLY_WAGON_FOLLOW_DISTANCE / curDistance;
	int destYLoc = targetYLoc + (curYLoc-targetYLoc) * SUPPLY_WAGON_FOLLOW_DISTANCE / curDistance;

	move_to(destXLoc, destYLoc);
	return 1;
}
//----------- End of function Unit::supply_wagon_follow -----------//


//--------- Begin of function Unit::is_near_own_building ---------//
//
// return : <int> 1 if one of the nation's towns or firms is within
//			  SUPPLY_BUILDING_RANGE of the unit
//
int Unit::is_near_own_building()
{
	int xLoc = next_x_loc();
	int yLoc = next_y_loc();
	int i;

	for( i=town_array.size() ; i>0 ; i-- )
	{
		if( town_array.is_deleted(i) )
			continue;

		Town* townPtr = town_array[i];

		if( townPtr->nation_recno == nation_recno &&
			 loc_rect_distance(xLoc, yLoc, townPtr->loc_x1, townPtr->loc_y1, townPtr->loc_x2, townPtr->loc_y2) <= SUPPLY_BUILDING_RANGE )
		{
			return 1;
		}
	}

	for( i=firm_array.size() ; i>0 ; i-- )
	{
		if( firm_array.is_deleted(i) )
			continue;

		Firm* firmPtr = firm_array[i];

		if( firmPtr->nation_recno == nation_recno &&
			 loc_rect_distance(xLoc, yLoc, firmPtr->loc_x1, firmPtr->loc_y1, firmPtr->loc_x2, firmPtr->loc_y2) <= SUPPLY_BUILDING_RANGE )
		{
			return 1;
		}
	}

	return 0;
}
//----------- End of function Unit::is_near_own_building -----------//


//--------- Begin of function Unit::process_auto_return ---------//
//
// Called once a day by Unit::next_day(). The player's units which have
// stood idle far from home for AUTO_RETURN_IDLE_DAYS go back.
//
void Unit::process_auto_return()
{
	if( info.game_date % SUPPLY_CHECK_INTERVAL_DAYS != sprite_recno % SUPPLY_CHECK_INTERVAL_DAYS )
		return;

	//------ only the player's units out on the map ------//

	int isEligible = config_adv.unit_auto_return_idle &&
						  nation_recno && nation_recno == nation_array.player_recno &&
						  !spy_recno && unit_mode == 0 && mobile_type == UNIT_LAND && is_visible() &&
						  rank_id != RANK_KING && !farm_town_recno &&		// not a farmer, see OCIVIL.CPP
						  ( (unit_res[unit_id]->unit_class == UNIT_CLASS_HUMAN && race_id) ||
							 unit_res[unit_id]->unit_class == UNIT_CLASS_WEAPON );

	int isIdle = cur_action == SPRITE_IDLE && action_mode == ACTION_STOP && action_mode2 == ACTION_STOP;

	if( !isEligible || !isIdle || is_near_own_building() )
	{
		idle_far_days = 0;
		return;
	}

	idle_far_days = (char) MIN( idle_far_days + SUPPLY_CHECK_INTERVAL_DAYS, 120 );

	if( idle_far_days < AUTO_RETURN_IDLE_DAYS )
		return;

	idle_far_days = 0;		// try again after another AUTO_RETURN_IDLE_DAYS if there is nowhere to go

	auto_return_home();
}
//----------- End of function Unit::process_auto_return -----------//


//--------- Begin of function Unit::auto_return_home ---------//
//
// return : <int> 1 if the unit has been sent home
//
int Unit::auto_return_home()
{
	int curXLoc = next_x_loc();
	int curYLoc = next_y_loc();
	int regionId = region_id();
	int i;

	UnitInfo* unitInfo = unit_res[unit_id];
	int isCivilian = unitInfo->unit_class == UNIT_CLASS_HUMAN && is_civilian();

	//------ soldiers and war machines: their own fort first ------//

	if( !isCivilian && rank_id == RANK_SOLDIER && home_camp_firm_recno &&
		 !firm_array.is_deleted(home_camp_firm_recno) &&
		 firm_array[home_camp_firm_recno]->nation_recno == nation_recno &&
		 firm_array[home_camp_firm_recno]->region_id == regionId &&
		 firm_array[home_camp_firm_recno]->worker_count < MAX_WORKER )
	{
		if( return_camp() )
			return 1;
	}

	//------ the nearest fort with room, or a town with room for civilians ------//

	int bestDistance = 0x7FFF, bestXLoc = -1, bestYLoc = -1;

	if( isCivilian )
	{
		for( i=town_array.size() ; i>0 ; i-- )
		{
			if( town_array.is_deleted(i) )
				continue;

			Town* townPtr = town_array[i];

			if( townPtr->nation_recno != nation_recno || townPtr->region_id != regionId ||
				 townPtr->population >= MAX_TOWN_POPULATION )
			{
				continue;
			}

			int curDistance = misc.points_distance(curXLoc, curYLoc, townPtr->center_x, townPtr->center_y);

			if( curDistance < bestDistance )
			{
				bestDistance = curDistance;
				bestXLoc = townPtr->loc_x1;
				bestYLoc = townPtr->loc_y1;
			}
		}
	}
	else
	{
		for( i=firm_array.size() ; i>0 ; i-- )
		{
			if( firm_array.is_deleted(i) )
				continue;

			Firm* firmPtr = firm_array[i];

			if( firmPtr->nation_recno != nation_recno || firmPtr->firm_id != FIRM_CAMP ||
				 firmPtr->under_construction || firmPtr->region_id != regionId )
			{
				continue;
			}

			if( rank_id == RANK_GENERAL ? firmPtr->overseer_recno != 0 : firmPtr->worker_count >= MAX_WORKER )
				continue;

			int curDistance = misc.points_distance(curXLoc, curYLoc, firmPtr->center_x, firmPtr->center_y);

			if( curDistance < bestDistance )
			{
				bestDistance = curDistance;
				bestXLoc = firmPtr->loc_x1;
				bestYLoc = firmPtr->loc_y1;
			}
		}
	}

	if( bestXLoc >= 0 )
	{
		assign(bestXLoc, bestYLoc);
		return 1;
	}

	//------ otherwise next to the nearest own building ------//

	for( i=firm_array.size() ; i>0 ; i-- )
	{
		if( firm_array.is_deleted(i) )
			continue;

		Firm* firmPtr = firm_array[i];

		if( firmPtr->nation_recno != nation_recno || firmPtr->region_id != regionId )
			continue;

		int curDistance = misc.points_distance(curXLoc, curYLoc, firmPtr->center_x, firmPtr->center_y);

		if( curDistance < bestDistance )
		{
			bestDistance = curDistance;
			bestXLoc = firmPtr->center_x;
			bestYLoc = firmPtr->center_y;
		}
	}

	for( i=town_array.size() ; i>0 ; i-- )
	{
		if( town_array.is_deleted(i) )
			continue;

		Town* townPtr = town_array[i];

		if( townPtr->nation_recno != nation_recno || townPtr->region_id != regionId )
			continue;

		int curDistance = misc.points_distance(curXLoc, curYLoc, townPtr->center_x, townPtr->center_y);

		if( curDistance < bestDistance )
		{
			bestDistance = curDistance;
			bestXLoc = townPtr->center_x;
			bestYLoc = townPtr->center_y;
		}
	}

	if( bestXLoc < 0 )		// nothing of ours on this land, stay
		return 0;

	move_to(bestXLoc, bestYLoc);
	return 1;
}
//----------- End of function Unit::auto_return_home -----------//


//--------- Begin of function Unit::process_supply ---------//
//
// Called once a day by Unit::next_day(). Units out of supply lose hit points.
//
void Unit::process_supply()
{
	if( info.game_date % SUPPLY_CHECK_INTERVAL_DAYS != sprite_recno % SUPPLY_CHECK_INTERVAL_DAYS )
		return;

	if( !is_supply_unit() || is_supplied() )
		return;

	float minHitPoints = (float) max_hit_points * SUPPLY_MIN_HIT_POINTS_PERCENT / 100;

	if( hit_points <= minHitPoints )
		return;

	float loss = (float) MAX(1, max_hit_points * SUPPLY_ATTRITION_PERCENT / 100);

	hit_points -= loss;

	if( hit_points <= minHitPoints )
	{
		hit_points = minHitPoints;

		AiWarState::log( "ATTRITION   nation %d unit %d out of supply at %d,%d is down to %.0f/%d hp",
							  nation_recno, sprite_recno, next_x_loc(), next_y_loc(), hit_points, max_hit_points );
	}
}
//----------- End of function Unit::process_supply -----------//
