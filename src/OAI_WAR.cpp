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

//Filename    : OAI_WAR.CPP
//Description : War records, war weariness and strategic posture of nations
//
// Every war between two nations is recorded from both sides. A nation
// gets weary of a war as it drags on and as it loses units, buildings
// and towns. Weary AI nations stop launching offensives, become willing
// to cease fire and stay at peace for a while afterwards.
//
// Once a month each AI nation also chooses a strategic posture which
// decides whether it may start new wars and offensives.

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <OAIWAR.h>
#include <ONATION.h>
#include <OUNIT.h>
#include <OUNITRES.h>
#include <OINFO.h>
#include <OFILE.h>
#include <OTOWN.h>
#include <OSITE.h>
#include <OREGION.h>
#include <OMISC.h>
#include <ConfigAdv.h>

//---------- Define constants ------------//

#define DAILY_WAR_WEARINESS			0.08f		// for a nation with 50 peacefulness
#define IDLE_WAR_WEARINESS				0.05f		// extra daily weariness for a war without fighting
#define IDLE_WAR_DAYS					90
#define DAILY_PEACE_RECOVERY			0.25f
#define LOSS_WEARINESS_MULTIPLIER	0.6f
#define KILL_WEARINESS_MULTIPLIER	0.25f
#define FIRM_LOST_WEARINESS			2.0f
#define TOWN_LOST_WEARINESS			12.0f
#define TOWN_GAINED_WEARINESS			6.0f
#define MIN_TRUCE_DAYS					180
#define POSTURE_MIN_DAYS				60			// minimum days before leaving a non-emergency posture
#define CONSOLIDATE_MIN_DAYS			180
#define STRATEGY_REVIEW_DAYS			365
#define STRATEGY_SWITCH_MARGIN		10			// a new strategy must rate this much higher to replace the current one
#define STRATEGY_SCAN_DISTANCE		80			// how far from its towns a nation looks for villages and raw materials

//------- Begin of function AiWarState::log -------//
//
// Append a line to the file named by the SKAI_LOG environment variable.
// It only writes a file and never affects the game.
//
void AiWarState::log(const char* format, ...)
{
	static int   initFlag = 0;
	static FILE* logFile = NULL;

	if( !initFlag )
	{
		initFlag = 1;
		const char* logPath = getenv("SKAI_LOG");

		if( logPath && logPath[0] )
			logFile = fopen(logPath, "a");
	}

	if( !logFile )
		return;

	fprintf( logFile, "%04d-%02d-%02d ", info.game_year, info.game_month, info.game_day );

	va_list args;
	va_start(args, format);
	vfprintf(logFile, format, args);
	va_end(args);

	fputc('\n', logFile);
	fflush(logFile);
}
//-------- End of function AiWarState::log --------//


//------- Begin of function AiWarState::init -------//
//
void AiWarState::init()
{
	memset( war_record, 0, sizeof(war_record) );
	memset( posture, AI_POSTURE_EXPAND, sizeof(posture) );
	memset( posture_date, 0, sizeof(posture_date) );
	memset( strategy, AI_STRATEGY_ECONOMY, sizeof(strategy) );
	memset( strategy_date, 0, sizeof(strategy_date) );
}
//-------- End of function AiWarState::init --------//


//------- Begin of function AiWarState::reset_pair -------//
//
// Called when the relation between two nations is initialized,
// e.g. a new nation is created and its recno may be a reused one.
//
void AiWarState::reset_pair(int nationRecno, int withNationRecno)
{
	WarRecord* warRecord = get_record(nationRecno, withNationRecno);

	if( warRecord )
		memset( warRecord, 0, sizeof(WarRecord) );
}
//-------- End of function AiWarState::reset_pair --------//


//------- Begin of function AiWarState::reset_nation -------//
//
void AiWarState::reset_nation(int nationRecno)
{
	if( nationRecno < 1 || nationRecno > MAX_NATION )
		return;

	for( int i=1 ; i<=MAX_NATION ; i++ )
	{
		reset_pair(nationRecno, i);
		reset_pair(i, nationRecno);
	}

	posture[nationRecno-1] = AI_POSTURE_EXPAND;
	posture_date[nationRecno-1] = info.game_date;

	strategy[nationRecno-1] = AI_STRATEGY_ECONOMY;
	strategy_date[nationRecno-1] = 0;		// evaluated on the nation's first day
}
//-------- End of function AiWarState::reset_nation --------//


