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

//Filename    : OU_FERRY.CPP
//Description : Ferries, ships carrying land units between harbors
//
// A ship that carries units is a ferry when it sails along two or more
// harbor stops: a transport (units only) in SHIP_MODE_AUTO, or a caravel or
// galleon in SHIP_MODE_FERRY. Each time a ferry enters one of its stop
// harbors, it puts the units it carries ashore next to the harbor, then
// takes on its nation's land units waiting there, and carries them to its
// next stop.
//
// Land units wait for a ferry when they are sent (right-clicked) to their
// own harbor that is a stop of one of their ferries: they walk next to the
// harbor and board the next ferry in it while they stand there idle.

#include <ALL.h>
#include <OU_MARI.h>
#include <OF_HARB.h>
#include <OUNIT.h>
#include <OUNITRES.h>
#include <OWORLD.h>
#include <ONATION.h>
#include <OINFO.h>
#include <OCONFIG.h>
#include <OREMOTE.h>

//------- Define constants -------//

#define FERRY_BOARD_RANGE		4		// units waiting within this distance of the harbor board a ferry in it

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


//--------- Begin of function UnitMarine::is_ferry ---------//
//
// return : <int> 1 if this ship carries units between its stops
//
int UnitMarine::is_ferry()
{
	UnitInfo* unitInfo = unit_res[unit_id];

	if( unitInfo->carry_unit_capacity <= 0 || stop_defined_num < 2 )
		return 0;

	if( unitInfo->carry_goods_capacity > 0 )		// caravels and galleons ferry units in ferry mode only, not when just trading
		return auto_mode == SHIP_MODE_FERRY;

	return auto_mode != SHIP_MODE_MANUAL;
}
//----------- End of function UnitMarine::is_ferry -----------//


//--------- Begin of function UnitMarine::has_menu_switch ---------//
//
// return : <int> 1 if the ship's interface switches between its units
//			  and its goods (or ferry route) menus
//
int UnitMarine::has_menu_switch()
{
	return unit_res[unit_id]->carry_unit_capacity > 0;
}
//----------- End of function UnitMarine::has_menu_switch -----------//


//--------- Begin of function UnitMarine::auto_mode_str ---------//
//
// return : <const char*> the label of the auto mode button: "T" for trade,
//			  "F" for ferry, "C" for manual control
//
const char* UnitMarine::auto_mode_str()
{
	if( auto_mode == SHIP_MODE_MANUAL )
		return "C";

	if( auto_mode == SHIP_MODE_FERRY || unit_res[unit_id]->carry_goods_capacity == 0 )
		return "F";

	return "T";
}
//----------- End of function UnitMarine::auto_mode_str -----------//


//--------- Begin of function UnitMarine::next_auto_mode ---------//
//
// return : <int> the auto mode set when the player clicks the auto mode button:
//			  T -> F -> C -> T for ships carrying goods and units,
//			  F <-> C for ships carrying units only
//
int UnitMarine::next_auto_mode()
{
	if( unit_res[unit_id]->carry_goods_capacity == 0 )
		return auto_mode == SHIP_MODE_MANUAL ? SHIP_MODE_AUTO : SHIP_MODE_MANUAL;

	switch( auto_mode )
	{
		case SHIP_MODE_AUTO:
			return SHIP_MODE_FERRY;

		case SHIP_MODE_FERRY:
			return SHIP_MODE_MANUAL;

		default:
			return SHIP_MODE_AUTO;
	}
}
//----------- End of function UnitMarine::next_auto_mode -----------//


//--------- Begin of function UnitMarine::ferry_unload_units ---------//
//
// Put the units the ferry carries ashore next to the given harbor.
// Units which can't find room stay on board.
//
// <FirmHarbor*> harborPtr - the harbor the ferry is entering
//
void UnitMarine::ferry_unload_units(FirmHarbor* harborPtr)
{
	while( unit_count > 0 )
	{
		Unit* unitPtr = unit_array[ unit_recno_array[unit_count-1] ];

		int xLoc = harborPtr->loc_x1;
		int yLoc = harborPtr->loc_y1;

		if( !world.locate_space( &xLoc, &yLoc, harborPtr->loc_x2, harborPtr->loc_y2,
					unitPtr->sprite_info->loc_width, unitPtr->sprite_info->loc_height, UNIT_LAND, harborPtr->land_region_id ) )
		{
			break;
		}

		unitPtr->init_sprite(xLoc, yLoc);
		unitPtr->set_mode(0);

		unit_count--;
	}

	if( unit_array.selected_recno == sprite_recno &&
		 (nation_recno == nation_array.player_recno || config.show_ai_info) )
	{
		info.disp();
	}
}
//----------- End of function UnitMarine::ferry_unload_units -----------//


