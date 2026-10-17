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

//Filename    : OCIVIL.CPP
//Description : The game mechanics of the civil technologies
//
// The civil technologies are researched in the Tower of Science (see
// CIVIL_??? in OTECHRES.H). Each one brings a new mechanic:
//
// Agriculture    - In the farming season, some of the peasants of a village
//                  walk out to fields next to it and work them, moving from
//                  one part of the fields to another: a farmer grows
//                  FARMER_FOOD_YEAR_PRODUCTION food a year, eight times as
//                  much as a peasant in the village, which the village
//                  brings in as its harvest at the end of the season. The
//                  fields are drawn while they are farmed. Farmers are
//                  people on the map, enemies can kill them; they are as
//                  loyal as their village and never betray. They go home at
//                  the end of the season and when the village is attacked.
//                  Level II: more farmers and a longer season. The player
//                  can stop the farming of a village with its Fields button.
// Medicine       - A soldier or general falling inside its own kingdom's
//                  territory may be carried back to the nearest fort instead
//                  of dying (1 in 4, 1 in 2 at level II). Soldiers in forts
//                  and units in the field heal faster.
// Engineering    - A village can build a watchtower, which shoots arrows at
//                  enemies near the village every day. Level II: farther,
//                  more and stronger arrows.
// Industry       - A mine that runs dry digs deeper for a new vein, once a
//                  level.
// Administration - A village can be given an edict for a year: a Festival
//                  (costs cash, more loyal villagers), a Labour Levy (double
//                  food from its peasants, less loyal villagers) or a Militia
//                  (more and stronger defenders, slightly less loyal
//                  villagers). Level II: stronger edicts.
// Commerce       - Foreign caravans on the kingdom's roads in its territory
//                  pay tolls (not allies, nor enemies), and its treasury
//                  earns interest.
// Military Drill - Forts with a commander recruit from their linked villages
//                  by themselves (each fort has an Auto-recruit switch).
//                  Level II: soldiers train faster.
// Navigation     - Harbors catch fish (food), and ships sail faster.
//
// The AI uses them all (see CivilTech::ai_think()), and what they do is
// logged when SKAI_LOG is set (see AiWarState::log()).

#include <ALL.h>
#include <OCIVIL.h>
#include <OTECHRES.h>
#include <ONATION.h>
#include <OTOWN.h>
#include <OUNIT.h>
#include <OUNITRES.h>
#include <OFIRM.h>
#include <OF_MINE.h>
#include <OF_CAMP.h>
#include <OSITE.h>
#include <OWORLD.h>
#include <OIMGRES.h>
#include <OINFO.h>
#include <ONEWS.h>
#include <OEFFECT.h>
#include <OAIWAR.h>
#include <OMISC.h>
#include <OSYS.h>
#include <OROAD.h>
#include <vector>
#include "gettext.h"

//------- Define constants -------//

#define FARMER_FOOD_YEAR_PRODUCTION		240		// a peasant in the village produces PEASANT_FOOD_YEAR_PRODUCTION (30)
#define FARM_FIELD_CANDIDATE_COUNT		10			// places for 2x2 fields around a village
#define FARM_FIELD_RELEASE_DISTANCE		12			// a farmer ordered farther than this from its village is no longer a farmer
#define FARMER_ATTACK_RECALL_DAYS		15			// farmers stay home this long after the village was attacked
#define FARMER_WORK_MOVE_DAYS				3			// a farmer moves to another part of the fields this often
#define FARMER_RECALL_SLACK				2			// farmers are called home when there are more than this many too many
#define FARMER_LOST_DISTANCE				50			// a farmer this far from its village is lost and no longer a farmer

#define MEDIC_SAVE_PERCENT_PER_LEVEL	25

#define WATCHTOWER_RANGE					6			// locations from the village at level I, +2 a level
#define WATCHTOWER_DAMAGE					15			// hit points per arrow at level I, +10 a level
#define WATCHTOWER_ARROWS					2			// arrows a day at level I, +1 a level

#define DEEP_VEIN_QTY						10000

#define TOLL_PER_DAY							3			// per caravan on a road and level
#define INTEREST_YEAR_PERCENT				2			// per level
#define MAX_MONTHLY_INTEREST				250		// per level

#define AUTO_RECRUIT_INTERVAL_DAYS		5
#define TRAIN_SPEED_PERCENT_PER_LEVEL	25

#define FISH_YEAR_FOOD						300		// per harbor and level

#define SHIP_EXTRA_STEP_FRAMES_LEVEL_1	7			// ships of a nation with Navigation I make an extra step every 7th frame
#define SHIP_EXTRA_STEP_FRAMES_LEVEL_2	4

//------- Define global variables -------//

CivilTech civil_tech;

//------- Define static variables -------//

static float toll_paid_array[MAX_NATION];		// tolls received in the current month, for the log
static int	 medic_saved_array[MAX_NATION];		// soldiers carried back in the current month, for the log
static int	 work_move_array[MAX_NATION];			// farmers that moved to another part of the fields in the current month, for the log
static int	 work_stuck_array[MAX_NATION];		// farmers that found no free spot to move to

//------- Define static functions -------//

static int  field_loc(Town* townPtr, int fieldId, int& xLoc, int& yLoc);
static int  valid_field_count(Town* townPtr);
static int  active_field_count(Town* townPtr);
static int  find_work_loc(Town* townPtr, Unit* unitPtr, int& xLoc, int& yLoc);
static int  watchtower_effect_id();
static char* field_bitmap(int growthStage);


//--------- Begin of function CivilTech::reset ---------//
//
// Called when a game starts or is loaded.
//
void CivilTech::reset()
{
	medic_count = 0;

	memset( toll_paid_array, 0, sizeof(toll_paid_array) );
	memset( medic_saved_array, 0, sizeof(medic_saved_array) );
	memset( work_move_array, 0, sizeof(work_move_array) );
	memset( work_stuck_array, 0, sizeof(work_stuck_array) );
}
//----------- End of function CivilTech::reset -----------//


//--------- Begin of function CivilTech::next_day ---------//
//
// Called by Info::next_day().
//
void CivilTech::next_day()
{
	process_farming();
	process_watchtowers();
	process_edicts();
	process_tolls();
	process_fishing();

	//------ monthly log of tolls and medics ------//

	static int lastLogMonth = 0;

	if( info.game_month != lastLogMonth )
	{
		lastLogMonth = info.game_month;

		for( int i=0 ; i<MAX_NATION ; i++ )
		{
			if( toll_paid_array[i] > 0 || medic_saved_array[i] > 0 || work_move_array[i] > 0 || work_stuck_array[i] > 0 )
			{
				AiWarState::log( "CIVIL       nation %d last month: tolls received %.0f, soldiers carried back by medics %d, farmer work moves %d (no free spot %d)",
									  i+1, toll_paid_array[i], medic_saved_array[i], work_move_array[i], work_stuck_array[i] );
			}
		}

		memset( toll_paid_array, 0, sizeof(toll_paid_array) );
		memset( medic_saved_array, 0, sizeof(medic_saved_array) );
		memset( work_move_array, 0, sizeof(work_move_array) );
		memset( work_stuck_array, 0, sizeof(work_stuck_array) );
	}

	//------ the AI uses the civil technologies once a month ------//

	for( int i=nation_array.size() ; i>0 ; i-- )
	{
		if( nation_array.is_deleted(i) )
			continue;

		Nation* nationPtr = nation_array[i];

		if( nationPtr->is_ai() && info.game_date%30 == (i*7)%30 )
			ai_think(nationPtr);
	}
}
//----------- End of function CivilTech::next_day -----------//


//======================== Agriculture ========================//


//--------- Begin of function CivilTech::is_farm_season ---------//
//
// return : <int> 1 if it's the farming season for the given nation:
//			  March to September with Agriculture I, February to October with II
//
int CivilTech::is_farm_season(int nationRecno)
{
	int level = tech_res.civil_level(nationRecno, CIVIL_AGRICULTURE);

	if( !level )
		return 0;

	if( level >= 2 )
		return info.game_month >= 2 && info.game_month <= 10;
	else
		return info.game_month >= 3 && info.game_month <= 9;
}
//----------- End of function CivilTech::is_farm_season -----------//


