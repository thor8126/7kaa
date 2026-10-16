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

//Filename    : OAI_INTEL.CPP
//Description : The AI's knowledge of the world (fog of war for the AI)
//
// With config_adv.nation_ai_fog_of_war, an AI nation only plans with
// what it has scouted, like a player under fog of war:
//
// - Towns and firms of other nations are known once they have been
//   within EXPLORE_RANGE of one of the nation's units (or within
//   EXPLORE_RANGE-1 of its towns, firms and of buildings its spies are
//   in), the same ranges that explore the map for the player. The
//   knowledge is kept in Town/Firm::ai_seen_nation_mask.
//
// - Units of other nations are only counted when the nation currently
//   sees them: within the visual range of its units, or EXPLORE_RANGE-1
//   of its towns, firms and of buildings its spies are in, the ranges
//   of the player's fog of war.
//
// - It only plans wars on nations it has found, judging their strength
//   by their forts it knows and the units it sees.
//
// When nations are still unknown, think_explore() sends out scouts. As
// time passes word of towns also spreads: a town becomes known after
// AI_RUMOUR_DAYS_PER_LOC days for each location between it and the
// nation's nearest town (forts and armies still have to be seen).

#include <ALL.h>
#include <ONATION.h>
#include <OFIRM.h>
#include <OF_CAMP.h>
#include <OTOWN.h>
#include <OUNIT.h>
#include <OUNITRES.h>
#include <OSPY.h>
#include <OWORLD.h>
#include <OINFO.h>
#include <OAIWAR.h>
#include <OMISC.h>
#include <ConfigAdv.h>

//------- Define constants -------//

#define FIRM_TOWN_SIGHT_RANGE		(EXPLORE_RANGE-1)		// towns, firms and buildings with spies see this far, same as World::visit() for the player
#define MIN_SCOUT_DISTANCE			40							// scouts explore at least this far from the nation's largest town
#define MAX_SCOUT_DISTANCE			90
#define AI_RUMOUR_DAYS_PER_LOC		15							// days for word of a town to spread one location

//------- Define static functions -------//

static void mark_seen_area(int xLoc1, int yLoc1, int xLoc2, int yLoc2, int range, uint16_t nationBit);

