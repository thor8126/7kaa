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

//Filename    : OREBUILD.H
//Description : Rebuilding the player's destroyed buildings, see OREBUILD.CPP

#ifndef __OREBUILD_H
#define __OREBUILD_H

class Firm;

//------------- Define constants --------------//

#define MAX_REBUILD_FIRM		20		// buildings waiting to be rebuilt
#define REBUILD_EXPIRE_DAYS	365	// a building not rebuilt within this time is forgotten

//------------- Define struct RebuildFirm --------------//

struct RebuildFirm
{
	char	firm_id;
	short	x_loc, y_loc;
	int	destroyed_date;
};

//------------- Define class RebuildList --------------//

class RebuildList
{
public:
	RebuildFirm	rebuild_array[MAX_REBUILD_FIRM];
	int			rebuild_count;

public:
	RebuildList()		{ rebuild_count = 0; }

	void	reset()			{ rebuild_count = 0; }
	void	add(Firm* firmPtr);
	void	next_day();

private:
	int	rebuild(RebuildFirm* rebuildFirm);
	void	del(int i);
};

extern RebuildList rebuild_list;

//--------------------------------------------//

#endif
