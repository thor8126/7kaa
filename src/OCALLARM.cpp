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

//Filename    : OCALLARM.CPP
//Description : Call to arms
//
// When one of the player's towns or firms is attacked, a news message
// tells the player, and a Defend button appears over the zoom view for
// CALL_TO_ARMS_ACTIVE_DAYS days. Pressing it sends the player's troops
// on the same land there:
//
// - the soldiers and war machines in all forts come out (the generals
//   stay, they are needed to keep the villages loyal),
// - the soldiers and war machines out on the map join them.
//
// The king, generals, civilians, spies, caravans, ships, units on ships
// or in buildings, and units in the middle of a fight are not sent.
//
// When there have been no attacks there for CALL_TO_ARMS_RETURN_QUIET_DAYS
// and no enemies are within CALL_TO_ARMS_ENEMY_DISTANCE, the troops go
// back: those from forts to their forts, the others to where they were
// standing when the attack began (war machines kept out of forts, for
// example). Troops already fighting there when Defend is pressed aren't
// given new orders, but go back afterwards too. Units the player has
// given other orders to meanwhile are left alone. The alert and the
// troops sent are not saved with the game.

#include <ALL.h>
#include <OCALLARM.h>
#include <ONATION.h>
#include <OFIRM.h>
#include <OF_CAMP.h>
#include <OUNIT.h>
#include <OUNITRES.h>
#include <OWORLD.h>
#include <OINFO.h>
#include <ONEWS.h>
#include <OPOWER.h>
#include <OBOX.h>
#include <OREMOTE.h>
#include <OMISC.h>
#include <OAIWAR.h>
#include "gettext.h"

//------- Define global variables -------//

CallToArms call_to_arms;

//------- Define static functions -------//

static int is_call_to_arms_unit(Unit* unitPtr, int nationRecno, int regionId);

//--------- Begin of function CallToArms::CallToArms ---------//
//
CallToArms::CallToArms()
{
	alert_date = 0;
	alert_x_loc = alert_y_loc = 0;
	alert_answered = 0;

	sent_array = NULL;
	sent_count = 0;
	sent_x_loc = sent_y_loc = 0;
	sent_date = last_fight_date = 0;

	snapshot_array = NULL;
	snapshot_count = 0;
}
//----------- End of function CallToArms::CallToArms -----------//


//--------- Begin of function CallToArms::~CallToArms ---------//
//
CallToArms::~CallToArms()
{
	if( sent_array )
		mem_del(sent_array);

	if( snapshot_array )
		mem_del(snapshot_array);
}
//----------- End of function CallToArms::~CallToArms -----------//


//--------- Begin of function CallToArms::reset ---------//
//
// Called when a new game starts.
//
void CallToArms::reset()
{
	alert_date = 0;
	alert_answered = 0;

	if( sent_array )
	{
		mem_del(sent_array);
		sent_array = NULL;
	}

	sent_count = 0;

	if( snapshot_array )
	{
		mem_del(snapshot_array);
		snapshot_array = NULL;
	}

	snapshot_count = 0;
}
//----------- End of function CallToArms::reset -----------//


//--------- Begin of function CallToArms::alert ---------//
//
// Called when one of the player's towns or firms is attacked.
//
// <int> xLoc, yLoc - location of the town or firm
// <int> firmId     - id. of the firm, 0 for a town
// <int> townNameId - name id. of the town, or of the town closest to the firm
//
void CallToArms::alert(int xLoc, int yLoc, int firmId, int townNameId)
{
	int isRepeat = alert_date && info.game_date - alert_date <= CALL_TO_ARMS_REPEAT_DAYS &&
						misc.points_distance(xLoc, yLoc, alert_x_loc, alert_y_loc) <= CALL_TO_ARMS_REPEAT_DISTANCE;

	alert_x_loc = xLoc;
	alert_y_loc = yLoc;
	alert_date  = info.game_date;

	//--- still fighting where the troops were sent: they don't go back yet ---//

	if( sent_count && misc.points_distance(xLoc, yLoc, sent_x_loc, sent_y_loc) <= CALL_TO_ARMS_REPEAT_DISTANCE )
		last_fight_date = info.game_date;

	if( !isRepeat )
	{
		alert_answered = 0;		// a new attack, the Defend button is shown again

		take_snapshot(xLoc, yLoc);

		char newsAddFlag = news_array.news_add_flag;		// news is disabled while a town is being hit, see Unit::hit_town()
		news_array.enable();
		news_array.under_attack(xLoc, yLoc, firmId, townNameId);
		news_array.news_add_flag = newsAddFlag;
	}
}
//----------- End of function CallToArms::alert -----------//


