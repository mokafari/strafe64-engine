/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
//
/*
=======================================================================

MAIN MENU

=======================================================================
*/


#include "ui_local.h"


#define ID_SINGLEPLAYER			10
#define ID_MULTIPLAYER			11
#define ID_SETUP				12
#define ID_DEMOS				13
#define ID_CINEMATICS			14
#define ID_TEAMARENA		15
#define ID_MODS					16
#define ID_EXIT					17

// STRAFE 64 game modes (each launches a strafegen course with the right cvars)
#define ID_S64_DAILY			20
#define ID_S64_TRIAL			21
#define ID_S64_VECTOR			22
#define ID_S64_LATTICE			23
#define ID_S64_PRACTICE			24

// flagship strafegen courses, kept in baseoa as .pk3
#define S64_MAP_TOWER			"strafe64_1337_x3"
#define S64_MAP_TRIAL			"strafe64_1337"
#define S64_MAP_ARENA			"strafe64dm_1337"
#define S64_MAP_LATTICE			"lattice_arena_64"
#define S64_MAP_PRACTICE		"strafe64_7"

#define MAIN_BANNER_MODEL				"models/mapobjects/banner/banner5.md3"
#define MAIN_MENU_VERTICAL_SPACING		30
#define MAIN_MENU_SYSTEM_SPACING		24

// STRAFE 64 — NERV/MAGI "structural collapse" identity for the main menu
static vec4_t s64_grid   = { 0.10f,  0.55f,  0.22f, 0.16f };  // MAGI green gridlines
static vec4_t s64_amber  = { 1.00f,  0.62f,  0.05f, 1.00f };  // menu item amber
static vec4_t s64_green  = { 0.45f,  1.00f,  0.55f, 1.00f };  // terminal green
static vec4_t s64_redhot = { 1.00f,  0.16f,  0.22f, 1.00f };  // warning red
static vec4_t s64_dim    = { 0.45f,  0.55f,  0.55f, 1.00f };  // dim label

/*
===============
Strafe64_WarningBar

Top NERV header: a pulsing red alert strip and a live countdown to
structural collapse — the rising void, dressed as a system alarm.
===============
*/
static void Strafe64_WarningBar( void ) {
	vec4_t	bar = { 0.55f, 0.04f, 0.09f, 0.0f };
	vec4_t	hot;
	int		secs;
	float	pulse;
	char	buf[64];

	pulse = 0.45f + 0.35f * (float)sin( (double)uis.realtime / 180.0 );
	bar[3] = 0.35f + 0.25f * pulse;
	UI_FillRect( 12, 14, 616, 22, bar );

	hot[0] = 1.0f; hot[1] = 0.16f + 0.2f * pulse; hot[2] = 0.22f; hot[3] = 1.0f;
	UI_DrawString( 22, 20, "WARNING", UI_LEFT|UI_SMALLFONT, hot );
	UI_DrawString( 110, 20, "STRUCTURAL INTEGRITY FAILING",
		UI_LEFT|UI_SMALLFONT, s64_dim );

	// counts down, then loops — it is never not falling
	secs = 599999 - ( ( uis.realtime / 1000 ) % 600000 );
	if ( secs < 0 ) {
		secs = 0;
	}
	Com_sprintf( buf, sizeof( buf ), "T-COLLAPSE %02i:%02i:%02i",
		secs / 3600, ( secs / 60 ) % 60, secs % 60 );
	UI_DrawString( 618, 20, buf, UI_RIGHT|UI_SMALLFONT, hot );
}

