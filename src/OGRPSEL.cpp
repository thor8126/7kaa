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

#include <memory.h>
#include <OGRPSEL.h>
#include <OPOWER.h>
#include <OUNIT.h>
#include <ONATION.h>
#include <OUNITRES.h>
#include <OWORLD.h>
#include <OINFO.h>
#include <OSERES.h>

//--------------- begin of function GroupSelect::GroupSelect ----------------//
GroupSelect::GroupSelect()
{
	//memset(this, 0, sizeof(GroupSelect));
}
//--------------- end of function GroupSelect::GroupSelect ----------------//


//--------------- begin of function GroupSelect::~GroupSelect ----------------//
GroupSelect::~GroupSelect()
{
	deinit();
}
//--------------- end of function GroupSelect::~GroupSelect ----------------//


//--------------- begin of function GroupSelect::init ----------------//
void GroupSelect::init()
{
}
//--------------- end of function GroupSelect::init ----------------//


//--------------- begin of function GroupSelect::deinit ----------------//
void GroupSelect::deinit()
{
}
//--------------- end of function GroupSelect::deinit ----------------//


//--------------- begin of function GroupSelect::group_units ----------------//
// <int> groupNum = 1 - MAX_SELECT_GROUP_NUM
//
void GroupSelect::group_units(int groupNum)
{
	err_when(groupNum<1 || groupNum>MAX_SELECT_GROUP_NUM);

	Unit	*unitPtr;
	int playerNation;

	if(!unit_array.selected_recno)
		return; // invalid call
	else
	{
		playerNation = nation_array.player_recno;
		unitPtr = unit_array[unit_array.selected_recno];
		if(unitPtr->nation_recno!=playerNation)
			return;
	}

	//---------- group selected units ----------//
	int arraySize = unit_array.size();
	for(int i=arraySize; i>0; --i)
	{
		if(unit_array.is_deleted(i))
			continue;

		unitPtr = unit_array[i];
		if(unitPtr->nation_recno!=playerNation)
			continue;


		if(unitPtr->selected_flag)
				unitPtr->group_select_id = groupNum; // new selected unit
		else if(unitPtr->group_select_id==groupNum) // old member in this group
			unitPtr->group_select_id = 0;
	}
}
//--------------- end of function GroupSelect::group_units ----------------//


//--------------- begin of function GroupSelect::select_grouped_units ----------------//
// <int> groupNum = 1 - MAX_SELECT_GROUP_NUM
//
void GroupSelect::select_grouped_units(int groupNum)
{
	err_when(groupNum<1 || groupNum>MAX_SELECT_GROUP_NUM);

	int arraySize = unit_array.size();
	Unit	*unitPtr;
	int	playerNation = nation_array.player_recno;

	//---------- count selected units ----------//
	int groupUnitCount = 0;
	int i;
	for(i=arraySize; i>0; --i)
	{
		if(unit_array.is_deleted(i))
			continue;

		unitPtr = unit_array[i];

		if(unitPtr->nation_recno!=playerNation)
			continue;

		if(!unitPtr->is_visible())
			continue;

		if(unitPtr->group_select_id==groupNum)
		{
			groupUnitCount++;
			break;
		}
	}

	if(!groupUnitCount)
		return;
	
	power.reset_selection();

	//---------- select units in this group ----------//
	Unit	*showSelectedUnitPtr = NULL;
	unit_array.selected_count = 0;
	for(i=arraySize; i>0; --i)
	{
		if(unit_array.is_deleted(i))
			continue;

		unitPtr = unit_array[i];

		if(unitPtr->nation_recno!=playerNation)
			continue;

		if(!unitPtr->is_visible())
			continue;

		if(unitPtr->group_select_id==groupNum)
		{
			unitPtr->selected_flag = 1;
			unit_array.selected_count++;

			if(showSelectedUnitPtr==NULL || unitPtr->hit_points>showSelectedUnitPtr->hit_points)
				showSelectedUnitPtr = unitPtr;
		}
	}

	if(showSelectedUnitPtr)
	{
		unit_array.selected_recno = showSelectedUnitPtr->sprite_recno;
		info.disp();
	}
}
//--------------- end of function GroupSelect::select_grouped_units ----------------//



//--------------- begin of function GroupSelect::is_army_unit ----------------//
//
// Whether the unit is part of the player's mobile army: soldiers, generals
// and war machines out on the map. The king, civilians, spies, caravans,
// ships, monsters, gods and units inside buildings or ships are excluded.
//
int GroupSelect::is_army_unit(Unit* unitPtr)
{
	if( !unitPtr->is_own() || unitPtr->spy_recno )
		return 0;

	if( !unitPtr->is_visible() || unitPtr->hit_points <= 0 || unitPtr->is_shealth() )
		return 0;

	switch( unit_res[unitPtr->unit_id]->unit_class )
	{
		case UNIT_CLASS_HUMAN:
			return unitPtr->rank_id != RANK_KING && !unitPtr->is_civilian();

		case UNIT_CLASS_WEAPON:
			return 1;
	}

	return 0;
}
//--------------- end of function GroupSelect::is_army_unit ----------------//


