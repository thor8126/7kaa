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
//
// New wars are chosen by war_target_rating(): an old grudge, how close the
// kingdom is, the odds counting the kingdoms already fighting it and the
// coalition partners trailing the same leader, whether it is the leading
// kingdom, whether it is a human player (by AI aggressiveness) and the
// trade with it. Kingdoms trailing the same leader don't fight each other.

#include <ALL.h>
#include <OCONFIG.h>
#include <ONATION.h>
#include <OWORLD.h>
#include <OTOWN.h>
#include <OFIRM.h>
#include <OUNIT.h>
#include <OUNITRES.h>
#include <OF_CAMP.h>
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


//--------- Begin of function Nation::co_belligerent_military ---------//
//
// return : <int> the military rank ratings of the other kingdoms at war
//			  with the given enemy and not with us, counted at 3/4 as they
//			  fight it on their own terms
//
int Nation::co_belligerent_military(int enemyNationRecno)
{
	int totalMilitary = 0;

	for( int i=nation_array.size() ; i>0 ; i-- )
	{
		if( nation_array.is_deleted(i) || i==nation_recno || i==enemyNationRecno )
			continue;

		if( get_relation_status(i) == NATION_HOSTILE )
			continue;

		if( nation_array[i]->get_relation_status(enemyNationRecno) == NATION_HOSTILE )
			totalMilitary += nation_array[i]->military_rank_rating() * 3 / 4;
	}

	return totalMilitary;
}
//----------- End of function Nation::co_belligerent_military -----------//


//--------- Begin of function Nation::total_co_belligerent_military ---------//
//
// return : <int> the military rank ratings of the other kingdoms at war
//			  with any of our enemies and not with us, counted at 3/4
//
int Nation::total_co_belligerent_military()
{
	int totalMilitary = 0;

	for( int i=nation_array.size() ; i>0 ; i-- )
	{
		if( nation_array.is_deleted(i) || i==nation_recno || get_relation_status(i) == NATION_HOSTILE )
			continue;

		Nation* nationPtr = nation_array[i];

		for( int j=nation_array.size() ; j>0 ; j-- )
		{
			if( nation_array.is_deleted(j) || j==nation_recno || j==i )
				continue;

			if( get_relation_status(j) == NATION_HOSTILE && nationPtr->get_relation_status(j) == NATION_HOSTILE )
			{
				totalMilitary += nationPtr->military_rank_rating() * 3 / 4;
				break;
			}
		}
	}

	return totalMilitary;
}
//----------- End of function Nation::total_co_belligerent_military -----------//


