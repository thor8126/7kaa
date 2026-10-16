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
