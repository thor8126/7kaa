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

//Filename    : OAI_RAID.CPP
//Description : AI raids on the economy of enemies
//
// While at war, an AI nation that is not on the defensive sends small
// raiding parties (up to MAX_RAID_SIZE soldiers from one fort, leaving
// MIN_CAMP_SOLDIERS_LEFT behind) against weakly defended mines,
// factories and markets of its enemies that it knows about, instead of
// only attacking towns and forts. It also hits enemy caravans it sees
// near its forts, but rarely: killing a caravan costs the attacker 10
// reputation and 2 loyalty in all its towns (NationBase::civilian_killed),
// so only nations with a good reputation that are not peaceful do it.
// The raiders return to their fort when done (Unit::process_ai, home camp).

#include <ALL.h>
#include <ONATION.h>
#include <OFIRM.h>
#include <OFIRMRES.h>
#include <OF_CAMP.h>
#include <OUNIT.h>
#include <OUNITRES.h>
#include <OWORLD.h>
#include <OINFO.h>
#include <OAIWAR.h>
#include <OMISC.h>
#include <ConfigAdv.h>

//------- Define constants -------//

#define MAX_RAID_SIZE						4		// soldiers in a raiding party
#define MIN_CAMP_SOLDIERS_LEFT			1		// soldiers left in the fort the raiders come from
#define MAX_RAID_DISTANCE					80		// targets at most this far from the raiders' fort
#define CARAVAN_RAID_MIN_REPUTATION		40		// killing a caravan costs 10 reputation
#define CARAVAN_RAID_MAX_PEACEFULNESS	60

//------- Define static functions -------//

static int raid_soldier_count(FirmCamp* firmCamp);
static int raid_party_strength(FirmCamp* firmCamp, int raidSize);

//--------- Begin of function Nation::think_raid ---------//
//
// return : <int> 1 if a raiding party has been sent
//
int Nation::think_raid()
{
	if( !config_adv.nation_ai_raids || !ai_camp_count )
		return 0;

	int curPosture = ai_war_state.get_posture(nation_recno);

	if( curPosture == AI_POSTURE_DEFEND || curPosture == AI_POSTURE_RECOVER )
		return 0;

	//------- only while at war -------//

	int i;

	for( i=nation_array.size() ; i>0 ; i-- )
	{
		if( !nation_array.is_deleted(i) && i != nation_recno &&
			 get_relation_status(i) == NATION_HOSTILE )
		{
			break;
		}
	}

	if( i==0 )
		return 0;

	//------- find the best target -------//

	int 		 bestRating = 0, bestTargetXLoc = 0, bestTargetYLoc = 0, bestTargetUnitRecno = 0, bestRaidSize = 0;
	FirmCamp* bestCamp = NULL;
	const char* bestTargetName = NULL;
	int		 hasWar;


	//---- weakly defended mines, factories and markets ----//

	for( i=firm_array.size() ; i>0 ; i-- )
	{
		if( firm_array.is_deleted(i) )
			continue;

		Firm* firmPtr = firm_array[i];

		int typeValue;

		switch( firmPtr->firm_id )
		{
			case FIRM_MINE:	 typeValue = 100; break;
			case FIRM_FACTORY: typeValue = 80;  break;
			case FIRM_MARKET:	 typeValue = 60;  break;
			default:				 continue;
		}

		if( !firmPtr->nation_recno || firmPtr->nation_recno == nation_recno ||
			 get_relation_status(firmPtr->nation_recno) != NATION_HOSTILE ||
			 firmPtr->under_construction || !ai_has_seen_firm(firmPtr) )		// only buildings it has scouted, see OAI_INTEL.CPP
		{
			continue;
		}

		int defenseLevel = enemy_firm_combat_level(firmPtr, 1, hasWar);		// 1-return -1 if there is a battle there already

		if( defenseLevel < 0 )
			continue;

		//--- the nearest fort that can spare enough soldiers ---//

		for( int j=0 ; j<ai_camp_count ; j++ )
		{
			FirmCamp* firmCamp = (FirmCamp*) firm_array[ ai_camp_array[j] ];

			if( firmCamp->region_id != firmPtr->region_id || firmCamp->is_attack_camp || firmCamp->ai_capture_town_recno )
				continue;

			int campDistance = misc.points_distance( firmCamp->center_x, firmCamp->center_y, firmPtr->center_x, firmPtr->center_y );

			if( campDistance > MAX_RAID_DISTANCE )
				continue;

			int raidSize = MIN( MAX_RAID_SIZE, raid_soldier_count(firmCamp) );

			if( raidSize < 2 )
				continue;

			if( raid_party_strength(firmCamp, raidSize) < defenseLevel * 6 / 5 + 50 )
				continue;

			int curRating = typeValue - campDistance - defenseLevel/5;

			if( curRating > bestRating )
			{
				bestRating = curRating;
				bestCamp = firmCamp;
				bestRaidSize = raidSize;
				bestTargetXLoc = firmPtr->loc_x1;
				bestTargetYLoc = firmPtr->loc_y1;
				bestTargetUnitRecno = 0;
				bestTargetName = firm_res[firmPtr->firm_id]->name;
			}
		}
	}

	//---- caravans seen near our forts ----//

	if( reputation >= CARAVAN_RAID_MIN_REPUTATION && pref_peacefulness < CARAVAN_RAID_MAX_PEACEFULNESS )
	{
		for( i=unit_array.size() ; i>0 ; i-- )
		{
			if( unit_array.is_deleted(i) )
				continue;

			Unit* unitPtr = unit_array[i];

			if( unitPtr->unit_id != UNIT_CARAVAN || !unitPtr->is_visible() ||
				 !unitPtr->nation_recno || unitPtr->nation_recno == nation_recno ||
				 get_relation_status(unitPtr->nation_recno) != NATION_HOSTILE ||
				 !ai_sees_unit(unitPtr) )
			{
				continue;
			}

			int xLoc = unitPtr->next_x_loc();
			int yLoc = unitPtr->next_y_loc();
			int regionId = world.get_region_id(xLoc, yLoc);

			int defenseLevel = mobile_defense_combat_level(xLoc, yLoc, unitPtr->nation_recno, 1, hasWar);		// escorts

			if( defenseLevel < 0 )
				continue;

			for( int j=0 ; j<ai_camp_count ; j++ )
			{
				FirmCamp* firmCamp = (FirmCamp*) firm_array[ ai_camp_array[j] ];

				if( firmCamp->region_id != regionId || firmCamp->is_attack_camp || firmCamp->ai_capture_town_recno )
					continue;

				int campDistance = misc.points_distance( firmCamp->center_x, firmCamp->center_y, xLoc, yLoc );

				if( campDistance > MAX_RAID_DISTANCE/2 )		// caravans move on, only those close by
					continue;

				int raidSize = MIN( MAX_RAID_SIZE, raid_soldier_count(firmCamp) );

				if( raidSize < 2 )
					continue;

				if( raid_party_strength(firmCamp, raidSize) < defenseLevel * 6 / 5 + 50 )
					continue;

				int curRating = 70 - campDistance*2 - defenseLevel/5;

				if( curRating > bestRating )
				{
					bestRating = curRating;
					bestCamp = firmCamp;
					bestRaidSize = raidSize;
					bestTargetXLoc = xLoc;
					bestTargetYLoc = yLoc;
					bestTargetUnitRecno = i;
					bestTargetName = "Caravan";
				}
			}
		}
	}


	if( !bestCamp )
		return 0;

	//------- send the raiding party: the soldiers with the most hit points -------//

	short raidArray[MAX_RAID_SIZE];
	int   raidCount = 0;

	while( raidCount < bestRaidSize )
	{
		int bestWorkerId = 0;

		for( int w=1 ; w<=bestCamp->worker_count ; w++ )
		{
			Worker* workerPtr = bestCamp->worker_array + w - 1;

			if( workerPtr->skill_id == SKILL_LEADING &&
				 (!bestWorkerId || workerPtr->hit_points > bestCamp->worker_array[bestWorkerId-1].hit_points) )
			{
				bestWorkerId = w;
			}
		}

		if( !bestWorkerId )
			break;

		int unitRecno = bestCamp->mobilize_worker( bestWorkerId, COMMAND_AI );

		if( !unitRecno )		// no room around the fort
			break;

		raidArray[raidCount++] = unitRecno;
	}

	if( raidCount == 0 )
		return 0;

	enable_should_attack_on_target( bestTargetXLoc, bestTargetYLoc );

	unit_array.attack( bestTargetXLoc, bestTargetYLoc, 0, raidArray, raidCount, COMMAND_AI, bestTargetUnitRecno );

	AiWarState::log( "RAID        nation %d sends %d soldiers from fort %d to raid a %s at %d,%d",
						  nation_recno, raidCount, bestCamp->firm_recno, bestTargetName, bestTargetXLoc, bestTargetYLoc );
	return 1;
}
//----------- End of function Nation::think_raid -----------//