//--------- Begin of function Nation::war_target_rating ---------//
//
// How much this nation wants a war with the given nation, for
// think_declare_war(). It logs how the rating is made up.
//
// return : <int> the rating, 0 for a war it can't win even with the
//			  kingdoms already fighting the target and its coalition partners
//
int Nation::war_target_rating(int nationRecno)
{
	Nation*			 targetNation   = nation_array[nationRecno];
	NationRelation* nationRelation = get_relation(nationRecno);
	int				 isHuman			 = !targetNation->is_ai();
	int				 leaderNationRecno = nation_array.max_overall_nation_recno;

	//------ an old grudge: a former enemy has a relation level near 0, a neutral kingdom 40 ------//

	int grudgeRating = MAX( -30, MIN( 30, 30 - nationRelation->ai_relation_level ) );

	grudgeRating += MIN( 30, nationRelation->started_war_on_us_count*10 );

	//------ how close it is: our town nearest to one of its towns we know of on the same land ------//

	int minDistance = 0x7FFF;

	for( int i=town_array.size() ; i>0 ; i-- )
	{
		if( town_array.is_deleted(i) )
			continue;

		Town* townPtr = town_array[i];

		if( townPtr->nation_recno != nationRecno || !ai_has_seen_town(townPtr) )
			continue;

		for( int j=0 ; j<ai_town_count ; j++ )
		{
			Town* ownTown = town_array[ ai_town_array[j] ];

			if( ownTown->region_id != townPtr->region_id )
				continue;

			int curDistance = misc.points_distance( ownTown->center_x, ownTown->center_y, townPtr->center_x, townPtr->center_y );

			if( curDistance < minDistance )
				minDistance = curDistance;
		}
	}

	int nearRating = minDistance < 0x7FFF ? MAX( 0, 30 - minDistance/4 ) : 0;		// 30 next door, 0 at 120 locations or across the sea

	//------ the odds, counting those already fighting it and the coalition partners trailing the same leader ------//

	int ownMilitary = military_rank_rating();
	int coBelligerentMilitary = co_belligerent_military(nationRecno);
	int partnerMilitary = 0;

	for( int i=nation_array.size() ; i>0 ; i-- )
	{
		if( nation_array.is_deleted(i) || i==nation_recno || i==nationRecno )
			continue;

		if( get_relation_status(i) >= NATION_NEUTRAL && common_threat_rating(i) > 0 &&
			 nationRecno == leaderNationRecno &&
			 nation_array[i]->get_relation_status(nationRecno) != NATION_HOSTILE )		// those at war with it are co-belligerents already
		{
			partnerMilitary += nation_array[i]->military_rank_rating() / 3;		// they may join, see consider_declare_war()
		}
	}

	int ourSideMilitary = ownMilitary + coBelligerentMilitary + partnerMilitary;
	int theirMilitary   = intel_military_rank_rating(nationRecno);		// as far as we know, see OAI_INTEL.CPP

	int minOddsPercent = 80;		// our side must be at least this strong compared with the target

	if( isHuman )
	{
		if( config.ai_aggressiveness == OPTION_HIGH )
			minOddsPercent = 60;
		else if( config.ai_aggressiveness >= OPTION_VERY_HIGH )
			minOddsPercent = 50;
	}

	//--- a runaway leader must be stopped before it is too late: worse odds are accepted, more by bold styles ---//

	if( nationRecno == leaderNationRecno && targetNation->overall_rank_rating() >= overall_rank_rating() + 30 )
		minOddsPercent = MIN( minOddsPercent, ai_style()==AI_STYLE_WARLORD || ai_style()==AI_STYLE_OPPORTUNIST ? 50 : 60 );

	//--- a war against a stronger kingdom isn't hopeless where its border is weak: the forces near its town closest to us ---//

	int localOurs=0, localTheirs=0;
	int localOdds = local_war_odds(nationRecno, localOurs, localTheirs);		// percent, 0 if no town of it within reach

	int localAdvantage = localOdds >= 150 && localOurs >= 150;		// clearly stronger there, with a real force

	if( config_adv.nation_ai_posture && ourSideMilitary*100 < theirMilitary*minOddsPercent && !localAdvantage )
	{
		AiWarState::log( "WAR TARGET  nation %d rates %d%s: hopeless (our side %d = own %d + co-belligerents %d + partners %d, theirs as we know %d, needs %d%%; near its closest town ours %d, theirs %d)",
							  nation_recno, nationRecno, isHuman ? " (human)" : "", ourSideMilitary, ownMilitary,
							  coBelligerentMilitary, partnerMilitary, theirMilitary, minOddsPercent, localOurs, localTheirs );
		return 0;
	}

	int oddsRating = MAX( -40, MIN( 40, (ourSideMilitary - theirMilitary)/2 ) );

	if( localAdvantage )		// a strike at its weak border
		oddsRating = MAX( oddsRating, MIN( 30, (localOdds-100)/5 ) );

	//------ the leading kingdom, which the trailing kingdoms should stop ------//

	int leaderRating = 0;

	if( nationRecno == leaderNationRecno && is_coalition_target(nationRecno) &&
		 targetNation->overall_rank_rating() >= overall_rank_rating() + 30 )
	{
		leaderRating = 25;
	}

	//------ humans, by AI aggressiveness ------//

	int humanRating = 0;

	if( isHuman )
	{
		switch( config.ai_aggressiveness )
		{
			case OPTION_MODERATE:  humanRating = 10; break;
			case OPTION_HIGH:		  humanRating = 20; break;
			case OPTION_VERY_HIGH: humanRating = 30; break;
		}
	}

	//------ kingdoms trailing the same leader don't fight each other, see OAI_GRAN.CPP ------//

	int threatRating = -2 * common_threat_rating(nationRecno);

	int tradeRating = -trade_rating(nationRecno)/2;

	//------ its style, the grudge it holds and the chance it sees now, see OAIWAR.H ------//

	int styleRating = 0;

	switch( ai_style() )
	{
		case AI_STYLE_WARLORD:		styleRating = 15;  break;
		case AI_STYLE_MERCHANT:		styleRating = -15; tradeRating *= 2; break;
		case AI_STYLE_BUILDER:		styleRating = -10; break;
		case AI_STYLE_SCHEMER:		styleRating = 5;   break;
		case AI_STYLE_OPPORTUNIST:	styleRating = 5;   break;
	}

	AiMemory* memPtr = ai_war_state.get_memory(nation_recno, nationRecno);

	int revengeRating = memPtr ? (int) memPtr->grudge * (100-pref_forgiveness) / 300 : 0;		// 0 to 33

	char opportunityStr[120];
	int  opportunity = opportunity_rating(nationRecno, opportunityStr, sizeof(opportunityStr));
	int  opportunityWeight = ai_style()==AI_STYLE_OPPORTUNIST ? 150 : ai_style()==AI_STYLE_WARLORD ? 100 : ai_style()==AI_STYLE_SCHEMER ? 80 : 40;
	int  opportunityRating = opportunity * opportunityWeight / 100;

	int totalRating = grudgeRating + nearRating + oddsRating + leaderRating + humanRating + threatRating + tradeRating +
							styleRating + revengeRating + opportunityRating;

	AiWarState::log( "WAR TARGET  nation %d rates %d%s: %d = grudge %d (relation %d, wars on us %d) + near %d (distance %d) + odds %d (our side %d = own %d + co-belligerents %d + partners %d, theirs %d) + leader %d + human %d + common threat %d + trade %d + %s %d + revenge %d (grudge %.0f) + opportunity %d (%s)",
						  nation_recno, nationRecno, isHuman ? " (human)" : "", totalRating,
						  grudgeRating, nationRelation->ai_relation_level, nationRelation->started_war_on_us_count,
						  nearRating, minDistance < 0x7FFF ? minDistance : -1,
						  oddsRating, ourSideMilitary, ownMilitary, coBelligerentMilitary, partnerMilitary, theirMilitary,
						  leaderRating, humanRating, threatRating, tradeRating,
						  AiWarState::style_name(ai_style()), styleRating, revengeRating, memPtr ? memPtr->grudge : 0.0f,
						  opportunityRating, opportunityStr );

	return totalRating;
}
//----------- End of function Nation::war_target_rating -----------//