//------- Begin of function AiWarState::get_record -------//
//
// return : <WarRecord*> nationRecno's record of its war with withNationRecno,
//			   NULL if either recno is invalid.
//
WarRecord* AiWarState::get_record(int nationRecno, int withNationRecno)
{
	if( nationRecno < 1 || nationRecno > MAX_NATION ||
		 withNationRecno < 1 || withNationRecno > MAX_NATION ||
		 nationRecno == withNationRecno )
	{
		return NULL;
	}

	return &war_record[nationRecno-1][withNationRecno-1];
}
//-------- End of function AiWarState::get_record --------//


//------- Begin of function AiWarState::weariness -------//
//
float AiWarState::weariness(int nationRecno, int withNationRecno)
{
	WarRecord* warRecord = get_record(nationRecno, withNationRecno);

	return warRecord ? warRecord->weariness : 0.0f;
}
//-------- End of function AiWarState::weariness --------//


//------- Begin of function AiWarState::get_posture -------//
//
int AiWarState::get_posture(int nationRecno)
{
	if( !config_adv.nation_ai_posture || nationRecno < 1 || nationRecno > MAX_NATION )
		return AI_POSTURE_EXPAND;

	return posture[nationRecno-1];
}
//-------- End of function AiWarState::get_posture --------//


//------- Begin of function AiWarState::get_strategy -------//
//
// return : <int> the nation's opening strategy, -1 if strategies are disabled
//
int AiWarState::get_strategy(int nationRecno)
{
	if( !config_adv.nation_ai_opening_strategy || nationRecno < 1 || nationRecno > MAX_NATION )
		return -1;

	return strategy[nationRecno-1];
}
//-------- End of function AiWarState::get_strategy --------//


//------- Begin of function AiWarState::add_weariness -------//
//
void AiWarState::add_weariness(int nationRecno, int withNationRecno, float amount)
{
	WarRecord* warRecord = get_record(nationRecno, withNationRecno);

	if( !warRecord )
		return;

	warRecord->weariness += amount;

	if( warRecord->weariness < 0.0f )
		warRecord->weariness = 0.0f;

	if( warRecord->weariness > (float) MAX_WAR_WEARINESS )
		warRecord->weariness = (float) MAX_WAR_WEARINESS;
}
//-------- End of function AiWarState::add_weariness --------//


//------- Begin of function AiWarState::status_changed -------//
//
// Called by NationBase::set_relation_status() after nationRecno's
// relation status with withNationRecno has been changed.
//
void AiWarState::status_changed(int nationRecno, int withNationRecno, int oldStatus, int newStatus)
{
	WarRecord* warRecord = get_record(nationRecno, withNationRecno);

	if( !warRecord || oldStatus == newStatus )
		return;

	//------- a new war starts ---------//

	if( newStatus == NATION_HOSTILE )
	{
		float   lastWeariness   = warRecord->weariness;		// weariness of a recent war carries over
		int32_t noWarUntilDate = warRecord->no_war_until_date;

		memset( warRecord, 0, sizeof(WarRecord) );

		warRecord->start_date        = info.game_date;
		warRecord->last_combat_date  = info.game_date;
		warRecord->weariness         = lastWeariness;
		warRecord->no_war_until_date = noWarUntilDate;

		log( "WAR START   nation %d vs %d (weariness carried %.1f)", nationRecno, withNationRecno, lastWeariness );
	}

	//------- a war ends ---------//

	else if( oldStatus == NATION_HOSTILE )
	{
		log( "WAR END     nation %d vs %d -> status %d, %d days, lost %.1f killed %.1f, towns -%d +%d, firms -%d, weariness %.1f",
			  nationRecno, withNationRecno, newStatus,
			  warRecord->start_date ? info.game_date - warRecord->start_date : 0,
			  warRecord->losses, warRecord->kills, warRecord->towns_lost, warRecord->towns_gained,
			  warRecord->firms_lost, warRecord->weariness );

		warRecord->start_date        = 0;
		warRecord->losses            = 0.0f;
		warRecord->kills             = 0.0f;
		warRecord->towns_lost        = 0;
		warRecord->towns_gained      = 0;
		warRecord->firms_lost        = 0;
		warRecord->no_war_until_date = info.game_date + MIN_TRUCE_DAYS + (int) (warRecord->weariness * 3);

		//--- cancel an ongoing plan to capture a town of the former enemy, otherwise it will resume attacking after the war ends ---//

		if( !nation_array.is_deleted(nationRecno) )
		{
			Nation* nationPtr = nation_array[nationRecno];

			if( nationPtr->is_ai() && nationPtr->ai_capture_enemy_town_recno &&
				 ( town_array.is_deleted(nationPtr->ai_capture_enemy_town_recno) ||
					town_array[nationPtr->ai_capture_enemy_town_recno]->nation_recno == withNationRecno ) )
			{
				nationPtr->ai_capture_enemy_town_recno = 0;
				nationPtr->ai_capture_enemy_town_start_attack_date = 0;
			}
		}
	}
}
//-------- End of function AiWarState::status_changed --------//


