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

//Filename    : OAI_OFFN.CPP
//Description : AI war campaigns
//
// In the original game an AI nation at war only attacked enemy towns when
// a rejected demand made it declare war (at high aggressiveness), or when
// the enemy was almost destroyed: most wars were declared and then nothing
// happened until war weariness ended them.
//
// Now, while at war and not on the defensive, every 20 days the nation
// goes after an enemy town it knows about from its town closest to one:
// Nation::think_capture_new_enemy_town() picks the target, attacks its
// defenders and captures it (by building a fort with a general next to it
// that wears its loyalty down; at most MAX_CAPTURE_OPERATIONS towns at a
// time). It commits all its forts when it is much stronger than the enemy
// as far as it knows.
//
// Many wars were also fought between nations on different land, which
// couldn't reach each other: AI nations no longer plan surprise wars on
// nations they can't reach (ai_can_reach_nation()), and accept peace more
// readily in wars where neither side can reach the other.

#include <ALL.h>
#include <ONATION.h>
#include <OWORLD.h>
#include <OTOWN.h>
#include <OFIRM.h>
#include <OUNIT.h>
#include <OINFO.h>
#include <OAIWAR.h>
#include <OMISC.h>
#include <ConfigAdv.h>

//------- Define constants -------//

#define MAX_CAPTURE_OPERATIONS		2		// enemy towns with our forts next to them, being worn down for capturing

//--------- Begin of function Nation::ai_can_reach_nation ---------//
//
// return : <int> 1 if this nation can reach the given nation to fight it:
//			  one of its towns we know of is on the same land as one of ours
//			  (the AI's town offensives are fought over land), or we have a
//			  warship and know one of its harbors (for sea attacks, see
//			  Nation::think_sea_attack_enemy())
//
int Nation::ai_can_reach_nation(int nationRecno)
{
	int i;

	for( i=town_array.size() ; i>0 ; i-- )
	{
		if( town_array.is_deleted(i) )
			continue;

		Town* townPtr = town_array[i];

		if( townPtr->nation_recno != nationRecno || !ai_has_seen_town(townPtr) )
			continue;

		for( int j=0 ; j<ai_town_count ; j++ )
		{
			if( town_array[ ai_town_array[j] ]->region_id == townPtr->region_id )
				return 1;
		}
	}

	//------ a naval war: a warship of ours and a harbor of theirs ------//

	int hasWarship = 0;

	for( i=0 ; i<ai_ship_count ; i++ )
	{
		if( !unit_array.is_deleted(ai_ship_array[i]) && unit_array[ ai_ship_array[i] ]->attack_count > 0 )
		{
			hasWarship = 1;
			break;
		}
	}

	if( !hasWarship )
		return 0;

	for( i=firm_array.size() ; i>0 ; i-- )
	{
		if( firm_array.is_deleted(i) )
			continue;

		Firm* firmPtr = firm_array[i];

		if( firmPtr->firm_id == FIRM_HARBOR && firmPtr->nation_recno == nationRecno && ai_has_seen_firm(firmPtr) )
			return 1;
	}

	return 0;
}
//----------- End of function Nation::ai_can_reach_nation -----------//


//--------- Begin of function Nation::ai_capture_operation_busy ---------//
//
// Capturing an enemy town means building a fort with a general next to it
// that wears its loyalty down. Called by think_capture_new_enemy_town()
// for all its callers, so that new operations are not started over and over.
//
// return : <int> 1 if no new capture operation should be started now
//
int Nation::ai_capture_operation_busy()
{
	int i;

	//--- a fort ordered next to an enemy town hasn't been built yet ---//

	for( i=action_count() ; i>0 ; i-- )
	{
		ActionNode* actionNode = get_action(i);

		if( actionNode->action_mode != ACTION_AI_BUILD_FIRM || actionNode->action_para != FIRM_CAMP ||
			 actionNode->ref_x_loc < 0 || actionNode->ref_y_loc < 0 )
		{
			continue;
		}

		Location* refLocPtr = world.get_loc(actionNode->ref_x_loc, actionNode->ref_y_loc);

		if( !refLocPtr->is_town() )
			continue;

		Town* refTown = town_array[refLocPtr->town_recno()];

		if( refTown->nation_recno && refTown->nation_recno != nation_recno )
		{
			AiWarState::log( "OFFENSIVE   nation %d: waiting for its fort to be built next to town %d of nation %d (ordered %d days ago)",
								  nation_recno, refTown->town_recno, refTown->nation_recno, info.game_date - actionNode->add_date );
			return 1;
		}
	}

	//--- enemy towns already being worn down by our forts ---//

	int captureCampCount = 0;

	for( i=town_array.size() ; i>0 ; i-- )
	{
		if( town_array.is_deleted(i) )
			continue;

		Town* townPtr = town_array[i];

		if( townPtr->nation_recno && townPtr->nation_recno != nation_recno &&
			 townPtr->has_linked_camp(nation_recno, 0) )
		{
			captureCampCount++;
		}
	}

	if( captureCampCount >= MAX_CAPTURE_OPERATIONS )
	{
		AiWarState::log( "OFFENSIVE   nation %d: waiting, %d enemy towns are being worn down by its forts", nation_recno, captureCampCount );
		return 1;
	}

	return 0;
}
//----------- End of function Nation::ai_capture_operation_busy -----------//


