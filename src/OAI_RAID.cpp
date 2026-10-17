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
#include <OF_MARK.h>
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
// The targets are rated by the damage the raid would do: mines,
// factories and markets (by their stock), supply wagons that keep an
// enemy army going, and caravans. The style changes what a nation likes
// to hit, and raids that rarely pay off (the building wasn't destroyed)
// are sent less often. Among the best targets the choice is weighted
// (see AiWarState::weighted_pick()).
//
// return : <int> 1 if a raiding party has been sent
//
int Nation::think_raid()
{
	if( !config_adv.nation_ai_raids || !ai_camp_count )
		return 0;

	//--- an outmatched nation defending itself still harasses the enemy's economy with spare troops, an exhausted one doesn't ---//

	if( ai_war_state.get_posture(nation_recno) == AI_POSTURE_RECOVER )
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

	//------- learn from the last raid on a building: did it fall? -------//

	AiLearning* learnPtr = ai_war_state.get_learning(nation_recno);

	if( learnPtr->raid_target_firm_recno && info.game_date > learnPtr->raid_date + 20 )
	{
		int paidOff = firm_array.is_deleted(learnPtr->raid_target_firm_recno);

		if( paidOff )
			learnPtr->raids_paid_off++;

		AiWarState::log( "RAID        nation %d: its raid on building %d %s (raids paid off %d of %d)",
							  nation_recno, learnPtr->raid_target_firm_recno, paidOff ? "paid off, it is destroyed" : "didn't destroy it",
							  learnPtr->raids_paid_off, learnPtr->raids_tried );

		learnPtr->raid_target_firm_recno = 0;
	}

	//------- raids that rarely pay off are sent half as often, a merchant raids every other month -------//

	if( learnPtr->raids_tried >= 4 && learnPtr->raids_paid_off*4 < learnPtr->raids_tried && misc.random(2) )
	{
		AiWarState::log( "RAID        nation %d skips a raid: they rarely pay off (%d of %d)", nation_recno, learnPtr->raids_paid_off, learnPtr->raids_tried );
		return 0;
	}

	if( ai_style() == AI_STYLE_MERCHANT && (info.game_date/30) % 2 )
		return 0;

	//------- the targets -------//

	enum { MAX_RAID_CANDIDATE = 40 };

	struct RaidCandidate
	{
		FirmCamp*	camp;
		int			raid_size;
		int			x_loc, y_loc;
		int			unit_recno;
		int			firm_recno;
		const char* name;
	};

	RaidCandidate candidateArray[MAX_RAID_CANDIDATE];
	int			  ratingArray[MAX_RAID_CANDIDATE];
	int			  candidateCount = 0;
	int			  hasWar;
	int			  styleId = ai_style();

	//---- weakly defended mines, factories and markets ----//

	for( i=firm_array.size() ; i>0 && candidateCount < MAX_RAID_CANDIDATE ; i-- )
	{
		if( firm_array.is_deleted(i) )
			continue;

		Firm* firmPtr = firm_array[i];

		int typeValue;

		switch( firmPtr->firm_id )
		{
			case FIRM_MINE:	 typeValue = 100; break;
			case FIRM_FACTORY: typeValue = 80;  break;
			case FIRM_MARKET:	 typeValue = 60 + ((FirmMarket*)firmPtr)->stock_value_index()/4 + (styleId == AI_STYLE_MERCHANT ? 20 : 0); break;
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

		FirmCamp* bestCamp = NULL;
		int		 bestRating = -1000, bestRaidSize = 0;

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
			}
		}

		if( bestCamp && bestRating > 0 )
		{
			RaidCandidate* c = candidateArray + candidateCount;

			c->camp = bestCamp; c->raid_size = bestRaidSize;
			c->x_loc = firmPtr->loc_x1; c->y_loc = firmPtr->loc_y1;
			c->unit_recno = 0; c->firm_recno = firmPtr->firm_recno;
			c->name = firm_res[firmPtr->firm_id]->name;

			ratingArray[candidateCount++] = bestRating;
		}
	}

	//---- supply wagons and caravans seen near our forts ----//

	int canRaidCaravan = reputation >= CARAVAN_RAID_MIN_REPUTATION && pref_peacefulness < CARAVAN_RAID_MAX_PEACEFULNESS;

	for( i=unit_array.size() ; i>0 && candidateCount < MAX_RAID_CANDIDATE ; i-- )
	{
		if( unit_array.is_deleted(i) )
			continue;

		Unit* unitPtr = unit_array[i];

		int typeValue;

		if( unitPtr->unit_id == UNIT_SUPPLY_WAGON )		// it keeps an enemy army going, see OUNITSUP.CPP
			typeValue = 110 + (styleId == AI_STYLE_WARLORD ? 20 : 0);
		else if( unitPtr->unit_id == UNIT_CARAVAN && canRaidCaravan )
			typeValue = 70 + (styleId == AI_STYLE_MERCHANT ? 10 : 0);
		else
			continue;

		if( !unitPtr->is_visible() || !unitPtr->nation_recno || unitPtr->nation_recno == nation_recno ||
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

		FirmCamp* bestCamp = NULL;
		int		 bestRating = -1000, bestRaidSize = 0;

		for( int j=0 ; j<ai_camp_count ; j++ )
		{
			FirmCamp* firmCamp = (FirmCamp*) firm_array[ ai_camp_array[j] ];

			if( firmCamp->region_id != regionId || firmCamp->is_attack_camp || firmCamp->ai_capture_town_recno )
				continue;

			int campDistance = misc.points_distance( firmCamp->center_x, firmCamp->center_y, xLoc, yLoc );

			if( campDistance > MAX_RAID_DISTANCE/2 )		// they move on, only those close by
				continue;

			int raidSize = MIN( MAX_RAID_SIZE, raid_soldier_count(firmCamp) );

			if( raidSize < 2 )
				continue;

			if( raid_party_strength(firmCamp, raidSize) < defenseLevel * 6 / 5 + 50 )
				continue;

			int curRating = typeValue - campDistance*2 - defenseLevel/5;

			if( curRating > bestRating )
			{
				bestRating = curRating;
				bestCamp = firmCamp;
				bestRaidSize = raidSize;
			}
		}

		if( bestCamp && bestRating > 0 )
		{
			RaidCandidate* c = candidateArray + candidateCount;

			c->camp = bestCamp; c->raid_size = bestRaidSize;
			c->x_loc = xLoc; c->y_loc = yLoc;
			c->unit_recno = i; c->firm_recno = 0;
			c->name = unitPtr->unit_id == UNIT_SUPPLY_WAGON ? "supply wagon" : "caravan";

			ratingArray[candidateCount++] = bestRating;
		}
	}

	//------- choose among the best -------//

	int pick = AiWarState::weighted_pick(ratingArray, candidateCount, 80);

	if( pick < 0 )
		return 0;

	RaidCandidate* c = candidateArray + pick;

	int raidCount = send_raid_party( c->camp, c->raid_size, c->x_loc, c->y_loc, c->unit_recno );

	if( raidCount == 0 )
		return 0;

	if( c->firm_recno )
	{
		learnPtr->raids_tried++;
		learnPtr->raid_target_firm_recno = c->firm_recno;
		learnPtr->raid_date = info.game_date;
	}

	AiWarState::log( "RAID        nation %d (%s) sends %d soldiers from fort %d to raid a %s at %d,%d (rating %d, of %d targets)",
						  nation_recno, AiWarState::style_name(styleId), raidCount, c->camp->firm_recno, c->name,
						  c->x_loc, c->y_loc, ratingArray[pick], candidateCount );
	return 1;
}
//----------- End of function Nation::think_raid -----------//