//--------- Begin of function CallToArms::is_active ---------//
//
// return : <int> 1 if the Defend button should be shown
//
int CallToArms::is_active()
{
	return alert_date && !alert_answered && nation_array.player_recno &&
			 info.game_date - alert_date <= CALL_TO_ARMS_ACTIVE_DAYS;
}
//----------- End of function CallToArms::is_active -----------//


//--------- Begin of function CallToArms::execute ---------//
//
// The player has pressed the Defend button.
//
void CallToArms::execute(char remoteAction)
{
	if( !is_active() )
		return;

	if( !remoteAction && remote.is_enable() )
	{
		// packet structure : <x loc> <y loc>
		short *shortPtr = (short *)remote.new_send_queue_msg(MSG_CALL_TO_ARMS, 2*sizeof(short));
		shortPtr[0] = alert_x_loc;
		shortPtr[1] = alert_y_loc;
		alert_answered = 1;
		return;
	}

	int sentCount = send_troops(alert_x_loc, alert_y_loc);

	alert_answered = 1;

	if( sentCount < 0 )
		box.msg( _("Your troops near there are already fighting. They will go back when it is over.") );
	else if( !sentCount )
		box.msg( _("There are no troops on this land to send.") );
}
//----------- End of function CallToArms::execute -----------//