//--------- Begin of function Nation::think_war_offensive ---------//
//
// return : <int> 1 if an offensive has been started
//
int Nation::think_war_offensive()
{
	if( !config_adv.nation_ai_war_offensive || !ai_camp_count || !ai_town_count )
		return 0;

	if( ai_capture_enemy_town_recno || attack_camp_count > 0 )		// an offensive is going on already
		return 0;

	int curPosture = ai_war_state.get_posture(nation_recno);

	if( curPosture == AI_POSTURE_DEFEND || curPosture == AI_POSTURE_RECOVER )
		return 0;

	//------ the own town closest to a known enemy town ------//

	Town* capturerTown = NULL;
	Town* nearestEnemyTown = NULL;
	int   minDistance = 0x7FFF;

	for( int i=town_array.size() ; i>0 ; i-- )
	{
		if( town_array.is_deleted(i) )
			continue;

		Town* enemyTown = town_array[i];

		if( !enemyTown->nation_recno || enemyTown->nation_recno == nation_recno ||
			 get_relation_status(enemyTown->nation_recno) != NATION_HOSTILE ||
			 !ai_has_seen_town(enemyTown) )
		{
			continue;
		}

		if( enemyTown->has_linked_camp(nation_recno, 0) )		// already being worn down by our fort
			continue;

		for( int j=0 ; j<ai_town_count ; j++ )
		{
			Town* ownTown = town_array[ ai_town_array[j] ];

			if( ownTown->region_id != enemyTown->region_id )
				continue;

			int curDistance = misc.points_distance( ownTown->center_x, ownTown->center_y,
															 enemyTown->center_x, enemyTown->center_y );

			if( curDistance < minDistance )
			{
				minDistance = curDistance;
				capturerTown = ownTown;
				nearestEnemyTown = enemyTown;
			}
		}
	}


	if( !capturerTown )		// no enemy town we know of on our land
	{
		AiWarState::log( "OFFENSIVE   nation %d: no enemy town it knows of on its land", nation_recno );
		return 0;
	}

	//--- commit all forts when much stronger than the enemy as far as we know ---//

	int enemyMilitary = intel_military_rank_rating(nearestEnemyTown->nation_recno);
	int useAllCamp = military_rank_rating() >= enemyMilitary * 2;

	AiWarState::log( "OFFENSIVE   nation %d considers nation %d from town %d (enemy town %d, distance %d, our military %d, theirs as we know %d, cash %.0f)",
						  nation_recno, nearestEnemyTown->nation_recno, capturerTown->town_recno, nearestEnemyTown->town_recno,
						  minDistance, military_rank_rating(), enemyMilitary, cash );

	int oldTargetTownRecno = ai_capture_enemy_town_recno;

	if( !think_capture_new_enemy_town(capturerTown, useAllCamp) )		// it logs why not
		return 0;

	AiWarState::log( "OFFENSIVE   nation %d goes after nation %d from town %d (%s, our military %d, theirs as we know %d)",
						  nation_recno, nearestEnemyTown->nation_recno, capturerTown->town_recno,
						  ai_capture_enemy_town_recno != oldTargetTownRecno ? "attacking its defense" : "capturing",
						  military_rank_rating(), enemyMilitary );
	return 1;
}
//----------- End of function Nation::think_war_offensive -----------//