//------- Begin of function AiWarState::unit_war_value -------//
//
// return : <float> how much the loss of the given unit matters in a war
//
float AiWarState::unit_war_value(Unit* unitPtr)
{
	UnitInfo* unitInfo = unit_res[unitPtr->unit_id];

	if( unitInfo->unit_class == UNIT_CLASS_WEAPON || unitInfo->unit_class == UNIT_CLASS_SHIP )
		return 3.0f;

	if( !unitPtr->race_id )
		return 1.0f;

	if( unitPtr->rank_id == RANK_KING )
		return 10.0f;

	if( unitPtr->rank_id == RANK_GENERAL )
		return 3.0f + unitPtr->skill.combat_level / 25.0f;

	if( unitPtr->is_civilian() )
		return 0.5f;

	return 1.0f + unitPtr->skill.combat_level / 25.0f;		// 1 to 5
}
//-------- End of function AiWarState::unit_war_value --------//


//------- Begin of function AiWarState::unit_killed -------//
//
// Called by Unit::hit_target() when targetUnit is killed.
//
void AiWarState::unit_killed(int attackerNationRecno, Unit* targetUnit)
{
	int victimNationRecno = targetUnit->nation_recno;

	WarRecord* victimRecord   = get_record(victimNationRecno, attackerNationRecno);
	WarRecord* attackerRecord = get_record(attackerNationRecno, victimNationRecno);

	if( !victimRecord || !attackerRecord )		// monsters and independent units are not recorded
		return;

	float warValue = unit_war_value(targetUnit);

	victimRecord->losses += warValue;
	victimRecord->last_combat_date = info.game_date;

	attackerRecord->kills += warValue;
	attackerRecord->last_combat_date = info.game_date;

	//--- losses weigh less on a dominant military, 0.3 for the strongest to 0.75 for the weakest ---//

	float lossMultiplier = LOSS_WEARINESS_MULTIPLIER;

	if( !nation_array.is_deleted(victimNationRecno) )
		lossMultiplier = lossMultiplier * (125 - nation_array[victimNationRecno]->military_rank_rating()*3/4) / 100;

	add_weariness( victimNationRecno, attackerNationRecno, warValue * lossMultiplier );
	add_weariness( attackerNationRecno, victimNationRecno, -warValue * KILL_WEARINESS_MULTIPLIER );
}
//-------- End of function AiWarState::unit_killed --------//


//------- Begin of function AiWarState::firm_destroyed -------//
//
void AiWarState::firm_destroyed(int attackerNationRecno, int victimNationRecno)
{
	WarRecord* victimRecord = get_record(victimNationRecno, attackerNationRecno);

	if( !victimRecord )
		return;

	victimRecord->firms_lost++;
	victimRecord->last_combat_date = info.game_date;

	add_weariness( victimNationRecno, attackerNationRecno, FIRM_LOST_WEARINESS );
}
//-------- End of function AiWarState::firm_destroyed --------//


//------- Begin of function AiWarState::town_captured -------//
//
// Called by Town::surrender() before the town changes nation.
//
void AiWarState::town_captured(int fromNationRecno, int toNationRecno)
{
	WarRecord* loserRecord  = get_record(fromNationRecno, toNationRecno);
	WarRecord* winnerRecord = get_record(toNationRecno, fromNationRecno);

	if( !loserRecord || !winnerRecord || !loserRecord->start_date )		// only count towns taken in a war
		return;

	loserRecord->towns_lost++;
	winnerRecord->towns_gained++;

	add_weariness( fromNationRecno, toNationRecno, TOWN_LOST_WEARINESS );
	add_weariness( toNationRecno, fromNationRecno, -TOWN_GAINED_WEARINESS );
}
//-------- End of function AiWarState::town_captured --------//


