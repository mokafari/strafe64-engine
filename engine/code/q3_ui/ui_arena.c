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

STRAFE 64 — ARENA SETUP

Configure the bullet-time duel before dropping in: pick the map and the
weapon ruleset (katana vs. the speed-scaled Vectorgun rail), tune the
SUPERHOT clock, seat the Assassin bots, and dial in the visual stack
(audio trails, datamosh, gore, speed FX). FIGHT latches the ruleset
cvars, layers the shared ARENA effect preset (exec arena), overrides the
visuals with the menu choices, loads the chosen map and seats the bots —
the same end state the arena.cfg launchers produce, only driven from the
menu.

=======================================================================
*/

#include "ui_local.h"


#define ID_WEAPON			40
#define ID_MAP				41
#define ID_BULLETTIME		42
#define ID_OPPONENTS		43
#define ID_SKILL			44
#define ID_TRAILS			45
#define ID_DATAMOSH			46
#define ID_GORE				47
#define ID_SPEEDFX			48
#define ID_FIGHT			49
#define ID_BACK				50

#define MAX_ARENA_BOTS		7


// NERV/MAGI palette — kept in sync with ui_menu.c so the submenu reads
// as part of the same console
static vec4_t arena_amber  = { 1.00f, 0.62f, 0.05f, 1.00f };  // headline / FIGHT
static vec4_t arena_green  = { 0.45f, 1.00f, 0.55f, 1.00f };  // terminal green
static vec4_t arena_grid   = { 0.10f, 0.55f, 0.22f, 0.16f };  // dim rule
static vec4_t arena_dim    = { 0.45f, 0.55f, 0.55f, 1.00f };  // labels


typedef struct {
	menuframework_s	menu;

	// ruleset
	menulist_s		weapon;
	menulist_s		map;
	menulist_s		bullettime;
	menulist_s		opponents;
	menulist_s		skill;

	// visuals
	menulist_s		trails;
	menulist_s		datamosh;
	menulist_s		gore;
	menulist_s		speedfx;

	menutext_s		fight;
	menutext_s		back;
} arenamenu_t;

static arenamenu_t	s_arena;

// curated combat-arena maps that ship with bot nav (.aas). map_keys feeds
// the "map" command; map_names is what the spinner shows (must stay in
// lockstep — same order, map_names NULL-terminated for the control).
static const char *map_keys[] = {
	"strafe64dm_1337",
	"strafe64kb_8224",
	"strafe64kb_court_8240",
	"strafe64kb_9000",
	"strafe64kb_8232",
	"lattice_arena_64"
};
static const char *map_names[] = {
	"VELODROME",
	"KILLBOX 8224",
	"COURT",
	"KILLBOX 9000",
	"KILLBOX 8232",
	"LATTICE GRID",
	NULL
};
#define NUM_ARENA_MAPS		( ARRAY_LEN( map_keys ) )

static const char *weapon_names[] = {
	"SWORD  (katana)",
	"VECTORGUN  (speed-rail)",
	NULL
};

static const char *onoff_names[] = {
	"OFF",
	"ON",
	NULL
};

static const char *opponent_names[] = {
	"NONE",
	"1",
	"2",
	"3",
	"4",
	"5",
	"6",
	"7",
	NULL
};

static const char *datamosh_names[] = {
	"OFF",
	"SOFT",
	"HARD",
	NULL
};
static const float datamosh_values[] = { 0.0f, 0.35f, 0.7f };

static const char *skill_names[] = {
	"I    INITIATE",
	"II",
	"III",
	"IV",
	"V    MASTER",
	NULL
};


