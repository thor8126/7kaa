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

//Filename    : OTECHRES.CPP
//Description : Tech class

#include <ALL.h>
#include <OGAME.h>
#include <OGAMESET.h>
#include <OF_RESE.h>
#include <OUNIT.h>
#include <ORESX.h>
#include <ONATION.h>
#include <OTECHRES.h>
#include "gettext.h"

//---------- #define constant ------------//

#define TECH_DB		  		"TECH"
#define TECH_CLASS_DB 	 	"TECHCLAS"
#define TECH_BITMAP_FILE	DIR_RES"I_TECH.RES"

//------ civil technologies, see CIVIL_??? in OTECHRES.H ------//
//
// Their icons are borrowed from units. The descriptions of the levels
// are shown in the research menu of the Tower of Science.
//
struct CivilTechDef
{
	const char* name;
	char  max_level;
	short complex_level;			// as in TECH.DBF: 20 for catapults to 100 for galleons
	char  parent_civil_id;
	char  parent_level;
	short icon_unit_id;
	char  icon_rank_id;
	const char* level_des[2];
};

static CivilTechDef civil_tech_def_array[CIVIL_TECH_COUNT] =
{
	{ N_("Agriculture"),     2, 30, 0,                    0, UNIT_MAYA,           RANK_SOLDIER, { N_("Villagers farm the fields"), N_("More farmers, longer seasons") } },
	{ N_("Medicine"),        2, 40, CIVIL_AGRICULTURE,    1, UNIT_PERSIAN_HEALER, RANK_SOLDIER, { N_("1 in 4 fallen soldiers saved"), N_("1 in 2 fallen soldiers saved") } },
	{ N_("Engineering"),     2, 40, 0,                    0, UNIT_BALLISTA,       RANK_SOLDIER, { N_("Village watchtowers"), N_("Village walls, stronger towers") } },
	{ N_("Industry"),        2, 50, CIVIL_ENGINEERING,    1, UNIT_EXPLOSIVE_CART, RANK_SOLDIER, { N_("Dry mines dig deeper once"), N_("Dry mines dig deeper twice") } },
	{ N_("Administration"),  2, 40, 0,                    0, UNIT_GREEK,          RANK_KING,    { N_("Village edicts"), N_("Stronger village edicts") } },
	{ N_("Commerce"),        2, 40, CIVIL_ADMINISTRATION, 1, UNIT_CARAVAN,        RANK_SOLDIER, { N_("Road tolls and interest"), N_("Higher tolls and interest") } },
	{ N_("Military Drill"),  2, 40, CIVIL_ADMINISTRATION, 1, UNIT_NORMAN,         RANK_GENERAL, { N_("Forts recruit by themselves"), N_("Forts train faster") } },
	{ N_("Navigation"),      2, 50, CIVIL_ENGINEERING,    1, UNIT_TRANSPORT,      RANK_SOLDIER, { N_("Harbors catch fish"), N_("Harbors catch more fish") } },
};

//------- Begin of function TechRes::TechRes -----------//

TechRes::TechRes()
{
	init_flag=0;
}
//--------- End of function TechRes::TechRes -----------//


//---------- Begin of function TechRes::init -----------//
//
// This function must be called after a map is generated.
//
void TechRes::init()
{
	deinit();

	//---------- init bitmap resource ---------//

	res_bitmap.init(TECH_BITMAP_FILE, 1);		// 1-read all into buffer

	//------- load database information --------//

	load_tech_class();
	load_tech_info();

	init_flag=1;
}
//---------- End of function TechRes::init -----------//


//---------- Begin of function TechRes::deinit -----------//

void TechRes::deinit()
{
	if( init_flag )
	{
		mem_del(tech_class_array);
		mem_del(tech_info_array);

		res_bitmap.deinit();

		init_flag=0;
	}
}
//---------- End of function TechRes::deinit -----------//


