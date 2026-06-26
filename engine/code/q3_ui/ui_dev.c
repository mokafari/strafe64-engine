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

STRAFE 64 — DEVMODE

A test bench for the whole feature stack: pick any map, any weapon, any
combination of the gameplay levers (rising void / bullet-time / lattice
trails / instant grapple), seat bots, and LAUNCH. Unlike the curated hero
modes on the main menu — each of which locks a fixed preset — DEVMODE lets
every toggle move independently, so any combination can be tried in one
drop. Two subpanels carry the deeper settings: VISUAL FX (trails, datamosh,
gore, speed FX, blade trail — applied live) and DEBUG (cheats, give all,
god, noclip — applied on the next LAUNCH).

The hub keeps its state on the menu stack while a subpanel sits on top, so
choices survive a trip into a subpanel and back.

=======================================================================
*/

#include "ui_local.h"


// ---- hub ----
#define ID_MAP			70
#define ID_WEAPON		71
#define ID_BOTS			72
#define ID_SKILL		73
#define ID_VOID			74
#define ID_BULLETTIME	75
#define ID_LATTICE		76
#define ID_GRAPPLE		77
#define ID_LAUNCH		78
#define ID_FXLINK		79
#define ID_DBGLINK		80
#define ID_BACK			81
#define ID_WORLDLINK	82

// ---- fx subpanel ----
#define ID_FX_TRAILS	90
#define ID_FX_DATAMOSH	91
#define ID_FX_GORE		92
#define ID_FX_SPEED		93
#define ID_FX_BLADE		94
#define ID_FX_BACK		95

// ---- world subpanel ----
#define ID_WORLD_GRAV	110
#define ID_WORLD_SPEED	111
#define ID_WORLD_DASH	112
#define ID_WORLD_BSPEED	113
#define ID_WORLD_KNOCK	114
#define ID_WORLD_HOT	115
#define ID_WORLD_FF		116
#define ID_WORLD_BACK	117

// ---- debug subpanel ----
#define ID_DBG_CHEATS	100
#define ID_DBG_GIVEALL	101
#define ID_DBG_GOD		102
#define ID_DBG_NOCLIP	103
#define ID_DBG_BACK		104

#define MAX_DEV_BOTS	7

// weapon spinner indices
enum {
	DW_SWORD,
	DW_VECTORGUN,
	DW_ALL
};


// NERV/MAGI palette — kept in sync with ui_menu.c / ui_arena.c / ui_generate.c
static vec4_t dev_amber  = { 1.00f, 0.62f, 0.05f, 1.00f };  // headline / LAUNCH
static vec4_t dev_green  = { 0.45f, 1.00f, 0.55f, 1.00f };  // terminal green
static vec4_t dev_grid   = { 0.10f, 0.55f, 0.22f, 0.16f };  // dim rule
static vec4_t dev_dim    = { 0.45f, 0.55f, 0.55f, 1.00f };  // labels
static vec4_t dev_redhot = { 1.00f, 0.16f, 0.22f, 1.00f };  // dev/cheat warning


// curated test maps that ship with bot nav (.aas). dev_map_keys feeds the
// "map" command; dev_map_names is what the spinner shows — keep both in
// lockstep (same order; dev_map_names NULL-terminated for the control).
static const char *dev_map_keys[] = {
	"strafe64_1337",
	"strafe64_1337_x3",
	"strafe64_7",
	"strafe64dm_1337",
	"lattice_arena_64",
	"strafe64kb_8224",
	"strafe64kb_court_8240",
	"strafe64kb_9000",
	"strafe64kb_8232"
};
static const char *dev_map_names[] = {
	"TIME TRIAL  (1337)",
	"TOWER  (1337 x3)",
	"PRACTICE  (7)",
	"VELODROME  (dm 1337)",
	"LATTICE GRID",
	"KILLBOX 8224",
	"COURT",
	"KILLBOX 9000",
	"KILLBOX 8232",
	NULL
};
#define NUM_DEV_MAPS	( ARRAY_LEN( dev_map_keys ) )

static const char *dev_weapon_names[] = {
	"SWORD  (katana)",
	"VECTORGUN  (speed-rail)",
	"ALL WEAPONS",
	NULL
};

static const char *dev_onoff_names[] = {
	"OFF",
	"ON",
	NULL
};

