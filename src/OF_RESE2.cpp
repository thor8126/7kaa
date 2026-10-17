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

//Filename    : OF_RESE2.CPP
//Description : Firm Research Center - AI functions

#include <ONATION.h>
#include <OINFO.h>
#include <OTOWN.h>
#include <OUNIT.h>
#include <OTECHRES.h>
#include <OF_RESE.h>
#include <OAIWAR.h>
#include <OSTR.h>
#include <OMISC.h>

//--------- Begin of function FirmResearch::process_ai ---------//

void FirmResearch::process_ai()
{
	//---- think about which technology to research ----//

	if( !tech_id )
		think_new_research();

	//------- recruit workers ---------//

	if( info.game_date%15==firm_recno%15 )
	{
		if( worker_count < MAX_WORKER )
			ai_recruit_worker();
	}

	//----- think about closing down this firm -----//

	if( info.game_date%30==firm_recno%30 )
	{
		if( think_del() )
			return;
	}
}
//----------- End of function FirmResearch::process_ai -----------//


//------- Begin of function FirmResearch::think_del -----------//
//
// Think about deleting this firm.
//
int FirmResearch::think_del()
{
	//----- if all technologies have been researched -----//

	if( nation_array[nation_recno]->total_tech_level() == tech_res.total_tech_level )		// all technology have been researched
	{
		ai_del_firm();
		return 1;
	}

   //----------------------------------------------// 

	if( worker_count > 0 )
		return 0;

	//-- check whether the firm is linked to any towns or not --//

	for( int i=0 ; i<linked_town_count ; i++ )
	{
		if( linked_town_enable_array[i] == LINK_EE )
			return 0;
	}

	//------------------------------------------------//

	ai_del_firm();

	return 1;
}
//--------- End of function FirmResearch::think_del -----------//