//------- Begin of function TechRes::load_tech_class -------//
//
// Read in information of TECHCLAS.DBF into memory array
//
void TechRes::load_tech_class()
{
	TechClassRec  *techClassRec;
	TechClass     *techClass;
	Database *dbTechClass = game_set.open_db(TECH_CLASS_DB);

	tech_class_count = (short) dbTechClass->rec_count();
	tech_class_array = (TechClass*) mem_add( sizeof(TechClass)*tech_class_count );

	//------ read in tech information array -------//

	memset( tech_class_array, 0, sizeof(TechClass) * tech_class_count );

	for( int i=0 ; i<tech_class_count ; i++ )
	{
		techClassRec = (TechClassRec*) dbTechClass->read(i+1);
		techClass    = tech_class_array+i;

		techClass->class_id   = i+1;
		techClass->icon_index = res_bitmap.get_index( misc.nullify(techClassRec->icon_name, techClassRec->ICON_NAME_LEN) );

		err_when( !techClass->icon_index );
	}
}
//--------- End of function TechRes::load_tech_class ---------//


//------- Begin of function TechRes::load_tech_info -------//
//
// Read in information of TECH.DBF into memory array
//
void TechRes::load_tech_info()
{
	TechRec  *techRec;
	TechInfo *techInfo;
	Database *dbTech = game_set.open_db(TECH_DB);

	military_tech_count = (short) dbTech->rec_count();
	tech_count      = military_tech_count + CIVIL_TECH_COUNT;		// civil technologies come after those of TECH.DBF
	tech_info_array = (TechInfo*) mem_add( sizeof(TechInfo)*tech_count );

	//------ read in tech information array -------//

	memset( tech_info_array, 0, sizeof(TechInfo) * tech_count );

	int 		  techClassId=0;
	TechClass* techClass;

	total_tech_level = 0;

	for( int i=0 ; i<military_tech_count ; i++ )
	{
		techRec  = (TechRec*) dbTech->read(i+1);
		techInfo = tech_info_array+i;

		techInfo->tech_id			 = i+1;
		techInfo->class_id = misc.atoi( techRec->class_id, techRec->ID_LEN );

		techInfo->max_tech_level = misc.atoi( techRec->max_tech_level, techRec->MAX_TECH_LEVEL_LEN );
		techInfo->complex_level = misc.atoi( techRec->complex_level, techRec->COMPLEX_LEVEL_LEN );

		techInfo->unit_id = misc.atoi( techRec->unit_id, techRec->ID_LEN );
		techInfo->firm_id = misc.atoi( techRec->firm_id, techRec->ID_LEN );
		techInfo->parent_unit_id = misc.atoi( techRec->parent_unit_id, techRec->ID_LEN );
		techInfo->parent_firm_id = misc.atoi( techRec->parent_firm_id, techRec->ID_LEN );
		techInfo->parent_level = techRec->parent_level - '0';

		if( techInfo->parent_unit_id || techInfo->parent_firm_id )
			err_when( techInfo->parent_level<1 || techInfo->parent_level>9 );

		techInfo->icon_index = res_bitmap.get_index( misc.nullify(techRec->icon_name, techRec->ICON_NAME_LEN) );

		if( techClassId != techInfo->class_id )
		{
			techClass = tech_class(techInfo->class_id);
			techClassId = techInfo->class_id;

			techClass->first_tech_id = i+1;
			techClass->tech_count = 1;
		}
		else
			techClass->tech_count++;

		total_tech_level += techInfo->max_tech_level;
	}

	//------ civil technologies ------//

	for( int i=0 ; i<CIVIL_TECH_COUNT ; i++ )
	{
		CivilTechDef* civilDef = civil_tech_def_array + i;

		techInfo = tech_info_array + military_tech_count + i;

		techInfo->tech_id        = military_tech_count + i + 1;
		techInfo->class_id       = TECH_CIVIL;
		techInfo->civil_id       = i + 1;
		techInfo->max_tech_level = civilDef->max_level;
		techInfo->complex_level  = civilDef->complex_level;

		if( civilDef->parent_civil_id )
		{
			techInfo->parent_tech_id    = military_tech_count + civilDef->parent_civil_id;
			techInfo->parent_tech_level = civilDef->parent_level;
		}

		total_tech_level += techInfo->max_tech_level;
	}
}	
//--------- End of function TechRes::load_tech_info ---------//


//---------- Begin of function TechClass::tech_icon -----------//

char* TechClass::tech_icon()
{
	return tech_res.res_bitmap.get_data(icon_index);
}

//---------- End of function TechClass::tech_icon -----------//


//---------- Begin of function TechInfo::tech_large_icon -----------//

