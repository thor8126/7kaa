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

//Filename    : OCALLARM.H
//Description : Call to arms, see OCALLARM.CPP

#ifndef __OCALLARM_H
#define __OCALLARM_H

//------------- Define constants --------------//

#define CALL_TO_ARMS_ACTIVE_DAYS		20		// the Defend button is shown for this many days after an attack
#define CALL_TO_ARMS_REPEAT_DAYS		15		// attacks this soon after the last alert and near it don't make a new message
#define CALL_TO_ARMS_REPEAT_DISTANCE	25

//------------- Define class CallToArms --------------//

class CallToArms
{
public:
	short	alert_x_loc, alert_y_loc;		// where the player was last attacked
	int	alert_date;							// 0 if there is no alert

public:
	CallToArms()		{ alert_date = 0; alert_x_loc = alert_y_loc = 0; }

	void	reset()			{ alert_date = 0; }
	void	alert(int xLoc, int yLoc, int firmId, int townNameId);
	int	is_active();
	void	execute(char remoteAction);
	int	send_troops(int xLoc, int yLoc);
};

extern CallToArms call_to_arms;

//--------------------------------------------//

#endif