/*
===============
Strafe64_Title

Chromatic-split STRAFE 64 wordmark + MAGI subtitle.
===============
*/
static void Strafe64_Title( void ) {
	vec4_t	cyan = { 0.20f, 0.95f, 1.00f, 0.55f };
	vec4_t	red  = { 1.00f, 0.18f, 0.30f, 0.55f };
	float	j;

	// glitch jitter: the wordmark stutters a couple px every ~2 s
	j = ( ( uis.realtime / 90 ) % 22 == 0 ) ? 2.0f : 0.0f;

	UI_DrawProportionalString( 320 - 3 + j, 70, "STRAFE 64",
		UI_CENTER, red );
	UI_DrawProportionalString( 320 + 3, 70, "STRAFE 64",
		UI_CENTER, cyan );
	UI_DrawProportionalString( 320, 70, "STRAFE 64",
		UI_CENTER, s64_amber );

	UI_DrawString( 320, 112, "MAGI-01  / /  VOID PROTOCOL  / /  SPEED IS LIFE",
		UI_CENTER|UI_SMALLFONT, s64_green );

	// EVANGELION-style status block under the wordmark
	UI_DrawString( 320, 138, "EVACUATE BY MOVEMENT - THE FLOOR IS NEVER SAFE",
		UI_CENTER|UI_SMALLFONT, s64_dim );
}

/*
===============
Strafe64_Divider

A dim MAGI rule + label that separates the header from the operation
list, killing the dead space above the menu.
===============
*/
static void Strafe64_Divider( void ) {
	UI_FillRect( 180, 172, 280, 1, s64_grid );
	UI_DrawString( 320, 176, "[  SELECT  OPERATION  ]",
		UI_CENTER|UI_SMALLFONT, s64_green );
}

/*
===============
Strafe64_Footer

Bottom status line + a NERV-style stamp.
===============
*/
static void Strafe64_Footer( void ) {
	UI_DrawString( 22, 452, "SYS 6.66   //   baseoa   //   vm_game 0   //   NERV",
		UI_LEFT|UI_SMALLFONT, s64_green );
	UI_DrawString( 618, 452, "PROJECT  No.666", UI_RIGHT|UI_SMALLFONT, s64_redhot );
}


typedef struct {
	menuframework_s	menu;

	menutext_s		daily;
	menutext_s		trial;
	menutext_s		vector;
	menutext_s		lattice;
	menutext_s		practice;
	menutext_s		multiplayer;
	menutext_s		setup;
	menutext_s		mods;
	menutext_s		exit;

	qhandle_t		bannerModel;
} mainmenu_t;


static mainmenu_t s_main;

typedef struct {
	menuframework_s menu;	
	char errorMessage[4096];
} errorMessage_t;

static errorMessage_t s_errorMessage;

/*
=================
MainMenu_ExitAction
=================
*/
static void MainMenu_ExitAction( qboolean result ) {
	if( !result ) {
		return;
	}
	UI_PopMenu();
	UI_CreditMenu();
}