//--------- Begin of function CivilTech::wanted_farmer_count ---------//
//
// return : <int> the no. of the village's peasants who should be farming now
//
int CivilTech::wanted_farmer_count(Town* townPtr)
{
	if( farming_block_reason(townPtr) )
		return 0;

	int level = tech_res.civil_level(townPtr->nation_recno, CIVIL_AGRICULTURE);

	int peasantCount = townPtr->farmer_count;		// the farmers out are peasants of the village too

	for( int raceId=1 ; raceId<=MAX_RACE ; raceId++ )
		peasantCount += townPtr->recruitable_race_pop(raceId, 0);		// 0-not spies

	int wantedCount = peasantCount * (level>=2 ? 25 : 15) / 100;

	return MIN( wantedCount, level>=2 ? 10 : 6 );
}
//----------- End of function CivilTech::wanted_farmer_count -----------//


//--------- Begin of function CivilTech::farming_block_reason ---------//
//
// return : <int> why the village has no farmers now, FARM_BLOCK_???,
//			  0 if it may have farmers
//
int CivilTech::farming_block_reason(Town* townPtr)
{
	if( !townPtr->nation_recno || townPtr->rebel_recno || !tech_res.civil_level(townPtr->nation_recno, CIVIL_AGRICULTURE) )
		return FARM_BLOCK_NO_AGRICULTURE;

	if( townPtr->farming_off )
		return FARM_BLOCK_OFF;

	if( !is_farm_season(townPtr->nation_recno) )
		return FARM_BLOCK_SEASON;

	//--- farmers stay home while the village is in danger ---//

	if( townPtr->town_defender_count > 0 ||
		 info.game_date < townPtr->last_being_attacked_date + FARMER_ATTACK_RECALL_DAYS )
	{
		return FARM_BLOCK_DANGER;
	}

	if( !valid_field_count(townPtr) )		// no room for fields next to the village
		return FARM_BLOCK_NO_ROOM;

	return 0;
}
//----------- End of function CivilTech::farming_block_reason -----------//


//--------- Begin of function CivilTech::process_farming ---------//
//
// Count the farmers of each village, let them farm and produce food, and
// send peasants out to farm or call farmers home as needed.
//
void CivilTech::process_farming()
{
	int i;
	int townCount = town_array.size();

	std::vector<short> workingCountArray(townCount+1, 0);		// farmers in the fields
	std::vector<short> returningCountArray(townCount+1, 0);	// farmers walking home

	//------- the farmers out on the map -------//

	for( i=unit_array.size() ; i>0 ; i-- )
	{
		if( unit_array.is_deleted(i) )
			continue;

		Unit* unitPtr = unit_array[i];

		if( !unitPtr->farm_town_recno )
			continue;

		int townRecno = unitPtr->farm_town_recno;

		if( townRecno > townCount || town_array.is_deleted(townRecno) )
		{
			release_farmer(unitPtr, "its village is gone");
			continue;
		}

		Town* townPtr = town_array[townRecno];

		if( townPtr->nation_recno != unitPtr->nation_recno )
		{
			release_farmer(unitPtr, "its village has changed hands");
			continue;
		}

		if( !unitPtr->is_visible() || unitPtr->unit_mode != 0 || unitPtr->hit_points <= 0 || unitPtr->spy_recno )
		{
			release_farmer(unitPtr, "it is no longer on the map");
			continue;
		}

		//---- the player may have given it another order ----//

		int isReturning = 0;

		if( unitPtr->action_mode2 == ACTION_ASSIGN_TO_TOWN )
		{
			Location* locPtr = world.get_loc(unitPtr->action_x_loc2, unitPtr->action_y_loc2);

			if( locPtr->town_recno() != townRecno )
			{
				release_farmer(unitPtr, "it has been sent to another village");
				continue;
			}

			isReturning = 1;
		}
		else if( unitPtr->action_mode2 == ACTION_MOVE )
		{
			if( misc.rects_distance( unitPtr->action_x_loc2, unitPtr->action_y_loc2, unitPtr->action_x_loc2, unitPtr->action_y_loc2,
											 townPtr->loc_x1, townPtr->loc_y1, townPtr->loc_x2, townPtr->loc_y2 ) > FARM_FIELD_RELEASE_DISTANCE )
			{
				release_farmer(unitPtr, "it has been ordered away");
				continue;
			}
		}
		else if( unitPtr->action_mode2 != ACTION_STOP )
		{
			char reasonStr[80];
			snprintf( reasonStr, sizeof(reasonStr), "it has been given another order (action %d/%d)", unitPtr->action_mode, unitPtr->action_mode2 );
			release_farmer(unitPtr, reasonStr);
			continue;
		}

		//--- a farmer that strayed far from its village while walking around something goes home, or is lost ---//

		int townDistance = misc.rects_distance( unitPtr->next_x_loc(), unitPtr->next_y_loc(), unitPtr->next_x_loc(), unitPtr->next_y_loc(),
															 townPtr->loc_x1, townPtr->loc_y1, townPtr->loc_x2, townPtr->loc_y2 );

		if( townDistance > FARMER_LOST_DISTANCE )
		{
			char reasonStr[120];
			snprintf( reasonStr, sizeof(reasonStr), "it is lost far from its village (at %d,%d, village %d,%d, action %d/%d to %d,%d)",
						 unitPtr->next_x_loc(), unitPtr->next_y_loc(), townPtr->loc_x1, townPtr->loc_y1,
						 unitPtr->action_mode, unitPtr->action_mode2, unitPtr->action_x_loc2, unitPtr->action_y_loc2 );
			release_farmer(unitPtr, reasonStr);
			continue;
		}

		if( townDistance > FARM_FIELD_RELEASE_DISTANCE + 8 && !isReturning )
		{
			AiWarState::log( "FARM        nation %d: farmer %d of town %d strayed to %d,%d, it goes home",
								  unitPtr->nation_recno, unitPtr->sprite_recno, townRecno, unitPtr->next_x_loc(), unitPtr->next_y_loc() );

			unitPtr->assign(townPtr->loc_x1, townPtr->loc_y1);
			isReturning = 1;
		}

		//--- as loyal as the people of its village, see Unit::update_loyalty() ---//

		if( unitPtr->race_id )
			unitPtr->loyalty = unitPtr->target_loyalty = (int) townPtr->race_loyalty_array[unitPtr->race_id-1];

		//------ farm -------//

		if( isReturning )
		{
			returningCountArray[townRecno]++;
			continue;
		}

		workingCountArray[townRecno]++;

		townPtr->harvest_food += (float) FARMER_FOOD_YEAR_PRODUCTION * edict_food_multiplier(townPtr) / 365;		// brought in at the end of the season

		//--- work on another part of the fields every few days (a farmer that stopped next to a busy spot is still on ACTION_MOVE) ---//

		if( (unitPtr->action_mode2 == ACTION_STOP || unitPtr->action_mode2 == ACTION_MOVE) && unitPtr->cur_action == SPRITE_IDLE &&
			 info.game_date % FARMER_WORK_MOVE_DAYS == i % FARMER_WORK_MOVE_DAYS )
		{
			int xLoc, yLoc;

			if( find_work_loc(townPtr, unitPtr, xLoc, yLoc) )
			{
				unitPtr->move_to(xLoc, yLoc);
				work_move_array[unitPtr->nation_recno-1]++;
			}
			else
				work_stuck_array[unitPtr->nation_recno-1]++;
		}
	}

	//------- the villages -------//

	float harvestFoodArray[MAX_NATION];		// the harvests brought in today, by nation
	float bestHarvestArray[MAX_NATION];
	short harvestTownCountArray[MAX_NATION];
	short bestHarvestTownArray[MAX_NATION];

	memset( harvestFoodArray, 0, sizeof(harvestFoodArray) );
	memset( bestHarvestArray, 0, sizeof(bestHarvestArray) );
	memset( harvestTownCountArray, 0, sizeof(harvestTownCountArray) );
	memset( bestHarvestTownArray, 0, sizeof(bestHarvestTownArray) );

	for( i=townCount ; i>0 ; i-- )
	{
		if( town_array.is_deleted(i) )
			continue;

		Town* townPtr = town_array[i];

		townPtr->farmer_count = workingCountArray[i] + returningCountArray[i];

		//------ the end of the season: the harvest is brought in ------//

		if( townPtr->harvest_food > 0 && townPtr->nation_recno && !is_farm_season(townPtr->nation_recno) )
		{
			int n = townPtr->nation_recno - 1;

			harvestFoodArray[n] += townPtr->harvest_food;
			harvestTownCountArray[n]++;

			if( townPtr->harvest_food > bestHarvestArray[n] )
			{
				bestHarvestArray[n] = townPtr->harvest_food;
				bestHarvestTownArray[n] = i;
			}

			AiWarState::log( "FARM        nation %d: town %d brings in its harvest: %.0f food", townPtr->nation_recno, i, townPtr->harvest_food );

			townPtr->harvest_food = 0;
		}

		if( !townPtr->nation_recno || ( !townPtr->farmer_count && !tech_res.civil_level(townPtr->nation_recno, CIVIL_AGRICULTURE) ) )
			continue;

		int wantedCount  = wanted_farmer_count(townPtr);
		int workingCount = workingCountArray[i];

		if( workingCount < wantedCount )
		{
			if( info.game_date%2 == i%2 )		// one more farmer every other day
				send_farmer(townPtr);
		}
		else if( workingCount > wantedCount &&
					( wantedCount==0 || workingCount > wantedCount + FARMER_RECALL_SLACK ) )		// not for every small change in the no. of peasants
		{
			recall_farmers(townPtr, workingCount - wantedCount);
		}
	}

	//------- the harvests -------//

	for( i=0 ; i<MAX_NATION ; i++ )
	{
		if( !harvestTownCountArray[i] || nation_array.is_deleted(i+1) )
			continue;

		nation_array[i+1]->add_food( harvestFoodArray[i] );

		AiWarState::log( "FARM        nation %d: the harvest is in: %.0f food from %d villages (food now %.0f)",
							  i+1, harvestFoodArray[i], harvestTownCountArray[i], nation_array[i+1]->food );

		if( i+1 == nation_array.player_recno )
			news_array.harvest_in( bestHarvestTownArray[i], (int) harvestFoodArray[i], harvestTownCountArray[i] );
	}
}
//----------- End of function CivilTech::process_farming -----------//


