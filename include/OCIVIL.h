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

//Filename    : OCIVIL.H
//Description : The game mechanics of the civil technologies

#ifndef __OCIVIL_H
#define __OCIVIL_H

class Unit;
class Town;
class Nation;
class NationBase;
class FirmMine;
class FirmCamp;

//------- Define edicts, see Administration -------//

enum { EDICT_NONE=0,
		 EDICT_FESTIVAL,			// costs cash, the villagers are more loyal
		 EDICT_LEVY,				// the peasants produce double food, the villagers are less loyal
		 EDICT_MILITIA,			// more and stronger defenders when the village is attacked
		 EDICT_TYPE_COUNT = EDICT_MILITIA
	  };

//------- Define why a village has no farmers -------//

enum { FARM_BLOCK_NO_AGRICULTURE=1, FARM_BLOCK_OFF, FARM_BLOCK_SEASON, FARM_BLOCK_DANGER, FARM_BLOCK_NO_ROOM };

//------- Define constants -------//

#define EDICT_DAYS					365		// an edict lasts a year
#define WATCHTOWER_COST				800
#define WALL_COST						1200		// the walls around a village, Engineering II
#define MAX_MEDIC_EVACUATION		50			// fallen soldiers being carried back to forts at the same time

//------- Define class CivilTech -------//

class CivilTech
{
public:
	short	medic_unit_array[MAX_MEDIC_EVACUATION];	// Medicine: fallen soldiers to be carried back to a fort, not saved with the game
	short	medic_camp_array[MAX_MEDIC_EVACUATION];
	int	medic_count;

public:
	void	reset();
	void	next_day();
	void	ai_think(Nation* nationPtr);

	//------ Agriculture: farmers ------//

	int	is_farm_season(int nationRecno);
	int	farming_block_reason(Town* townPtr);
	int	wanted_farmer_count(Town* townPtr);
	void	release_farmer(Unit* unitPtr, const char* reason);
	void	draw_town_extras(Town* townPtr, int displayLayer);		// the fields being farmed and the watchtower

	//------ Medicine: fallen soldiers carried back ------//

	int	medics_try_save(Unit* unitPtr);
	void	process_medics();
	int	medic_recovery_bonus(int nationRecno);

	//------ Engineering: village watchtowers ------//

	int	can_build_watchtower(Town* townPtr, int checkCash=1);
	void	build_watchtower(Town* townPtr);
	int	wall_section_count(Town* townPtr, int* avgHitPoints=0, int* ringLength=0);
	int	has_walls(Town* townPtr);
	int	can_build_walls(Town* townPtr, int checkCash=1);
	void	build_walls(Town* townPtr);

	//------ Industry: deep mines ------//

	int	deep_dig(FirmMine* firmMine);

	//------ Administration: village edicts ------//

	int	edict_cost(Town* townPtr, int edictId);
	int	can_issue_edict(Town* townPtr, int edictId, int checkCash=1);
	void	issue_edict(Town* townPtr, int edictId);
	int	active_edict(Town* townPtr);
	int	edict_loyalty_change(Town* townPtr);
	int	edict_food_multiplier(Town* townPtr);
	int	edict_militia_combat_bonus(Town* townPtr);
	static const char* edict_name(int edictId);

	//------ Commerce: tolls and interest ------//

	void	pay_interest(NationBase* nationPtr);

	//------ Military Drill: forts recruit by themselves ------//

	void	auto_recruit(FirmCamp* firmCamp);
	int	train_speed_percent(int nationRecno);

	//------ Navigation: fishing and faster ships ------//

	int	ship_extra_step(Unit* unitPtr);

private:
	void	process_farming();
	void	send_farmer(Town* townPtr);
	void	recall_farmers(Town* townPtr, int recallCount);
	int	find_field_loc(Town* townPtr, int& xLoc, int& yLoc);

	int	find_medic_camp(Unit* unitPtr);

	void	process_watchtowers();
	int	is_watchtower_target(Town* townPtr, Unit* unitPtr);

	void	process_edicts();
	void	process_tolls();
	void	process_fishing();
};

extern CivilTech civil_tech;

//-------------------------------------//

#endif