//--------- Begin of function Nation::update_ai_intel ---------//
//
// Mark the towns and firms that this nation's units, buildings and
// spies can see as seen by it. Called every few days by process_ai().
//
void Nation::update_ai_intel()
{
	if( !config_adv.nation_ai_fog_of_war )
		return;

	uint16_t nationBit = 1 << (nation_recno-1);
	int i;

	//------- units out on the map ------//

	for( i=unit_array.size() ; i>0 ; i-- )
	{
		if( unit_array.is_deleted(i) )
			continue;

		Unit* unitPtr = unit_array[i];

		if( !unitPtr->is_visible() || unitPtr->true_nation_recno() != nation_recno )
			continue;

		int xLoc = unitPtr->next_x_loc();
		int yLoc = unitPtr->next_y_loc();

		mark_seen_area( xLoc, yLoc, xLoc, yLoc, EXPLORE_RANGE, nationBit );
	}

	//------- own towns and firms ------//

	for( i=town_array.size() ; i>0 ; i-- )
	{
		if( town_array.is_deleted(i) )
			continue;

		Town* townPtr = town_array[i];

		if( townPtr->nation_recno == nation_recno )
			mark_seen_area( townPtr->loc_x1, townPtr->loc_y1, townPtr->loc_x2, townPtr->loc_y2, FIRM_TOWN_SIGHT_RANGE, nationBit );
	}

	for( i=firm_array.size() ; i>0 ; i-- )
	{
		if( firm_array.is_deleted(i) )
			continue;

		Firm* firmPtr = firm_array[i];

		if( firmPtr->nation_recno == nation_recno )
			mark_seen_area( firmPtr->loc_x1, firmPtr->loc_y1, firmPtr->loc_x2, firmPtr->loc_y2, FIRM_TOWN_SIGHT_RANGE, nationBit );
	}

	//------- word of distant towns spreads over time ------//

	int daysPassed = info.game_date - info.game_start_date;

	for( i=town_array.size() ; i>0 ; i-- )
	{
		if( town_array.is_deleted(i) )
			continue;

		Town* townPtr = town_array[i];

		if( !townPtr->nation_recno || townPtr->nation_recno == nation_recno ||
			 (townPtr->ai_seen_nation_mask & nationBit) )
		{
			continue;
		}

		for( int j=0 ; j<ai_town_count ; j++ )
		{
			Town* ownTown = town_array[ ai_town_array[j] ];

			if( daysPassed > AI_RUMOUR_DAYS_PER_LOC *
				 misc.points_distance(townPtr->center_x, townPtr->center_y, ownTown->center_x, ownTown->center_y) )
			{
				AiWarState::log( "INTEL       nation %d hears of town %d of nation %d", nation_recno, townPtr->town_recno, townPtr->nation_recno );
				townPtr->ai_seen_nation_mask |= nationBit;
				break;
			}
		}
	}

	//------- buildings its spies are in ------//

	for( i=spy_array.size() ; i>0 ; i-- )
	{
		if( spy_array.is_deleted(i) )
			continue;

		Spy* spyPtr = spy_array[i];

		if( spyPtr->true_nation_recno != nation_recno )
			continue;

		if( spyPtr->spy_place == SPY_TOWN && !town_array.is_deleted(spyPtr->spy_place_para) )
		{
			Town* townPtr = town_array[spyPtr->spy_place_para];
			mark_seen_area( townPtr->loc_x1, townPtr->loc_y1, townPtr->loc_x2, townPtr->loc_y2, FIRM_TOWN_SIGHT_RANGE, nationBit );
		}
		else if( spyPtr->spy_place == SPY_FIRM && !firm_array.is_deleted(spyPtr->spy_place_para) )
		{
			Firm* firmPtr = firm_array[spyPtr->spy_place_para];
			mark_seen_area( firmPtr->loc_x1, firmPtr->loc_y1, firmPtr->loc_x2, firmPtr->loc_y2, FIRM_TOWN_SIGHT_RANGE, nationBit );
		}
	}
}
//----------- End of function Nation::update_ai_intel -----------//


//--------- Begin of static function mark_seen_area ---------//
//
// Mark the towns and firms within the given range of a rectangle as seen.
//
static void mark_seen_area(int xLoc1, int yLoc1, int xLoc2, int yLoc2, int range, uint16_t nationBit)
{
	xLoc1 = MAX(0, xLoc1-range);
	yLoc1 = MAX(0, yLoc1-range);
	xLoc2 = MIN(MAX_WORLD_X_LOC-1, xLoc2+range);
	yLoc2 = MIN(MAX_WORLD_Y_LOC-1, yLoc2+range);

	for( int yLoc=yLoc1 ; yLoc<=yLoc2 ; yLoc++ )
	{
		Location* locPtr = world.get_loc(xLoc1, yLoc);

		for( int xLoc=xLoc1 ; xLoc<=xLoc2 ; xLoc++, locPtr++ )
		{
			if( locPtr->is_firm() )
				firm_array[locPtr->firm_recno()]->ai_seen_nation_mask |= nationBit;

			else if( locPtr->is_town() )
			{
				Town* townPtr = town_array[locPtr->town_recno()];

				if( !(townPtr->ai_seen_nation_mask & nationBit) && townPtr->nation_recno &&
					 nationBit != (1 << (townPtr->nation_recno-1)) )
				{
					int nationRecno = 1;
					while( !(nationBit & (1 << (nationRecno-1))) )
						nationRecno++;

					AiWarState::log( "INTEL       nation %d finds town %d of nation %d", nationRecno, townPtr->town_recno, townPtr->nation_recno );
				}

				townPtr->ai_seen_nation_mask |= nationBit;
			}
		}
	}
}
//----------- End of static function mark_seen_area -----------//