//--------- Begin of function CivilTech::send_farmer ---------//
//
// A peasant of the village goes out to farm the fields.
//
void CivilTech::send_farmer(Town* townPtr)
{
	if( townPtr->population <= 2 )
		return;

	//---- a peasant of the race with the most peasants ----//

	int raceId, bestRaceId=0, bestCount=0;

	for( raceId=1 ; raceId<=MAX_RACE ; raceId++ )
	{
		int peasantCount = townPtr->recruitable_race_pop(raceId, 0);		// 0-not spies

		if( peasantCount > bestCount )
		{
			bestCount  = peasantCount;
			bestRaceId = raceId;
		}
	}

	if( !bestRaceId )
		return;

	int xLoc, yLoc;

	if( !find_field_loc(townPtr, xLoc, yLoc) )
		return;

	int unitRecno = townPtr->mobilize_town_people(bestRaceId, 1, 0);		// 1-decrease the population, 0-not spies

	if( !unitRecno )
		return;

	Unit* unitPtr = unit_array[unitRecno];

	unitPtr->farm_town_recno = townPtr->town_recno;
	unitPtr->move_to(xLoc, yLoc);

	townPtr->farmer_count++;

	AiWarState::log( "FARM        nation %d: a peasant of town %d goes out to farm at %d,%d (unit %d, %d farmers, %d wanted, population %d)",
						  townPtr->nation_recno, townPtr->town_recno, xLoc, yLoc, unitRecno,
						  townPtr->farmer_count, wanted_farmer_count(townPtr), townPtr->population );
}
//----------- End of function CivilTech::send_farmer -----------//


//--------- Begin of function CivilTech::recall_farmers ---------//
//
// Call farmers of the village home. If the village has no room for them,
// they settle in the nearest own village with room, or stay out on the
// map as ordinary units.
//
void CivilTech::recall_farmers(Town* townPtr, int recallCount)
{
	int returningCount = 0;

	for( int i=unit_array.size() ; i>0 && recallCount>0 ; i-- )
	{
		if( unit_array.is_deleted(i) )
			continue;

		Unit* unitPtr = unit_array[i];

		if( unitPtr->farm_town_recno != townPtr->town_recno || unitPtr->action_mode2 == ACTION_ASSIGN_TO_TOWN )
			continue;

		recallCount--;

		//---- the village has room ----//

		if( townPtr->population + returningCount < MAX_TOWN_POPULATION )
		{
			unitPtr->assign(townPtr->loc_x1, townPtr->loc_y1);
			returningCount++;
			continue;
		}

		//---- the nearest own village with room ----//

		Town* bestTown = NULL;
		int   minDistance = 0x7FFF;

		for( int j=town_array.size() ; j>0 ; j-- )
		{
			if( town_array.is_deleted(j) )
				continue;

			Town* otherTown = town_array[j];

			if( otherTown->nation_recno != unitPtr->nation_recno || otherTown->region_id != townPtr->region_id ||
				 otherTown->population + otherTown->farmer_count >= MAX_TOWN_POPULATION )
			{
				continue;
			}

			int curDistance = misc.points_distance( otherTown->center_x, otherTown->center_y, townPtr->center_x, townPtr->center_y );

			if( curDistance < minDistance )
			{
				minDistance = curDistance;
				bestTown = otherTown;
			}
		}

		if( bestTown )
		{
			release_farmer( unitPtr, "its village is full, it settles in another village" );
			unitPtr->assign(bestTown->loc_x1, bestTown->loc_y1);
		}
		else
		{
			release_farmer( unitPtr, "its village is full and no other village has room" );
		}
	}

	if( returningCount )
	{
		AiWarState::log( "FARM        nation %d: %d farmers of town %d go home (%s, %d farmers out, %d wanted)",
							  townPtr->nation_recno, returningCount, townPtr->town_recno,
							  !is_farm_season(townPtr->nation_recno) ? "end of the season" :
							  townPtr->farming_off ? "farming stopped" :
							  info.game_date < townPtr->last_being_attacked_date + FARMER_ATTACK_RECALL_DAYS || townPtr->town_defender_count ? "the village is in danger" : "too many farmers",
							  townPtr->farmer_count, wanted_farmer_count(townPtr) );
	}
}
//----------- End of function CivilTech::recall_farmers -----------//


//--------- Begin of function CivilTech::release_farmer ---------//
//
// The unit is no longer a farmer.
//
void CivilTech::release_farmer(Unit* unitPtr, const char* reason)
{
	AiWarState::log( "FARM        nation %d: unit %d of town %d is no longer a farmer: %s",
						  unitPtr->nation_recno, unitPtr->sprite_recno, unitPtr->farm_town_recno, reason );

	unitPtr->farm_town_recno = 0;
}
//----------- End of function CivilTech::release_farmer -----------//