//------- Begin of function AiWarState::next_day -------//
//
void AiWarState::next_day()
{
	int nationCount = nation_array.size();

	for( int i=1 ; i<=nationCount ; i++ )
	{
		if( nation_array.is_deleted(i) )
			continue;

		Nation* nationPtr = nation_array[i];

		float dailyWeariness = DAILY_WAR_WEARINESS;

		if( nationPtr->is_ai() )
			dailyWeariness = dailyWeariness * (50 + nationPtr->pref_peacefulness) / 100;		// 50% to 150% of the base rate

		for( int j=1 ; j<=nationCount ; j++ )
		{
			if( i==j || nation_array.is_deleted(j) )
				continue;

			WarRecord* warRecord = get_record(i, j);

			if( !warRecord )
				continue;

			if( nationPtr->get_relation_status(j) == NATION_HOSTILE )
			{
				if( !warRecord->start_date )		// e.g. a war already going on when a game saved by an older version was loaded
				{
					warRecord->start_date       = info.game_date;
					warRecord->last_combat_date = info.game_date;
				}

				float addWeariness = dailyWeariness;

				if( info.game_date > warRecord->last_combat_date + IDLE_WAR_DAYS )
					addWeariness += IDLE_WAR_WEARINESS;

				add_weariness( i, j, addWeariness );
			}
			else
			{
				add_weariness( i, j, -DAILY_PEACE_RECOVERY );
			}
		}

		//------ yearly summary for the event log ------//

		if( info.game_date%365 == i%365 )
		{
			int largestTownPop = 0;

			if( nationPtr->largest_town_recno && !town_array.is_deleted(nationPtr->largest_town_recno) )
				largestTownPop = town_array[nationPtr->largest_town_recno]->population;

			log( "SUMMARY     nation %d: towns %d, population %d (largest town %d), cash %.0f, yearly profit %.0f, mines %d, military rank %d",
				  i, nationPtr->ai_town_count, nationPtr->total_population, largestTownPop,
				  nationPtr->cash, nationPtr->profit_365days(), nationPtr->ai_mine_count, nationPtr->military_rank_rating() );
		}

		//------ update the posture of AI nations once a month ------//

		if( nationPtr->is_ai() && info.game_date%30 == i%30 )
			update_posture(i);

		//------ review the opening strategy once a year ------//

		if( nationPtr->is_ai() && config_adv.nation_ai_opening_strategy &&
			 ( !strategy_date[i-1] || info.game_date >= strategy_date[i-1] + STRATEGY_REVIEW_DAYS ) )
		{
			update_strategy(i);
		}
	}
}
//-------- End of function AiWarState::next_day --------//


//------- Begin of function AiWarState::recently_at_war -------//
//
// return : <int> 1 if the nation has ended a war within the minimum truce period
//
int AiWarState::recently_at_war(int nationRecno)
{
	for( int i=1 ; i<=MAX_NATION ; i++ )
	{
		WarRecord* warRecord = get_record(nationRecno, i);

		if( warRecord && !warRecord->start_date &&
			 warRecord->no_war_until_date > info.game_date + MIN_TRUCE_DAYS/2 )
		{
			return 1;
		}
	}

	return 0;
}
//-------- End of function AiWarState::recently_at_war --------//