//--------- Begin of function Nation::ai_has_seen_firm ---------//
//
// return : <int> 1 if this nation knows about the firm
//
int Nation::ai_has_seen_firm(Firm* firmPtr)
{
	if( !config_adv.nation_ai_fog_of_war || firmPtr->nation_recno == nation_recno )
		return 1;

	return (firmPtr->ai_seen_nation_mask >> (nation_recno-1)) & 1;
}
//----------- End of function Nation::ai_has_seen_firm -----------//


//--------- Begin of function Nation::ai_has_seen_town ---------//
//
// return : <int> 1 if this nation knows about the town
//
int Nation::ai_has_seen_town(Town* townPtr)
{
	if( !config_adv.nation_ai_fog_of_war || townPtr->nation_recno == nation_recno )
		return 1;

	return (townPtr->ai_seen_nation_mask >> (nation_recno-1)) & 1;
}
//----------- End of function Nation::ai_has_seen_town -----------//


//--------- Begin of function Nation::ai_sees_loc ---------//
//
// return : <int> 1 if this nation currently sees the given location
//
int Nation::ai_sees_loc(int xLoc, int yLoc)
{
	if( !config_adv.nation_ai_fog_of_war )
		return 1;

	int xLoc1 = MAX(0, xLoc-EXPLORE_RANGE), xLoc2 = MIN(MAX_WORLD_X_LOC-1, xLoc+EXPLORE_RANGE);
	int yLoc1 = MAX(0, yLoc-EXPLORE_RANGE), yLoc2 = MIN(MAX_WORLD_Y_LOC-1, yLoc+EXPLORE_RANGE);

	static const int mobileTypeArray[] = { UNIT_LAND, UNIT_SEA, UNIT_AIR };

	for( int y=yLoc1 ; y<=yLoc2 ; y++ )
	{
		Location* locPtr = world.get_loc(xLoc1, y);

		for( int x=xLoc1 ; x<=xLoc2 ; x++, locPtr++ )
		{
			int locDistance = MAX( abs(x-xLoc), abs(y-yLoc) );

			//------- its units ---------//

			for( int m=0 ; m<3 ; m++ )
			{
				int unitRecno = locPtr->has_unit(mobileTypeArray[m]);

				if( !unitRecno || unit_array.is_deleted(unitRecno) )
					continue;

				Unit* unitPtr = unit_array[unitRecno];

				if( unitPtr->true_nation_recno() == nation_recno &&
					 locDistance <= unit_res[unitPtr->unit_id]->visual_range )
				{
					return 1;
				}
			}

			//------- its towns and firms ---------//

			if( locDistance > FIRM_TOWN_SIGHT_RANGE )
				continue;

			if( locPtr->is_firm() )
			{
				if( firm_array[locPtr->firm_recno()]->nation_recno == nation_recno )
					return 1;
			}
			else if( locPtr->is_town() )
			{
				if( town_array[locPtr->town_recno()]->nation_recno == nation_recno )
					return 1;
			}
		}
	}

	//------- buildings its spies are in ---------//

	for( int i=spy_array.size() ; i>0 ; i-- )
	{
		if( spy_array.is_deleted(i) )
			continue;

		Spy* spyPtr = spy_array[i];

		if( spyPtr->true_nation_recno != nation_recno )
			continue;

		int x1, y1, x2, y2;

		if( spyPtr->spy_place == SPY_TOWN && !town_array.is_deleted(spyPtr->spy_place_para) )
		{
			Town* townPtr = town_array[spyPtr->spy_place_para];
			x1 = townPtr->loc_x1; y1 = townPtr->loc_y1; x2 = townPtr->loc_x2; y2 = townPtr->loc_y2;
		}
		else if( spyPtr->spy_place == SPY_FIRM && !firm_array.is_deleted(spyPtr->spy_place_para) )
		{
			Firm* firmPtr = firm_array[spyPtr->spy_place_para];
			x1 = firmPtr->loc_x1; y1 = firmPtr->loc_y1; x2 = firmPtr->loc_x2; y2 = firmPtr->loc_y2;
		}
		else
			continue;

		if( xLoc >= x1-FIRM_TOWN_SIGHT_RANGE && xLoc <= x2+FIRM_TOWN_SIGHT_RANGE &&
			 yLoc >= y1-FIRM_TOWN_SIGHT_RANGE && yLoc <= y2+FIRM_TOWN_SIGHT_RANGE )
		{
			return 1;
		}
	}

	return 0;
}
//----------- End of function Nation::ai_sees_loc -----------//