//--------- Begin of static function raid_soldier_count ---------//
//
// return : <int> the no. of soldiers the fort can spare for a raid
//
static int raid_soldier_count(FirmCamp* firmCamp)
{
	int soldierCount = 0;

	for( int i=0 ; i<firmCamp->worker_count ; i++ )
	{
		if( firmCamp->worker_array[i].skill_id == SKILL_LEADING )
			soldierCount++;
	}

	return MAX( 0, soldierCount - MIN_CAMP_SOLDIERS_LEFT );
}
//----------- End of static function raid_soldier_count -----------//


//--------- Begin of static function raid_party_strength ---------//
//
// return : <int> the combat strength of the raiding party, the soldiers
//			  with the most hit points, counted like Unit::unit_power()
//
static int raid_party_strength(FirmCamp* firmCamp, int raidSize)
{
	int hitPointsArray[MAX_WORKER];
	int soldierCount = 0;

	for( int i=0 ; i<firmCamp->worker_count ; i++ )
	{
		if( firmCamp->worker_array[i].skill_id == SKILL_LEADING )
			hitPointsArray[soldierCount++] = firmCamp->worker_array[i].hit_points;
	}

	//------ add up the strongest ones ------//

	int strength = 0;

	for( int n=0 ; n<raidSize && n<soldierCount ; n++ )
	{
		int best = n;

		for( int k=n+1 ; k<soldierCount ; k++ )
		{
			if( hitPointsArray[k] > hitPointsArray[best] )
				best = k;
		}

		int temp = hitPointsArray[n];
		hitPointsArray[n] = hitPointsArray[best];
		hitPointsArray[best] = temp;

		strength += hitPointsArray[n];
	}

	return strength;
}
//----------- End of static function raid_party_strength -----------//