//------- Begin of function AiWarState::update_posture -------//
//
void AiWarState::update_posture(int nationRecno)
{
	if( !config_adv.nation_ai_posture )
		return;

	Nation* nationPtr = nation_array[nationRecno];

	//------ gather the nation's situation ------//

	float maxWeariness = 0.0f;
	int   hasEnemy = 0;

	for( int i=nation_array.size() ; i>0 ; i-- )
	{
		if( i==nationRecno || nation_array.is_deleted(i) )
			continue;

		if( nationPtr->get_relation_status(i) != NATION_HOSTILE )
			continue;

		hasEnemy = 1;

		if( weariness(nationRecno, i) > maxWeariness )
			maxWeariness = weariness(nationRecno, i);
	}

	float monthlyExpense = nationPtr->expense_365days() / 12;
	int   isBroke    = nationPtr->cash < monthlyExpense && nationPtr->profit_365days() < 0;
	int   isStarving = nationPtr->food <= 0;
	int   enemyMilitary = nationPtr->total_enemy_military();
	int   ownMilitary   = nationPtr->military_rank_rating();

	int curPosture  = posture[nationRecno-1];
	int daysInPosture = info.game_date - posture_date[nationRecno-1];
	int newPosture;

	//------ emergency postures take effect immediately ------//

	if( isBroke || isStarving ||
		 ( config_adv.nation_ai_war_weariness && maxWeariness >= WEARINESS_FORCE_PEACE ) )
	{
		newPosture = AI_POSTURE_RECOVER;
	}
	else if( hasEnemy && enemyMilitary >= 30 && enemyMilitary > ownMilitary*13/10 + 10 )
	{
		newPosture = AI_POSTURE_DEFEND;
	}

	//------ non-emergency postures ------//

	else if( daysInPosture < POSTURE_MIN_DAYS )
	{
		newPosture = curPosture;		// a posture is kept for a while before it is changed
	}
	else if( curPosture == AI_POSTURE_CONSOLIDATE && !hasEnemy && daysInPosture < CONSOLIDATE_MIN_DAYS )
	{
		newPosture = curPosture;
	}
	else if( !hasEnemy && curPosture != AI_POSTURE_CONSOLIDATE &&
				( ( nationPtr->profit_365days() < 0 &&													// the economy is in trouble
					 nationPtr->cash < 1000 + 2000 * nationPtr->pref_cash_reserve / 100 ) ||
				  recently_at_war(nationRecno) ) )														// rebuild after a war
	{
		newPosture = AI_POSTURE_CONSOLIDATE;
	}
	else
	{
		newPosture = AI_POSTURE_EXPAND;
	}

	if( newPosture != curPosture )
	{
		static const char* postureName[AI_POSTURE_COUNT] = { "EXPAND", "CONSOLIDATE", "DEFEND", "RECOVER" };

		log( "POSTURE     nation %d: %s -> %s (cash %.0f, food %.0f, own mil %d, enemy mil %d, max weariness %.1f)",
			  nationRecno, postureName[curPosture], postureName[newPosture],
			  nationPtr->cash, nationPtr->food, ownMilitary, enemyMilitary, maxWeariness );

		posture[nationRecno-1] = newPosture;
		posture_date[nationRecno-1] = info.game_date;
	}
}
//-------- End of function AiWarState::update_posture --------//