//--------- Begin of function Nation::ai_sees_unit ---------//
//
// return : <int> 1 if this nation currently sees the given unit
//
int Nation::ai_sees_unit(Unit* unitPtr)
{
	if( !config_adv.nation_ai_fog_of_war || unitPtr->true_nation_recno() == nation_recno )
		return 1;

	if( !unitPtr->is_visible() )
		return 0;

	return ai_sees_loc( unitPtr->next_x_loc(), unitPtr->next_y_loc() );
}
//----------- End of function Nation::ai_sees_unit -----------//


//--------- Begin of function Nation::ai_knows_nation ---------//
//
// return : <int> 1 if this nation has found any town or firm of the given nation
//
int Nation::ai_knows_nation(int nationRecno)
{
	if( !config_adv.nation_ai_fog_of_war || nationRecno == nation_recno )
		return 1;

	int i;

	for( i=town_array.size() ; i>0 ; i-- )
	{
		if( !town_array.is_deleted(i) && town_array[i]->nation_recno == nationRecno &&
			 ai_has_seen_town(town_array[i]) )
		{
			return 1;
		}
	}

	for( i=firm_array.size() ; i>0 ; i-- )
	{
		if( !firm_array.is_deleted(i) && firm_array[i]->nation_recno == nationRecno &&
			 ai_has_seen_firm(firm_array[i]) )
		{
			return 1;
		}
	}

	return 0;
}
//----------- End of function Nation::ai_knows_nation -----------//


//--------- Begin of function Nation::intel_military_rank_rating ---------//
//
// The military rank rating of a nation as this nation knows it: from the
// forts of the nation it has seen and its units it currently sees,
// counted the same way as NationArray::update_military_rating().
//
// return : <int> 0 to 100
//
int Nation::intel_military_rank_rating(int nationRecno)
{
	if( !config_adv.nation_ai_fog_of_war || nationRecno == nation_recno )
		return nation_array[nationRecno]->military_rank_rating();

	if( nation_array.max_military_rating <= 0 )
		return 0;

	int combatLevel = 0;
	int i;

	for( i=firm_array.size() ; i>0 ; i-- )
	{
		if( firm_array.is_deleted(i) )
			continue;

		Firm* firmPtr = firm_array[i];

		if( firmPtr->nation_recno != nationRecno || firmPtr->firm_id != FIRM_CAMP ||
			 !ai_has_seen_firm(firmPtr) )
		{
			continue;
		}

		combatLevel += ((FirmCamp*)firmPtr)->total_combat_level() +
							((firmPtr->overseer_recno>0) + firmPtr->worker_count) * 20;
	}

	for( i=unit_array.size() ; i>0 ; i-- )
	{
		if( unit_array.is_deleted(i) )
			continue;

		Unit* unitPtr = unit_array[i];

		if( unitPtr->nation_recno != nationRecno || unitPtr->unit_mode == UNIT_MODE_OVERSEE ||
			 !unitPtr->is_visible() || (!unitPtr->race_id && unit_res[unitPtr->unit_id]->unit_class != UNIT_CLASS_WEAPON) )
		{
			continue;
		}

		if( !ai_sees_unit(unitPtr) )
			continue;

		combatLevel += unitPtr->unit_power() + 20;
	}

	return MIN( 100, 100 * (combatLevel/50) / nation_array.max_military_rating );
}
//----------- End of function Nation::intel_military_rank_rating -----------//