/*
=================
Main_MenuEvent
=================
*/
void Main_MenuEvent (void* ptr, int event) {
	if( event != QM_ACTIVATED ) {
		return;
	}

	switch( ((menucommon_s*)ptr)->id ) {
	// ---- STRAFE 64 game modes -----------------------------------------
	// each sets the latched mode cvars (applied on the map load that
	// follows) then drops straight into a strafegen course
	case ID_S64_DAILY:
		trap_Cvar_SetValue( "g_lattice", 0 );
		trap_Cvar_SetValue( "g_vectorgun", 0 );
		trap_Cvar_SetValue( "g_voidRise", 1 );
		trap_Cmd_ExecuteText( EXEC_APPEND, "map " S64_MAP_TOWER "\n" );
		break;

	case ID_S64_TRIAL:
		trap_Cvar_SetValue( "g_lattice", 0 );
		trap_Cvar_SetValue( "g_vectorgun", 0 );
		trap_Cvar_SetValue( "g_voidRise", 1 );
		trap_Cmd_ExecuteText( EXEC_APPEND, "map " S64_MAP_TRIAL "\n" );
		break;

	case ID_S64_VECTOR:
		// one gun, no void — speed is the whole weapon. fill with bots
		trap_Cvar_SetValue( "g_lattice", 0 );
		trap_Cvar_SetValue( "g_vectorgun", 1 );
		trap_Cvar_SetValue( "g_voidRise", 0 );
		trap_Cvar_Set( "bot_minplayers", "3" );
		trap_Cmd_ExecuteText( EXEC_APPEND, "map " S64_MAP_ARENA "\n" );
		break;

	case ID_S64_LATTICE:
		// last pilot standing: every pilot carves a damaging speed-trail, and
		// the collapsing floor (auto-void) flushes the survivors together. The
		// full FFA-N bracket plays the field down to one champion. g_lattice /
		// g_voidRise / g_gametype / g_latticeBracket are latched — they take
		// effect on the map load queued below.
		trap_Cvar_SetValue( "g_lattice", 1 );
		trap_Cvar_SetValue( "g_vectorgun", 0 );
		trap_Cvar_SetValue( "g_voidRise", 1 );
		trap_Cvar_SetValue( "g_latticeBracket", 1 );
		trap_Cvar_SetValue( "g_gametype", 0 );			// FFA
		trap_Cvar_Set( "bot_enable", "1" );
		trap_Cvar_Set( "bot_minplayers", "5" );			// seat a real bracket
		trap_Cmd_ExecuteText( EXEC_APPEND, "map " S64_MAP_LATTICE "\n" );
		break;

	case ID_S64_PRACTICE:
		// the course, the timer, the ghost — but the floor won't kill you
		trap_Cvar_SetValue( "g_lattice", 0 );
		trap_Cvar_SetValue( "g_vectorgun", 0 );
		trap_Cvar_SetValue( "g_voidRise", 0 );
		trap_Cmd_ExecuteText( EXEC_APPEND, "map " S64_MAP_PRACTICE "\n" );
		break;

	// ---- system --------------------------------------------------------
	case ID_MULTIPLAYER:
		UI_ArenaServersMenu();
		break;

	case ID_SETUP:
		UI_SetupMenu();
		break;

	case ID_MODS:
		UI_ModsMenu();
		break;

	case ID_EXIT:
		UI_ConfirmMenu( "EXIT GAME?", 0, MainMenu_ExitAction );
		break;
	}
}


/*
===============
MainMenu_Cache
===============
*/
void MainMenu_Cache( void ) {
	s_main.bannerModel = trap_R_RegisterModel( MAIN_BANNER_MODEL );
}

sfxHandle_t ErrorMessage_Key(int key)
{
	trap_Cvar_Set( "com_errorMessage", "" );
	UI_MainMenu();
	return (menu_null_sound);
}

/*
===============
Main_MenuDraw
TTimo: this function is common to the main menu and errorMessage menu
===============
*/

static void Main_MenuDraw( void ) {
	// the framework already painted the shared NERV backdrop; layer the
	// main-screen header on top
	Strafe64_WarningBar();
	Strafe64_Title();
	Strafe64_Divider();

	if (strlen(s_errorMessage.errorMessage))
	{
		UI_DrawProportionalString_AutoWrapped( 320, 220, 600, 20, s_errorMessage.errorMessage, UI_CENTER|UI_SMALLFONT|UI_DROPSHADOW, s64_redhot );
	}
	else
	{
		// standard menu drawing
		Menu_Draw( &s_main.menu );
	}

	Strafe64_Footer();
}