//------- Begin of function AiWarState::rate_strategies -------//
//
// Rate each opening strategy from the nation's surroundings and personality.
//
// <int*> ratingArray - array of AI_STRATEGY_COUNT ratings to be filled
//
void AiWarState::rate_strategies(int nationRecno, int* ratingArray)
{
	Nation* nationPtr = nation_array[nationRecno];

	memset( ratingArray, 0, sizeof(int) * AI_STRATEGY_COUNT );

	if( !nationPtr->largest_town_recno || town_array.is_deleted(nationPtr->largest_town_recno) )
		return;

	Town* baseTown   = town_array[nationPtr->largest_town_recno];
	int   baseRegion = baseTown->region_id;
	int   i, j;

	//------- capturable villages near our towns -------//

	int villageRating = 0;

	for( i=town_array.size() ; i>0 ; i-- )
	{
		if( town_array.is_deleted(i) )
			continue;

		Town* townPtr = town_array[i];

		if( townPtr->nation_recno || townPtr->rebel_recno || townPtr->region_id != baseRegion )
			continue;

		int minDistance = 0x7FFF;

		for( j=0 ; j<nationPtr->ai_town_count ; j++ )
		{
			Town* ownTown = town_array[ nationPtr->ai_town_array[j] ];
			int townDistance = misc.points_distance( townPtr->center_x, townPtr->center_y, ownTown->center_x, ownTown->center_y );
			minDistance = MIN( minDistance, townDistance );
		}

		if( minDistance > STRATEGY_SCAN_DISTANCE )
			continue;

		villageRating += minDistance <= STRATEGY_SCAN_DISTANCE/2 ? 10 : 5;

		if( townPtr->majority_race() == nationPtr->race_id )		// villages of our race are easier to win over
			villageRating += 5;
	}

	villageRating = MIN( villageRating, 60 );

	//------- untapped raw material sites near the capital -------//

	int rawRating = 0;

	for( i=site_array.size() ; i>0 ; i-- )
	{
		if( site_array.is_deleted(i) )
			continue;

		Site* sitePtr = site_array[i];

		if( sitePtr->site_type != SITE_RAW || sitePtr->has_mine || sitePtr->region_id != baseRegion )
			continue;

		if( misc.points_distance( sitePtr->map_x_loc, sitePtr->map_y_loc, baseTown->center_x, baseTown->center_y ) <= STRATEGY_SCAN_DISTANCE )
			rawRating += 15;
	}

	rawRating = MIN( rawRating, 45 );

	//------- the nearest rival kingdom reachable by land -------//

	int rivalDistance = 0x7FFF;

	for( i=town_array.size() ; i>0 ; i-- )
	{
		if( town_array.is_deleted(i) )
			continue;

		Town* townPtr = town_array[i];

		if( !townPtr->nation_recno || townPtr->nation_recno == nationRecno || townPtr->region_id != baseRegion )
			continue;

		int townDistance = misc.points_distance( townPtr->center_x, townPtr->center_y, baseTown->center_x, baseTown->center_y );
		rivalDistance = MIN( rivalDistance, townDistance );
	}

	int rivalRating = rivalDistance < 70 ? 70 - rivalDistance : 0;		// the closer the rival, the higher

	//------- a small home island with little to take -------//

	int isSmallIsland = region_array[baseRegion]->region_size < 2500 &&
							  villageRating < 10 && rawRating == 0;

	//------- rate the strategies -------//

	ratingArray[AI_STRATEGY_ECONOMY] = rawRating + nationPtr->pref_economic_development/3;
	ratingArray[AI_STRATEGY_EXPAND]  = villageRating + nationPtr->pref_inc_pop_by_capture/3;

	if( rivalRating )
	{
		ratingArray[AI_STRATEGY_MILITARY] = rivalRating + nationPtr->pref_military_development/3 +
														(100-nationPtr->pref_peacefulness)/5;
	}

	ratingArray[AI_STRATEGY_TECH] = (rivalRating ? 0 : 15) + nationPtr->pref_use_weapon/3;

	if( isSmallIsland )
		ratingArray[AI_STRATEGY_NAVAL] = 60 + nationPtr->pref_use_marine/3;
}
//-------- End of function AiWarState::rate_strategies --------//


//------- Begin of function AiWarState::update_strategy -------//
//
void AiWarState::update_strategy(int nationRecno)
{
	int ratingArray[AI_STRATEGY_COUNT];

	rate_strategies(nationRecno, ratingArray);

	int curStrategy  = strategy[nationRecno-1];
	int bestStrategy = curStrategy;
	int isFirstTime  = !strategy_date[nationRecno-1];

	for( int i=0 ; i<AI_STRATEGY_COUNT ; i++ )
	{
		int requiredRating = ratingArray[bestStrategy];

		if( !isFirstTime && bestStrategy == curStrategy )
			requiredRating += STRATEGY_SWITCH_MARGIN;		// keep the current strategy unless another is clearly better

		if( ratingArray[i] > requiredRating )
			bestStrategy = i;
	}

	strategy_date[nationRecno-1] = info.game_date;

	if( bestStrategy != curStrategy || isFirstTime )
	{
		static const char* strategyName[AI_STRATEGY_COUNT] = { "ECONOMY", "EXPAND", "MILITARY", "TECH", "NAVAL" };

		log( "STRATEGY    nation %d: %s -> %s (economy %d, expand %d, military %d, tech %d, naval %d)",
			  nationRecno, strategyName[curStrategy], strategyName[bestStrategy],
			  ratingArray[0], ratingArray[1], ratingArray[2], ratingArray[3], ratingArray[4] );

		strategy[nationRecno-1] = bestStrategy;
	}
}
//-------- End of function AiWarState::update_strategy --------//