//--------------- begin of function GroupSelect::select_army ----------------//
//
// Select the player's whole army anywhere on the map. When the whole army
// is already selected, move the view to the leading unit instead.
//
void GroupSelect::select_army()
{
	if( !nation_array.player_recno )
		return;

	int arraySize = unit_array.size();
	int armyCount = 0, alreadySelectedCount = 0;
	int i;
	Unit* unitPtr;

	for( i=arraySize ; i>0 ; i-- )
	{
		if( unit_array.is_deleted(i) )
			continue;

		unitPtr = unit_array[i];

		if( !is_army_unit(unitPtr) )
			continue;

		armyCount++;

		if( unitPtr->selected_flag )
			alreadySelectedCount++;
	}

	if( !armyCount )
		return;

	//--- pressed again with the whole army selected: look at the army ---//

	if( alreadySelectedCount == armyCount && unit_array.selected_count == armyCount &&
		 unit_array.selected_recno && !unit_array.is_deleted(unit_array.selected_recno) )
	{
		unitPtr = unit_array[unit_array.selected_recno];
		world.go_loc( unitPtr->next_x_loc(), unitPtr->next_y_loc() );
		return;
	}

	//---------- select the army ----------//

	power.reset_selection();

	Unit* leadUnitPtr = NULL;

	for( i=arraySize ; i>0 ; i-- )
	{
		if( unit_array.is_deleted(i) )
			continue;

		unitPtr = unit_array[i];

		if( !is_army_unit(unitPtr) )
			continue;

		unitPtr->selected_flag = 1;
		unit_array.selected_count++;

		//--- the leading unit shown in the info panel: generals first, then soldiers, then war machines ---//

		if( !leadUnitPtr || unitPtr->rank_id > leadUnitPtr->rank_id ||
			 ( unitPtr->rank_id == leadUnitPtr->rank_id &&
				unit_res[unitPtr->unit_id]->unit_class == UNIT_CLASS_HUMAN &&
				unit_res[leadUnitPtr->unit_id]->unit_class != UNIT_CLASS_HUMAN ) )
		{
			leadUnitPtr = unitPtr;
		}
	}

	unit_array.selected_recno = leadUnitPtr->sprite_recno;

	if( se_res.mark_select_object_time() )
	{
		se_res.sound( leadUnitPtr->cur_x_loc(), leadUnitPtr->cur_y_loc(), 1, 'S',
						  leadUnitPtr->sprite_id, "SEL" );
	}

	info.disp();
}
//--------------- end of function GroupSelect::select_army ----------------//


//--------------- begin of function GroupSelect::is_idle_unit ----------------//
//
// Whether the player's unit out on the map has nothing to do: people,
// except the king. War machines, caravans and ships are not counted.
// Units waiting for a ferry are not idle.
//
int GroupSelect::is_idle_unit(Unit* unitPtr)
{
	if( !unitPtr->is_own() || unitPtr->spy_recno || !unitPtr->is_visible() ||
		 unitPtr->hit_points <= 0 || unitPtr->unit_mode != 0 || unitPtr->ferry_harbor_recno ||
		 unitPtr->farm_town_recno )		// farmers are at work, see OCIVIL.CPP
	{
		return 0;
	}

	if( unitPtr->cur_action != SPRITE_IDLE || unitPtr->action_mode != ACTION_STOP ||
		 unitPtr->action_mode2 != ACTION_STOP )
	{
		return 0;
	}

	return unit_res[unitPtr->unit_id]->unit_class == UNIT_CLASS_HUMAN && unitPtr->race_id &&
			 unitPtr->rank_id != RANK_KING;
}
//--------------- end of function GroupSelect::is_idle_unit ----------------//


//--------------- begin of function GroupSelect::idle_unit_count ----------------//
//
int GroupSelect::idle_unit_count()
{
	if( !nation_array.player_recno )
		return 0;

	int idleCount = 0;

	for( int i=unit_array.size() ; i>0 ; i-- )
	{
		if( !unit_array.is_deleted(i) && is_idle_unit(unit_array[i]) )
			idleCount++;
	}

	return idleCount;
}
//--------------- end of function GroupSelect::idle_unit_count ----------------//


//--------------- begin of function GroupSelect::select_next_idle_unit ----------------//
//
// Select the next idle unit of the player and look at it (the Idle button
// and Ctrl+I). Pressing it again goes on to the next one.
//
void GroupSelect::select_next_idle_unit()
{
	if( !nation_array.player_recno )
		return;

	static int lastIdleRecno = 0;

	int arraySize = unit_array.size();
	int foundRecno = 0;

	for( int n=1 ; n<=arraySize ; n++ )
	{
		int i = (lastIdleRecno + n - 1) % arraySize + 1;		// the units after the last one first, then from the start

		if( !unit_array.is_deleted(i) && is_idle_unit(unit_array[i]) )
		{
			foundRecno = i;
			break;
		}
	}

	if( !foundRecno )
		return;

	lastIdleRecno = foundRecno;

	Unit* unitPtr = unit_array[foundRecno];

	power.reset_selection();

	unitPtr->selected_flag = 1;
	unit_array.selected_count = 1;
	unit_array.selected_recno = foundRecno;

	world.go_loc( unitPtr->next_x_loc(), unitPtr->next_y_loc() );

	if( se_res.mark_select_object_time() )
		se_res.sound( unitPtr->cur_x_loc(), unitPtr->cur_y_loc(), 1, 'S', unitPtr->sprite_id, "SEL" );

	info.disp();
}
//--------------- end of function GroupSelect::select_next_idle_unit ----------------//