//--------- Begin of static function field_loc ---------//
//
// The village's fields are 2x2 locations right next to it, in its
// nation's territory (which reaches 2 locations beyond the village), on
// open land.
//
// <int> fieldId - 0 to FARM_FIELD_CANDIDATE_COUNT-1
// <int&> xLoc, yLoc - the top left location of the field
//
// return : <int> 1 if the field can be there
//
static int field_loc(Town* townPtr, int fieldId, int& xLoc, int& yLoc)
{
	//--- offsets of the top left location from the village's top left one, for a 4x4 village ---//

	static short offsetArray[FARM_FIELD_CANDIDATE_COUNT][2] =
	{
		{ -2, -2 }, { -2, 0 }, { -2, 2 },		// left side, with the top left corner
		{  4, -2 }, {  4, 0 }, {  4, 2 },		// right side, with the top right corner
		{  0, -2 }, {  2, -2 },					// top side
		{  0,  4 }, {  2,  4 },					// bottom side
	};

	xLoc = townPtr->loc_x1 + offsetArray[fieldId][0] * STD_TOWN_LOC_WIDTH / 4;
	yLoc = townPtr->loc_y1 + offsetArray[fieldId][1] * STD_TOWN_LOC_HEIGHT / 4;

	if( xLoc < 0 || yLoc < 0 || xLoc+1 >= MAX_WORLD_X_LOC || yLoc+1 >= MAX_WORLD_Y_LOC )
		return 0;

	for( int y=yLoc ; y<=yLoc+1 ; y++ )
	{
		for( int x=xLoc ; x<=xLoc+1 ; x++ )
		{
			Location* locPtr = world.get_loc(x, y);

			if( locPtr->region_id != townPtr->region_id || !locPtr->is_accessible(UNIT_LAND) ||
				 locPtr->is_firm() || locPtr->is_town() || locPtr->is_plant() || locPtr->has_site() || locPtr->has_road() ||
				 locPtr->power_nation_recno != townPtr->nation_recno )
			{
				return 0;
			}
		}
	}

	return 1;
}
//----------- End of static function field_loc -----------//


//--------- Begin of static function valid_field_count ---------//
//
static int valid_field_count(Town* townPtr)
{
	int count=0, xLoc, yLoc;

	for( int i=0 ; i<FARM_FIELD_CANDIDATE_COUNT ; i++ )
		count += field_loc(townPtr, i, xLoc, yLoc);

	return count;
}
//----------- End of static function valid_field_count -----------//


//--------- Begin of static function active_field_count ---------//
//
// return : <int> the no. of fields being farmed: one for every two farmers
//
static int active_field_count(Town* townPtr)
{
	if( !townPtr->farmer_count )
		return 0;

	return MIN( valid_field_count(townPtr), (townPtr->farmer_count+1)/2 );
}
//----------- End of static function active_field_count -----------//


//--------- Begin of static function find_work_loc ---------//
//
// Another location of the fields being farmed for a farmer to work on:
// in the field nearest to it, or now and then in another one.
//
static int find_work_loc(Town* townPtr, Unit* unitPtr, int& xLoc, int& yLoc)
{
	int fieldCount = active_field_count(townPtr);

	if( !fieldCount )
		return 0;

	//----- the field nearest to the farmer -----//

	int fieldNo = 0, minDistance = 0x7FFF, activeNo = 0;

	for( int i=0 ; i<FARM_FIELD_CANDIDATE_COUNT && activeNo<fieldCount ; i++ )
	{
		if( !field_loc(townPtr, i, xLoc, yLoc) )
			continue;

		int curDistance = misc.points_distance( xLoc, yLoc, unitPtr->next_x_loc(), unitPtr->next_y_loc() );

		if( curDistance < minDistance )
		{
			minDistance = curDistance;
			fieldNo = activeNo;
		}

		activeNo++;
	}

	if( fieldCount > 1 && misc.random(4)==0 )		// one time in four, another field
		fieldNo = misc.random(fieldCount);

	for( int i=0 ; i<FARM_FIELD_CANDIDATE_COUNT ; i++ )
	{
		if( !field_loc(townPtr, i, xLoc, yLoc) )
			continue;

		if( fieldNo-- > 0 )
			continue;

		int startNo = misc.random(4);

		for( int j=0 ; j<4 ; j++ )
		{
			int x = xLoc + (startNo+j)%2;
			int y = yLoc + (startNo+j)/2%2;

			if( (x != unitPtr->next_x_loc() || y != unitPtr->next_y_loc()) &&
				 world.get_loc(x, y)->can_move(UNIT_LAND) )
			{
				xLoc = x;
				yLoc = y;
				return 1;
			}
		}

		return 0;		// the field is full
	}

	return 0;
}
//----------- End of static function find_work_loc -----------//


//--------- Begin of function CivilTech::find_field_loc ---------//
//
// A location in one of the village's fields being farmed, or the first
// field if none is being farmed yet.
//
int CivilTech::find_field_loc(Town* townPtr, int& xLoc, int& yLoc)
{
	int fieldCount = MAX( 1, active_field_count(townPtr) + 1 );		// +1: a new farmer may start a new field
	int fieldNo = misc.random(fieldCount);

	for( int i=0 ; i<FARM_FIELD_CANDIDATE_COUNT ; i++ )
	{
		if( !field_loc(townPtr, i, xLoc, yLoc) )
			continue;

		if( fieldNo-- > 0 )
			continue;

		//--- a free location of the field, so the farmer doesn't walk around looking for one ---//

		int startNo = misc.random(4);

		for( int j=0 ; j<4 ; j++ )
		{
			int x = xLoc + (startNo+j)%2;
			int y = yLoc + (startNo+j)/2%2;

			if( world.get_loc(x, y)->can_move(UNIT_LAND) )
			{
				xLoc = x;
				yLoc = y;
				return 1;
			}
		}

		return 0;		// the field is full
	}

	//--- fewer valid fields than wanted: the first valid one ---//

	for( int i=0 ; i<FARM_FIELD_CANDIDATE_COUNT ; i++ )
	{
		if( field_loc(townPtr, i, xLoc, yLoc) )
		{
			xLoc += misc.random(2);
			yLoc += misc.random(2);
			return 1;
		}
	}

	return 0;
}
//----------- End of function CivilTech::find_field_loc -----------//


//--------- Begin of function CivilTech::draw_town_extras ---------//
//
// Called by Town::draw(). Draw the fields being farmed under everything
// else (layer 4), and the watchtower over the village (layer 1).
//
void CivilTech::draw_town_extras(Town* townPtr, int displayLayer)
{
	if( displayLayer == 4 )
	{
		int fieldCount = active_field_count(townPtr);

		if( !fieldCount )
			return;

		//--- the crops grow through the season: sprouts until April, green until July, then ripe ---//

		char* fieldBitmap = field_bitmap( info.game_month <= 4 ? 0 : info.game_month <= 7 ? 1 : 2 );

		int xLoc, yLoc;

		for( int i=0 ; i<FARM_FIELD_CANDIDATE_COUNT && fieldCount>0 ; i++ )
		{
			if( !field_loc(townPtr, i, xLoc, yLoc) )
				continue;

			fieldCount--;

			for( int y=yLoc ; y<=yLoc+1 ; y++ )
			{
				for( int x=xLoc ; x<=xLoc+1 ; x++ )
				{
					int drawX = x * ZOOM_LOC_WIDTH  - world.view_top_x + ZOOM_X1;
					int drawY = y * ZOOM_LOC_HEIGHT - world.view_top_y + ZOOM_Y1;

					world.zoom_matrix->put_bitmap_clip(drawX, drawY, fieldBitmap);
				}
			}
		}
	}
	else if( displayLayer == 1 && townPtr->has_watchtower )
	{
		//--- no picture of a tower in the game: a ballista over the top right corner of the village ---//

		char* iconPtr = unit_res[UNIT_BALLISTA]->get_small_icon_ptr(RANK_SOLDIER);

		int drawX = (townPtr->loc_x2+1) * ZOOM_LOC_WIDTH  - world.view_top_x + ZOOM_X1 - 24;
		int drawY = townPtr->loc_y1 * ZOOM_LOC_HEIGHT - world.view_top_y + ZOOM_Y1 - 12;

		world.zoom_matrix->put_bitmap_clip(drawX, drawY, iconPtr);
	}
}
//----------- End of function CivilTech::draw_town_extras -----------//