//--------- Begin of function CallToArms::send_troops ---------//
//
// Send the player's troops on the same land to the given location.
//
// return : <int> the no. of units sent
//
int CallToArms::send_troops(int xLoc, int yLoc)
{
	int nationRecno = nation_array.player_recno;

	if( !nationRecno || nation_array.is_deleted(nationRecno) )
		return 0;

	int    regionId   = world.get_region_id(xLoc, yLoc);
	short* sendArray  = (short*) mem_add( sizeof(short) * (unit_array.size() + firm_array.size() * MAX_WORKER + 1) );
	int    sendCount  = 0;
	int    i;

	//------- soldiers and war machines out on the map --------//

	int fightingCount = 0;

	for( i=unit_array.size() ; i>0 ; i-- )
	{
		if( unit_array.is_deleted(i) )
			continue;

		int rc = is_call_to_arms_unit(unit_array[i], nationRecno, regionId);

		if( !rc )
			continue;

		//--- where it stood when the attack began, or where it is now ---//

		int returnXLoc, returnYLoc;

		if( !get_snapshot(i, returnXLoc, returnYLoc) )
		{
			if( rc == 2 )		// it was already busy when the attack began; it is fighting now, leave it be
				continue;

			returnXLoc = unit_array[i]->next_x_loc();
			returnYLoc = unit_array[i]->next_y_loc();
		}

		add_sent_unit(i, 0, returnXLoc, returnYLoc);

		if( rc == 1 )
			sendArray[sendCount++] = i;
		else
			fightingCount++;		// already fighting: no new orders, but it goes back afterwards
	}

	//------- soldiers and war machines in the forts, the generals stay --------//

	for( i=firm_array.size() ; i>0 ; i-- )
	{
		if( firm_array.is_deleted(i) )
			continue;

		Firm* firmPtr = firm_array[i];

		if( firmPtr->nation_recno != nationRecno || firmPtr->firm_id != FIRM_CAMP ||
			 firmPtr->under_construction || firmPtr->region_id != regionId )
		{
			continue;
		}

		FirmCamp* firmCamp = (FirmCamp*) firmPtr;

		while( firmCamp->worker_count > 0 )
		{
			int isSoldier = firmCamp->worker_array[0].skill_id == SKILL_LEADING;
			int unitRecno = firmCamp->mobilize_worker(1, COMMAND_AUTO);

			if( !unitRecno )		// no room around the fort for more units
				break;

			add_sent_unit(unitRecno, firmCamp->firm_recno, firmCamp->center_x, firmCamp->center_y);

			Unit* unitPtr = unit_array[unitRecno];

			unitPtr->team_id = unit_array.cur_team_id;

			if( isSoldier && firmCamp->overseer_recno )		// the soldiers stay under their general's command, see FirmCamp::patrol_all_soldier()
			{
				unitPtr->leader_unit_recno = firmCamp->overseer_recno;
				unitPtr->update_loyalty();
			}

			sendArray[sendCount++] = unitRecno;
		}

		unit_array.cur_team_id++;
	}

	//------- send them and select them --------//

	if( sendCount > 0 || fightingCount > 0 )
	{
		sent_x_loc = xLoc;
		sent_y_loc = yLoc;
		sent_date  = info.game_date;
		last_fight_date = info.game_date;
	}

	if( sendCount > 0 )
	{
		power.reset_selection();

		for( i=0 ; i<sendCount ; i++ )
		{
			Unit* unitPtr = unit_array[ sendArray[i] ];

			unitPtr->selected_flag = 1;
			unit_array.selected_count++;

			if( !unit_array.selected_recno )
				unit_array.selected_recno = sendArray[i];
		}

		unit_array.move_to( xLoc, yLoc, 0, sendArray, sendCount, COMMAND_AUTO );

		info.disp();
	}

	AiWarState::log( "CALLARMS    %d units sent to %d,%d, %d already fighting there (%d to send back afterwards, %d positions remembered)",
						  sendCount, xLoc, yLoc, fightingCount, sent_count, snapshot_count );

	mem_del(sendArray);

	return sendCount ? sendCount : -fightingCount;		// negative: only troops already fighting
}
//----------- End of function CallToArms::send_troops -----------//


//--------- Begin of function CallToArms::take_snapshot ---------//
//
// Remember where the player's idle troops on the land of the attack
// stand when it begins, to send them back there afterwards.
//
void CallToArms::take_snapshot(int xLoc, int yLoc)
{
	snapshot_count = 0;

	int nationRecno = nation_array.player_recno;
	int regionId = world.get_region_id(xLoc, yLoc);

	for( int i=unit_array.size() ; i>0 ; i-- )
	{
		if( unit_array.is_deleted(i) )
			continue;

		Unit* unitPtr = unit_array[i];

		if( is_call_to_arms_unit(unitPtr, nationRecno, regionId) != 1 ||		// idle or moving, not fighting
			 unitPtr->action_mode2 != ACTION_STOP )		// standing where the player put it
		{
			continue;
		}

		snapshot_array = (CallToArmsUnit*) mem_resize( snapshot_array, sizeof(CallToArmsUnit) * (snapshot_count+1) );

		CallToArmsUnit* snapUnit = snapshot_array + snapshot_count++;

		snapUnit->unit_recno = i;
		snapUnit->return_x_loc = unitPtr->next_x_loc();
		snapUnit->return_y_loc = unitPtr->next_y_loc();
		snapUnit->home_camp_recno = 0;
	}

	AiWarState::log( "CALLARMS    attack at %d,%d: remembering where %d idle troops stand", xLoc, yLoc, snapshot_count );
}
//----------- End of function CallToArms::take_snapshot -----------//