char* TechInfo::tech_large_icon()
{
	if( civil_id )
	{
		CivilTechDef* civilDef = civil_tech_def_array + civil_id - 1;
		return unit_res[civilDef->icon_unit_id]->get_large_icon_ptr(civilDef->icon_rank_id);
	}

	if( unit_id )
		// ######## begin Gilbert 8/8 #########//
		return unit_res[unit_id]->get_large_icon_ptr(0);
		// ######## end Gilbert 8/8 #########//
	else
		return tech_res.res_bitmap.get_data(icon_index);
}
//---------- End of function TechInfo::tech_large_icon -----------//


//---------- Begin of function TechInfo::tech_small_icon -----------//

char* TechInfo::tech_small_icon()
{
	if( civil_id )
	{
		CivilTechDef* civilDef = civil_tech_def_array + civil_id - 1;
		return unit_res[civilDef->icon_unit_id]->get_small_icon_ptr(civilDef->icon_rank_id);
	}

	if( unit_id )
		// ####### begin Gilbert 17/10 #########//
		return unit_res[unit_id]->get_small_icon_ptr(RANK_SOLDIER);
		// ####### end Gilbert 17/10 #########//
	else
		return tech_res.res_bitmap.get_data(icon_index);
}
//---------- End of function TechInfo::tech_small_icon -----------//


//---------- Begin of function TechInfo::tech_des -----------//

const char* TechInfo::tech_des()
{
	if( civil_id )
		return _(civil_tech_def_array[civil_id-1].name);

	if( unit_id )
		return _(unit_res[unit_id]->name);

	else if( firm_id )
		return _(firm_res[firm_id]->name);

	else
		return "";
}
//---------- End of function TechInfo::tech_des -----------//


//---------- Begin of function TechInfo::level_des -----------//
//
// <int> techLevel - 1 or 2
//
const char* TechInfo::level_des(int techLevel)
{
	if( !civil_id || techLevel < 1 || techLevel > 2 )
		return "";

	return _(civil_tech_def_array[civil_id-1].level_des[techLevel-1]);
}
//---------- End of function TechInfo::level_des -----------//


//---------- Begin of function TechRes::civil_level -----------//
//
// return : <int> the nation's level of the given civil technology,
//			  0 if it hasn't researched it (or for no nation)
//
int TechRes::civil_level(int nationRecno, int civilId)
{
	if( !init_flag || nationRecno < 1 || nationRecno > MAX_NATION )
		return 0;

	return tech_info_array[ military_tech_count + civilId - 1 ].get_nation_tech_level(nationRecno);
}
//---------- End of function TechRes::civil_level -----------//

#ifdef DYNARRAY_DEBUG_ELEMENT_ACCESS

//---------- Begin of function TechRes::operator[] -----------//

TechInfo* TechRes::operator[](int techId)
{
	err_if( techId<1 || techId>tech_count )
		err_now( "TechRes::operator[]" );

	return tech_info_array+techId-1;
}
//------------ End of function TechRes::operator[] -----------//


//---------- Begin of function TechRes::tech_class -----------//

TechClass* TechRes::tech_class(int techClassId)
{
	err_if( techClassId<1 || techClassId>tech_count )
		err_now( "TechRes::tech_class" );

	return tech_class_array+techClassId-1;
}
//------------ End of function TechRes::tech_class -----------//

#endif

//--------- Begin of function TechRes::init_nation_tech --------//
//
// Close down all firms under this nation.
//
void TechRes::init_nation_tech(int nationRecno)
{
	int 		 i;
	TechInfo* techInfo = tech_res.tech_info_array;

	for( i=0 ; i<tech_res.tech_count ; i++, techInfo++ )
	{
		techInfo->set_nation_tech_level(nationRecno, 0);
	}
}
//----------- End of function TechRes::init_nation_tech ---------//


//--------- Begin of function TechInfo::is_parent_tech_invented --------//
//
// Whether this technology can be researched or not.
//
int TechInfo::is_parent_tech_invented(int nationRecno)
{
	if( parent_tech_id )		// a civil technology needing another one
	{
		if( tech_res[parent_tech_id]->get_nation_tech_level(nationRecno) < parent_tech_level )
			return 0;
	}

	if( parent_unit_id )
	{
		if( unit_res[parent_unit_id]->get_nation_tech_level(nationRecno) < parent_level )
			return 0;
	}

	if( parent_firm_id )
	{
		if( firm_res[parent_firm_id]->get_nation_tech_level(nationRecno) < parent_level )
			return 0;
	}

	return 1;
}
//----------- End of function TechInfo::is_parent_tech_invented ---------//