//--------- Begin of static function field_bitmap ---------//
//
// The bitmap of one location of a field: furrows of brown soil with rows
// of crops, made in the game palette when first needed.
//
// <int> growthStage - 0: sprouts, 1: green crops, 2: ripe crops
//
static char* field_bitmap(int growthStage)
{
	enum { FIELD_TILE_SIZE = 32 };		// ZOOM_LOC_WIDTH and ZOOM_LOC_HEIGHT

	static char* bitmapArray[3] = { NULL, NULL, NULL };

	if( bitmapArray[growthStage] )
		return bitmapArray[growthStage];

	int soilDark = road_res.pick_color( 92,  62,  34);
	int soilMid  = road_res.pick_color(124,  88,  50);
	int cropDark, cropLight, cropSpacing;

	switch( growthStage )
	{
		case 0:
			cropDark  = road_res.pick_color(104, 146,  56);
			cropLight = road_res.pick_color(140, 180,  80);
			cropSpacing = 4;
			break;

		case 1:
			cropDark  = road_res.pick_color( 56, 108,  32);
			cropLight = road_res.pick_color( 92, 144,  48);
			cropSpacing = 2;
			break;

		default:
			cropDark  = road_res.pick_color(176, 140,  48);
			cropLight = road_res.pick_color(220, 190,  92);
			cropSpacing = 2;
	}

	char* bitmapPtr = mem_add( 4 + FIELD_TILE_SIZE*FIELD_TILE_SIZE );

	((short*)bitmapPtr)[0] = FIELD_TILE_SIZE;
	((short*)bitmapPtr)[1] = FIELD_TILE_SIZE;

	char* pixelPtr = bitmapPtr + 4;

	for( int y=0 ; y<FIELD_TILE_SIZE ; y++ )
	{
		int furrowY = y % 8;		// a furrow every 8 pixels

		for( int x=0 ; x<FIELD_TILE_SIZE ; x++ )
		{
			char color = (char) (furrowY < 2 ? soilDark : soilMid);

			//--- a row of plants on each ridge ---//

			if( growthStage == 0 )		// rows of short sprouts
			{
				if( furrowY >= 4 && furrowY <= 5 && (x + y/8) % cropSpacing == 0 )
					color = (char) (furrowY == 4 ? cropLight : cropDark);
			}
			else if( furrowY >= 2 )		// thick rows of green or ripe crops, a little uneven
			{
				if( (x*7 + y*3) % 5 != 0 )
					color = (char) (furrowY <= 3 || (x+y)%3==0 ? cropLight : cropDark);
			}

			*pixelPtr++ = color;
		}
	}

	bitmapArray[growthStage] = bitmapPtr;
	return bitmapPtr;
}
//----------- End of static function field_bitmap -----------//


//======================== Medicine ========================//


//--------- Begin of function CivilTech::medics_try_save ---------//
//
// Called by Unit::hit_target() when a unit's hit points have just run out.
// A soldier or general of a nation with Medicine falling inside its
// territory may be carried back to the nearest fort instead of dying.
//
// return : <int> 1 if the unit is saved: it is left with 1 hit point and
//			  will be carried back by process_medics()
//
int CivilTech::medics_try_save(Unit* unitPtr)
{
	int nationRecno = unitPtr->nation_recno;

	if( !nationRecno || nation_array.is_deleted(nationRecno) )
		return 0;

	int level = tech_res.civil_level(nationRecno, CIVIL_MEDICINE);

	if( !level )
		return 0;

	if( !unitPtr->race_id || (unitPtr->rank_id != RANK_SOLDIER && unitPtr->rank_id != RANK_GENERAL) ||
		 unitPtr->is_civilian() || unitPtr->spy_recno || unitPtr->unit_mode != 0 ||
		 !unitPtr->is_visible() || unitPtr->mobile_type != UNIT_LAND )
	{
		return 0;
	}

	//--- once only: if it falls again before it is carried away, it dies ---//

	int i;
	for( i=0 ; i<medic_count ; i++ )
	{
		if( medic_unit_array[i] == unitPtr->sprite_recno )
			return 0;
	}

	if( medic_count >= MAX_MEDIC_EVACUATION )
		return 0;

	//--- only inside its own territory, where it can be carried back ---//

	if( world.get_loc(unitPtr->next_x_loc(), unitPtr->next_y_loc())->power_nation_recno != nationRecno )
		return 0;

	if( misc.random(100) >= MEDIC_SAVE_PERCENT_PER_LEVEL * level )
		return 0;

	int campRecno = find_medic_camp(unitPtr);

	if( !campRecno )
	{
		AiWarState::log( "MEDIC       nation %d: unit %d could have been saved but no fort has room for it", nationRecno, unitPtr->sprite_recno );
		return 0;
	}

	unitPtr->hit_points = (float) 1;

	medic_unit_array[medic_count] = unitPtr->sprite_recno;
	medic_camp_array[medic_count] = campRecno;
	medic_count++;

	return 1;
}
//----------- End of function CivilTech::medics_try_save -----------//


//--------- Begin of function CivilTech::find_medic_camp ---------//
//
// return : <int> the nearest fort of the unit's nation on the same land
//			  with room for it, 0 if none
//
int CivilTech::find_medic_camp(Unit* unitPtr)
{
	int bestCampRecno=0, minDistance=0x7FFF;
	int regionId = unitPtr->region_id();

	for( int i=firm_array.size() ; i>0 ; i-- )
	{
		if( firm_array.is_deleted(i) )
			continue;

		Firm* firmPtr = firm_array[i];

		if( firmPtr->firm_id != FIRM_CAMP || firmPtr->nation_recno != unitPtr->nation_recno ||
			 firmPtr->under_construction || firmPtr->region_id != regionId )
		{
			continue;
		}

		if( unitPtr->rank_id == RANK_GENERAL )
		{
			if( firmPtr->overseer_recno )
				continue;
		}
		else if( firmPtr->worker_count >= MAX_WORKER )
		{
			continue;
		}

		int curDistance = misc.points_distance( firmPtr->center_x, firmPtr->center_y, unitPtr->next_x_loc(), unitPtr->next_y_loc() );

		if( curDistance < minDistance )
		{
			minDistance = curDistance;
			bestCampRecno = i;
		}
	}

	return bestCampRecno;
}
//----------- End of function CivilTech::find_medic_camp -----------//


//--------- Begin of function CivilTech::process_medics ---------//
//
// Called at the start of UnitArray::process(), outside of any attack in
// progress: carry the saved units back to their forts.
//
void CivilTech::process_medics()
{
	for( int i=0 ; i<medic_count ; i++ )
	{
		int unitRecno = medic_unit_array[i];

		if( unit_array.is_deleted(unitRecno) )
			continue;

		Unit* unitPtr = unit_array[unitRecno];

		if( unitPtr->hit_points <= 0 || unitPtr->cur_action == SPRITE_DIE || unitPtr->action_mode == ACTION_DIE ||
			 !unitPtr->is_visible() || unitPtr->unit_mode != 0 )
		{
			AiWarState::log( "MEDIC       nation %d: unit %d fell again before the medics could carry it away", unitPtr->nation_recno, unitRecno );
			continue;
		}

		//---- the fort may have been filled or lost in the meantime ----//

		int campRecno = medic_camp_array[i];

		if( firm_array.is_deleted(campRecno) || firm_array[campRecno]->nation_recno != unitPtr->nation_recno ||
			 ( unitPtr->rank_id == RANK_GENERAL ? firm_array[campRecno]->overseer_recno != 0 :
															  firm_array[campRecno]->worker_count >= MAX_WORKER ) )
		{
			campRecno = find_medic_camp(unitPtr);
		}

		if( !campRecno )
		{
			AiWarState::log( "MEDIC       nation %d: no fort has room for unit %d any more, it stays on the field", unitPtr->nation_recno, unitRecno );
			continue;
		}

		FirmCamp* firmCamp = (FirmCamp*) firm_array[campRecno];
		int nationRecno = unitPtr->nation_recno;

		unitPtr->stop2();

		if( unitPtr->rank_id == RANK_GENERAL )
			firmCamp->assign_overseer(unitRecno);
		else
			firmCamp->assign_worker(unitRecno);

		medic_saved_array[nationRecno-1]++;

		AiWarState::log( "MEDIC       nation %d: fallen %s %d carried back to fort %d",
							  nationRecno, unit_array.is_deleted(unitRecno) || unit_array[unitRecno]->rank_id != RANK_GENERAL ? "soldier" : "general",
							  unitRecno, campRecno );
	}

	medic_count = 0;
}
//----------- End of function CivilTech::process_medics -----------//