//--------- Begin of function Nation::think_explore --------//
//
// While there are nations this nation hasn't found yet, send a soldier
// to scout a part of the map far from its largest town.
//
void Nation::think_explore()
{
	if( !config_adv.nation_ai_fog_of_war || !largest_town_recno )
		return;

	int curPosture = ai_war_state.get_posture(nation_recno);

	if( curPosture == AI_POSTURE_DEFEND || curPosture == AI_POSTURE_RECOVER )
		return;

	//------- are there any nations not found yet? -------//

	int i;

	for( i=nation_array.size() ; i>0 ; i-- )
	{
		if( !nation_array.is_deleted(i) && !ai_knows_nation(i) )
			break;
	}

	if( i==0 )
		return;

	//------- pick a soldier who has nothing to do -------//

	Town* homeTown = town_array[largest_town_recno];
	Unit* scoutUnit = NULL;

	for( i=unit_array.size() ; i>0 ; i-- )
	{
		if( unit_array.is_deleted(i) )
			continue;

		Unit* unitPtr = unit_array[i];

		//--- one scout at a time: a soldier on its way far from home ---//

		if( unitPtr->nation_recno == nation_recno && unitPtr->rank_id == RANK_SOLDIER &&
			 unitPtr->action_mode == ACTION_MOVE && unitPtr->is_visible() &&
			 misc.points_distance(unitPtr->move_to_x_loc, unitPtr->move_to_y_loc,
				homeTown->center_x, homeTown->center_y) >= MIN_SCOUT_DISTANCE )
		{
			return;
		}

		if( unitPtr->nation_recno != nation_recno || unitPtr->spy_recno ||
			 !unitPtr->is_visible() || !unitPtr->race_id || unitPtr->rank_id != RANK_SOLDIER || unitPtr->is_civilian() ||
			 unitPtr->ai_action_id || unitPtr->leader_unit_recno ||
			 unitPtr->action_mode != ACTION_STOP || unitPtr->action_mode2 != ACTION_STOP ||
			 unitPtr->region_id() != homeTown->region_id )
		{
			continue;
		}

		if( !scoutUnit )		// keep checking the other units for a scout already out
			scoutUnit = unitPtr;
	}

	//---- if there is none, call one out of the fort with the most soldiers ----//

	if( !scoutUnit )
	{
		FirmCamp* bestCamp = NULL;

		for( i=0 ; i<ai_camp_count ; i++ )
		{
			FirmCamp* firmCamp = (FirmCamp*) firm_array[ ai_camp_array[i] ];

			if( firmCamp->region_id == homeTown->region_id && firmCamp->worker_count >= 4 &&
				 (!bestCamp || firmCamp->worker_count > bestCamp->worker_count) )
			{
				bestCamp = firmCamp;
			}
		}

		if( !bestCamp )
			return;

		int workerId;

		for( workerId=bestCamp->worker_count ; workerId>0 ; workerId-- )		// a soldier, not a weapon
		{
			if( bestCamp->worker_array[workerId-1].skill_id == SKILL_LEADING )
				break;
		}

		if( !workerId )
			return;

		int unitRecno = bestCamp->mobilize_worker( workerId, COMMAND_AI );

		if( !unitRecno )
			return;

		scoutUnit = unit_array[unitRecno];
	}

	//------- pick a place to scout --------//

	for( int tries=0 ; tries<50 ; tries++ )
	{
		int xLoc = misc.random(MAX_WORLD_X_LOC);
		int yLoc = misc.random(MAX_WORLD_Y_LOC);
		int scoutDistance = misc.points_distance(xLoc, yLoc, homeTown->center_x, homeTown->center_y);

		if( scoutDistance < MIN_SCOUT_DISTANCE || scoutDistance > MAX_SCOUT_DISTANCE )
			continue;

		Location* locPtr = world.get_loc(xLoc, yLoc);

		if( locPtr->region_id != homeTown->region_id || !locPtr->can_move(UNIT_LAND) )
			continue;

		scoutUnit->move_to(xLoc, yLoc);

		AiWarState::log( "SCOUT       nation %d sends unit %d to scout %d,%d", nation_recno, scoutUnit->sprite_recno, xLoc, yLoc );
		return;
	}
}
//---------- End of function Nation::think_explore --------//