//--------- Begin of function CallToArms::get_snapshot ---------//
//
// return : <int> 1 if the unit's position at the start of the attack is known
//
int CallToArms::get_snapshot(int unitRecno, int& xLoc, int& yLoc)
{
	for( int i=0 ; i<snapshot_count ; i++ )
	{
		if( snapshot_array[i].unit_recno == unitRecno )
		{
			xLoc = snapshot_array[i].return_x_loc;
			yLoc = snapshot_array[i].return_y_loc;
			return 1;
		}
	}

	return 0;
}
//----------- End of function CallToArms::get_snapshot -----------//


//--------- Begin of function CallToArms::add_sent_unit ---------//
//
// Remember a unit answering the call, to send it back afterwards. A unit
// already sent by an earlier call keeps where it came from.
//
void CallToArms::add_sent_unit(int unitRecno, int homeCampRecno, int returnXLoc, int returnYLoc)
{
	for( int i=0 ; i<sent_count ; i++ )
	{
		if( sent_array[i].unit_recno == unitRecno )
			return;
	}

	sent_array = (CallToArmsUnit*) mem_resize( sent_array, sizeof(CallToArmsUnit) * (sent_count+1) );

	CallToArmsUnit* sentUnit = sent_array + sent_count++;

	sentUnit->unit_recno      = unitRecno;
	sentUnit->return_x_loc    = returnXLoc;
	sentUnit->return_y_loc    = returnYLoc;
	sentUnit->home_camp_recno = homeCampRecno;
}
//----------- End of function CallToArms::add_sent_unit -----------//


//--------- Begin of function CallToArms::next_day ---------//
//
// Send the troops back once the fighting is over.
//
void CallToArms::next_day()
{
	if( !sent_count )
		return;

	if( !nation_array.player_recno || nation_array.is_deleted(nation_array.player_recno) )
	{
		sent_count = 0;
		return;
	}

	if( info.game_date - last_fight_date < CALL_TO_ARMS_RETURN_QUIET_DAYS )
		return;

	//------ wait while there are enemies near ------//

	int enemyCount = 0;
	int xLoc1 = MAX(0, sent_x_loc-CALL_TO_ARMS_ENEMY_DISTANCE), xLoc2 = MIN(MAX_WORLD_X_LOC-1, sent_x_loc+CALL_TO_ARMS_ENEMY_DISTANCE);
	int yLoc1 = MAX(0, sent_y_loc-CALL_TO_ARMS_ENEMY_DISTANCE), yLoc2 = MIN(MAX_WORLD_Y_LOC-1, sent_y_loc+CALL_TO_ARMS_ENEMY_DISTANCE);
	Nation* playerNation = nation_array[nation_array.player_recno];

	for( int y=yLoc1 ; y<=yLoc2 ; y++ )
	{
		Location* locPtr = world.get_loc(xLoc1, y);

		for( int x=xLoc1 ; x<=xLoc2 ; x++, locPtr++ )
		{
			if( !locPtr->has_unit(UNIT_LAND) )
				continue;

			Unit* unitPtr = unit_array[ locPtr->unit_recno(UNIT_LAND) ];

			if( unitPtr->nation_recno == nation_array.player_recno )
				continue;

			if( unitPtr->nation_recno ? playerNation->get_relation_status(unitPtr->nation_recno) == NATION_HOSTILE
				 : (unit_res[unitPtr->unit_id]->unit_class == UNIT_CLASS_MONSTER || unitPtr->unit_mode == UNIT_MODE_REBEL) )
			{
				enemyCount++;
			}
		}
	}

	if( enemyCount > 0 )
	{
		if( info.game_date % 5 == 0 )
			AiWarState::log( "CALLARMS    the %d units sent to %d,%d wait: %d enemies near", sent_count, sent_x_loc, sent_y_loc, enemyCount );
		return;
	}

	send_back();
}
//----------- End of function CallToArms::next_day -----------//