/*
=================
ArenaMenu_SetMenuItems

Seed the controls from the live cvars so the menu reflects the current
ruleset every time it opens.
=================
*/
static void ArenaMenu_SetMenuItems( void ) {
	int		bots;
	int		skill;
	int		i;
	char	curmap[MAX_QPATH];
	float	glitch;

	s_arena.weapon.curvalue     = trap_Cvar_VariableValue( "g_vectorgun" ) != 0;
	s_arena.bullettime.curvalue = trap_Cvar_VariableValue( "g_timeBind" ) != 0;

	// match the saved map name back to the spinner, default to the first
	s_arena.map.curvalue = 0;
	trap_Cvar_VariableStringBuffer( "ui_arenaMap", curmap, sizeof( curmap ) );
	for ( i = 0; i < (int)NUM_ARENA_MAPS; i++ ) {
		if ( !Q_stricmp( curmap, map_keys[i] ) ) {
			s_arena.map.curvalue = i;
			break;
		}
	}

	bots = (int)trap_Cvar_VariableValue( "bot_minplayers" );
	if ( bots < 0 ) {
		bots = 0;
	} else if ( bots > MAX_ARENA_BOTS ) {
		bots = MAX_ARENA_BOTS;
	}
	s_arena.opponents.curvalue = bots;

	skill = (int)trap_Cvar_VariableValue( "g_spSkill" ) - 1;
	if ( skill < 0 ) {
		skill = 0;
	} else if ( skill > 4 ) {
		skill = 4;
	}
	s_arena.skill.curvalue = skill;

	// visuals
	s_arena.trails.curvalue  = trap_Cvar_VariableValue( "cg_arenaTrails" ) != 0;
	s_arena.gore.curvalue    = trap_Cvar_VariableValue( "cg_bloodSpurt" ) != 0;
	s_arena.speedfx.curvalue = trap_Cvar_VariableValue( "cg_speedLines" ) != 0;

	glitch = trap_Cvar_VariableValue( "cg_latticeGlitch" );
	if ( glitch <= 0.0f ) {
		s_arena.datamosh.curvalue = 0;
	} else if ( glitch < 0.5f ) {
		s_arena.datamosh.curvalue = 1;
	} else {
		s_arena.datamosh.curvalue = 2;
	}
}


/*
=================
ArenaMenu_StartFight

Latch the chosen ruleset, layer the shared ARENA effect preset, override
the visuals with the menu choices, load the chosen map and seat the
Assassins. Mirrors arena.cfg + sword_arena.cfg / arena_vg.cfg, only
parameterised by the menu.
=================
*/
static void ArenaMenu_StartFight( void ) {
	int			vectorgun = s_arena.weapon.curvalue;
	int			bots      = s_arena.opponents.curvalue;
	int			skill     = s_arena.skill.curvalue + 1;
	const char	*mapname  = map_keys[ s_arena.map.curvalue ];
	int			i;
	int			delay;
	char		cmd[128];

	// --- weapon ruleset (latched; applied on the map load queued below) ---
	trap_Cvar_SetValue( "g_lattice", 0 );
	trap_Cvar_SetValue( "g_vectorgun", vectorgun );
	trap_Cvar_SetValue( "g_voidRise", 0 );			// arena is a duel — no collapsing floor
	trap_Cvar_SetValue( "g_timeBind", s_arena.bullettime.curvalue );
	trap_Cvar_SetValue( "g_gametype", 0 );			// FFA

	// bots inherit the weapon: rail-toting in vectorgun, sword-only in melee
	trap_Cvar_SetValue( "g_botSwordOnly", vectorgun ? 0 : 1 );
	trap_Cvar_SetValue( "g_spSkill", skill );
	trap_Cvar_Set( "bot_enable", "1" );
	trap_Cvar_SetValue( "bot_minplayers", bots );

	// remember the chosen map so the menu reopens on it
	trap_Cvar_Set( "ui_arenaMap", mapname );

	// slot 1 carries the chosen weapon (railgun = the vectorgun, else katana)
	if ( vectorgun ) {
		trap_Cmd_ExecuteText( EXEC_APPEND, UI_BIND_VECTORGUN );	// WP_RAILGUN
	} else {
		trap_Cmd_ExecuteText( EXEC_APPEND, UI_BIND_SWORD );	// WP_SWORD
	}

	// shared effect stack + audio-reactive trails + bullet-time tuning
	trap_Cmd_ExecuteText( EXEC_APPEND, "exec arena\n" );

	// --- visual overrides: these run AFTER exec arena, so the menu wins ---
	// audio-reactive speed-trails (and the music-driven warp that rides them)
	if ( s_arena.trails.curvalue ) {
		trap_Cmd_ExecuteText( EXEC_APPEND,
			"seta cg_arenaTrails 1; seta cg_latticeAudio 1.4; seta cg_latticeWave 1.3\n" );
	} else {
		trap_Cmd_ExecuteText( EXEC_APPEND,
			"seta cg_arenaTrails 0; seta cg_latticeAudio 0; seta cg_latticeWave 0\n" );
	}
	// neon datamosh glitch intensity (OFF / SOFT / HARD)
	Com_sprintf( cmd, sizeof( cmd ), "seta cg_latticeGlitch %g\n",
		datamosh_values[ s_arena.datamosh.curvalue ] );
	trap_Cmd_ExecuteText( EXEC_APPEND, cmd );
	// gore: arterial spray + spreading pools + ragdoll corpses
	if ( s_arena.gore.curvalue ) {
		trap_Cmd_ExecuteText( EXEC_APPEND,
			"seta cg_bloodSpurt 1; seta cg_bloodPool 1; seta cg_ragdoll 1\n" );
	} else {
		trap_Cmd_ExecuteText( EXEC_APPEND,
			"seta cg_bloodSpurt 0; seta cg_bloodPool 0; seta cg_ragdoll 0\n" );
	}
	// speed FX: radial rush lines + the velocity FOV punch
	if ( s_arena.speedfx.curvalue ) {
		trap_Cmd_ExecuteText( EXEC_APPEND, "seta cg_speedLines 1; seta cg_speedFov 1\n" );
	} else {
		trap_Cmd_ExecuteText( EXEC_APPEND, "seta cg_speedLines 0; seta cg_speedFov 0\n" );
	}

	// load the arena (this is where the latched ruleset takes effect)
	Com_sprintf( cmd, sizeof( cmd ), "map %s\n", mapname );
	trap_Cmd_ExecuteText( EXEC_APPEND, cmd );

	// seat the Assassins, staggered so they trickle in
	if ( bots > 0 ) {
		trap_Cmd_ExecuteText( EXEC_APPEND, "wait 80\n" );
		for ( i = 0; i < bots; i++ ) {
			delay = 400 + i * 700;
			Com_sprintf( cmd, sizeof( cmd ),
				"addbot Assassin %i free %i\n", skill, delay );
			trap_Cmd_ExecuteText( EXEC_APPEND, cmd );
		}
	}
}