/*
===============
UI_MainMenu

The main menu only comes up when not in a game,
so make sure that the attract loop server is down
and that local cinematics are killed
===============
*/
void UI_MainMenu( void ) {
	int		y;
	int		style = UI_CENTER | UI_DROPSHADOW;
	int		sysstyle = UI_CENTER | UI_DROPSHADOW | UI_SMALLFONT;

	trap_Cvar_Set( "sv_killserver", "1" );

	if( !uis.demoversion && !ui_cdkeychecked.integer ) {
		char	key[17];

		trap_GetCDKey( key, sizeof(key) );
		if( trap_VerifyCDKey( key, NULL ) == qfalse ) {
			UI_CDKeyMenu();
			return;
		}
	}
	
	memset( &s_main, 0 ,sizeof(mainmenu_t) );
	memset( &s_errorMessage, 0 ,sizeof(errorMessage_t) );

	// com_errorMessage would need that too
	MainMenu_Cache();
	
	trap_Cvar_VariableStringBuffer( "com_errorMessage", s_errorMessage.errorMessage, sizeof(s_errorMessage.errorMessage) );
	if (strlen(s_errorMessage.errorMessage))
	{	
		s_errorMessage.menu.draw = Main_MenuDraw;
		s_errorMessage.menu.key = ErrorMessage_Key;
		s_errorMessage.menu.fullscreen = qtrue;
		s_errorMessage.menu.wrapAround = qtrue;
		s_errorMessage.menu.showlogo = qtrue;		

		trap_Key_SetCatcher( KEYCATCH_UI );
		uis.menusp = 0;
		UI_PushMenu ( &s_errorMessage.menu );
		
		return;
	}

	s_main.menu.draw = Main_MenuDraw;
	s_main.menu.fullscreen = qtrue;
	s_main.menu.wrapAround = qtrue;
	s_main.menu.showlogo = qfalse;

	// ---- STRAFE 64 game modes (the heroes: big amber) ------------------
	y = 200;
	s_main.daily.generic.type				= MTYPE_PTEXT;
	s_main.daily.generic.flags				= QMF_CENTER_JUSTIFY|QMF_PULSEIFFOCUS;
	s_main.daily.generic.x					= 320;
	s_main.daily.generic.y					= y;
	s_main.daily.generic.id					= ID_S64_DAILY;
	s_main.daily.generic.callback			= Main_MenuEvent;
	s_main.daily.string						= "DAILY RUN";
	s_main.daily.color						= s64_amber;
	s_main.daily.style						= style;

	y += MAIN_MENU_VERTICAL_SPACING;
	s_main.trial.generic.type				= MTYPE_PTEXT;
	s_main.trial.generic.flags				= QMF_CENTER_JUSTIFY|QMF_PULSEIFFOCUS;
	s_main.trial.generic.x					= 320;
	s_main.trial.generic.y					= y;
	s_main.trial.generic.id					= ID_S64_TRIAL;
	s_main.trial.generic.callback			= Main_MenuEvent;
	s_main.trial.string						= "TIME TRIAL";
	s_main.trial.color						= s64_amber;
	s_main.trial.style						= style;

	y += MAIN_MENU_VERTICAL_SPACING;
	s_main.vector.generic.type				= MTYPE_PTEXT;
	s_main.vector.generic.flags				= QMF_CENTER_JUSTIFY|QMF_PULSEIFFOCUS;
	s_main.vector.generic.x					= 320;
	s_main.vector.generic.y					= y;
	s_main.vector.generic.id				= ID_S64_VECTOR;
	s_main.vector.generic.callback			= Main_MenuEvent;
	s_main.vector.string					= "VECTORGUN";
	s_main.vector.color						= s64_amber;
	s_main.vector.style						= style;

	y += MAIN_MENU_VERTICAL_SPACING;
	s_main.lattice.generic.type				= MTYPE_PTEXT;
	s_main.lattice.generic.flags			= QMF_CENTER_JUSTIFY|QMF_PULSEIFFOCUS;
	s_main.lattice.generic.x				= 320;
	s_main.lattice.generic.y				= y;
	s_main.lattice.generic.id				= ID_S64_LATTICE;
	s_main.lattice.generic.callback			= Main_MenuEvent;
	s_main.lattice.string					= "LATTICE";
	s_main.lattice.color					= s64_amber;
	s_main.lattice.style					= style;

	y += MAIN_MENU_VERTICAL_SPACING;
	s_main.practice.generic.type			= MTYPE_PTEXT;
	s_main.practice.generic.flags			= QMF_CENTER_JUSTIFY|QMF_PULSEIFFOCUS;
	s_main.practice.generic.x				= 320;
	s_main.practice.generic.y				= y;
	s_main.practice.generic.id				= ID_S64_PRACTICE;
	s_main.practice.generic.callback		= Main_MenuEvent;
	s_main.practice.string					= "PRACTICE";
	s_main.practice.color					= s64_amber;
	s_main.practice.style					= style;

	// ---- system (smaller, dim green) -----------------------------------
	y += MAIN_MENU_VERTICAL_SPACING + 12;
	s_main.multiplayer.generic.type			= MTYPE_PTEXT;
	s_main.multiplayer.generic.flags		= QMF_CENTER_JUSTIFY|QMF_PULSEIFFOCUS;
	s_main.multiplayer.generic.x			= 320;
	s_main.multiplayer.generic.y			= y;
	s_main.multiplayer.generic.id			= ID_MULTIPLAYER;
	s_main.multiplayer.generic.callback		= Main_MenuEvent;
	s_main.multiplayer.string				= "MULTIPLAYER";
	s_main.multiplayer.color				= s64_dim;
	s_main.multiplayer.style				= sysstyle;

	y += MAIN_MENU_SYSTEM_SPACING;
	s_main.setup.generic.type				= MTYPE_PTEXT;
	s_main.setup.generic.flags				= QMF_CENTER_JUSTIFY|QMF_PULSEIFFOCUS;
	s_main.setup.generic.x					= 320;
	s_main.setup.generic.y					= y;
	s_main.setup.generic.id					= ID_SETUP;
	s_main.setup.generic.callback			= Main_MenuEvent;
	s_main.setup.string						= "SETUP";
	s_main.setup.color						= s64_dim;
	s_main.setup.style						= sysstyle;

	y += MAIN_MENU_SYSTEM_SPACING;
	s_main.mods.generic.type				= MTYPE_PTEXT;
	s_main.mods.generic.flags				= QMF_CENTER_JUSTIFY|QMF_PULSEIFFOCUS;
	s_main.mods.generic.x					= 320;
	s_main.mods.generic.y					= y;
	s_main.mods.generic.id					= ID_MODS;
	s_main.mods.generic.callback			= Main_MenuEvent;
	s_main.mods.string						= "MODS";
	s_main.mods.color						= s64_dim;
	s_main.mods.style						= sysstyle;

	y += MAIN_MENU_SYSTEM_SPACING;
	s_main.exit.generic.type				= MTYPE_PTEXT;
	s_main.exit.generic.flags				= QMF_CENTER_JUSTIFY|QMF_PULSEIFFOCUS;
	s_main.exit.generic.x					= 320;
	s_main.exit.generic.y					= y;
	s_main.exit.generic.id					= ID_EXIT;
	s_main.exit.generic.callback			= Main_MenuEvent;
	s_main.exit.string						= "QUIT";
	s_main.exit.color						= s64_dim;
	s_main.exit.style						= sysstyle;

	Menu_AddItem( &s_main.menu,	&s_main.daily );
	Menu_AddItem( &s_main.menu,	&s_main.trial );
	Menu_AddItem( &s_main.menu,	&s_main.vector );
	Menu_AddItem( &s_main.menu,	&s_main.lattice );
	Menu_AddItem( &s_main.menu,	&s_main.practice );
	Menu_AddItem( &s_main.menu,	&s_main.multiplayer );
	Menu_AddItem( &s_main.menu,	&s_main.setup );
	Menu_AddItem( &s_main.menu,	&s_main.mods );
	Menu_AddItem( &s_main.menu,	&s_main.exit );

	trap_Key_SetCatcher( KEYCATCH_UI );
	uis.menusp = 0;
	UI_PushMenu ( &s_main.menu );

}