static const char *dev_opponent_names[] = {
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

static const char *dev_skill_names[] = {
	"I    INITIATE",
	"II",
	"III",
	"IV",
	"V    MASTER",
	NULL
};

static const char *dev_datamosh_names[] = {
	"OFF",
	"SOFT",
	"HARD",
	NULL
};
static const float dev_datamosh_values[] = { 0.0f, 0.35f, 0.7f };

// ---- WORLD tuning levers (preset stops over the live g_ cvars) ----
static const char *dev_grav_names[]  = { "MOON", "LOW", "NORMAL", "HIGH", NULL };
static const float dev_grav_values[] = { 200.0f, 500.0f, 1000.0f, 1600.0f };

static const char *dev_run_names[]   = { "SLOW", "NORMAL", "FAST", "SONIC", NULL };
static const float dev_run_values[]  = { 240.0f, 320.0f, 480.0f, 640.0f };

static const char *dev_dash_names[]  = { "SHORT", "NORMAL", "LONG", NULL };
static const float dev_dash_values[] = { 300.0f, 430.0f, 650.0f };

static const char *dev_bspeed_names[]  = { "FROZEN", "SLOW", "NORMAL", "FULL", NULL };
static const float dev_bspeed_values[] = { 0.15f, 0.30f, 0.45f, 1.0f };

static const char *dev_knock_names[]  = { "LOW", "NORMAL", "HIGH", NULL };
static const float dev_knock_values[] = { 500.0f, 1000.0f, 2000.0f };


// DEBUG intentions: applied on the next LAUNCH, not live. Kept as module
// statics so they survive opening/closing the debug subpanel. Cheats default
// ON — DEVMODE drops you into a devmap so god/noclip/give all work.
static int	dev_cheats  = 1;
static int	dev_giveAll = 0;
static int	dev_god     = 0;
static int	dev_noclip  = 0;


typedef struct {
	menuframework_s	menu;

	// launch config
	menulist_s		map;
	menulist_s		weapon;
	menulist_s		bots;
	menulist_s		skill;

	// gameplay features (independent toggles)
	menulist_s		voidrise;
	menulist_s		bullettime;
	menulist_s		lattice;
	menulist_s		grapple;

	menutext_s		launch;
	menutext_s		fxlink;
	menutext_s		worldlink;
	menutext_s		dbglink;
	menutext_s		back;
} devmenu_t;

static devmenu_t	s_dev;


/*
=================================================================
   shared helpers
=================================================================
*/

/*
=================
DevMenu_AddSpin

Every control on these screens is the same small-font spinner.
=================
*/
static void DevMenu_AddSpin( menulist_s *ctrl, int id, int x, int y,
		const char *name, const char **itemnames, void (*cb)( void *, int ) ) {
	ctrl->generic.type		= MTYPE_SPINCONTROL;
	ctrl->generic.flags		= QMF_PULSEIFFOCUS|QMF_SMALLFONT;
	ctrl->generic.x			= x;
	ctrl->generic.y			= y;
	ctrl->generic.name		= name;
	ctrl->generic.id		= id;
	ctrl->generic.callback	= cb;
	ctrl->itemnames			= itemnames;
}

/*
=================
DevMenu_AddText

A PTEXT button (LAUNCH / links / BACK).
=================
*/
static void DevMenu_AddText( menutext_s *t, int id, int x, int y,
		const char *str, vec4_t color, int style, void (*cb)( void *, int ) ) {
	t->generic.type		= MTYPE_PTEXT;
	t->generic.flags	= QMF_CENTER_JUSTIFY|QMF_PULSEIFFOCUS;
	t->generic.x		= x;
	t->generic.y		= y;
	t->generic.id		= id;
	t->generic.callback	= cb;
	t->string			= (char *)str;
	t->color			= color;
	t->style			= style;
}

/*
=================
DevMenu_NearestIndex

Seed a value-spinner from a live cvar: pick the preset closest to the
current value (the cvar may hold anything, the spinner only a few stops).
=================
*/
static int DevMenu_NearestIndex( const char *cvar, const float *vals, int n ) {
	float	v = trap_Cvar_VariableValue( cvar );
	float	bestd = 1.0e9f;
	int		best = 0;
	int		i;

	for ( i = 0; i < n; i++ ) {
		float d = vals[i] - v;
		if ( d < 0 ) {
			d = -d;
		}
		if ( d < bestd ) {
			bestd = d;
			best = i;
		}
	}
	return best;
}


/*
=================================================================
   VISUAL FX subpanel — applied live (seta) so the choices ride
   straight into the launched game
=================================================================
*/

typedef struct {
	menuframework_s	menu;

	menulist_s		trails;
	menulist_s		datamosh;
	menulist_s		gore;
	menulist_s		speedfx;
	menulist_s		blade;

	menutext_s		back;
} devfxmenu_t;

static devfxmenu_t	s_devfx;


static void DevFxMenu_Apply( void ) {
	char	cmd[128];

	// audio-reactive speed-trails + the music-driven warp that rides them
	if ( s_devfx.trails.curvalue ) {
		trap_Cmd_ExecuteText( EXEC_APPEND,
			"seta cg_arenaTrails 1; seta cg_latticeAudio 1.4; seta cg_latticeWave 1.3\n" );
	} else {
		trap_Cmd_ExecuteText( EXEC_APPEND,
			"seta cg_arenaTrails 0; seta cg_latticeAudio 0; seta cg_latticeWave 0\n" );
	}

	// neon datamosh glitch intensity (OFF / SOFT / HARD)
	Com_sprintf( cmd, sizeof( cmd ), "seta cg_latticeGlitch %g\n",
		dev_datamosh_values[ s_devfx.datamosh.curvalue ] );
	trap_Cmd_ExecuteText( EXEC_APPEND, cmd );

	// gore: arterial spray + spreading pools + ragdoll corpses
	if ( s_devfx.gore.curvalue ) {
		trap_Cmd_ExecuteText( EXEC_APPEND,
			"seta cg_bloodSpurt 1; seta cg_bloodPool 1; seta cg_ragdoll 1\n" );
	} else {
		trap_Cmd_ExecuteText( EXEC_APPEND,
			"seta cg_bloodSpurt 0; seta cg_bloodPool 0; seta cg_ragdoll 0\n" );
	}

	// speed FX: radial rush lines + the velocity FOV punch
	if ( s_devfx.speedfx.curvalue ) {
		trap_Cmd_ExecuteText( EXEC_APPEND, "seta cg_speedLines 1; seta cg_speedFov 1\n" );
	} else {
		trap_Cmd_ExecuteText( EXEC_APPEND, "seta cg_speedLines 0; seta cg_speedFov 0\n" );
	}

	// the katana slash-trail
	Com_sprintf( cmd, sizeof( cmd ), "seta cg_swordTrail %i\n", s_devfx.blade.curvalue );
	trap_Cmd_ExecuteText( EXEC_APPEND, cmd );
}

static void DevFxMenu_SetItems( void ) {
	float	glitch;

	s_devfx.trails.curvalue  = trap_Cvar_VariableValue( "cg_arenaTrails" ) != 0;
	s_devfx.gore.curvalue    = trap_Cvar_VariableValue( "cg_bloodSpurt" ) != 0;
	s_devfx.speedfx.curvalue = trap_Cvar_VariableValue( "cg_speedLines" ) != 0;
	s_devfx.blade.curvalue   = trap_Cvar_VariableValue( "cg_swordTrail" ) != 0;

	glitch = trap_Cvar_VariableValue( "cg_latticeGlitch" );
	if ( glitch <= 0.0f ) {
		s_devfx.datamosh.curvalue = 0;
	} else if ( glitch < 0.5f ) {
		s_devfx.datamosh.curvalue = 1;
	} else {
		s_devfx.datamosh.curvalue = 2;
	}
}

static void DevFxMenu_Event( void *ptr, int notification ) {
	if ( notification != QM_ACTIVATED ) {
		return;
	}

	switch ( ((menucommon_s *)ptr)->id ) {
	case ID_FX_BACK:
		UI_PopMenu();
		break;

	default:
		// any spinner moved — push the whole stack live
		DevFxMenu_Apply();
		break;
	}
}

static void DevFxMenu_Draw( void ) {
	UI_DrawProportionalString( 320, 70, "VISUAL FX", UI_CENTER, dev_amber );
	UI_DrawString( 320, 112, "APPLIED LIVE  / /  THESE RIDE INTO THE NEXT DROP",
		UI_CENTER|UI_SMALLFONT, dev_green );

	UI_FillRect( 180, 150, 280, 1, dev_grid );
	UI_DrawString( 320, 154, "[  EFFECT STACK  ]", UI_CENTER|UI_SMALLFONT, dev_green );

	Menu_Draw( &s_devfx.menu );

	UI_DrawString( 320, 464, "DEVMODE // visual stack // NERV",
		UI_CENTER|UI_SMALLFONT, dev_dim );
}

static void DevFxMenu_Init( void ) {
	int	y;

	memset( &s_devfx, 0, sizeof( s_devfx ) );

	s_devfx.menu.draw       = DevFxMenu_Draw;
	s_devfx.menu.fullscreen = qtrue;
	s_devfx.menu.wrapAround = qtrue;

	y = 178;
	DevMenu_AddSpin( &s_devfx.trails,   ID_FX_TRAILS,   320, y, "AUDIO TRAILS:", dev_onoff_names,    DevFxMenu_Event );
	y += 24;
	DevMenu_AddSpin( &s_devfx.datamosh, ID_FX_DATAMOSH, 320, y, "DATAMOSH:",     dev_datamosh_names, DevFxMenu_Event );
	y += 24;
	DevMenu_AddSpin( &s_devfx.gore,     ID_FX_GORE,     320, y, "GORE:",         dev_onoff_names,    DevFxMenu_Event );
	y += 24;
	DevMenu_AddSpin( &s_devfx.speedfx,  ID_FX_SPEED,    320, y, "SPEED FX:",     dev_onoff_names,    DevFxMenu_Event );
	y += 24;
	DevMenu_AddSpin( &s_devfx.blade,    ID_FX_BLADE,    320, y, "BLADE TRAIL:",  dev_onoff_names,    DevFxMenu_Event );

	y += 40;
	DevMenu_AddText( &s_devfx.back, ID_FX_BACK, 320, y, "BACK", dev_dim,
		UI_CENTER|UI_DROPSHADOW|UI_SMALLFONT, DevFxMenu_Event );

	Menu_AddItem( &s_devfx.menu, &s_devfx.trails );
	Menu_AddItem( &s_devfx.menu, &s_devfx.datamosh );
	Menu_AddItem( &s_devfx.menu, &s_devfx.gore );
	Menu_AddItem( &s_devfx.menu, &s_devfx.speedfx );
	Menu_AddItem( &s_devfx.menu, &s_devfx.blade );
	Menu_AddItem( &s_devfx.menu, &s_devfx.back );

	DevFxMenu_SetItems();
}


/*
=================================================================
   WORLD subpanel — movement / world tuning, applied live over the
   g_ cvars (they persist across the LAUNCH map load)
=================================================================
*/

typedef struct {
	menuframework_s	menu;

	menulist_s		gravity;
	menulist_s		runspeed;
	menulist_s		dash;
	menulist_s		bspeed;
	menulist_s		knockback;
	menulist_s		hotfloor;
	menulist_s		friendly;

	menutext_s		back;
} devworldmenu_t;

static devworldmenu_t	s_devworld;


static void DevWorldMenu_Apply( void ) {
	trap_Cvar_SetValue( "g_gravity",     dev_grav_values[ s_devworld.gravity.curvalue ] );
	trap_Cvar_SetValue( "g_speed",       dev_run_values[ s_devworld.runspeed.curvalue ] );
	trap_Cvar_SetValue( "g_dashSpeed",   dev_dash_values[ s_devworld.dash.curvalue ] );
	trap_Cvar_SetValue( "g_bulletSpeed", dev_bspeed_values[ s_devworld.bspeed.curvalue ] );
	trap_Cvar_SetValue( "g_knockback",   dev_knock_values[ s_devworld.knockback.curvalue ] );
	trap_Cvar_SetValue( "g_hotFloor",    s_devworld.hotfloor.curvalue );
	trap_Cvar_SetValue( "g_friendlyFire", s_devworld.friendly.curvalue );
}

static void DevWorldMenu_SetItems( void ) {
	s_devworld.gravity.curvalue   = DevMenu_NearestIndex( "g_gravity",     dev_grav_values,   ARRAY_LEN( dev_grav_values ) );
	s_devworld.runspeed.curvalue  = DevMenu_NearestIndex( "g_speed",       dev_run_values,    ARRAY_LEN( dev_run_values ) );
	s_devworld.dash.curvalue      = DevMenu_NearestIndex( "g_dashSpeed",   dev_dash_values,   ARRAY_LEN( dev_dash_values ) );
	s_devworld.bspeed.curvalue    = DevMenu_NearestIndex( "g_bulletSpeed", dev_bspeed_values, ARRAY_LEN( dev_bspeed_values ) );
	s_devworld.knockback.curvalue = DevMenu_NearestIndex( "g_knockback",   dev_knock_values,  ARRAY_LEN( dev_knock_values ) );
	s_devworld.hotfloor.curvalue  = trap_Cvar_VariableValue( "g_hotFloor" )     != 0;
	s_devworld.friendly.curvalue  = trap_Cvar_VariableValue( "g_friendlyFire" ) != 0;
}

static void DevWorldMenu_Event( void *ptr, int notification ) {
	if ( notification != QM_ACTIVATED ) {
		return;
	}

	switch ( ((menucommon_s *)ptr)->id ) {
	case ID_WORLD_BACK:
		UI_PopMenu();
		break;

	default:
		// any lever moved — push the whole stack to the live cvars
		DevWorldMenu_Apply();
		break;
	}
}

static void DevWorldMenu_Draw( void ) {
	UI_DrawProportionalString( 320, 70, "WORLD", UI_CENTER, dev_amber );
	UI_DrawString( 320, 112, "MOVEMENT  &  WORLD TUNING  / /  APPLIED LIVE",
		UI_CENTER|UI_SMALLFONT, dev_green );

	UI_FillRect( 180, 150, 280, 1, dev_grid );
	UI_DrawString( 320, 154, "[  PHYSICS  ]", UI_CENTER|UI_SMALLFONT, dev_green );

	Menu_Draw( &s_devworld.menu );

	UI_DrawString( 320, 464, "DEVMODE // world tuning // NERV",
		UI_CENTER|UI_SMALLFONT, dev_dim );
}

static void DevWorldMenu_Init( void ) {
	int	y;

	memset( &s_devworld, 0, sizeof( s_devworld ) );

	s_devworld.menu.draw       = DevWorldMenu_Draw;
	s_devworld.menu.fullscreen = qtrue;
	s_devworld.menu.wrapAround = qtrue;

	y = 178;
	DevMenu_AddSpin( &s_devworld.gravity,   ID_WORLD_GRAV,   320, y, "GRAVITY:",       dev_grav_names,   DevWorldMenu_Event );
	y += 24;
	DevMenu_AddSpin( &s_devworld.runspeed,  ID_WORLD_SPEED,  320, y, "RUN SPEED:",     dev_run_names,    DevWorldMenu_Event );
	y += 24;
	DevMenu_AddSpin( &s_devworld.dash,      ID_WORLD_DASH,   320, y, "DASH:",          dev_dash_names,   DevWorldMenu_Event );
	y += 24;
	DevMenu_AddSpin( &s_devworld.bspeed,    ID_WORLD_BSPEED, 320, y, "BULLET SPEED:",  dev_bspeed_names, DevWorldMenu_Event );
	y += 24;
	DevMenu_AddSpin( &s_devworld.knockback, ID_WORLD_KNOCK,  320, y, "KNOCKBACK:",     dev_knock_names,  DevWorldMenu_Event );
	y += 24;
	DevMenu_AddSpin( &s_devworld.hotfloor,  ID_WORLD_HOT,    320, y, "HOT FLOOR:",     dev_onoff_names,  DevWorldMenu_Event );
	y += 24;
	DevMenu_AddSpin( &s_devworld.friendly,  ID_WORLD_FF,     320, y, "FRIENDLY FIRE:", dev_onoff_names,  DevWorldMenu_Event );

	y += 40;
	DevMenu_AddText( &s_devworld.back, ID_WORLD_BACK, 320, y, "BACK", dev_dim,
		UI_CENTER|UI_DROPSHADOW|UI_SMALLFONT, DevWorldMenu_Event );

	Menu_AddItem( &s_devworld.menu, &s_devworld.gravity );
	Menu_AddItem( &s_devworld.menu, &s_devworld.runspeed );
	Menu_AddItem( &s_devworld.menu, &s_devworld.dash );
	Menu_AddItem( &s_devworld.menu, &s_devworld.bspeed );
	Menu_AddItem( &s_devworld.menu, &s_devworld.knockback );
	Menu_AddItem( &s_devworld.menu, &s_devworld.hotfloor );
	Menu_AddItem( &s_devworld.menu, &s_devworld.friendly );
	Menu_AddItem( &s_devworld.menu, &s_devworld.back );

	DevWorldMenu_SetItems();
}


/*
=================================================================
   DEBUG subpanel — intentions latched for the next LAUNCH
=================================================================
*/

typedef struct {
	menuframework_s	menu;

	menulist_s		cheats;
	menulist_s		giveall;
	menulist_s		god;
	menulist_s		noclip;

	menutext_s		back;
} devdbgmenu_t;

static devdbgmenu_t	s_devdbg;


static void DevDbgMenu_Event( void *ptr, int notification ) {
	if ( notification != QM_ACTIVATED ) {
		return;
	}

	switch ( ((menucommon_s *)ptr)->id ) {
	case ID_DBG_CHEATS:
		dev_cheats = s_devdbg.cheats.curvalue;
		break;
	case ID_DBG_GIVEALL:
		dev_giveAll = s_devdbg.giveall.curvalue;
		break;
	case ID_DBG_GOD:
		dev_god = s_devdbg.god.curvalue;
		break;
	case ID_DBG_NOCLIP:
		dev_noclip = s_devdbg.noclip.curvalue;
		break;
	case ID_DBG_BACK:
		UI_PopMenu();
		break;
	}
}

static void DevDbgMenu_Draw( void ) {
	UI_DrawProportionalString( 320, 70, "DEBUG", UI_CENTER, dev_redhot );
	UI_DrawString( 320, 112, "LATCHED FOR THE NEXT LAUNCH  / /  HANDLE WITH CARE",
		UI_CENTER|UI_SMALLFONT, dev_green );

	UI_FillRect( 180, 150, 280, 1, dev_grid );
	UI_DrawString( 320, 154, "[  CHEATS  ]", UI_CENTER|UI_SMALLFONT, dev_green );

	Menu_Draw( &s_devdbg.menu );

	UI_DrawString( 320, 360,
		"god / noclip / give all force a devmap (cheats on).",
		UI_CENTER|UI_SMALLFONT, dev_dim );

	UI_DrawString( 320, 464, "DEVMODE // debug // NERV",
		UI_CENTER|UI_SMALLFONT, dev_dim );
}

static void DevDbgMenu_Init( void ) {
	int	y;

	memset( &s_devdbg, 0, sizeof( s_devdbg ) );

	s_devdbg.menu.draw       = DevDbgMenu_Draw;
	s_devdbg.menu.fullscreen = qtrue;
	s_devdbg.menu.wrapAround = qtrue;

	y = 178;
	DevMenu_AddSpin( &s_devdbg.cheats,  ID_DBG_CHEATS,  320, y, "CHEATS (devmap):", dev_onoff_names, DevDbgMenu_Event );
	y += 24;
	DevMenu_AddSpin( &s_devdbg.giveall, ID_DBG_GIVEALL, 320, y, "GIVE ALL:",        dev_onoff_names, DevDbgMenu_Event );
	y += 24;
	DevMenu_AddSpin( &s_devdbg.god,     ID_DBG_GOD,     320, y, "GODMODE:",         dev_onoff_names, DevDbgMenu_Event );
	y += 24;
	DevMenu_AddSpin( &s_devdbg.noclip,  ID_DBG_NOCLIP,  320, y, "NOCLIP:",          dev_onoff_names, DevDbgMenu_Event );

	y += 40;
	DevMenu_AddText( &s_devdbg.back, ID_DBG_BACK, 320, y, "BACK", dev_dim,
		UI_CENTER|UI_DROPSHADOW|UI_SMALLFONT, DevDbgMenu_Event );

	Menu_AddItem( &s_devdbg.menu, &s_devdbg.cheats );
	Menu_AddItem( &s_devdbg.menu, &s_devdbg.giveall );
	Menu_AddItem( &s_devdbg.menu, &s_devdbg.god );
	Menu_AddItem( &s_devdbg.menu, &s_devdbg.noclip );
	Menu_AddItem( &s_devdbg.menu, &s_devdbg.back );

	// seed from the latched intentions
	s_devdbg.cheats.curvalue  = dev_cheats  != 0;
	s_devdbg.giveall.curvalue = dev_giveAll != 0;
	s_devdbg.god.curvalue     = dev_god     != 0;
	s_devdbg.noclip.curvalue  = dev_noclip  != 0;
}


/*
=================================================================
   DEVMODE hub
=================================================================
*/

/*
=================
DevMenu_SetItems

Seed the hub controls from the live cvars so the bench reflects the
current state every time it opens fresh.
=================
*/
static void DevMenu_SetItems( void ) {
	int		bots, skill, i;
	char	curmap[MAX_QPATH];

	// map: match the saved name back to the spinner, default to the first
	s_dev.map.curvalue = 0;
	trap_Cvar_VariableStringBuffer( "ui_devMap", curmap, sizeof( curmap ) );
	for ( i = 0; i < (int)NUM_DEV_MAPS; i++ ) {
		if ( !Q_stricmp( curmap, dev_map_keys[i] ) ) {
			s_dev.map.curvalue = i;
			break;
		}
	}

	// weapon
	if ( trap_Cvar_VariableValue( "g_vectorgun" ) != 0 ) {
		s_dev.weapon.curvalue = DW_VECTORGUN;
	} else {
		s_dev.weapon.curvalue = DW_SWORD;
	}

	bots = (int)trap_Cvar_VariableValue( "bot_minplayers" );
	if ( bots < 0 ) {
		bots = 0;
	} else if ( bots > MAX_DEV_BOTS ) {
		bots = MAX_DEV_BOTS;
	}
	s_dev.bots.curvalue = bots;

	skill = (int)trap_Cvar_VariableValue( "g_spSkill" ) - 1;
	if ( skill < 0 ) {
		skill = 0;
	} else if ( skill > 4 ) {
		skill = 4;
	}
	s_dev.skill.curvalue = skill;

	// gameplay feature toggles
	s_dev.voidrise.curvalue   = trap_Cvar_VariableValue( "g_voidRise" ) != 0;
	s_dev.bullettime.curvalue = trap_Cvar_VariableValue( "g_timeBind" ) != 0;
	s_dev.lattice.curvalue    = trap_Cvar_VariableValue( "g_lattice" ) != 0;
	s_dev.grapple.curvalue    = trap_Cvar_VariableValue( "g_grappleInstant" ) != 0;
}


/*
=================
DevMenu_Launch

Latch the chosen feature combination, set the weapon ruleset, load the
map (devmap when cheats are on) and seat the bots. Cheat conveniences
(give all / god / noclip) run a few frames after the world spawns.
=================
*/
static void DevMenu_Launch( void ) {
	int			weapon  = s_dev.weapon.curvalue;
	int			bots    = s_dev.bots.curvalue;
	int			skill   = s_dev.skill.curvalue + 1;
	const char	*mapname = dev_map_keys[ s_dev.map.curvalue ];
	qboolean	giveAll = ( dev_giveAll || weapon == DW_ALL );
	qboolean	cheats  = ( dev_cheats || dev_god || dev_noclip || giveAll );
	int			i, delay;
	char		cmd[128];

	// --- gameplay feature latches (applied on the map load queued below) ---
	trap_Cvar_SetValue( "g_voidRise", s_dev.voidrise.curvalue );
	trap_Cvar_SetValue( "g_timeBind", s_dev.bullettime.curvalue );
	trap_Cvar_SetValue( "g_lattice", s_dev.lattice.curvalue );
	trap_Cvar_SetValue( "g_grappleInstant", s_dev.grapple.curvalue );
	trap_Cvar_SetValue( "g_gametype", 0 );			// FFA

	// --- weapon ruleset ---
	switch ( weapon ) {
	case DW_VECTORGUN:
		trap_Cvar_SetValue( "g_vectorgun", 1 );
		trap_Cvar_SetValue( "g_botSwordOnly", 0 );
		trap_Cmd_ExecuteText( EXEC_APPEND, UI_BIND_VECTORGUN );	// WP_RAILGUN
		break;
	case DW_ALL:
		trap_Cvar_SetValue( "g_vectorgun", 0 );
		trap_Cvar_SetValue( "g_botSwordOnly", 0 );
		break;
	default:	// DW_SWORD
		trap_Cvar_SetValue( "g_vectorgun", 0 );
		trap_Cvar_SetValue( "g_botSwordOnly", 1 );
		trap_Cmd_ExecuteText( EXEC_APPEND, UI_BIND_SWORD );	// WP_SWORD
		break;
	}

	// --- bots ---
	if ( bots > 0 ) {
		trap_Cvar_Set( "bot_enable", "1" );
	}
	trap_Cvar_SetValue( "bot_minplayers", bots );
	trap_Cvar_SetValue( "g_spSkill", skill );

	// remember the chosen map so the bench reopens on it
	trap_Cvar_Set( "ui_devMap", mapname );

	// --- load (devmap unlocks god/noclip/give all) ---
	Com_sprintf( cmd, sizeof( cmd ), "%s %s\n", cheats ? "devmap" : "map", mapname );
	trap_Cmd_ExecuteText( EXEC_APPEND, cmd );

	// --- cheat conveniences, once the world is up ---
	if ( cheats ) {
		trap_Cmd_ExecuteText( EXEC_APPEND, "wait 60\n" );
		if ( giveAll ) {
			trap_Cmd_ExecuteText( EXEC_APPEND, "give all\n" );
		}
		if ( dev_god ) {
			trap_Cmd_ExecuteText( EXEC_APPEND, "god\n" );
		}
		if ( dev_noclip ) {
			trap_Cmd_ExecuteText( EXEC_APPEND, "noclip\n" );
		}
	}

	// --- seat the bots, staggered so they trickle in ---
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


static void DevMenu_Event( void *ptr, int notification ) {
	if ( notification != QM_ACTIVATED ) {
		return;
	}

	switch ( ((menucommon_s *)ptr)->id ) {
	case ID_LAUNCH:
		DevMenu_Launch();
		break;

	case ID_FXLINK:
		DevFxMenu_Init();
		UI_PushMenu( &s_devfx.menu );
		break;

	case ID_WORLDLINK:
		DevWorldMenu_Init();
		UI_PushMenu( &s_devworld.menu );
		break;

	case ID_DBGLINK:
		DevDbgMenu_Init();
		UI_PushMenu( &s_devdbg.menu );
		break;

	case ID_BACK:
		UI_PopMenu();
		break;
	}
}


static void DevMenu_Draw( void ) {
	UI_DrawProportionalString( 320, 64, "DEVMODE", UI_CENTER, dev_amber );
	UI_DrawString( 320, 104, "TEST BENCH  / /  ANY MAP  -  ANY FEATURE  -  ANY MIX",
		UI_CENTER|UI_SMALLFONT, dev_green );

	UI_FillRect( 180, 134, 280, 1, dev_grid );
	UI_DrawString( 320, 138, "[  LAUNCH CONFIG  ]", UI_CENTER|UI_SMALLFONT, dev_green );

	UI_FillRect( 180, 250, 280, 1, dev_grid );
	UI_DrawString( 320, 254, "[  FEATURES  ]", UI_CENTER|UI_SMALLFONT, dev_green );

	Menu_Draw( &s_dev.menu );

	UI_DrawString( 320, 464, "DEVMODE // free combination bench // NERV",
		UI_CENTER|UI_SMALLFONT, dev_dim );
}


static void DevMenu_Init( void ) {
	int	y;

	memset( &s_dev, 0, sizeof( s_dev ) );

	s_dev.menu.draw       = DevMenu_Draw;
	s_dev.menu.fullscreen = qtrue;
	s_dev.menu.wrapAround = qtrue;

	// ---- LAUNCH CONFIG ----
	y = 158;
	DevMenu_AddSpin( &s_dev.map,    ID_MAP,    320, y, "MAP:",       dev_map_names,      DevMenu_Event );
	y += 22;
	DevMenu_AddSpin( &s_dev.weapon, ID_WEAPON, 320, y, "WEAPON:",    dev_weapon_names,   DevMenu_Event );
	y += 22;
	DevMenu_AddSpin( &s_dev.bots,   ID_BOTS,   320, y, "BOTS:",      dev_opponent_names, DevMenu_Event );
	y += 22;
	DevMenu_AddSpin( &s_dev.skill,  ID_SKILL,  320, y, "BOT SKILL:", dev_skill_names,    DevMenu_Event );

	// ---- FEATURES (header rule drawn at y=250 in DevMenu_Draw) ----
	y = 274;
	DevMenu_AddSpin( &s_dev.voidrise,   ID_VOID,       320, y, "RISING VOID:",   dev_onoff_names, DevMenu_Event );
	y += 22;
	DevMenu_AddSpin( &s_dev.bullettime, ID_BULLETTIME, 320, y, "BULLET-TIME:",   dev_onoff_names, DevMenu_Event );
	y += 22;
	DevMenu_AddSpin( &s_dev.lattice,    ID_LATTICE,    320, y, "LATTICE TRAILS:", dev_onoff_names, DevMenu_Event );
	y += 22;
	DevMenu_AddSpin( &s_dev.grapple,    ID_GRAPPLE,    320, y, "INSTANT GRAPPLE:", dev_onoff_names, DevMenu_Event );

	// ---- LAUNCH (hero) ----
	y += 36;
	DevMenu_AddText( &s_dev.launch, ID_LAUNCH, 320, y, "LAUNCH", dev_amber,
		UI_CENTER|UI_DROPSHADOW, DevMenu_Event );

	// ---- subpanel links (side by side) ----
	y += 32;
	DevMenu_AddText( &s_dev.fxlink,    ID_FXLINK,    198, y, "VISUAL FX", dev_green,
		UI_CENTER|UI_SMALLFONT, DevMenu_Event );
	DevMenu_AddText( &s_dev.worldlink, ID_WORLDLINK, 320, y, "WORLD", dev_green,
		UI_CENTER|UI_SMALLFONT, DevMenu_Event );
	DevMenu_AddText( &s_dev.dbglink,   ID_DBGLINK,   438, y, "DEBUG", dev_redhot,
		UI_CENTER|UI_SMALLFONT, DevMenu_Event );

	// ---- BACK ----
	y += 26;
	DevMenu_AddText( &s_dev.back, ID_BACK, 320, y, "BACK", dev_dim,
		UI_CENTER|UI_DROPSHADOW|UI_SMALLFONT, DevMenu_Event );

	Menu_AddItem( &s_dev.menu, &s_dev.map );
	Menu_AddItem( &s_dev.menu, &s_dev.weapon );
	Menu_AddItem( &s_dev.menu, &s_dev.bots );
	Menu_AddItem( &s_dev.menu, &s_dev.skill );
	Menu_AddItem( &s_dev.menu, &s_dev.voidrise );
	Menu_AddItem( &s_dev.menu, &s_dev.bullettime );
	Menu_AddItem( &s_dev.menu, &s_dev.lattice );
	Menu_AddItem( &s_dev.menu, &s_dev.grapple );
	Menu_AddItem( &s_dev.menu, &s_dev.launch );
	Menu_AddItem( &s_dev.menu, &s_dev.fxlink );
	Menu_AddItem( &s_dev.menu, &s_dev.worldlink );
	Menu_AddItem( &s_dev.menu, &s_dev.dbglink );
	Menu_AddItem( &s_dev.menu, &s_dev.back );

	DevMenu_SetItems();
}


/*
=================
UI_DevMenu
=================
*/
void UI_DevMenu( void ) {
	DevMenu_Init();
	trap_Key_SetCatcher( KEYCATCH_UI );
	UI_PushMenu( &s_dev.menu );
}
