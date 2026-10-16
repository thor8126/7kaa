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

//Filename    : OFIRMRAL.CPP
//Description : Rally points of forts and war factories
//
// With one of the player's forts or war factories selected (and no units
// selected), right-clicking the map sets the building's rally point. New
// weapons built in a war factory, and the soldiers and general a fort sends
// out on patrol, then move there. Right-clicking a building with units
// selected still sends the units into it. The Clear button in the
// building's interface removes the rally point.

#include <ALL.h>
#include <OFIRM.h>
#include <OUNIT.h>
#include <ONATION.h>
#include <OWORLD.h>
#include <OINFO.h>
#include <OVGA.h>
#include <vga_util.h>
#include <OFONT.h>
#include <OBUTTON.h>
#include <OREMOTE.h>
#include <OSE.h>
#include "gettext.h"

//------- Define static vars -------//

static Button button_clear_rally;

//--------- Begin of function Firm::can_set_rally_point ---------//
//
// return : <int> 1 if this kind of building can have a rally point
//
int Firm::can_set_rally_point()
{
	return firm_id == FIRM_CAMP || firm_id == FIRM_WAR_FACTORY;
}
//----------- End of function Firm::can_set_rally_point -----------//


//--------- Begin of function Firm::set_rally_point ---------//
//
// <int>  xLoc, yLoc   - the location of the rally point, -1 to clear it
// <char> remoteAction - remote action type
//
void Firm::set_rally_point(int xLoc, int yLoc, char remoteAction)
{
	if( !can_set_rally_point() )
		return;

	if( xLoc >= MAX_WORLD_X_LOC || yLoc >= MAX_WORLD_Y_LOC )
		return;

	if( xLoc < 0 || yLoc < 0 )
		xLoc = yLoc = -1;

	if( !remoteAction && remote.is_enable() )
	{
		// packet structure : <firm recno> <x loc> <y loc>
		short *shortPtr = (short *)remote.new_send_queue_msg(MSG_FIRM_SET_RALLY_POINT, 3*sizeof(short));
		shortPtr[0] = firm_recno;
		shortPtr[1] = xLoc;
		shortPtr[2] = yLoc;
		return;
	}

	rally_x_loc = xLoc;
	rally_y_loc = yLoc;

	if( firm_array.selected_recno == firm_recno )
		info.disp();
}
//----------- End of function Firm::set_rally_point -----------//


//--------- Begin of function Firm::send_to_rally_point ---------//
//
// Order units which have just come out of this building to its rally point.
// AI buildings have no rally points.
//
// <short*> unitArray - recno of the units
// <int>    unitCount - no. of units
//
void Firm::send_to_rally_point(short* unitArray, int unitCount)
{
	if( !has_rally_point() || firm_ai || unitCount <= 0 )
		return;

	//--- only units out on the map, as some may not have found room to come out ---//

	short* moveArray = (short*) mem_add( sizeof(short) * unitCount );
	int    moveCount = 0;

	for( int i=0 ; i<unitCount ; i++ )
	{
		if( unitArray[i] && !unit_array.is_deleted(unitArray[i]) && unit_array[unitArray[i]]->is_visible() )
			moveArray[moveCount++] = unitArray[i];
	}

	if( moveCount > 0 )
		unit_array.move_to( rally_x_loc, rally_y_loc, 0, moveArray, moveCount, COMMAND_AUTO );

	mem_del(moveArray);
}
//----------- End of function Firm::send_to_rally_point -----------//


//--------- Begin of function Firm::disp_rally_point ---------//
//
// Display the rally point line in the building's interface.
//
// <int> dispY1      - the top y coordinate of the line
// <int> refreshFlag - refresh flag
//
void Firm::disp_rally_point(int dispY1, int refreshFlag)
{
	if( refreshFlag != INFO_REPAINT )
		return;

	vga_util.d3_panel_up( INFO_X1, dispY1, INFO_X2, dispY1+24 );

	if( has_rally_point() )
	{
		font_san.put( INFO_X1+6, dispY1+6, _("Rally point set") );
		button_clear_rally.paint_text( INFO_X2-60, dispY1+3, INFO_X2-4, dispY1+21, _("Clear") );
	}
	else
	{
		font_san.put( INFO_X1+6, dispY1+6, _("Right-click map: rally point") );
	}
}
//----------- End of function Firm::disp_rally_point -----------//


//--------- Begin of function Firm::detect_rally_point ---------//
//
// return : <int> 1 if the Clear button has been pressed
//
int Firm::detect_rally_point()
{
	if( !has_rally_point() || !button_clear_rally.detect() )
		return 0;

	se_ctrl.immediate_sound("TURN_OFF");
	set_rally_point( -1, -1, COMMAND_PLAYER );
	return 1;
}
//----------- End of function Firm::detect_rally_point -----------//
