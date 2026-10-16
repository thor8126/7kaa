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

//Filename    : OBATREP.CPP
//Description : Battle reports for the player
//
// Units and buildings lost by the player, or destroyed by the player,
// are grouped into battles by place (within BATTLE_AREA_DISTANCE of where
// the battle started). When there have been no losses in a battle for
// BATTLE_END_DAYS, a news message reports the losses on both sides, if
// there were at least MIN_BATTLE_REPORT_LOSSES. Not saved with the game.

#include <ALL.h>
#include <OBATREP.h>
#include <OUNIT.h>
#include <OFIRM.h>
#include <OTOWN.h>
#include <ONATION.h>
#include <OINFO.h>
#include <ONEWS.h>
#include <OAIWAR.h>
#include <OMISC.h>

//------- Define global variables -------//

BattleReport battle_report;

//--------- Begin of function BattleReport::unit_killed ---------//
//
// Called when a unit is killed by another nation's unit.
//
void BattleReport::unit_killed(int attackerNationRecno, Unit* targetUnit)
{
	int playerRecno = nation_array.player_recno;

	if( !playerRecno || !targetUnit->is_visible() )
		return;

	int isOwnLoss = targetUnit->nation_recno == playerRecno;

	if( !isOwnLoss && attackerNationRecno != playerRecno )		// not a battle of the player's
		return;

	BattleRecord* battlePtr = get_battle( targetUnit->next_x_loc(), targetUnit->next_y_loc() );

	if( isOwnLoss )
	{
		battlePtr->own_unit_lost++;
		battlePtr->enemy_nation_recno = attackerNationRecno;
	}
	else
	{
		battlePtr->enemy_unit_lost++;
		battlePtr->enemy_nation_recno = targetUnit->nation_recno;
	}
}
//----------- End of function BattleReport::unit_killed -----------//


//--------- Begin of function BattleReport::firm_destroyed ---------//
//
// Called when a firm is destroyed by another nation's unit.
//
void BattleReport::firm_destroyed(int attackerNationRecno, Firm* targetFirm)
{
	int playerRecno = nation_array.player_recno;

	if( !playerRecno )
		return;

	int isOwnLoss = targetFirm->nation_recno == playerRecno;

	if( !isOwnLoss && attackerNationRecno != playerRecno )
		return;

	BattleRecord* battlePtr = get_battle( targetFirm->center_x, targetFirm->center_y );

	if( isOwnLoss )
	{
		battlePtr->own_firm_lost++;
		battlePtr->enemy_nation_recno = attackerNationRecno;
	}
	else
	{
		battlePtr->enemy_firm_lost++;
		battlePtr->enemy_nation_recno = targetFirm->nation_recno;
	}
}
//----------- End of function BattleReport::firm_destroyed -----------//


//--------- Begin of function BattleReport::get_battle ---------//
//
// return : <BattleRecord*> the battle going on near the location, or a new one
//
BattleRecord* BattleReport::get_battle(int xLoc, int yLoc)
{
	for( int i=0 ; i<battle_count ; i++ )
	{
		BattleRecord* battlePtr = battle_array + i;

		if( misc.points_distance(xLoc, yLoc, battlePtr->x_loc, battlePtr->y_loc) <= BATTLE_AREA_DISTANCE )
		{
			battlePtr->last_date = info.game_date;
			return battlePtr;
		}
	}

	//------ a new battle; report the oldest one if there are too many ------//

	if( battle_count == MAX_ACTIVE_BATTLE )
	{
		report(battle_array);
		del(0);
	}

	BattleRecord* battlePtr = battle_array + battle_count++;

	memset( battlePtr, 0, sizeof(BattleRecord) );

	battlePtr->x_loc = xLoc;
	battlePtr->y_loc = yLoc;
	battlePtr->start_date = info.game_date;
	battlePtr->last_date = info.game_date;

	AiWarState::log( "BATTLE      a battle starts at %d,%d (%d going on)", xLoc, yLoc, battle_count );

	return battlePtr;
}
//----------- End of function BattleReport::get_battle -----------//


//--------- Begin of function BattleReport::next_day ---------//
//
void BattleReport::next_day()
{
	for( int i=battle_count-1 ; i>=0 ; i-- )
	{
		if( info.game_date - battle_array[i].last_date >= BATTLE_END_DAYS )
		{
			report(battle_array+i);
			del(i);
		}
	}
}
//----------- End of function BattleReport::next_day -----------//


//--------- Begin of function BattleReport::report ---------//
//
void BattleReport::report(BattleRecord* battlePtr)
{
	int totalLosses = battlePtr->own_unit_lost + battlePtr->enemy_unit_lost +
							battlePtr->own_firm_lost + battlePtr->enemy_firm_lost;

	AiWarState::log( "BATTLE      the battle at %d,%d is over after %d days: player lost %d units %d buildings, enemy (nation %d) lost %d units %d buildings%s",
						  battlePtr->x_loc, battlePtr->y_loc, battlePtr->last_date - battlePtr->start_date,
						  battlePtr->own_unit_lost, battlePtr->own_firm_lost, battlePtr->enemy_nation_recno,
						  battlePtr->enemy_unit_lost, battlePtr->enemy_firm_lost,
						  totalLosses < MIN_BATTLE_REPORT_LOSSES ? " (too small to report)" : "" );

	if( totalLosses < MIN_BATTLE_REPORT_LOSSES )
		return;

	//------ the name of the town closest to the battle ------//

	int townNameId = 0, minDistance = 0x7FFF;

	for( int i=town_array.size() ; i>0 ; i-- )
	{
		if( town_array.is_deleted(i) )
			continue;

		Town* townPtr = town_array[i];
		int curDistance = misc.points_distance( townPtr->center_x, townPtr->center_y, battlePtr->x_loc, battlePtr->y_loc );

		if( curDistance < minDistance )
		{
			minDistance = curDistance;
			townNameId = townPtr->town_name_id;
		}
	}

	int enemyNationRecno = battlePtr->enemy_nation_recno;

	if( enemyNationRecno && nation_array.is_deleted(enemyNationRecno) )
		enemyNationRecno = 0;

	news_array.battle_report( battlePtr->x_loc, battlePtr->y_loc, townNameId, enemyNationRecno,
		battlePtr->own_unit_lost, battlePtr->own_firm_lost, battlePtr->enemy_unit_lost, battlePtr->enemy_firm_lost );
}
//----------- End of function BattleReport::report -----------//


//--------- Begin of function BattleReport::del ---------//
//
void BattleReport::del(int i)
{
	misc.del_array_rec( battle_array, battle_count, sizeof(BattleRecord), i+1 );
	battle_count--;
}
//----------- End of function BattleReport::del -----------//
