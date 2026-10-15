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

//Filename    : OAIWAR.H
//Description : War records, war weariness and strategic posture of nations

#ifndef __OAIWAR_H
#define __OAIWAR_H

#include <stdint.h>
#include <GAMEDEF.h>

class File;
class Unit;

//------- Define strategic postures of AI nations -------//

enum { AI_POSTURE_EXPAND=0,		// normal behavior, may start new wars
		 AI_POSTURE_CONSOLIDATE,	// at peace and focusing on economy, no new wars
		 AI_POSTURE_DEFEND,			// outmatched by enemies, no new offensives
		 AI_POSTURE_RECOVER,			// broke, starving or exhausted by war, seeks peace
		 AI_POSTURE_COUNT };

#define MAX_WAR_WEARINESS				100
#define WEARINESS_BLOCK_OFFENSIVE	70		// no new offensives against an enemy at or above this weariness
#define WEARINESS_FORCE_PEACE			75		// willing to cease fire even during an attack mission

//------- Define struct WarRecord -------//
//
// war_record[a-1][b-1] is nation a's view of its war with nation b.
//
#pragma pack(1)
struct WarRecord
{
	int32_t	start_date;				// 0 if the two nations are not at war
	int32_t	last_combat_date;
	int32_t	no_war_until_date;	// do not declare a new war on the other nation before this date
	float		losses;					// war value this nation lost in the current war
	float		kills;					// war value this nation destroyed in the current war
	int16_t	towns_lost;
	int16_t	towns_gained;
	int16_t	firms_lost;
	float		weariness;				// 0 to MAX_WAR_WEARINESS, persists and decays in peace time
};
#pragma pack()

//------- Define class AiWarState -------//

class AiWarState
{
public:
	WarRecord	war_record[MAX_NATION][MAX_NATION];
	char			posture[MAX_NATION];
	int32_t		posture_date[MAX_NATION];		// the date the current posture was adopted

public:
	void			init();
	void			reset_pair(int nationRecno, int withNationRecno);
	void			reset_nation(int nationRecno);

	WarRecord*	get_record(int nationRecno, int withNationRecno);
	float			weariness(int nationRecno, int withNationRecno);
	int			get_posture(int nationRecno);

	//------ event notifications ------//

	void			status_changed(int nationRecno, int withNationRecno, int oldStatus, int newStatus);
	void			unit_killed(int attackerNationRecno, Unit* targetUnit);
	void			firm_destroyed(int attackerNationRecno, int victimNationRecno);
	void			town_captured(int fromNationRecno, int toNationRecno);
	void			next_day();

	//------ AI decision helpers ------//

	int			can_start_offensive(int nationRecno, int targetNationRecno);
	int			can_declare_war(int nationRecno, int targetNationRecno);
	int			cease_war_rating(int nationRecno, int withNationRecno);

	int			write_file(File* filePtr);
	int			read_file(File* filePtr);

private:
	void			add_weariness(int nationRecno, int withNationRecno, float amount);
	void			update_posture(int nationRecno);
	int			recently_at_war(int nationRecno);
	static float unit_war_value(Unit* unitPtr);
};

extern AiWarState ai_war_state;

#endif