/*
=================
ArenaMenu_Event
=================
*/
static void ArenaMenu_Event( void *ptr, int notification ) {
	if ( notification != QM_ACTIVATED ) {
		return;
	}

	switch ( ((menucommon_s *)ptr)->id ) {
	case ID_FIGHT:
		ArenaMenu_StartFight();
		break;

	case ID_BACK:
		UI_PopMenu();
		break;
	}
}


/*
=================
ArenaMenu_Draw

The framework painted the shared NERV backdrop; layer the ARENA header +
section rules on top, then the controls.
=================
*/
static void ArenaMenu_Draw( void ) {
	UI_DrawProportionalString( 320, 64, "ARENA", UI_CENTER, arena_amber );
	UI_DrawString( 320, 104, "CONFIGURE  THE  DUEL  / /  SPEED  IS  THE  TRIGGER",
		UI_CENTER|UI_SMALLFONT, arena_green );

	UI_FillRect( 180, 138, 280, 1, arena_grid );
	UI_DrawString( 320, 142, "[  RULESET  ]", UI_CENTER|UI_SMALLFONT, arena_green );

	UI_FillRect( 180, 286, 280, 1, arena_grid );
	UI_DrawString( 320, 290, "[  VISUALS  ]", UI_CENTER|UI_SMALLFONT, arena_green );

	Menu_Draw( &s_arena.menu );

	UI_DrawString( 320, 464, "SUPERHOT // bullet-time duel // NERV",
		UI_CENTER|UI_SMALLFONT, arena_dim );
}


/*
=================
ArenaMenu_AddSpin

Small helper: every control on this screen is an identical small-font
spinner, so stamp the shared fields in one place.
=================
*/
static void ArenaMenu_AddSpin( menulist_s *ctrl, int id, int y,
		const char *name, const char **itemnames ) {
	ctrl->generic.type		= MTYPE_SPINCONTROL;
	ctrl->generic.flags		= QMF_PULSEIFFOCUS|QMF_SMALLFONT;
	ctrl->generic.x			= 320;
	ctrl->generic.y			= y;
	ctrl->generic.name		= name;
	ctrl->generic.id		= id;
	ctrl->generic.callback	= ArenaMenu_Event;
	ctrl->itemnames			= itemnames;
}