//----- Begin of function FirmResearch::think_new_research ------//
//
// Think about which technology to research. In the original game the AI
// picked one at random. Now it rates each technology by its opening
// strategy (see OAIWAR.H), its situation and the AI preferences, and
// logs the choice with the ratings.
//
void FirmResearch::think_new_research()
{
	TechInfo* techInfo;
	Nation*	 nationPtr = nation_array[nation_recno];
	int		 bestTechId=0, curRating, bestRating=0;
	int		 strategy  = ai_war_state.get_strategy(nation_recno);
	int		 isAtWar   = nationPtr->total_enemy_count() > 0;
	String	 ratingStr;

	enum { MAX_TECH_CANDIDATE = 50 };
	int		 techIdArray[MAX_TECH_CANDIDATE], ratingArray[MAX_TECH_CANDIDATE], candidateCount=0;

	for( int techId=tech_res.tech_count ; techId>0 ; techId-- )
	{
		techInfo = tech_res[techId];

		if( !techInfo->can_research(nation_recno) )
			continue;

		curRating = 100 + techInfo->is_nation_researching(nation_recno)*20;		// keep researching what other towers research

		if( techInfo->civil_id )
		{
			curRating += strategy == AI_STRATEGY_TECH ? 20 : 0;		// a technology-minded nation values all research

			switch( techInfo->civil_id )
			{
				case CIVIL_AGRICULTURE:			// a harvest from every village every year (food sells in markets), and it leads to Medicine
					curRating += nationPtr->ai_has_enough_food() ? 30 : 70;
					curRating += MIN( 30, nationPtr->ai_town_count * 5 );
					curRating += strategy == AI_STRATEGY_ECONOMY || strategy == AI_STRATEGY_EXPAND ? 20 : 0;
					break;

				case CIVIL_MEDICINE:				// soldiers survive
					curRating += isAtWar ? 40 : 10;
					curRating += strategy == AI_STRATEGY_MILITARY ? 20 : 0;
					break;

				case CIVIL_ENGINEERING:			// village watchtowers
					curRating += isAtWar ? 30 : 15;
					curRating += nationPtr->ai_town_count >= 4 ? 15 : 0;
					break;

				case CIVIL_INDUSTRY:				// mines that run dry dig deeper
					curRating += nationPtr->ai_mine_count * 10;
					curRating += strategy == AI_STRATEGY_ECONOMY ? 30 : 0;
					break;

				case CIVIL_ADMINISTRATION:		// village edicts
					curRating += MIN( 40, nationPtr->ai_town_count * 5 );
					curRating += strategy == AI_STRATEGY_EXPAND ? 20 : 0;
					break;

				case CIVIL_COMMERCE:				// road tolls and interest
					curRating += nationPtr->ai_market_count * 4 + nationPtr->pref_trading_tendency/5;
					curRating += strategy == AI_STRATEGY_ECONOMY ? 20 : 0;
					break;

				case CIVIL_MILITARY_DRILL:		// forts recruit by themselves and train faster
					curRating += MIN( 40, nationPtr->ai_camp_count * 6 );
					curRating += isAtWar ? 20 : 0;
					curRating += strategy == AI_STRATEGY_MILITARY ? 30 : 0;
					break;

				case CIVIL_NAVIGATION:			// harbors fish
					curRating += nationPtr->ai_harbor_count > 0 ? 40 : -60;
					curRating += strategy == AI_STRATEGY_NAVAL ? 50 : 0;
					break;
			}

			//--- the nation's style, see OAIWAR.H ---//

			switch( nationPtr->ai_style() )
			{
				case AI_STYLE_BUILDER:
					if( techInfo->civil_id == CIVIL_AGRICULTURE || techInfo->civil_id == CIVIL_INDUSTRY ||
						 techInfo->civil_id == CIVIL_ENGINEERING || techInfo->civil_id == CIVIL_ADMINISTRATION )
					{
						curRating += 20;
					}
					break;

				case AI_STYLE_MERCHANT:
					if( techInfo->civil_id == CIVIL_COMMERCE )
						curRating += 25;
					else if( techInfo->civil_id == CIVIL_NAVIGATION )
						curRating += 10;
					break;

				case AI_STYLE_WARLORD:
					if( techInfo->civil_id == CIVIL_MILITARY_DRILL )
						curRating += 20;
					else if( techInfo->civil_id == CIVIL_MEDICINE )
						curRating += 10;
					break;

				case AI_STYLE_SCHEMER:
					if( techInfo->civil_id == CIVIL_ADMINISTRATION )
						curRating += 10;
					break;
			}
		}
		else	//--- weapons and ships ---//
		{
			curRating += nationPtr->pref_use_weapon/3;
			curRating += isAtWar ? 30 : 0;
			curRating += strategy == AI_STRATEGY_TECH || strategy == AI_STRATEGY_MILITARY ? 30 : 0;

			if( techInfo->class_id == TECH_SHIP )		// useless without harbors
				curRating += nationPtr->ai_harbor_count > 0 || strategy == AI_STRATEGY_NAVAL ? 20 : -100;

			switch( nationPtr->ai_style() )
			{
				case AI_STYLE_WARLORD:		curRating += 20; break;
				case AI_STYLE_OPPORTUNIST:	curRating += 10; break;
				case AI_STYLE_MERCHANT:		curRating -= 10; break;
			}
		}

		ratingStr += techInfo->tech_des();
		ratingStr += " ";
		ratingStr += curRating;
		ratingStr += ", ";

		if( candidateCount < MAX_TECH_CANDIDATE )
		{
			techIdArray[candidateCount] = techId;
			ratingArray[candidateCount] = curRating;
			candidateCount++;
		}
	}

	//------ choose among the best, see AiWarState::weighted_pick() ------//

	int pick = AiWarState::weighted_pick(ratingArray, candidateCount, 85);

	if( pick >= 0 )
	{
		bestTechId = techIdArray[pick];
		bestRating = ratingArray[pick];
	}

	//------------------------------------//

	if( bestTechId )
	{
		AiWarState::log( "RESEARCH    nation %d (%s) tower %d researches %s %d (rating %d, strategy %d, at war %d; %s)",
							  nation_recno, AiWarState::style_name(nationPtr->ai_style()), firm_recno, tech_res[bestTechId]->tech_des(),
							  tech_res[bestTechId]->get_nation_tech_level(nation_recno)+1, bestRating, strategy, isAtWar, (char*) ratingStr );

		start_research( bestTechId, COMMAND_AI );
	}
}
//------ End of function FirmResearch::think_new_research -------//