//--------- Begin of function CallToArms::send_back ---------//
//
void CallToArms::send_back()
{
	int toFortCount = 0, toPlaceCount = 0, otherOrderCount = 0, goneCount = 0;

	for( int i=0 ; i<sent_count ; i++ )
	{
		CallToArmsUnit* sentUnit = sent_array + i;

		if( unit_array.is_deleted(sentUnit->unit_recno) )
		{
			goneCount++;
			continue;
		}

		Unit* unitPtr = unit_array[sentUnit->unit_recno];

		if( unitPtr->nation_recno != nation_array.player_recno || !unitPtr->is_visible() ||
			 unitPtr->hit_points <= 0 || unitPtr->unit_mode != 0 )
		{
			goneCount++;		// killed and replaced, betrayed, or in a building or on a ship now
			continue;
		}

		//--- units the player has given other orders to are left alone ---//

		int isAnswering = unitPtr->action_mode2 == ACTION_STOP ||
								( (unitPtr->action_mode2 == ACTION_MOVE ||
								   (unitPtr->action_mode2 >= ACTION_ATTACK_UNIT && unitPtr->action_mode2 <= ACTION_ATTACK_WALL)) &&
								  misc.points_distance(unitPtr->next_x_loc(), unitPtr->next_y_loc(), sent_x_loc, sent_y_loc) <= CALL_TO_ARMS_ENEMY_DISTANCE*2 );

		if( !isAnswering )
		{
			otherOrderCount++;
			continue;
		}

		//--- back to its fort if it still has room, otherwise to where it was ---//

		if( sentUnit->home_camp_recno && !firm_array.is_deleted(sentUnit->home_camp_recno) )
		{
			Firm* firmPtr = firm_array[sentUnit->home_camp_recno];

			if( firmPtr->nation_recno == nation_array.player_recno && firmPtr->firm_id == FIRM_CAMP &&
				 firmPtr->worker_count < MAX_WORKER && firmPtr->region_id == unitPtr->region_id() )
			{
				unitPtr->home_camp_firm_recno = sentUnit->home_camp_recno;

				if( unitPtr->return_camp() )
				{
					toFortCount++;
					continue;
				}
			}
		}

		unitPtr->move_to( sentUnit->return_x_loc, sentUnit->return_y_loc );
		toPlaceCount++;
	}

	AiWarState::log( "CALLARMS    the fighting at %d,%d is over: %d units go back to their forts, %d to where they were, %d have other orders, %d are gone",
						  sent_x_loc, sent_y_loc, toFortCount, toPlaceCount, otherOrderCount, goneCount );

	sent_count = 0;
}
//----------- End of function CallToArms::send_back -----------//


//--------- Begin of static function is_call_to_arms_unit ---------//
//
// return : <int> 1 if the unit should answer the call to arms,
//			  2 if it should but is in a fight now, 0 if it shouldn't
//
static int is_call_to_arms_unit(Unit* unitPtr, int nationRecno, int regionId)
{
	if( unitPtr->nation_recno != nationRecno || unitPtr->spy_recno || !unitPtr->is_visible() ||
		 unitPtr->hit_points <= 0 || unitPtr->unit_mode != 0 || unitPtr->mobile_type != UNIT_LAND )
	{
		return 0;
	}

	if( unitPtr->region_id() != regionId )
		return 0;

	int isTroop;

	switch( unit_res[unitPtr->unit_id]->unit_class )
	{
		case UNIT_CLASS_HUMAN:		// soldiers only: the king and generals stay to keep the villages loyal
			isTroop = unitPtr->rank_id == RANK_SOLDIER && !unitPtr->is_civilian();
			break;

		case UNIT_CLASS_WEAPON:
			isTroop = 1;
			break;

		default:
			isTroop = 0;
	}

	if( !isTroop )
		return 0;

	//--- don't pull units out of a fight ---//

	if( unitPtr->cur_action == SPRITE_ATTACK ||
		 (unitPtr->action_mode >= ACTION_ATTACK_UNIT && unitPtr->action_mode <= ACTION_ATTACK_WALL) )
	{
		return 2;
	}

	return 1;
}
//----------- End of static function is_call_to_arms_unit -----------//