//--------- Begin of function Nation::local_war_odds ---------//
//
// The odds near the given nation's town closest to us on the same land:
// our forts within reach of it against its forts there that we have seen
// and its units there that we see now, by combat strength.
//
// <int&> localOurs, localTheirs - the strength of both sides there
//
// return : <int> our strength there in percent of theirs, 0 if no town of
//          the nation we know of is within reach
//
int Nation::local_war_odds(int targetNationRecno, int& localOurs, int& localTheirs)
{
	enum { LOCAL_REACH = 40 };

	localOurs = localTheirs = 0;

	//------ its town closest to one of ours ------//

	Town* closestTown = NULL;
	int   minDistance = 0x7FFF;

	for( int i=town_array.size() ; i>0 ; i-- )
	{
		if( town_array.is_deleted(i) )
			continue;

		Town* townPtr = town_array[i];

		if( townPtr->nation_recno != targetNationRecno || !ai_has_seen_town(townPtr) )
			continue;

		for( int j=0 ; j<ai_town_count ; j++ )
		{
			Town* ownTown = town_array[ ai_town_array[j] ];

			if( ownTown->region_id != townPtr->region_id )
				continue;

			int curDistance = misc.points_distance( ownTown->center_x, ownTown->center_y, townPtr->center_x, townPtr->center_y );

			if( curDistance < minDistance )
			{
				minDistance = curDistance;
				closestTown = townPtr;
			}
		}
	}

	if( !closestTown || minDistance > LOCAL_REACH*2 )
		return 0;

	//------ the forts and units of both sides near it ------//

	for( int i=firm_array.size() ; i>0 ; i-- )
	{
		if( firm_array.is_deleted(i) )
			continue;

		Firm* firmPtr = firm_array[i];

		if( firmPtr->firm_id != FIRM_CAMP || firmPtr->under_construction )
			continue;

		int curDistance = misc.points_distance( firmPtr->center_x, firmPtr->center_y, closestTown->center_x, closestTown->center_y );

		if( firmPtr->nation_recno == nation_recno && curDistance <= LOCAL_REACH*2 )		// our forts can march there
			localOurs += ((FirmCamp*)firmPtr)->total_combat_level();

		else if( firmPtr->nation_recno == targetNationRecno && curDistance <= LOCAL_REACH && ai_has_seen_firm(firmPtr) )
			localTheirs += ((FirmCamp*)firmPtr)->total_combat_level();
	}

	for( int i=unit_array.size() ; i>0 ; i-- )
	{
		if( unit_array.is_deleted(i) )
			continue;

		Unit* unitPtr = unit_array[i];

		if( unitPtr->nation_recno != targetNationRecno || !unitPtr->is_visible() || unitPtr->unit_mode == UNIT_MODE_OVERSEE ||
			 (!unitPtr->race_id && unit_res[unitPtr->unit_id]->unit_class != UNIT_CLASS_WEAPON) )
		{
			continue;
		}

		if( misc.points_distance( unitPtr->next_x_loc(), unitPtr->next_y_loc(), closestTown->center_x, closestTown->center_y ) <= LOCAL_REACH &&
			 ai_sees_unit(unitPtr) )
		{
			localTheirs += unitPtr->unit_power();
		}
	}

	localTheirs += closestTown->population;		// its villagers defend it too

	return localTheirs > 0 ? localOurs * 100 / localTheirs : 1000;
}
//----------- End of function Nation::local_war_odds -----------//