//--------- Begin of function TechInfo::can_research --------//
//
// Whether this technology can be researched or not.
//
int TechInfo::can_research(int nationRecno)
{
	return get_nation_tech_level(nationRecno) < max_tech_level &&
			 is_parent_tech_invented(nationRecno);
}
//----------- End of function TechInfo::can_research ---------//


//--------- Begin of function TechInfo::progress --------//
//
// Make a progress with this technology's research.
//
// <int>   nationRecno	 - the nation which makes progresses on the research of this technology.
// <float> progressPoint - the progress point to be added to this research
//
// return: <int> 1 - the research is completed
//					  0 - the researhc is not completed yet.
//
int TechInfo::progress(int nationRecno, float progressPoint)
{
	err_when( nationRecno<1 || nationRecno>nation_array.size() );

	nation_research_progress_array[nationRecno-1] += progressPoint;

	if( nation_research_progress_array[nationRecno-1] > 100 )
	{
		set_nation_tech_level( nationRecno, nation_tech_level_array[nationRecno-1]+1 );
		nation_research_progress_array[nationRecno-1] = (float) 0;

		return 1;
	}

	return 0;
}
//----------- End of function TechInfo::progress ---------//


//--------- Begin of function TechInfo::get_progress --------//
//
float TechInfo::get_progress(int nationRecno)
{
	err_when( nationRecno<1 || nationRecno>nation_array.size() );

	return nation_research_progress_array[nationRecno-1];
}
//----------- End of function TechInfo::get_progress ---------//


//------ Begin of function TechInfo::inc_nation_is_researching ------//
//
void TechInfo::inc_nation_is_researching(int nationRecno)
{
	err_when( nationRecno<1 || nationRecno>nation_array.size() );

	nation_is_researching_array[nationRecno-1]++;
}
//------- End of function TechInfo::inc_nation_is_researching ------//


//------ Begin of function TechInfo::dec_nation_is_researching ------//
//
void TechInfo::dec_nation_is_researching(int nationRecno)
{
	err_when( nationRecno<1 || nationRecno>nation_array.size() );

	nation_is_researching_array[nationRecno-1]--;

	err_when( nation_is_researching_array[nationRecno-1] < 0 );
}
//------- End of function TechInfo::dec_nation_is_researching ------//


//------ Begin of function TechInfo::set_nation_tech_level ------//
//
// Set the nation's tech level on this technology.
//
void TechInfo::set_nation_tech_level(int nationRecno, int techLevel)
{
	err_when( nationRecno<1 || nationRecno>nation_array.size() );
	err_when( techLevel > max_tech_level );

	nation_tech_level_array[nationRecno-1] = techLevel;

	if( unit_id )
		unit_res[unit_id]->set_nation_tech_level( nationRecno, techLevel );

	else if( firm_id )
		firm_res[firm_id]->set_nation_tech_level( nationRecno, techLevel );

	//--- if the MAX level has been reached and there are still other firms researching this technology ---//

	if( techLevel == max_tech_level && is_nation_researching(nationRecno) > 0 )
	{
		//---- stop other firms researching the same tech -----//

		Firm* firmPtr;

		for( int i=firm_array.size() ; i>0 ; i-- )
		{
			if( firm_array.is_deleted(i) )
				continue;

			firmPtr = firm_array[i];

			if( firmPtr->firm_id == FIRM_RESEARCH &&
				 firmPtr->nation_recno == nationRecno &&
				 ((FirmResearch*)firmPtr)->tech_id == tech_id )
			{
				((FirmResearch*)firmPtr)->terminate_research();
			}
		}
	}
}
//------- End of function TechInfo::set_nation_tech_level -------//


//---------- Begin of function TechRes::inc_all_tech_level -----------//
//
// One of the cheating functions - increase the levels of all technology
// by one level for the specific nation.
//
void TechRes::inc_all_tech_level(int nationRecno)
{
	int 		 curTechLevel;
	TechInfo* techInfo = tech_res.tech_info_array;

	for( int i=1 ; i<=tech_count ; i++, techInfo++ )
	{
		curTechLevel = techInfo->get_nation_tech_level(nationRecno);

		if( curTechLevel < techInfo->max_tech_level )
			techInfo->set_nation_tech_level( nationRecno, curTechLevel+1 );
	}
}
//------------ End of function TechRes::inc_all_tech_level -----------//