//--------- Begin of function UnitMarine::ferry_load_units ---------//
//
// Let the land units waiting at the given harbor board the ferry.
//
// <FirmHarbor*> harborPtr - the harbor the ferry is in
//
void UnitMarine::ferry_load_units(FirmHarbor* harborPtr)
{
	int carryCapacity = MIN( (int) unit_res[unit_id]->carry_unit_capacity, MAX_UNIT_IN_SHIP );

	for( int i=unit_array.size() ; i>0 ; i-- )
	{
		if( unit_array.is_deleted(i) )
			continue;

		Unit* unitPtr = unit_array[i];

		if( unitPtr->ferry_harbor_recno != harborPtr->firm_recno )
			continue;

		//--- the unit has been given another order or has changed nation: it isn't waiting any more ---//

		if( unitPtr->nation_recno != nation_recno || !unitPtr->is_visible() ||
			 unitPtr->hit_points <= 0 || unitPtr->action_mode == ACTION_DIE )
		{
			unitPtr->ferry_harbor_recno = 0;
			continue;
		}

		int unitDistance = loc_rect_distance( unitPtr->next_x_loc(), unitPtr->next_y_loc(),
			harborPtr->loc_x1, harborPtr->loc_y1, harborPtr->loc_x2, harborPtr->loc_y2 );

		if( unitDistance > FERRY_BOARD_RANGE )
		{
			int destDistance = loc_rect_distance( unitPtr->move_to_x_loc, unitPtr->move_to_y_loc,
				harborPtr->loc_x1, harborPtr->loc_y1, harborPtr->loc_x2, harborPtr->loc_y2 );

			if( destDistance > FERRY_BOARD_RANGE )		// not on its way to the harbor any more
				unitPtr->ferry_harbor_recno = 0;

			continue;
		}

		if( unitPtr->cur_action != SPRITE_IDLE )		// board once it has arrived and stands still
			continue;

		if( unit_count >= carryCapacity )
			break;

		unitPtr->ferry_harbor_recno = 0;
		unitPtr->stop2();
		load_unit(i);
	}
}
//----------- End of function UnitMarine::ferry_load_units -----------//


//--------- Begin of function UnitArray::is_ferry_harbor ---------//
//
// <int> harborRecno - recno of the harbor
// <int> nationRecno - recno of the nation whose ferries are checked
//
// return : <int> 1 if the harbor is a stop of one of the nation's ferries
//
int UnitArray::is_ferry_harbor(int harborRecno, int nationRecno)
{
	for( int i=size() ; i>0 ; i-- )
	{
		if( is_deleted(i) )
			continue;

		Unit* unitPtr = operator[](i);

		if( unitPtr->nation_recno != nationRecno ||
			 unit_res[unitPtr->unit_id]->unit_class != UNIT_CLASS_SHIP )
		{
			continue;
		}

		UnitMarine* shipPtr = (UnitMarine*) unitPtr;

		if( !shipPtr->is_ferry() )
			continue;

		for( int j=0 ; j<shipPtr->stop_defined_num ; j++ )
		{
			if( shipPtr->stop_array[j].firm_recno == harborRecno )
				return 1;
		}
	}

	return 0;
}
//----------- End of function UnitArray::is_ferry_harbor -----------//


//--------- Begin of function UnitArray::wait_for_ferry ---------//
//
// Send land units to a harbor to wait for a ferry there.
//
// <int>    harborRecno   - recno of the harbor
// <short*> selectedArray - the units
// <int>    selectedCount - no. of units
// <char>   remoteAction  - remote action type
//
void UnitArray::wait_for_ferry(int harborRecno, short* selectedArray, int selectedCount, char remoteAction)
{
	if( firm_array.is_deleted(harborRecno) )
		return;

	Firm* harborPtr = firm_array[harborRecno];

	if( harborPtr->firm_id != FIRM_HARBOR )
		return;

	if( !remoteAction && remote.is_enable() )
	{
		// packet structure : <harbor recno> <no. of units> <unit recno ...>
		short *shortPtr = (short *)remote.new_send_queue_msg(MSG_UNITS_WAIT_FERRY, sizeof(short) * (2+selectedCount));
		shortPtr[0] = harborRecno;
		shortPtr[1] = selectedCount;
		memcpy(shortPtr+2, selectedArray, sizeof(short) * selectedCount);
		return;
	}

	//------ only the harbor's own land units, not caravans, wait for its ferries ------//

	short* waitArray = (short*) mem_add( sizeof(short) * selectedCount );
	int    waitCount = 0;

	for( int i=0 ; i<selectedCount ; i++ )
	{
		if( is_deleted(selectedArray[i]) )
			continue;

		Unit* unitPtr = operator[](selectedArray[i]);

		if( unitPtr->nation_recno != harborPtr->nation_recno || unitPtr->mobile_type != UNIT_LAND ||
			 !unitPtr->is_visible() || unit_res[unitPtr->unit_id]->unit_class == UNIT_CLASS_CARAVAN )
		{
			continue;
		}

		waitArray[waitCount++] = selectedArray[i];
	}

	if( waitCount > 0 )
	{
		move_to( harborPtr->center_x, harborPtr->center_y, 0, waitArray, waitCount, remoteAction );

		for( int i=0 ; i<waitCount ; i++ )
			operator[](waitArray[i])->ferry_harbor_recno = harborRecno;
	}

	mem_del(waitArray);
}
//----------- End of function UnitArray::wait_for_ferry -----------//
