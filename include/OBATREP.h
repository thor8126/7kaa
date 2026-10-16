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

//Filename    : OBATREP.H
//Description : Battle reports for the player, see OBATREP.CPP

#ifndef __OBATREP_H
#define __OBATREP_H

class Unit;
class Firm;

//------------- Define constants --------------//

#define MAX_ACTIVE_BATTLE			6		// battles followed at the same time
#define BATTLE_AREA_DISTANCE		20		// losses this close to a battle belong to it
#define BATTLE_END_DAYS				10		// a battle is over after this many days without losses
#define MIN_BATTLE_REPORT_LOSSES	3		// smaller skirmishes are not reported

//------------- Define struct BattleRecord --------------//

struct BattleRecord
{
	short	x_loc, y_loc;					// where it started
	int	start_date, last_date;
	char	enemy_nation_recno;			// the nation of the last enemy unit lost or that killed ours, 0 for rebels and Fryhtans
	short	own_unit_lost, enemy_unit_lost;
	short	own_firm_lost, enemy_firm_lost;
};

//------------- Define class BattleReport --------------//

class BattleReport
{
public:
	BattleRecord	battle_array[MAX_ACTIVE_BATTLE];
	int		battle_count;

public:
	BattleReport()		{ battle_count = 0; }

	void	reset()			{ battle_count = 0; }
	void	unit_killed(int attackerNationRecno, Unit* targetUnit);
	void	firm_destroyed(int attackerNationRecno, Firm* targetFirm);
	void	next_day();

private:
	BattleRecord* get_battle(int xLoc, int yLoc);
	void	report(BattleRecord* battlePtr);
	void	del(int i);
};

extern BattleReport battle_report;

//--------------------------------------------//

#endif
