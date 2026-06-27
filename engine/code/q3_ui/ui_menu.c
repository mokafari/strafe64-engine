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
#define ID_S64_ARENA			22
#define ID_S64_LATTICE			23
#define ID_S64_PRACTICE			24
#define ID_S64_FORGE			25
#define ID_S64_DEV				26

// flagship strafegen courses, kept in baseoa as .pk3
#define S64_MAP_TOWER			"strafe64_1337_x3"
#define S64_MAP_TRIAL			"strafe64_1337"
#define S64_MAP_ARENA			"strafe64dm_1337"
#define S64_MAP_LATTICE			"lattice_arena_64"
#define S64_MAP_PRACTICE		"strafe64_7"

#define MAIN_BANNER_MODEL				"models/mapobjects/banner/banner5.md3"
#define MAIN_MENU_VERTICAL_SPACING		30
#define MAIN_MENU_SYSTEM_SPACING		26

// the dim "system" nav bar sits midway between the hero column and the
// bottom status line (MULTIPLAYER / SETUP / MODS / QUIT)
#define MAIN_MENU_BAR_Y					410

// STRAFE 64 — NERV/MAGI "structural collapse" identity for the main menu
static vec4_t s64_grid   = { 0.10f,  0.55f,  0.22f, 0.16f };  // MAGI green gridlines
static vec4_t menu_amber  = { 1.00f,  0.62f,  0.05f, 1.00f };  // menu item amber
static vec4_t s64_green  = { 0.45f,  1.00f,  0.55f, 1.00f };  // terminal green
static vec4_t s64_redhot = { 1.00f,  0.16f,  0.22f, 1.00f };  // warning red
static vec4_t menu_dim    = { 0.45f,  0.55f,  0.55f, 1.00f };  // dim label

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
		UI_LEFT|UI_SMALLFONT, menu_dim );

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
		UI_CENTER, menu_amber );

	UI_DrawString( 320, 112, "MAGI-01  / /  VOID PROTOCOL  / /  SPEED IS LIFE",
		UI_CENTER|UI_SMALLFONT, s64_green );

	// EVANGELION-style status block under the wordmark
	UI_DrawString( 320, 138, "EVACUATE BY MOVEMENT - THE FLOOR IS NEVER SAFE",
		UI_CENTER|UI_SMALLFONT, menu_dim );
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

A slashed MAGI status line (real build version + the NERV build codename,
"MELCHIOR" — one of the three MAGI, tying back to the MAGI-01 subtitle)
above a thin rule, fencing the bottom system nav bar (MULTIPLAYER / SETUP
/ MODS / QUIT) off from the hero column. The nav labels themselves are
real menu items, painted by Menu_Draw.
===============
*/
static void Strafe64_Footer( void ) {
	// thin rule under the grey nav bar, then the green status line pinned to
	// the very bottom edge
	UI_FillRect( 40, MAIN_MENU_BAR_Y + 18, 560, 1, s64_grid );
	UI_DrawString( 320, 460,
		"STRAFE 64   //   v" PRODUCT_VERSION "   //   BUILD MELCHIOR   //   NERV   //   No.666",
		UI_CENTER|UI_SMALLFONT, s64_green );
}