//--------- Begin of function Nation::opportunity_rating ---------//
//
// How good a moment it is to strike the given nation, from what this
// nation can know: it is busy fighting another kingdom (wars are public),
// it has just lost troops, forts of it that this nation sees now are
// nearly empty, its soldiers are seen away from home.
//
// [char*] reasonStr - a description of the rating for the event log
//
// return : <int> 0 for no opportunity, 30 and more for a good one
//
int Nation::opportunity_rating(int targetNationRecno, char* reasonStr, int reasonLen)
{
	if( nation_array.is_deleted(targetNationRecno) || targetNationRecno == nation_recno )
		return 0;

	//------ busy fighting another kingdom ------//

	int busyRating = 0, busyWithNation = 0;

	for( int i=nation_array.size() ; i>0 ; i-- )
	{
		if( nation_array.is_deleted(i) || i==nation_recno || i==targetNationRecno )
			continue;

		if( nation_array[targetNationRecno]->get_relation_status(i) != NATION_HOSTILE )
			continue;

		WarRecord* warRecord = ai_war_state.get_record(targetNationRecno, i);

		if( warRecord && warRecord->start_date && info.game_date - warRecord->last_combat_date <= 30 )
		{
			int curRating = 25 + (warRecord->losses > warRecord->kills ? 10 : 0);		// more if it is losing

			if( curRating > busyRating )
			{
				busyRating = curRating;
				busyWithNation = i;
			}
		}
	}

	//------ it has just lost troops ------//

	float recentLosses = ai_war_state.recent_losses[targetNationRecno-1];

	int lossRating = recentLosses >= 30.0f ? 30 : recentLosses >= 15.0f ? 20 : recentLosses >= 8.0f ? 10 : 0;

	//------ its forts this nation sees now that are nearly empty ------//

	int emptyFortCount = 0;

	for( int i=firm_array.size() ; i>0 ; i-- )
	{
		if( firm_array.is_deleted(i) )
			continue;

		Firm* firmPtr = firm_array[i];

		if( firmPtr->nation_recno != targetNationRecno || firmPtr->firm_id != FIRM_CAMP || firmPtr->under_construction )
			continue;

		if( firmPtr->worker_count <= 2 && ai_sees_loc(firmPtr->center_x, firmPtr->center_y) )
			emptyFortCount++;
	}

	int fortRating = MIN( 24, emptyFortCount*8 );

	//------ its soldiers seen away from its land ------//

	int awayCount = 0;

	for( int i=unit_array.size() ; i>0 ; i-- )
	{
		if( unit_array.is_deleted(i) )
			continue;

		Unit* unitPtr = unit_array[i];

		if( unitPtr->nation_recno != targetNationRecno || !unitPtr->is_visible() || !unitPtr->race_id ||
			 (unitPtr->rank_id != RANK_SOLDIER && unitPtr->rank_id != RANK_GENERAL) || unitPtr->is_civilian() )
		{
			continue;
		}

		if( world.get_loc(unitPtr->next_x_loc(), unitPtr->next_y_loc())->power_nation_recno == targetNationRecno )
			continue;

		if( ai_sees_unit(unitPtr) )
			awayCount++;
	}

	int awayRating = awayCount >= 8 ? 20 : awayCount >= 4 ? 10 : 0;

	int totalRating = busyRating + lossRating + fortRating + awayRating;

	if( reasonStr )
	{
		snprintf( reasonStr, reasonLen, "busy %d (war with %d), losses %d (%.0f), empty forts %d (%d), soldiers away %d (%d)",
					 busyRating, busyWithNation, lossRating, recentLosses, fortRating, emptyFortCount, awayRating, awayCount );
	}

	return totalRating;
}
//----------- End of function Nation::opportunity_rating -----------//