//--------- Begin of function Nation::send_raid_party ---------//
//
// Send soldiers of a fort, those with the most hit points, to attack a
// target. They return to their fort when done (Unit::process_ai, home camp).
//
// <int> targetUnitRecno - the unit to attack, 0 for a building or town at the location
//
// return : <int> the no. of soldiers sent
//
int Nation::send_raid_party(FirmCamp* firmCamp, int partySize, int targetXLoc, int targetYLoc, int targetUnitRecno)
{
	short partyArray[MAX_RAID_SIZE];
	int   partyCount = 0;

	partySize = MIN( partySize, MAX_RAID_SIZE );

	while( partyCount < partySize )
	{
		int bestWorkerId = 0;

		for( int w=1 ; w<=firmCamp->worker_count ; w++ )
		{
			Worker* workerPtr = firmCamp->worker_array + w - 1;

			if( workerPtr->skill_id == SKILL_LEADING &&
				 (!bestWorkerId || workerPtr->hit_points > firmCamp->worker_array[bestWorkerId-1].hit_points) )
			{
				bestWorkerId = w;
			}
		}

		if( !bestWorkerId )
			break;

		int unitRecno = firmCamp->mobilize_worker( bestWorkerId, COMMAND_AI );

		if( !unitRecno )		// no room around the fort
			break;

		partyArray[partyCount++] = unitRecno;
	}

	if( partyCount == 0 )
		return 0;

	enable_should_attack_on_target( targetXLoc, targetYLoc );

	unit_array.attack( targetXLoc, targetYLoc, 0, partyArray, partyCount, COMMAND_AI, targetUnitRecno );

	return partyCount;
}
//----------- End of function Nation::send_raid_party -----------//