//--------- Begin of function CivilTech::medic_recovery_bonus ---------//
//
// return : <int> extra hit points recovered by the nation's units: its
//			  Medicine level
//
int CivilTech::medic_recovery_bonus(int nationRecno)
{
	return tech_res.civil_level(nationRecno, CIVIL_MEDICINE);
}
//----------- End of function CivilTech::medic_recovery_bonus -----------//


//======================== Engineering ========================//


//--------- Begin of function CivilTech::can_build_watchtower ---------//
//
int CivilTech::can_build_watchtower(Town* townPtr, int checkCash)
{
	if( !townPtr->nation_recno || townPtr->has_watchtower || townPtr->rebel_recno )
		return 0;

	if( !tech_res.civil_level(townPtr->nation_recno, CIVIL_ENGINEERING) )
		return 0;

	if( checkCash && nation_array[townPtr->nation_recno]->cash < WATCHTOWER_COST )
		return 0;

	return 1;
}
//----------- End of function CivilTech::can_build_watchtower -----------//


//--------- Begin of function CivilTech::build_watchtower ---------//
//
void CivilTech::build_watchtower(Town* townPtr)
{
	if( !can_build_watchtower(townPtr) )
		return;

	nation_array[townPtr->nation_recno]->add_expense( EXPENSE_FIRM, (float) WATCHTOWER_COST );

	townPtr->has_watchtower = 1;

	AiWarState::log( "TOWER       nation %d builds a watchtower in town %d", townPtr->nation_recno, townPtr->town_recno );
}
//----------- End of function CivilTech::build_watchtower -----------//


//--------- Begin of function CivilTech::process_watchtowers ---------//
//
// Every day each watchtower shoots its arrows at the nearest enemies.
//
void CivilTech::process_watchtowers()
{
	for( int i=town_array.size() ; i>0 ; i-- )
	{
		if( town_array.is_deleted(i) )
			continue;

		Town* townPtr = town_array[i];

		if( !townPtr->has_watchtower || !townPtr->nation_recno )
			continue;

		int level  = MAX( 1, tech_res.civil_level(townPtr->nation_recno, CIVIL_ENGINEERING) );		// the tower of a captured village works for its new nation
		int range  = WATCHTOWER_RANGE + 2*(level-1);
		int arrows = WATCHTOWER_ARROWS + (level-1);
		int damage = WATCHTOWER_DAMAGE + 10*(level-1);
		int hitCount = 0, killCount = 0;

		int xLoc1 = MAX(0, townPtr->loc_x1-range), xLoc2 = MIN(MAX_WORLD_X_LOC-1, townPtr->loc_x2+range);
		int yLoc1 = MAX(0, townPtr->loc_y1-range), yLoc2 = MIN(MAX_WORLD_Y_LOC-1, townPtr->loc_y2+range);

		for( int yLoc=yLoc1 ; yLoc<=yLoc2 && hitCount<arrows ; yLoc++ )
		{
			for( int xLoc=xLoc1 ; xLoc<=xLoc2 && hitCount<arrows ; xLoc++ )
			{
				Location* locPtr = world.get_loc(xLoc, yLoc);

				if( !locPtr->has_unit(UNIT_LAND) )
					continue;

				int unitRecno = locPtr->unit_recno(UNIT_LAND);

				if( unit_array.is_deleted(unitRecno) )
					continue;

				Unit* unitPtr = unit_array[unitRecno];

				if( !is_watchtower_target(townPtr, unitPtr) )
					continue;

				//---- shoot ----//

				int effectId = watchtower_effect_id();

				if( effectId )
					Effect::create(effectId, unitPtr->cur_x, unitPtr->cur_y, SPRITE_IDLE, unitPtr->cur_dir, 2, 0);

				unitPtr->hit_target( NULL, unitPtr, (float) damage + 1, townPtr->nation_recno );		// +1: Unit::hit_target() takes off a default armor of 4/ATTACK_SLOW_DOWN

				hitCount++;

				if( unitPtr->hit_points <= 0 )
					killCount++;
			}
		}

		if( hitCount )
		{
			AiWarState::log( "TOWER       town %d of nation %d shoots %d arrows (level %d, %d damage), %d enemies fall",
								  townPtr->town_recno, townPtr->nation_recno, hitCount, level, damage, killCount );
		}
	}
}
//----------- End of function CivilTech::process_watchtowers -----------//


//--------- Begin of function CivilTech::is_watchtower_target ---------//
//
// Enemies of the village's nation, Fryhtans and rebels near the village.
//
int CivilTech::is_watchtower_target(Town* townPtr, Unit* unitPtr)
{
	if( !unitPtr->is_visible() || unitPtr->hit_points <= 0 || unitPtr->cur_action == SPRITE_DIE ||
		 unitPtr->action_mode == ACTION_DIE || unitPtr->nation_recno == townPtr->nation_recno )
	{
		return 0;
	}

	if( unitPtr->nation_recno )
		return nation_array[townPtr->nation_recno]->get_relation_status(unitPtr->nation_recno) == NATION_HOSTILE;

	return unit_res[unitPtr->unit_id]->unit_class == UNIT_CLASS_MONSTER ||
			 unitPtr->unit_mode == UNIT_MODE_REBEL;
}
//----------- End of function CivilTech::is_watchtower_target -----------//


//--------- Begin of static function watchtower_effect_id ---------//
//
// The hit effect of an attack of a war machine, shown where an arrow hits.
//
static int watchtower_effect_id()
{
	static int effectId = -1;

	if( effectId >= 0 )
		return effectId;

	effectId = 0;

	static short unitIdArray[] = { UNIT_BALLISTA, UNIT_F_BALLISTA, UNIT_CATAPULT, UNIT_CANNON, UNIT_FLAMETHROWER };

	for( int i=0 ; i<(int)(sizeof(unitIdArray)/sizeof(unitIdArray[0])) && !effectId ; i++ )
	{
		UnitInfo* unitInfo = unit_res[unitIdArray[i]];

		for( int j=0 ; j<unitInfo->attack_count ; j++ )
		{
			AttackInfo* attackInfo = unit_res.get_attack_info(unitInfo->first_attack+j);

			if( attackInfo->effect_id )
			{
				effectId = attackInfo->effect_id;
				break;
			}
		}
	}

	AiWarState::log( "TOWER       arrows are shown with effect %d", effectId );

	return effectId;
}
//----------- End of static function watchtower_effect_id -----------//


//======================== Industry ========================//


//--------- Begin of function CivilTech::deep_dig ---------//
//
// Called by FirmMine::produce_raw() when the mine has run dry.
//
// return : <int> 1 if the mine has dug deeper and has a new vein
//
int CivilTech::deep_dig(FirmMine* firmMine)
{
	if( !firmMine->nation_recno || !firmMine->raw_id )
		return 0;

	int level = tech_res.civil_level(firmMine->nation_recno, CIVIL_INDUSTRY);

	if( firmMine->deep_dig_count >= level )
		return 0;

	firmMine->deep_dig_count++;
	firmMine->reserve_qty = (float) DEEP_VEIN_QTY;

	if( firmMine->site_recno )
		site_array[firmMine->site_recno]->reserve_qty = DEEP_VEIN_QTY;

	if( firmMine->nation_recno == nation_array.player_recno )
		news_array.mine_dug_deeper(firmMine->raw_id, firmMine->center_x, firmMine->center_y);

	AiWarState::log( "DEEPMINE    nation %d: the %s mine at %d,%d has run dry and digs deeper (dig %d of %d)",
						  firmMine->nation_recno, raw_res[firmMine->raw_id]->name, firmMine->loc_x1, firmMine->loc_y1,
						  firmMine->deep_dig_count, level );
	return 1;
}
//----------- End of function CivilTech::deep_dig -----------//