//------- Begin of function AiWarState::can_start_offensive -------//
//
// Whether an AI nation should start a new offensive against a nation
// it is already at war with. Defending is never restricted.
//
int AiWarState::can_start_offensive(int nationRecno, int targetNationRecno)
{
	if( !get_record(nationRecno, targetNationRecno) ||
		 nation_array.is_deleted(nationRecno) || nation_array.is_deleted(targetNationRecno) )
	{
		return 1;		// not a nation versus nation offensive
	}

	if( config_adv.nation_ai_war_weariness &&
		 weariness(nationRecno, targetNationRecno) >= WEARINESS_BLOCK_OFFENSIVE )
	{
		return 0;
	}

	int curPosture = get_posture(nationRecno);

	if( curPosture == AI_POSTURE_DEFEND || curPosture == AI_POSTURE_RECOVER )
		return 0;

	return 1;
}
//-------- End of function AiWarState::can_start_offensive --------//


//------- Begin of function AiWarState::can_declare_war -------//
//
// Whether an AI nation should start a new war with a nation it is not at war with.
//
int AiWarState::can_declare_war(int nationRecno, int targetNationRecno)
{
	WarRecord* warRecord = get_record(nationRecno, targetNationRecno);

	if( !warRecord )
		return 1;

	if( config_adv.nation_ai_war_weariness )
	{
		if( info.game_date < warRecord->no_war_until_date )
			return 0;

		if( warRecord->weariness >= MAX_WAR_WEARINESS/2 )		// still tired from the last war
			return 0;
	}

	if( get_posture(nationRecno) != AI_POSTURE_EXPAND )
		return 0;

	return 1;
}
//-------- End of function AiWarState::can_declare_war --------//


//------- Begin of function AiWarState::cease_war_rating -------//
//
// return : <int> the adjustment to Nation::consider_cease_war()'s rating
//
int AiWarState::cease_war_rating(int nationRecno, int withNationRecno)
{
	if( !config_adv.nation_ai_war_weariness )
		return 0;

	int rating = (int) weariness(nationRecno, withNationRecno) - 35;

	if( get_posture(nationRecno) == AI_POSTURE_RECOVER )
		rating += 20;

	return rating;
}
//-------- End of function AiWarState::cease_war_rating --------//


//------- Begin of function AiWarState::write_file -------//
//
int AiWarState::write_file(File* filePtr)
{
	if( !filePtr->file_write( war_record, sizeof(war_record) ) )
		return 0;

	if( !filePtr->file_write( posture, sizeof(posture) ) )
		return 0;

	if( !filePtr->file_write( posture_date, sizeof(posture_date) ) )
		return 0;

	if( !filePtr->file_write( strategy, sizeof(strategy) ) )
		return 0;

	if( !filePtr->file_write( strategy_date, sizeof(strategy_date) ) )
		return 0;

	return 1;
}
//-------- End of function AiWarState::write_file --------//


//------- Begin of function AiWarState::read_file -------//
//
// <int> hasStrategy - whether the saved game has the strategy data (save version 214 and later)
//
int AiWarState::read_file(File* filePtr, int hasStrategy)
{
	if( !filePtr->file_read( war_record, sizeof(war_record) ) )
		return 0;

	if( !filePtr->file_read( posture, sizeof(posture) ) )
		return 0;

	if( !filePtr->file_read( posture_date, sizeof(posture_date) ) )
		return 0;

	if( hasStrategy )
	{
		if( !filePtr->file_read( strategy, sizeof(strategy) ) )
			return 0;

		if( !filePtr->file_read( strategy_date, sizeof(strategy_date) ) )
			return 0;
	}
	else
	{
		memset( strategy, AI_STRATEGY_ECONOMY, sizeof(strategy) );
		memset( strategy_date, 0, sizeof(strategy_date) );		// evaluated again as the game goes on
	}

	//------ validate the values read -------//

	for( int i=0 ; i<MAX_NATION ; i++ )
	{
		if( posture[i] < 0 || posture[i] >= AI_POSTURE_COUNT )
			posture[i] = AI_POSTURE_EXPAND;

		if( strategy[i] < 0 || strategy[i] >= AI_STRATEGY_COUNT )
			strategy[i] = AI_STRATEGY_ECONOMY;

		for( int j=0 ; j<MAX_NATION ; j++ )
		{
			WarRecord* warRecord = &war_record[i][j];

			if( !(warRecord->weariness >= 0.0f && warRecord->weariness <= (float) MAX_WAR_WEARINESS) )		// also catches NaN
				warRecord->weariness = 0.0f;
		}
	}

	return 1;
}
//-------- End of function AiWarState::read_file --------//