typedef struct {
	menuframework_s	menu;

	menutext_s		daily;
	menutext_s		trial;
	menutext_s		arena;
	menutext_s		lattice;
	menutext_s		practice;
	menutext_s		forge;
	menutext_s		multiplayer;
	menutext_s		setup;
	menutext_s		dev;
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

	case ID_S64_ARENA:
		// the bullet-time combat arena — open its setup screen so the
		// pilot picks the weapon ruleset (sword / vectorgun), the clock,
		// the void and the opponent count before dropping in
		UI_ArenaMenu();
		break;

	case ID_S64_LATTICE:
		// last pilot standing: every pilot carves a damaging speed-trail, and
		// the full FFA-N bracket plays the field down to one champion. The
		// collapsing floor (auto-void) is OFF here — the trails are the whole
		// pressure. SUPERHOT bullet-time is ON: stand still and the heat
		// near-freezes, carve and time returns — so dodging a wall is a
		// slow-mo read. g_lattice / g_voidRise / g_gametype / g_latticeBracket
		// are latched — they take effect on the map load queued below.
		trap_Cvar_SetValue( "g_lattice", 1 );
		trap_Cvar_SetValue( "g_vectorgun", 0 );
		trap_Cvar_SetValue( "g_voidRise", 0 );			// no void collapse in LATTICE
		trap_Cvar_SetValue( "g_timeBind", 1 );			// SUPERHOT bullet-time
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

	case ID_S64_FORGE:
		// the in-game generator: scrub strafegen's parameters, preview the
		// layout and author a fresh course on the spot
		UI_GenerateMenu();
		break;

	case ID_S64_DEV:
		// the test bench: any map, any weapon, any combination of the
		// gameplay levers + the visual/debug subpanels
		UI_DevMenu();
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
	int		sysstyle = UI_LEFT | UI_DROPSHADOW | UI_SMALLFONT;
	int		bx, by, mw, sw, vw, dw, qw, bgap;

	trap_Cvar_Set( "sv_killserver", "1" );

	// STRAFE 64 is a standalone mod — there is no retail CD key to verify.
	// The stock Q3 gate would otherwise trap every fresh profile on the
	// "PLEASE ENTER YOUR CD KEY" screen before the main menu, so it is gone.

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
	s_main.daily.color						= menu_amber;
	s_main.daily.style						= style;

	y += MAIN_MENU_VERTICAL_SPACING;
	s_main.trial.generic.type				= MTYPE_PTEXT;
	s_main.trial.generic.flags				= QMF_CENTER_JUSTIFY|QMF_PULSEIFFOCUS;
	s_main.trial.generic.x					= 320;
	s_main.trial.generic.y					= y;
	s_main.trial.generic.id					= ID_S64_TRIAL;
	s_main.trial.generic.callback			= Main_MenuEvent;
	s_main.trial.string						= "TIME TRIAL";
	s_main.trial.color						= menu_amber;
	s_main.trial.style						= style;

	y += MAIN_MENU_VERTICAL_SPACING;
	s_main.arena.generic.type				= MTYPE_PTEXT;
	s_main.arena.generic.flags				= QMF_CENTER_JUSTIFY|QMF_PULSEIFFOCUS;
	s_main.arena.generic.x					= 320;
	s_main.arena.generic.y					= y;
	s_main.arena.generic.id				= ID_S64_ARENA;
	s_main.arena.generic.callback			= Main_MenuEvent;
	s_main.arena.string					= "ARENA";
	s_main.arena.color						= menu_amber;
	s_main.arena.style						= style;

	y += MAIN_MENU_VERTICAL_SPACING;
	s_main.lattice.generic.type				= MTYPE_PTEXT;
	s_main.lattice.generic.flags			= QMF_CENTER_JUSTIFY|QMF_PULSEIFFOCUS;
	s_main.lattice.generic.x				= 320;
	s_main.lattice.generic.y				= y;
	s_main.lattice.generic.id				= ID_S64_LATTICE;
	s_main.lattice.generic.callback			= Main_MenuEvent;
	s_main.lattice.string					= "LATTICE";
	s_main.lattice.color					= menu_amber;
	s_main.lattice.style					= style;

	y += MAIN_MENU_VERTICAL_SPACING;
	s_main.practice.generic.type			= MTYPE_PTEXT;
	s_main.practice.generic.flags			= QMF_CENTER_JUSTIFY|QMF_PULSEIFFOCUS;
	s_main.practice.generic.x				= 320;
	s_main.practice.generic.y				= y;
	s_main.practice.generic.id				= ID_S64_PRACTICE;
	s_main.practice.generic.callback		= Main_MenuEvent;
	s_main.practice.string					= "PRACTICE";
	s_main.practice.color					= menu_amber;
	s_main.practice.style					= style;

	y += MAIN_MENU_VERTICAL_SPACING;
	s_main.forge.generic.type				= MTYPE_PTEXT;
	s_main.forge.generic.flags				= QMF_CENTER_JUSTIFY|QMF_PULSEIFFOCUS;
	s_main.forge.generic.x					= 320;
	s_main.forge.generic.y					= y;
	s_main.forge.generic.id					= ID_S64_FORGE;
	s_main.forge.generic.callback			= Main_MenuEvent;
	s_main.forge.string						= "FORGE";
	s_main.forge.color						= menu_amber;
	s_main.forge.style						= style;

	// ---- system: a horizontal nav bar across the bottom (where the old
	// status-line flavour text used to sit) so the hero column owns the
	// centre and nothing spills off the screen. Even gaps, group-centred. --
	mw   = UI_ProportionalStringWidth( "MULTIPLAYER" ) * PROP_SMALL_SIZE_SCALE;
	sw   = UI_ProportionalStringWidth( "SETUP" )       * PROP_SMALL_SIZE_SCALE;
	vw   = UI_ProportionalStringWidth( "DEVMODE" )     * PROP_SMALL_SIZE_SCALE;
	dw   = UI_ProportionalStringWidth( "MODS" )        * PROP_SMALL_SIZE_SCALE;
	qw   = UI_ProportionalStringWidth( "QUIT" )        * PROP_SMALL_SIZE_SCALE;
	bgap = 34;
	bx   = 320 - ( mw + sw + vw + dw + qw + bgap * 4 ) / 2;	// left edge, centred
	by   = MAIN_MENU_BAR_Y;

	s_main.multiplayer.generic.type			= MTYPE_PTEXT;
	s_main.multiplayer.generic.flags		= QMF_LEFT_JUSTIFY|QMF_PULSEIFFOCUS;
	s_main.multiplayer.generic.x			= bx;
	s_main.multiplayer.generic.y			= by;
	s_main.multiplayer.generic.id			= ID_MULTIPLAYER;
	s_main.multiplayer.generic.callback		= Main_MenuEvent;
	s_main.multiplayer.string				= "MULTIPLAYER";
	s_main.multiplayer.color				= menu_dim;
	s_main.multiplayer.style				= sysstyle;
	bx += mw + bgap;

	s_main.setup.generic.type				= MTYPE_PTEXT;
	s_main.setup.generic.flags				= QMF_LEFT_JUSTIFY|QMF_PULSEIFFOCUS;
	s_main.setup.generic.x					= bx;
	s_main.setup.generic.y					= by;
	s_main.setup.generic.id					= ID_SETUP;
	s_main.setup.generic.callback			= Main_MenuEvent;
	s_main.setup.string						= "SETUP";
	s_main.setup.color						= menu_dim;
	s_main.setup.style						= sysstyle;
	bx += sw + bgap;

	// DEVMODE: the test bench — tinted red-hot so it reads as a dev affordance
	s_main.dev.generic.type					= MTYPE_PTEXT;
	s_main.dev.generic.flags				= QMF_LEFT_JUSTIFY|QMF_PULSEIFFOCUS;
	s_main.dev.generic.x					= bx;
	s_main.dev.generic.y					= by;
	s_main.dev.generic.id					= ID_S64_DEV;
	s_main.dev.generic.callback				= Main_MenuEvent;
	s_main.dev.string						= "DEVMODE";
	s_main.dev.color						= s64_redhot;
	s_main.dev.style						= sysstyle;
	bx += vw + bgap;

	s_main.mods.generic.type				= MTYPE_PTEXT;
	s_main.mods.generic.flags				= QMF_LEFT_JUSTIFY|QMF_PULSEIFFOCUS;
	s_main.mods.generic.x					= bx;
	s_main.mods.generic.y					= by;
	s_main.mods.generic.id					= ID_MODS;
	s_main.mods.generic.callback			= Main_MenuEvent;
	s_main.mods.string						= "MODS";
	s_main.mods.color						= menu_dim;
	s_main.mods.style						= sysstyle;
	bx += dw + bgap;

	s_main.exit.generic.type				= MTYPE_PTEXT;
	s_main.exit.generic.flags				= QMF_LEFT_JUSTIFY|QMF_PULSEIFFOCUS;
	s_main.exit.generic.x					= bx;
	s_main.exit.generic.y					= by;
	s_main.exit.generic.id					= ID_EXIT;
	s_main.exit.generic.callback			= Main_MenuEvent;
	s_main.exit.string						= "QUIT";
	s_main.exit.color						= menu_dim;
	s_main.exit.style						= sysstyle;

	Menu_AddItem( &s_main.menu,	&s_main.daily );
	Menu_AddItem( &s_main.menu,	&s_main.trial );
	Menu_AddItem( &s_main.menu,	&s_main.arena );
	Menu_AddItem( &s_main.menu,	&s_main.lattice );
	Menu_AddItem( &s_main.menu,	&s_main.practice );
	Menu_AddItem( &s_main.menu,	&s_main.forge );
	Menu_AddItem( &s_main.menu,	&s_main.multiplayer );
	Menu_AddItem( &s_main.menu,	&s_main.setup );
	Menu_AddItem( &s_main.menu,	&s_main.dev );
	Menu_AddItem( &s_main.menu,	&s_main.mods );
	Menu_AddItem( &s_main.menu,	&s_main.exit );

	trap_Key_SetCatcher( KEYCATCH_UI );
	uis.menusp = 0;
	UI_PushMenu ( &s_main.menu );

	// unregistered copies land on the license screen first (the engine also
	// blocks map loads until com_licensed is set; this is the place to fix it)
	if( !trap_Cvar_VariableValue( "com_licensed" ) ) {
		UI_CDKeyMenu();
	}

}