//======================== Administration ========================//


//--------- Begin of function CivilTech::edict_name ---------//
//
const char* CivilTech::edict_name(int edictId)
{
	switch( edictId )
	{
		case EDICT_FESTIVAL:	return _("Festival");
		case EDICT_LEVY:		return _("Labour Levy");
		case EDICT_MILITIA:	return _("Militia");
	}

	return "";
}
//----------- End of function CivilTech::edict_name -----------//


//--------- Begin of function CivilTech::edict_cost ---------//
//
int CivilTech::edict_cost(Town* townPtr, int edictId)
{
	if( edictId != EDICT_FESTIVAL || !townPtr->nation_recno )
		return 0;

	int level = tech_res.civil_level(townPtr->nation_recno, CIVIL_ADMINISTRATION);

	return townPtr->population * (level >= 2 ? 4 : 6);
}
//----------- End of function CivilTech::edict_cost -----------//


//--------- Begin of function CivilTech::active_edict ---------//
//
int CivilTech::active_edict(Town* townPtr)
{
	if( townPtr->edict_id && info.game_date < townPtr->edict_end_date )
		return townPtr->edict_id;

	return EDICT_NONE;
}
//----------- End of function CivilTech::active_edict -----------//


//--------- Begin of function CivilTech::can_issue_edict ---------//
//
int CivilTech::can_issue_edict(Town* townPtr, int edictId, int checkCash)
{
	if( !townPtr->nation_recno || townPtr->rebel_recno || active_edict(townPtr) ||
		 edictId < 1 || edictId > EDICT_TYPE_COUNT )
	{
		return 0;
	}

	if( !tech_res.civil_level(townPtr->nation_recno, CIVIL_ADMINISTRATION) )
		return 0;

	if( checkCash && nation_array[townPtr->nation_recno]->cash < edict_cost(townPtr, edictId) )
		return 0;

	return 1;
}
//----------- End of function CivilTech::can_issue_edict -----------//


//--------- Begin of function CivilTech::issue_edict ---------//
//
void CivilTech::issue_edict(Town* townPtr, int edictId)
{
	if( !can_issue_edict(townPtr, edictId) )
		return;

	int cost = edict_cost(townPtr, edictId);

	if( cost )
		nation_array[townPtr->nation_recno]->add_expense( EXPENSE_GRANT_OWN_TOWN, (float) cost );

	townPtr->edict_id = edictId;
	townPtr->edict_end_date = info.game_date + EDICT_DAYS;

	townPtr->update_target_loyalty();

	AiWarState::log( "EDICT       nation %d: town %d (population %d, loyalty %d) gets a %s edict for a year (cost %d)",
						  townPtr->nation_recno, townPtr->town_recno, townPtr->population, townPtr->average_loyalty(),
						  edict_name(edictId), cost );
}
//----------- End of function CivilTech::issue_edict -----------//


//--------- Begin of function CivilTech::process_edicts ---------//
//
// End the edicts that have lasted their year.
//
void CivilTech::process_edicts()
{
	for( int i=town_array.size() ; i>0 ; i-- )
	{
		if( town_array.is_deleted(i) )
			continue;

		Town* townPtr = town_array[i];

		if( !townPtr->edict_id || info.game_date < townPtr->edict_end_date )
			continue;

		int edictId = townPtr->edict_id;

		townPtr->edict_id = EDICT_NONE;
		townPtr->edict_end_date = 0;

		if( townPtr->nation_recno )
			townPtr->update_target_loyalty();

		if( townPtr->nation_recno && townPtr->nation_recno == nation_array.player_recno )
			news_array.edict_ended(townPtr->town_recno, edictId);

		AiWarState::log( "EDICT       nation %d: the %s edict of town %d has ended", townPtr->nation_recno, edict_name(edictId), townPtr->town_recno );
	}
}
//----------- End of function CivilTech::process_edicts -----------//


//--------- Begin of function CivilTech::edict_loyalty_change ---------//
//
// return : <int> the change to the target loyalty of the village's people
//
int CivilTech::edict_loyalty_change(Town* townPtr)
{
	int edictId = active_edict(townPtr);

	if( !edictId )
		return 0;

	int level = tech_res.civil_level(townPtr->nation_recno, CIVIL_ADMINISTRATION);

	switch( edictId )
	{
		case EDICT_FESTIVAL:	return level >= 2 ? 15 : 10;
		case EDICT_LEVY:		return level >= 2 ? -5 : -10;
		case EDICT_MILITIA:	return level >= 2 ? 0 : -5;
	}

	return 0;
}
//----------- End of function CivilTech::edict_loyalty_change -----------//


//--------- Begin of function CivilTech::edict_food_multiplier ---------//
//
// return : <int> how many times the usual food the village's peasants produce
//
int CivilTech::edict_food_multiplier(Town* townPtr)
{
	return active_edict(townPtr) == EDICT_LEVY ? 2 : 1;
}
//----------- End of function CivilTech::edict_food_multiplier -----------//


//--------- Begin of function CivilTech::edict_militia_combat_bonus ---------//
//
// return : <int> the extra combat level of the village's defenders, 0 if
//			  the village has no militia
//
int CivilTech::edict_militia_combat_bonus(Town* townPtr)
{
	if( active_edict(townPtr) != EDICT_MILITIA )
		return 0;

	return tech_res.civil_level(townPtr->nation_recno, CIVIL_ADMINISTRATION) >= 2 ? 35 : 20;
}
//----------- End of function CivilTech::edict_militia_combat_bonus -----------//


//======================== Commerce ========================//


//--------- Begin of function CivilTech::process_tolls ---------//
//
// Foreign caravans on roads in the territory of a nation with Commerce
// pay a toll every day, unless they are of an ally or an enemy.
//
void CivilTech::process_tolls()
{
	for( int i=unit_array.size() ; i>0 ; i-- )
	{
		if( unit_array.is_deleted(i) )
			continue;

		Unit* unitPtr = unit_array[i];

		if( unitPtr->unit_id != UNIT_CARAVAN || !unitPtr->nation_recno || !unitPtr->is_visible() )
			continue;

		Location* locPtr = world.get_loc(unitPtr->next_x_loc(), unitPtr->next_y_loc());

		if( !locPtr->has_road() )
			continue;

		int ownerRecno = locPtr->power_nation_recno;

		if( !ownerRecno || ownerRecno == unitPtr->nation_recno || nation_array.is_deleted(ownerRecno) )
			continue;

		int level = tech_res.civil_level(ownerRecno, CIVIL_COMMERCE);

		if( !level )
			continue;

		Nation* ownerNation = nation_array[ownerRecno];
		Nation* payerNation = nation_array[unitPtr->nation_recno];

		int relationStatus = ownerNation->get_relation_status(unitPtr->nation_recno);

		if( relationStatus == NATION_HOSTILE || relationStatus == NATION_ALLIANCE ||
			 ownerNation->get_relation(unitPtr->nation_recno)->road_treaty )		// a road treaty, see OTALKMSG.H
		{
			continue;
		}

		if( payerNation->cash <= 0 )
			continue;

		float toll = (float) TOLL_PER_DAY * level;

		payerNation->add_expense( EXPENSE_TOLLS, toll );
		ownerNation->add_income( INCOME_TOLLS, toll );

		toll_paid_array[ownerRecno-1] += toll;
	}
}
//----------- End of function CivilTech::process_tolls -----------//


