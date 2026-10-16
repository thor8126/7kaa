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
#define CALL_TO_ARMS_RETURN_QUIET_DAYS	5		// the troops go back after this many days without attacks there
#define CALL_TO_ARMS_ENEMY_DISTANCE		12		// ... and no enemies this close

//------------- Define struct CallToArmsUnit --------------//

struct CallToArmsUnit
{
	short	unit_recno;
	short	return_x_loc, return_y_loc;	// where the unit was when it was called
	short	home_camp_recno;					// the fort it came out of, 0 if it was out on the map
};

//------------- Define class CallToArms --------------//

class CallToArms
{
public:
	short	alert_x_loc, alert_y_loc;		// where the player was last attacked
	int	alert_date;							// 0 if there is no alert
	char	alert_answered;					// the player has pressed Defend for this alert

	//------ the troops sent, to send them back afterwards ------//

	CallToArmsUnit* sent_array;
	int	sent_count;

	CallToArmsUnit* snapshot_array;		// where the idle troops stood when the attack began
	int	snapshot_count;
	short	sent_x_loc, sent_y_loc;			// where they were sent
	int	sent_date;
	int	last_fight_date;					// the last attack on the player near where they were sent

public:
	CallToArms();
	~CallToArms();

	void	reset();
	void	alert(int xLoc, int yLoc, int firmId, int townNameId);
	int	is_active();
	void	execute(char remoteAction);
	int	send_troops(int xLoc, int yLoc);
	void	next_day();

private:
	void	add_sent_unit(int unitRecno, int homeCampRecno, int returnXLoc, int returnYLoc);
	void	take_snapshot(int xLoc, int yLoc);
	int	get_snapshot(int unitRecno, int& xLoc, int& yLoc);
	void	send_back();
};

extern CallToArms call_to_arms;

//--------------------------------------------//

#endif
