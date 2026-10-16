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
// The alert is not saved with the game.

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
#include "gettext.h"

//------- Define global variables -------//

CallToArms call_to_arms;

//------- Define static functions -------//

static int is_call_to_arms_unit(Unit* unitPtr, int nationRecno, int regionId);

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

	if( !isRepeat )
	{
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
	return alert_date && nation_array.player_recno &&
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
		reset();
		return;
	}

	int sentCount = send_troops(alert_x_loc, alert_y_loc);

	reset();

	if( !sentCount )
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

	for( i=unit_array.size() ; i>0 ; i-- )
	{
		if( unit_array.is_deleted(i) )
			continue;

		if( is_call_to_arms_unit(unit_array[i], nationRecno, regionId) )
			sendArray[sendCount++] = i;
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

	mem_del(sendArray);

	return sendCount;
}
//----------- End of function CallToArms::send_troops -----------//


//--------- Begin of static function is_call_to_arms_unit ---------//
//
// return : <int> 1 if the unit should answer the call to arms
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

	//--- don't pull units out of a fight ---//

	if( unitPtr->cur_action == SPRITE_ATTACK ||
		 (unitPtr->action_mode >= ACTION_ATTACK_UNIT && unitPtr->action_mode <= ACTION_ATTACK_WALL) )
	{
		return 0;
	}

	switch( unit_res[unitPtr->unit_id]->unit_class )
	{
		case UNIT_CLASS_HUMAN:		// soldiers only: the king and generals stay to keep the villages loyal
			return unitPtr->rank_id == RANK_SOLDIER && !unitPtr->is_civilian();

		case UNIT_CLASS_WEAPON:
			return 1;
	}

	return 0;
}
//----------- End of static function is_call_to_arms_unit -----------//