//--------- Begin of function CivilTech::pay_interest ---------//
//
// Called by NationBase::next_month(): the treasury of a nation with
// Commerce earns interest.
//
void CivilTech::pay_interest(NationBase* nationPtr)
{
	int level = tech_res.civil_level(nationPtr->nation_recno, CIVIL_COMMERCE);

	if( !level || nationPtr->cash <= 0 )
		return;

	float interest = nationPtr->cash * INTEREST_YEAR_PERCENT * level / 100 / 12;

	interest = MIN( interest, (float) MAX_MONTHLY_INTEREST * level );

	nationPtr->add_income( INCOME_INTEREST, interest );

	AiWarState::log( "CIVIL       nation %d earns %.0f interest on %.0f", nationPtr->nation_recno, interest, nationPtr->cash );
}
//----------- End of function CivilTech::pay_interest -----------//


//======================== Military Drill ========================//


//--------- Begin of function CivilTech::auto_recruit ---------//
//
// Called by FirmCamp::next_day(): a fort with a commander and room for
// soldiers recruits from its linked villages, preferring the commander's
// race. As when the player drags people from a village to the fort, the
// villagers come only if they are willing (see Firm::pull_town_people()).
//
void CivilTech::auto_recruit(FirmCamp* firmCamp)
{
	if( info.game_date % AUTO_RECRUIT_INTERVAL_DAYS != firmCamp->firm_recno % AUTO_RECRUIT_INTERVAL_DAYS )
		return;

	if( !firmCamp->nation_recno || firmCamp->auto_recruit_off || !firmCamp->overseer_recno ||
		 firmCamp->worker_count >= MAX_WORKER || firmCamp->under_construction )
	{
		return;
	}

	if( !tech_res.civil_level(firmCamp->nation_recno, CIVIL_MILITARY_DRILL) )
		return;

	if( nation_array[firmCamp->nation_recno]->cash < 0 )
		return;

	int overseerRaceId = unit_array[firmCamp->overseer_recno]->race_id;

	for( int pass=0 ; pass<2 ; pass++ )		// first the commander's race, then any race
	{
		for( int i=0 ; i<firmCamp->linked_town_count ; i++ )
		{
			if( firmCamp->linked_town_enable_array[i] != LINK_EE )
				continue;

			Town* townPtr = town_array[ firmCamp->linked_town_array[i] ];

			if( townPtr->nation_recno != firmCamp->nation_recno || townPtr->population <= 1 )
				continue;

			int raceId = pass==0 ? overseerRaceId : 0;

			if( raceId && townPtr->recruitable_race_pop(raceId, 0)==0 )
				continue;

			if( !raceId && townPtr->jobless_population==0 )
				continue;

			int oldWorkerCount = firmCamp->worker_count;

			firmCamp->pull_town_people( townPtr->town_recno, COMMAND_AUTO, raceId, 0 );		// 0-only if willing

			if( firmCamp->worker_count > oldWorkerCount )
			{
				AiWarState::log( "RECRUIT     nation %d: fort %d recruits a soldier from town %d by itself (%d soldiers)",
									  firmCamp->nation_recno, firmCamp->firm_recno, townPtr->town_recno, firmCamp->worker_count );
				return;
			}
		}
	}
}
//----------- End of function CivilTech::auto_recruit -----------//


//--------- Begin of function CivilTech::train_speed_percent ---------//
//
// return : <int> the speed soldiers of the nation train at, 100 normally
//
int CivilTech::train_speed_percent(int nationRecno)
{
	int level = tech_res.civil_level(nationRecno, CIVIL_MILITARY_DRILL);

	return 100 + (level >= 2 ? TRAIN_SPEED_PERCENT_PER_LEVEL*2 : 0);		// level I recruits, level II trains faster
}
//----------- End of function CivilTech::train_speed_percent -----------//


//======================== Navigation ========================//


//--------- Begin of function CivilTech::process_fishing ---------//
//
// The harbors of a nation with Navigation catch fish.
//
void CivilTech::process_fishing()
{
	for( int i=firm_array.size() ; i>0 ; i-- )
	{
		if( firm_array.is_deleted(i) )
			continue;

		Firm* firmPtr = firm_array[i];

		if( firmPtr->firm_id != FIRM_HARBOR || !firmPtr->nation_recno || firmPtr->under_construction )
			continue;

		int level = tech_res.civil_level(firmPtr->nation_recno, CIVIL_NAVIGATION);

		if( level )
			nation_array[firmPtr->nation_recno]->add_food( (float) FISH_YEAR_FOOD * level / 365 );
	}
}
//----------- End of function CivilTech::process_fishing -----------//


//--------- Begin of function CivilTech::ship_extra_step ---------//
//
// Called by Unit::process_move() every frame a unit moves.
//
// return : <int> 1 if the unit is a ship of a nation with Navigation that
//			  makes an extra step this frame
//
int CivilTech::ship_extra_step(Unit* unitPtr)
{
	if( unitPtr->mobile_type != UNIT_SEA || !unitPtr->nation_recno )
		return 0;

	int level = tech_res.civil_level(unitPtr->nation_recno, CIVIL_NAVIGATION);

	if( !level )
		return 0;

	int frames = level >= 2 ? SHIP_EXTRA_STEP_FRAMES_LEVEL_2 : SHIP_EXTRA_STEP_FRAMES_LEVEL_1;

	return (sys.frame_count + unitPtr->sprite_recno) % frames == 0;
}
//----------- End of function CivilTech::ship_extra_step -----------//


//======================== the AI ========================//


//--------- Begin of function CivilTech::ai_think ---------//
//
// Once a month an AI nation builds watchtowers and gives edicts.
// Farming, medics, deep mines, tolls, interest, recruiting and fishing
// work by themselves.
//
void CivilTech::ai_think(Nation* nationPtr)
{
	int nationRecno = nationPtr->nation_recno;
	int isAtWar		 = nationPtr->total_enemy_count() > 0;

	//------- a watchtower for a village in danger -------//

	if( tech_res.civil_level(nationRecno, CIVIL_ENGINEERING) &&
		 nationPtr->cash > WATCHTOWER_COST * 4 )
	{
		Town* bestTown = NULL;
		int   bestRating = 0;

		for( int i=0 ; i<nationPtr->ai_town_count ; i++ )
		{
			Town* townPtr = town_array[ nationPtr->ai_town_array[i] ];

			if( !can_build_watchtower(townPtr) )
				continue;

			int curRating = 0;

			if( info.game_date < townPtr->last_being_attacked_date + 365 )		// attacked in the last year
				curRating += 50;

			if( isAtWar )
				curRating += 20;

			if( !townPtr->has_linked_own_camp )		// no fort to defend it
				curRating += 20;

			curRating += townPtr->population / 10;

			if( curRating > bestRating )
			{
				bestRating = curRating;
				bestTown = townPtr;
			}
		}

		if( bestTown && bestRating >= 50 )
			build_watchtower(bestTown);
	}

	//------- edicts -------//

	if( tech_res.civil_level(nationRecno, CIVIL_ADMINISTRATION) )
	{
		int issuedCount = 0;
		int isShortOfFood = nationPtr->food < nationPtr->yearly_food_consumption() / 2;

		for( int i=0 ; i<nationPtr->ai_town_count && issuedCount<2 ; i++ )
		{
			Town* townPtr = town_array[ nationPtr->ai_town_array[i] ];

			if( active_edict(townPtr) )
				continue;

			int loyalty = townPtr->average_loyalty();
			int edictId = EDICT_NONE;

			if( loyalty < 45 && nationPtr->cash > edict_cost(townPtr, EDICT_FESTIVAL) * 4 )
				edictId = EDICT_FESTIVAL;

			else if( isAtWar && info.game_date < townPtr->last_being_attacked_date + 180 && loyalty >= 40 )
				edictId = EDICT_MILITIA;

			else if( isShortOfFood && loyalty > 60 && townPtr->jobless_population >= 10 )
				edictId = EDICT_LEVY;

			if( edictId && can_issue_edict(townPtr, edictId) )
			{
				issue_edict(townPtr, edictId);
				issuedCount++;
			}
		}
	}
}
//----------- End of function CivilTech::ai_think -----------//