//--------- Begin of function Nation::is_frontline_town ---------//
//
// return : <int> 1 if a village or building of a kingdom at war with this
//          nation is near the village (see also AI walls, OCIVIL.CPP, and
//          the forts that can go on an offensive, OAI_ATTK.CPP)
//
int Nation::is_frontline_town(Town* townPtr)
{
	enum { FRONTLINE_DISTANCE = 25 };

	for( int i=town_array.size() ; i>0 ; i-- )
	{
		if( town_array.is_deleted(i) )
			continue;

		Town* otherTown = town_array[i];

		if( otherTown->nation_recno && otherTown->nation_recno != nation_recno &&
			 get_relation_status(otherTown->nation_recno) == NATION_HOSTILE &&
			 misc.points_distance(otherTown->center_x, otherTown->center_y, townPtr->center_x, townPtr->center_y) <= FRONTLINE_DISTANCE )
		{
			return 1;
		}
	}

	for( int i=firm_array.size() ; i>0 ; i-- )
	{
		if( firm_array.is_deleted(i) )
			continue;

		Firm* firmPtr = firm_array[i];

		if( firmPtr->nation_recno && firmPtr->nation_recno != nation_recno &&
			 get_relation_status(firmPtr->nation_recno) == NATION_HOSTILE &&
			 misc.points_distance(firmPtr->center_x, firmPtr->center_y, townPtr->center_x, townPtr->center_y) <= FRONTLINE_DISTANCE )
		{
			return 1;
		}
	}

	return 0;
}
//----------- End of function Nation::is_frontline_town -----------//


//--------- Begin of function Nation::think_opportunity ---------//
//
// Opportunists and warlords look for a good moment to strike more often
// than the regular offensive thinking, see Nation::process_ai().
//
void Nation::think_opportunity()
{
	if( ai_capture_enemy_town_recno || attack_camp_count > 0 )
		return;

	int bestOpportunity = 0, bestNationRecno = 0;
	char reasonStr[120] = "", bestReasonStr[120] = "";

	for( int i=nation_array.size() ; i>0 ; i-- )
	{
		if( nation_array.is_deleted(i) || i==nation_recno || get_relation_status(i) != NATION_HOSTILE )
			continue;

		if( !ai_knows_nation(i) || !ai_can_reach_nation(i) )
			continue;

		int curOpportunity = opportunity_rating(i, reasonStr, sizeof(reasonStr));

		if( curOpportunity > bestOpportunity )
		{
			bestOpportunity = curOpportunity;
			bestNationRecno = i;
			strcpy( bestReasonStr, reasonStr );
		}
	}

	int threshold = ai_style()==AI_STYLE_OPPORTUNIST ? 30 : 40;

	if( !bestNationRecno || bestOpportunity < threshold )
		return;

	AiWarState::log( "OPPORTUNITY nation %d (%s) sees a chance against nation %d: %d (%s)",
						  nation_recno, AiWarState::style_name(ai_style()), bestNationRecno, bestOpportunity, bestReasonStr );

	think_war_offensive();
}
//----------- End of function Nation::think_opportunity -----------//


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

	//------ the enemy that offers the best opportunity now, see opportunity_rating() ------//

	int opportunityNation = 0, opportunity = 0;
	char opportunityStr[120] = "";

	for( int i=nation_array.size() ; i>0 ; i-- )
	{
		if( nation_array.is_deleted(i) || i==nation_recno || get_relation_status(i) != NATION_HOSTILE )
			continue;

		char curStr[120];
		int  curOpportunity = opportunity_rating(i, curStr, sizeof(curStr));

		if( curOpportunity > opportunity )
		{
			opportunity = curOpportunity;
			opportunityNation = i;
			strcpy( opportunityStr, curStr );
		}
	}

	ai_war_state.current_opportunity[nation_recno-1] = opportunity;		// it lowers the odds wanted, see AiWarState::attack_odds_percent()

	//------ the own town closest to a known enemy town, nearer counted for the enemy with the best opportunity ------//

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

			if( enemyTown->nation_recno == opportunityNation )
				curDistance -= opportunity/2;

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

	if( opportunity >= 40 && nearestEnemyTown->nation_recno == opportunityNation )		// strike hard while it lasts
		useAllCamp = 1;

	AiWarState::log( "OFFENSIVE   nation %d (%s) considers nation %d from town %d (enemy town %d, distance %d, our military %d, theirs as we know %d, cash %.0f, use all forts %d; best opportunity %d against %d: %s)",
						  nation_recno, AiWarState::style_name(ai_style()), nearestEnemyTown->nation_recno, capturerTown->town_recno, nearestEnemyTown->town_recno,
						  minDistance, military_rank_rating(), enemyMilitary, cash, useAllCamp, opportunity, opportunityNation, opportunityStr );

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
