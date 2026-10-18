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

//------- Define opening strategies of AI nations -------//
//
// Chosen from the nation's surroundings and personality, and
// re-evaluated every year.
//
enum { AI_STRATEGY_ECONOMY=0,		// raw materials nearby: mines, factories and trade first
		 AI_STRATEGY_EXPAND,			// capturable villages nearby: forts and captures first
		 AI_STRATEGY_MILITARY,		// a rival kingdom close by land: army first
		 AI_STRATEGY_TECH,			// safe from rivals: research and war machines
		 AI_STRATEGY_NAVAL,			// small home island: harbors and ships
		 AI_STRATEGY_COUNT };

//------ the lasting style of an AI nation, chosen at the start of a game, see Nation::init_style() ------//

enum { AI_STYLE_WARLORD=0,			// fights often, accepts worse odds, raids and feints
		 AI_STYLE_MERCHANT,			// trade and treaties first, a war only for a good reason
		 AI_STYLE_BUILDER,			// economy, research and defenses first
		 AI_STYLE_SCHEMER,			// buys time with peace pacts and breaks them when strong enough
		 AI_STYLE_OPPORTUNIST,		// strikes when an enemy is weak or busy elsewhere
		 AI_STYLE_COUNT };

#define MAX_GRUDGE						100
#define MAX_ODDS_EXTRA_PERCENT		60		// the most a nation adds to the odds it wants after failed offensives
#define MIN_ODDS_EXTRA_PERCENT		-20

#define MAX_WAR_WEARINESS				100
#define WEARINESS_BLOCK_OFFENSIVE	70		// no new offensives against an enemy at or above this weariness
#define WEARINESS_FORCE_PEACE			75		// willing to cease fire even during an attack mission
#define WEARINESS_AFTER_PEACE_REFUSED	45		// a nation whose request for peace is refused rallies to fight on, see AiWarState::peace_refused()

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

//------ what a nation remembers of another one, kept after their wars ------//

struct AiMemory
{
	float		grudge;							// 0 to MAX_GRUDGE: the harm the other nation did to it, fades over the years (revenge)
	int16_t	lost_town_x, lost_town_y;		// where it last lost a town to the other nation, -1 if never (it comes back for it)
	int16_t	weak_spot_x, weak_spot_y;		// where its last offensive on the other nation won, -1 if never
};

//------ what a nation learns from its offensives and tactics ------//

struct AiLearning
{
	int16_t	offensives_won;
	int16_t	offensives_lost;
	int16_t	odds_extra_percent;			// added to the odds it wants before attacking: up after a defeat, down after a victory
	int16_t	feints_tried;
	int16_t	feints_paid_off;				// the main attack the feint covered won
	int16_t	feint_pending;					// 1 while the current offensive is covered by a feint
	int16_t	raids_tried;
	int16_t	raids_paid_off;				// the raided building was destroyed
	int16_t	raid_target_firm_recno;		// the building of the last raid, to see if it paid off, 0 if none
	int32_t	raid_date;
	int16_t	scheme_nation_recno;			// a schemer's planned victim, kept at peace by a pact for now, 0 if none
	int32_t	scheme_date;
	int16_t	offensive_nation_recno;		// the target of the current offensive, see Nation::think_capture_new_enemy_town()
	int16_t	offensive_x_loc, offensive_y_loc;
};
#pragma pack()

//------- Define class AiWarState -------//

class AiWarState
{
public:
	WarRecord	war_record[MAX_NATION][MAX_NATION];
	char			posture[MAX_NATION];
	int32_t		posture_date[MAX_NATION];		// the date the current posture was adopted
	char			strategy[MAX_NATION];
	int32_t		strategy_date[MAX_NATION];		// the date the strategy was last evaluated, 0 if never

	char			style[MAX_NATION];				// AI_STYLE_???
	float			recent_losses[MAX_NATION];		// the war value a nation lost lately to anyone, fades in weeks (it just lost a battle)
	AiMemory		memory[MAX_NATION][MAX_NATION];	// [nation-1][other nation-1]
	AiLearning	learning[MAX_NATION];

	//------ not saved ------//

	short			current_opportunity[MAX_NATION];	// the opportunity a nation sees for its current offensive, see Nation::think_war_offensive()

public:
	void			init();
	void			reset_pair(int nationRecno, int withNationRecno);
	void			reset_nation(int nationRecno);

	WarRecord*	get_record(int nationRecno, int withNationRecno);
	float			weariness(int nationRecno, int withNationRecno);
	int			get_posture(int nationRecno);
	int			get_strategy(int nationRecno);
	int			get_style(int nationRecno);
	void			set_style(int nationRecno, int styleId);
	static const char* style_name(int styleId);
	AiMemory*	get_memory(int nationRecno, int withNationRecno);
	AiLearning*	get_learning(int nationRecno);
	int			attack_odds_percent(int nationRecno);

	//------ event notifications ------//

	void			status_changed(int nationRecno, int withNationRecno, int oldStatus, int newStatus);
	void			unit_killed(int attackerNationRecno, Unit* targetUnit);
	void			firm_destroyed(int attackerNationRecno, int victimNationRecno);
	void			town_captured(int fromNationRecno, int toNationRecno, int townXLoc, int townYLoc);
	void			offensive_result(int nationRecno, int targetNationRecno, int won, int xLoc, int yLoc, const char* reasonStr);
	void			add_grudge(int nationRecno, int againstNationRecno, float amount);
	void			peace_refused(int nationRecno, int refusingNationRecno);
	void			next_day();

	//------ AI decision helpers ------//

	int			can_start_offensive(int nationRecno, int targetNationRecno);
	int			can_declare_war(int nationRecno, int targetNationRecno, int ignorePeacePact=0);
	static int	weighted_pick(const int* ratingArray, int count, int closePercent);
	int			cease_war_rating(int nationRecno, int withNationRecno);

	int			write_file(File* filePtr);
	int			read_file(File* filePtr, int hasStrategy);

	//------ optional event log for tuning, enabled by the SKAI_LOG environment variable ------//

	static void	log(const char* format, ...);

private:
	void			add_weariness(int nationRecno, int withNationRecno, float amount);
	void			update_posture(int nationRecno);
	int			recently_at_war(int nationRecno);
	void			update_strategy(int nationRecno);
	void			rate_strategies(int nationRecno, int* ratingArray);
	static float unit_war_value(Unit* unitPtr);
};

extern AiWarState ai_war_state;

#endif