//--------- Begin of function Nation::think_feint ---------//
//
// When an offensive starts, a small party from another fort attacks a
// second town of the same enemy, far from the main target, to draw its
// defenders away. How often depends on the style, and on how often
// feints have paid off before (see AiLearning).
//
// return : <int> 1 if a feint has been sent
//
int Nation::think_feint(Town* mainTargetTown)
{
	AiLearning* learnPtr = ai_war_state.get_learning(nation_recno);

	int styleChance;

	switch( ai_style() )
	{
		case AI_STYLE_WARLORD:		styleChance = 60; break;
		case AI_STYLE_MERCHANT:		styleChance = 15; break;
		case AI_STYLE_BUILDER:		styleChance = 20; break;
		case AI_STYLE_SCHEMER:		styleChance = 70; break;
		default:							styleChance = 50; break;
	}

	int successPercent = (learnPtr->feints_paid_off+1) * 100 / (learnPtr->feints_tried+2);		// 50% before any feint
	int chance = MIN( 90, styleChance * successPercent / 50 );
	int roll   = misc.random(100);

	if( roll >= chance )
	{
		AiWarState::log( "FEINT       nation %d (%s) won't feint this time (chance %d%%, rolled %d; feints paid off %d of %d)",
							  nation_recno, AiWarState::style_name(ai_style()), chance, roll, learnPtr->feints_paid_off, learnPtr->feints_tried );
		return 0;
	}

	//------ another town of the same enemy, away from the main target ------//

	Town* feintTown = NULL;
	int   bestRating = -1000, hasWar;

	for( int i=town_array.size() ; i>0 ; i-- )
	{
		if( town_array.is_deleted(i) )
			continue;

		Town* townPtr = town_array[i];

		if( townPtr == mainTargetTown || townPtr->nation_recno != mainTargetTown->nation_recno ||
			 townPtr->region_id != mainTargetTown->region_id || !ai_has_seen_town(townPtr) )
		{
			continue;
		}

		int distance = misc.points_distance(townPtr->center_x, townPtr->center_y, mainTargetTown->center_x, mainTargetTown->center_y);

		if( distance < 12 || distance > 70 )
			continue;

		int defenseLevel = enemy_town_combat_level(townPtr, 0, hasWar);
		int curRating = distance - defenseLevel/5;

		if( curRating > bestRating )
		{
			bestRating = curRating;
			feintTown = townPtr;
		}
	}

	if( !feintTown )
	{
		AiWarState::log( "FEINT       nation %d: no other town of nation %d to feint at", nation_recno, mainTargetTown->nation_recno );
		return 0;
	}

	//------ a fort not in the main attack, with soldiers to spare ------//

	FirmCamp* bestCamp = NULL;
	int       minDistance = 0x7FFF;

	for( int j=0 ; j<ai_camp_count ; j++ )
	{
		FirmCamp* firmCamp = (FirmCamp*) firm_array[ ai_camp_array[j] ];

		if( firmCamp->region_id != feintTown->region_id || firmCamp->is_attack_camp || firmCamp->ai_capture_town_recno ||
			 raid_soldier_count(firmCamp) < 2 )
		{
			continue;
		}

		int curDistance = misc.points_distance(firmCamp->center_x, firmCamp->center_y, feintTown->center_x, feintTown->center_y);

		if( curDistance < minDistance )
		{
			minDistance = curDistance;
			bestCamp = firmCamp;
		}
	}

	if( !bestCamp )
	{
		AiWarState::log( "FEINT       nation %d: no fort can spare soldiers for a feint at town %d", nation_recno, feintTown->town_recno );
		return 0;
	}

	int sentCount = send_raid_party( bestCamp, MIN(3, raid_soldier_count(bestCamp)), feintTown->loc_x1, feintTown->loc_y1, 0 );

	if( !sentCount )
		return 0;

	learnPtr->feints_tried++;
	learnPtr->feint_pending = 1;

	AiWarState::log( "FEINT       nation %d sends %d soldiers from fort %d at town %d of nation %d, %d locations from the main target town %d",
						  nation_recno, sentCount, bestCamp->firm_recno, feintTown->town_recno, feintTown->nation_recno,
						  misc.points_distance(feintTown->center_x, feintTown->center_y, mainTargetTown->center_x, mainTargetTown->center_y),
						  mainTargetTown->town_recno );
	return 1;
}
//----------- End of function Nation::think_feint -----------//


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