/*
=================
ArenaMenu_Init
=================
*/
static void ArenaMenu_Init( void ) {
	int		y;

	memset( &s_arena, 0, sizeof( arenamenu_t ) );

	s_arena.menu.draw       = ArenaMenu_Draw;
	s_arena.menu.fullscreen = qtrue;
	s_arena.menu.wrapAround  = qtrue;

	// ---- RULESET ------------------------------------------------------
	y = 162;
	ArenaMenu_AddSpin( &s_arena.weapon,     ID_WEAPON,     y, "WEAPON:",      weapon_names );
	y += 24;
	ArenaMenu_AddSpin( &s_arena.map,        ID_MAP,        y, "MAP:",         map_names );
	y += 24;
	ArenaMenu_AddSpin( &s_arena.bullettime, ID_BULLETTIME, y, "BULLET-TIME:", onoff_names );
	y += 24;
	ArenaMenu_AddSpin( &s_arena.opponents,  ID_OPPONENTS,  y, "OPPONENTS:",   opponent_names );
	y += 24;
	ArenaMenu_AddSpin( &s_arena.skill,      ID_SKILL,      y, "SKILL:",       skill_names );

	// ---- VISUALS (header rule drawn at y=286 in ArenaMenu_Draw) --------
	y = 310;
	ArenaMenu_AddSpin( &s_arena.trails,     ID_TRAILS,     y, "AUDIO TRAILS:", onoff_names );
	y += 24;
	ArenaMenu_AddSpin( &s_arena.datamosh,   ID_DATAMOSH,   y, "DATAMOSH:",     datamosh_names );
	y += 24;
	ArenaMenu_AddSpin( &s_arena.gore,       ID_GORE,       y, "GORE:",         onoff_names );
	y += 24;
	ArenaMenu_AddSpin( &s_arena.speedfx,    ID_SPEEDFX,    y, "SPEED FX:",     onoff_names );

	// ---- FIGHT (the hero) + BACK -------------------------------------
	y += 36;
	s_arena.fight.generic.type		= MTYPE_PTEXT;
	s_arena.fight.generic.flags		= QMF_CENTER_JUSTIFY|QMF_PULSEIFFOCUS;
	s_arena.fight.generic.x			= 320;
	s_arena.fight.generic.y			= y;
	s_arena.fight.generic.id		= ID_FIGHT;
	s_arena.fight.generic.callback	= ArenaMenu_Event;
	s_arena.fight.string			= "FIGHT";
	s_arena.fight.color				= arena_amber;
	s_arena.fight.style				= UI_CENTER|UI_DROPSHADOW;

	y += 30;
	s_arena.back.generic.type		= MTYPE_PTEXT;
	s_arena.back.generic.flags		= QMF_CENTER_JUSTIFY|QMF_PULSEIFFOCUS;
	s_arena.back.generic.x			= 320;
	s_arena.back.generic.y			= y;
	s_arena.back.generic.id			= ID_BACK;
	s_arena.back.generic.callback	= ArenaMenu_Event;
	s_arena.back.string				= "BACK";
	s_arena.back.color				= arena_dim;
	s_arena.back.style				= UI_CENTER|UI_DROPSHADOW|UI_SMALLFONT;

	Menu_AddItem( &s_arena.menu, &s_arena.weapon );
	Menu_AddItem( &s_arena.menu, &s_arena.map );
	Menu_AddItem( &s_arena.menu, &s_arena.bullettime );
	Menu_AddItem( &s_arena.menu, &s_arena.opponents );
	Menu_AddItem( &s_arena.menu, &s_arena.skill );
	Menu_AddItem( &s_arena.menu, &s_arena.trails );
	Menu_AddItem( &s_arena.menu, &s_arena.datamosh );
	Menu_AddItem( &s_arena.menu, &s_arena.gore );
	Menu_AddItem( &s_arena.menu, &s_arena.speedfx );
	Menu_AddItem( &s_arena.menu, &s_arena.fight );
	Menu_AddItem( &s_arena.menu, &s_arena.back );

	ArenaMenu_SetMenuItems();
}


/*
=================
UI_ArenaMenu
=================
*/
void UI_ArenaMenu( void ) {
	ArenaMenu_Init();
	trap_Key_SetCatcher( KEYCATCH_UI );
	UI_PushMenu( &s_arena.menu );
}
