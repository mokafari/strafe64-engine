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

STRAFE 64 — FORGE  (in-game strafegen)

Author a course without leaving the game: scrub the parameters
(kind / seed / difficulty / length / void), read a live schematic of the
spine on the right, and hit FORGE. The menu emits a `forge` console command;
the engine runs strafegen.py as a child process, drops the fresh .bsp/.aas
into the writable game dir and devmaps it.

See docs/forge-ingame-strafegen-spec.md.

=======================================================================
*/

#include "ui_local.h"


#define ID_KIND			50
#define ID_DIFF			51
#define ID_LENGTH		52
#define ID_VOID			53
#define ID_SEED			54
#define ID_RANDOM		55
#define ID_FORGE		56
#define ID_BACK			57
#define ID_ARCH			58
#define ID_GFX			59
#define ID_SIZE			60
#define ID_DENSITY		61

// preview panel geometry
#define PV_X			384
#define PV_Y			198
#define PV_W			232
#define PV_H			196

// kinds (index → strafegen kind token)
enum {
	KIND_COURSE,
	KIND_COMBAT,
	KIND_ARENA,
	KIND_SURF,
	KIND_KILLBOX,
	KIND_SURFTURN,
	KIND_LATTICE,
	KIND_COUNT
};


// NERV/MAGI palette — kept in sync with ui_menu.c / ui_arena.c
static vec4_t forge_amber = { 1.00f, 0.62f, 0.05f, 1.00f };  // headline / FORGE
static vec4_t forge_green = { 0.45f, 1.00f, 0.55f, 1.00f };  // terminal green
static vec4_t forge_grid  = { 0.10f, 0.55f, 0.22f, 0.16f };  // dim rule
static vec4_t forge_dim   = { 0.45f, 0.55f, 0.55f, 1.00f };  // labels
static vec4_t forge_panel = { 0.04f, 0.09f, 0.07f, 0.55f };  // preview backdrop


typedef struct {
	menuframework_s	menu;

	menulist_s		kind;
	menulist_s		archetype;
	menulist_s		difficulty;
	menulist_s		length;
	menulist_s		voidrise;
	menulist_s		graphics;
	menulist_s		size;
	menulist_s		density;
	menufield_s		seed;

	menutext_s		randomize;
	menutext_s		forge;
	menutext_s		back;

	qboolean		forging;	// drew the FORGING frame, command queued
} forgemenu_t;

static forgemenu_t	s_forge;

static const char *kind_names[] = {
	"COURSE",
	"COMBAT",
	"ARENA",
	"SURF",
	"KILLBOX",
	"SURFTURN",
	"LATTICE",
	NULL
};

// the strafegen kind token for each menu index
static const char *kind_tokens[] = {
	"course", "combat", "arena", "surf", "killbox", "surfturn", "lattice"
};

// killbox centerpiece archetypes. Index 0 = AUTO (seed-random, token "-");
// the rest map 1:1 to strafegen --arch. Grayed unless kind == KILLBOX.
static const char *arch_names[] = {
	"AUTO",
	"SPIRE",
	"SPIRAL",
	"FOREST",
	"RING",
	"CROSS",
	"TWIN",
	"COURT",
	NULL
};

static const char *arch_tokens[] = {
	"-", "spire", "spiral", "forest", "ring", "cross", "twin", "court"
};

static const char *diff_names[] = {
	"I",
	"II",
	"III",
	NULL
};

static const char *length_names[] = {
	"x1",
	"x2",
	"x3",
	"x4",
	"x5",
	NULL
};

static const char *gen_onoff_names[] = {
	"OFF",
	"ON",
	NULL
};

// geometry-modifier presets -> strafegen --gen-* float flags (Com_Forge_f maps
// these tokens to numeric scales). NORMAL = no flag (unchanged).
static const char *size_names[] = {
	"SMALL",
	"NORMAL",
	"LARGE",
	"HUGE",
	NULL
};
static const char *size_tokens[] = { "small", "normal", "large", "huge" };

static const char *density_names[] = {
	"SPARSE",
	"NORMAL",
	"DENSE",
	NULL
};
static const char *density_tokens[] = { "sparse", "normal", "dense" };


/*
=================
ForgeMenu_Rand

Tiny deterministic LCG, used both for the RANDOMIZE button (seeded from the
clock) and for the schematic spine (seeded from the chosen seed).
=================
*/
static unsigned ForgeMenu_Rand( unsigned *state ) {
	*state = ( *state * 1103515245u ) + 12345u;
	return ( *state >> 16 ) & 0x7fff;
}


/*
=================
ForgeMenu_CurrentSeed
=================
*/
static int ForgeMenu_CurrentSeed( void ) {
	// Keep the seed non-negative so it round-trips cleanly through the
	// command line and the unsigned LCG (matches RANDOMIZE, which also
	// folds negatives away). An empty/garbage field decays to 0.
	int seed = atoi( s_forge.seed.field.buffer );
	if ( seed < 0 ) {
		seed = -seed;
	}
	return seed;
}


/*
=================
ForgeMenu_Randomize

Drop a fresh seed into the field from the realtime clock.
=================
*/
static void ForgeMenu_Randomize( void ) {
	unsigned	state = (unsigned)uis.realtime * 2654435761u + 101u;
	int			seed;

	seed = ( ForgeMenu_Rand( &state ) << 8 ) ^ ForgeMenu_Rand( &state );
	if ( seed < 0 ) {
		seed = -seed;
	}
	Com_sprintf( s_forge.seed.field.buffer, sizeof( s_forge.seed.field.buffer ),
		"%i", seed );
	s_forge.seed.field.cursor = strlen( s_forge.seed.field.buffer );
}


/*
=================
ForgeMenu_LengthApplies

Only the linear runs (course/combat) honour the length multiplier; the
arena/surf/killbox builders are single-piece.
=================
*/
static qboolean ForgeMenu_LengthApplies( void ) {
	int k = s_forge.kind.curvalue;
	return ( k == KIND_COURSE || k == KIND_COMBAT );
}


/*
=================
ForgeMenu_UpdateGrays
=================
*/
static void ForgeMenu_UpdateGrays( void ) {
	if ( ForgeMenu_LengthApplies() ) {
		s_forge.length.generic.flags &= ~QMF_GRAYED;
	} else {
		s_forge.length.generic.flags |= QMF_GRAYED;
	}
	// the archetype only steers the killbox centerpiece
	if ( s_forge.kind.curvalue == KIND_KILLBOX ) {
		s_forge.archetype.generic.flags &= ~QMF_GRAYED;
	} else {
		s_forge.archetype.generic.flags |= QMF_GRAYED;
	}
}


/*
=================
ForgeMenu_Dot

A small filled square — the schematic is drawn from these so arbitrary-angle
spines read without needing real line primitives.
=================
*/
static void ForgeMenu_Dot( float x, float y, float s, vec4_t color ) {
	// clip to the preview panel
	if ( x < PV_X || x > PV_X + PV_W || y < PV_Y || y > PV_Y + PV_H ) {
		return;
	}
	UI_FillRect( x, y, s, s, color );
}


/*
=================
ForgeMenu_Segment

Dotted line between two points (axis-aligned rects can't draw diagonals).
=================
*/
static void ForgeMenu_Segment( float x0, float y0, float x1, float y1,
		vec4_t color ) {
	float	dx = x1 - x0;
	float	dy = y1 - y0;
	float	len = sqrt( dx * dx + dy * dy );
	int		steps = (int)( len / 6.0f );
	int		i;

	if ( steps < 1 ) {
		steps = 1;
	}
	for ( i = 0; i <= steps; i++ ) {
		float t = (float)i / (float)steps;
		ForgeMenu_Dot( x0 + dx * t, y0 + dy * t, 2, color );
	}
}


/*
=================
ForgeMenu_DrawPreview

A SCHEMATIC of the spine — drawn in C from the same inputs (kind, seed,
length), updated every frame. Not the real BSP: it conveys that the seed
winds a different path, length grows it, and each kind has its own
silhouette. Honest about being a sketch.
=================
*/
static void ForgeMenu_DrawPreview( void ) {
	unsigned	state;
	int			kind = s_forge.kind.curvalue;
	int			diff = s_forge.difficulty.curvalue;
	int			i;
	vec4_t		spine;
	vec4_t		hot;
	float		cx = PV_X + PV_W * 0.5f;
	float		cy = PV_Y + PV_H * 0.5f;

	// panel + frame + header
	UI_FillRect( PV_X, PV_Y, PV_W, PV_H, forge_panel );
	UI_FillRect( PV_X, PV_Y, PV_W, 1, forge_grid );
	UI_FillRect( PV_X, PV_Y + PV_H, PV_W, 1, forge_grid );
	UI_FillRect( PV_X, PV_Y, 1, PV_H, forge_grid );
	UI_FillRect( PV_X + PV_W, PV_Y, 1, PV_H, forge_grid );
	UI_DrawString( PV_X + PV_W / 2, PV_Y + 6, "SCHEMATIC  -  not to scale",
		UI_CENTER|UI_SMALLFONT, forge_dim );

	// difficulty tints the spine hotter
	spine[0] = 0.30f + 0.35f * diff;
	spine[1] = 1.00f - 0.30f * diff;
	spine[2] = 0.55f - 0.20f * diff;
	spine[3] = 0.90f;
	hot[0] = 1.0f; hot[1] = 0.16f; hot[2] = 0.22f; hot[3] = 1.0f;

	// seed drives the layout
	state = (unsigned)ForgeMenu_CurrentSeed() * 2246822519u + 1u + kind * 7919u;

	if ( kind == KIND_COURSE || kind == KIND_COMBAT ) {
		// serpentine descent: nodes walk downward, wiggling sideways; length
		// grows the node count
		int		mult = ( ForgeMenu_LengthApplies() ) ? s_forge.length.curvalue + 1 : 1;
		int		nodes = 5 + 3 * mult;
		float	px, py, prevx, prevy;
		float	top = PV_Y + 26;
		float	bot = PV_Y + PV_H - 18;

		if ( nodes > 22 ) {
			nodes = 22;
		}
		prevx = cx;
		prevy = top;
		for ( i = 0; i < nodes; i++ ) {
			float frac = (float)i / (float)( nodes - 1 );
			float wob = (float)( ForgeMenu_Rand( &state ) % 1000 ) / 1000.0f - 0.5f;
			px = cx + wob * ( PV_W - 60 );
			py = top + frac * ( bot - top );
			if ( i > 0 ) {
				ForgeMenu_Segment( prevx, prevy, px, py, spine );
			}
			ForgeMenu_Dot( px - 1, py - 1, 3,
				( i == 0 ) ? forge_green : ( i == nodes - 1 ) ? forge_amber : spine );
			prevx = px;
			prevy = py;
		}
		UI_DrawString( PV_X + 8, bot + 4, "START", UI_LEFT|UI_SMALLFONT, forge_green );
		UI_DrawString( PV_X + PV_W - 8, PV_Y + 20, "FINISH",
			UI_RIGHT|UI_SMALLFONT, forge_amber );

		// the rising void: a dashed floor that climbs (when on)
		if ( s_forge.voidrise.curvalue ) {
			float vy = bot - 6;
			for ( i = 0; i < PV_W - 20; i += 8 ) {
				ForgeMenu_Dot( PV_X + 10 + i, vy, 2, hot );
			}
			UI_DrawString( PV_X + PV_W / 2, vy + 4, "VOID RISING",
				UI_CENTER|UI_SMALLFONT, hot );
		}

	} else if ( kind == KIND_SURF ) {
		// a banked lap: an oval ring of dots
		int		n = 40;
		float	rx = ( PV_W - 50 ) * 0.5f;
		float	ry = ( PV_H - 60 ) * 0.5f;
		float	jx = ( (float)( ForgeMenu_Rand( &state ) % 100 ) - 50 ) * 0.4f;
		for ( i = 0; i < n; i++ ) {
			float a = ( (float)i / (float)n ) * 6.2831853f;
			ForgeMenu_Dot( cx + jx + cos( a ) * rx, cy + sin( a ) * ry, 2, spine );
		}
		ForgeMenu_Dot( cx + jx + rx - 1, cy - 1, 4, forge_green );	// start/finish
		UI_DrawString( PV_X + PV_W / 2, cy, "LAP", UI_CENTER|UI_SMALLFONT, forge_dim );

	} else {
		// arena / killbox: a bounded box with a seed-chosen centerpiece + spawns
		int		arch = ForgeMenu_Rand( &state ) % 4;
		float	bx0 = PV_X + 24, by0 = PV_Y + 28;
		float	bx1 = PV_X + PV_W - 24, by1 = PV_Y + PV_H - 18;
		int		spawns;

		// arena floor
		ForgeMenu_Segment( bx0, by0, bx1, by0, forge_grid );
		ForgeMenu_Segment( bx0, by1, bx1, by1, forge_grid );
		ForgeMenu_Segment( bx0, by0, bx0, by1, forge_grid );
		ForgeMenu_Segment( bx1, by0, bx1, by1, forge_grid );

		// centerpiece silhouette by archetype
		switch ( arch ) {
		case 0:	// spire — a vertical bar
			ForgeMenu_Segment( cx, cy - 30, cx, cy + 30, spine );
			ForgeMenu_Dot( cx - 2, cy - 34, 5, forge_amber );
			break;
		case 1:	// ring — a small circle
			for ( i = 0; i < 20; i++ ) {
				float a = ( (float)i / 20.0f ) * 6.2831853f;
				ForgeMenu_Dot( cx + cos( a ) * 26, cy + sin( a ) * 22, 2, spine );
			}
			break;
		case 2:	// cross — an X of segments
			ForgeMenu_Segment( cx - 26, cy - 22, cx + 26, cy + 22, spine );
			ForgeMenu_Segment( cx - 26, cy + 22, cx + 26, cy - 22, spine );
			break;
		default: // forest — scattered pillars
			for ( i = 0; i < 6; i++ ) {
				float fx = cx + ( (float)( ForgeMenu_Rand( &state ) % 100 ) - 50 ) * 0.9f;
				float fy = cy + ( (float)( ForgeMenu_Rand( &state ) % 100 ) - 50 ) * 0.6f;
				ForgeMenu_Dot( fx, fy, 4, spine );
			}
			break;
		}

		// a handful of spawn points around the rim
		spawns = ( kind == KIND_KILLBOX ) ? 8 : 5;
		for ( i = 0; i < spawns; i++ ) {
			float a = ( (float)i / (float)spawns ) * 6.2831853f
				+ ( ForgeMenu_Rand( &state ) % 100 ) * 0.01f;
			ForgeMenu_Dot( cx + cos( a ) * ( PV_W * 0.40f ),
				cy + sin( a ) * ( PV_H * 0.34f ), 3, forge_green );
		}
		UI_DrawString( PV_X + PV_W / 2, by1 + 4,
			( kind == KIND_KILLBOX ) ? "VERTICAL  ARENA" : "DUEL  RING",
			UI_CENTER|UI_SMALLFONT, forge_dim );
	}
}


/*
=================
ForgeMenu_StartForge

Emit the forge command. The engine forks strafegen, deploys the map and
devmaps it. We drew the FORGING frame already (see ForgeMenu_Draw); the
command buffer runs after this UI frame.
=================
*/
static void ForgeMenu_StartForge( void ) {
	char	cmd[128];
	int		seed = ForgeMenu_CurrentSeed();

	if ( seed <= 0 ) {
		ForgeMenu_Randomize();
		seed = ForgeMenu_CurrentSeed();
	}

	// Fixed 7-arg form so the optional tail (archetype, graphics) keeps its slot
	// even when an earlier toggle is off — emit explicit void/novoid + arch "-"
	// placeholder + gfx/nogfx rather than blank tokens that would collapse.
	Com_sprintf( cmd, sizeof( cmd ), "forge %s %i %i %i %s %s %s %s %s\n",
		kind_tokens[s_forge.kind.curvalue],
		seed,
		s_forge.difficulty.curvalue,
		s_forge.length.curvalue + 1,
		s_forge.voidrise.curvalue ? "void" : "novoid",
		arch_tokens[s_forge.archetype.curvalue],
		s_forge.graphics.curvalue ? "gfx" : "nogfx",
		size_tokens[s_forge.size.curvalue],
		density_tokens[s_forge.density.curvalue] );
	trap_Cmd_ExecuteText( EXEC_APPEND, cmd );
}


/*
=================
ForgeMenu_Event
=================
*/
static void ForgeMenu_Event( void *ptr, int notification ) {
	if ( notification != QM_ACTIVATED ) {
		return;
	}

	switch ( ((menucommon_s *)ptr)->id ) {
	case ID_KIND:
		ForgeMenu_UpdateGrays();
		break;

	case ID_RANDOM:
		ForgeMenu_Randomize();
		break;

	case ID_FORGE:
		// flag the FORGE so the next draw paints the FORGING banner before the
		// queued command blocks the frame
		s_forge.forging = qtrue;
		ForgeMenu_StartForge();
		break;

	case ID_BACK:
		UI_PopMenu();
		break;
	}
}


/*
=================
ForgeMenu_Draw
=================
*/
static void ForgeMenu_Draw( void ) {
	UI_DrawProportionalString( 320, 70, "FORGE", UI_CENTER, forge_amber );
	UI_DrawString( 320, 112, "AUTHOR A COURSE  / /  SCRUB THE SEED, READ THE SPINE",
		UI_CENTER|UI_SMALLFONT, forge_green );

	UI_FillRect( 24, 150, 330, 1, forge_grid );
	UI_DrawString( 30, 154, "[  PARAMETERS  ]", UI_LEFT|UI_SMALLFONT, forge_green );

	// live schematic on the right
	ForgeMenu_DrawPreview();

	Menu_Draw( &s_forge.menu );

	// footer shows the exact map that FORGE will produce
	{
		char	foot[128];
		Com_sprintf( foot, sizeof( foot ),
			"STRAFEGEN // baseoa/maps // forge_%s_%i",
			kind_tokens[s_forge.kind.curvalue], ForgeMenu_CurrentSeed() );
		UI_DrawString( 320, 452, foot, UI_CENTER|UI_SMALLFONT, forge_dim );
	}

	// FORGING banner: we set this on the FORGE press; the generate command runs
	// after this frame so the player gets one frame of feedback before the freeze
	if ( s_forge.forging ) {
		vec4_t	bg = { 0.0f, 0.0f, 0.0f, 0.65f };
		UI_FillRect( 0, 222, 640, 40, bg );
		UI_DrawProportionalString( 320, 226, "FORGING...", UI_CENTER, forge_amber );
	}
}


/*
=================
ForgeMenu_Init
=================
*/
static void ForgeMenu_Init( void ) {
	int		x, y;

	memset( &s_forge, 0, sizeof( forgemenu_t ) );

	s_forge.menu.draw       = ForgeMenu_Draw;
	s_forge.menu.fullscreen = qtrue;
	s_forge.menu.wrapAround  = qtrue;

	x = 232;	// spin/field pivot (names right of this, values left)
	y = 188;	// tightened to fit the extra ARCHETYPE/GRAPHICS/SIZE/DENSITY rows

	s_forge.kind.generic.type		= MTYPE_SPINCONTROL;
	s_forge.kind.generic.flags		= QMF_PULSEIFFOCUS|QMF_SMALLFONT;
	s_forge.kind.generic.x			= x;
	s_forge.kind.generic.y			= y;
	s_forge.kind.generic.name		= "KIND:";
	s_forge.kind.generic.id			= ID_KIND;
	s_forge.kind.generic.callback	= ForgeMenu_Event;
	s_forge.kind.itemnames			= kind_names;

	y += 22;
	s_forge.archetype.generic.type		= MTYPE_SPINCONTROL;
	s_forge.archetype.generic.flags		= QMF_PULSEIFFOCUS|QMF_SMALLFONT;
	s_forge.archetype.generic.x			= x;
	s_forge.archetype.generic.y			= y;
	s_forge.archetype.generic.name		= "ARCHETYPE:";
	s_forge.archetype.generic.id		= ID_ARCH;
	s_forge.archetype.generic.callback	= ForgeMenu_Event;
	s_forge.archetype.itemnames			= arch_names;

	y += 22;
	s_forge.difficulty.generic.type		= MTYPE_SPINCONTROL;
	s_forge.difficulty.generic.flags	= QMF_PULSEIFFOCUS|QMF_SMALLFONT;
	s_forge.difficulty.generic.x		= x;
	s_forge.difficulty.generic.y		= y;
	s_forge.difficulty.generic.name		= "DIFFICULTY:";
	s_forge.difficulty.generic.id		= ID_DIFF;
	s_forge.difficulty.generic.callback	= ForgeMenu_Event;
	s_forge.difficulty.itemnames		= diff_names;
	s_forge.difficulty.curvalue			= 1;

	y += 22;
	s_forge.length.generic.type		= MTYPE_SPINCONTROL;
	s_forge.length.generic.flags	= QMF_PULSEIFFOCUS|QMF_SMALLFONT;
	s_forge.length.generic.x		= x;
	s_forge.length.generic.y		= y;
	s_forge.length.generic.name		= "LENGTH:";
	s_forge.length.generic.id		= ID_LENGTH;
	s_forge.length.generic.callback	= ForgeMenu_Event;
	s_forge.length.itemnames		= length_names;

	y += 22;
	s_forge.voidrise.generic.type		= MTYPE_SPINCONTROL;
	s_forge.voidrise.generic.flags		= QMF_PULSEIFFOCUS|QMF_SMALLFONT;
	s_forge.voidrise.generic.x			= x;
	s_forge.voidrise.generic.y			= y;
	s_forge.voidrise.generic.name		= "RISING VOID:";
	s_forge.voidrise.generic.id			= ID_VOID;
	s_forge.voidrise.generic.callback	= ForgeMenu_Event;
	s_forge.voidrise.itemnames			= gen_onoff_names;
	s_forge.voidrise.curvalue			= 1;

	y += 22;
	s_forge.graphics.generic.type		= MTYPE_SPINCONTROL;
	s_forge.graphics.generic.flags		= QMF_PULSEIFFOCUS|QMF_SMALLFONT;
	s_forge.graphics.generic.x			= x;
	s_forge.graphics.generic.y			= y;
	s_forge.graphics.generic.name		= "GRAPHICS:";
	s_forge.graphics.generic.id			= ID_GFX;
	s_forge.graphics.generic.callback	= ForgeMenu_Event;
	s_forge.graphics.itemnames			= gen_onoff_names;
	s_forge.graphics.curvalue			= 1;

	y += 22;
	s_forge.size.generic.type		= MTYPE_SPINCONTROL;
	s_forge.size.generic.flags		= QMF_PULSEIFFOCUS|QMF_SMALLFONT;
	s_forge.size.generic.x			= x;
	s_forge.size.generic.y			= y;
	s_forge.size.generic.name		= "SIZE:";
	s_forge.size.generic.id			= ID_SIZE;
	s_forge.size.generic.callback	= ForgeMenu_Event;
	s_forge.size.itemnames			= size_names;
	s_forge.size.curvalue			= 1;	// NORMAL

	y += 22;
	s_forge.density.generic.type		= MTYPE_SPINCONTROL;
	s_forge.density.generic.flags		= QMF_PULSEIFFOCUS|QMF_SMALLFONT;
	s_forge.density.generic.x			= x;
	s_forge.density.generic.y			= y;
	s_forge.density.generic.name		= "DENSITY:";
	s_forge.density.generic.id			= ID_DENSITY;
	s_forge.density.generic.callback	= ForgeMenu_Event;
	s_forge.density.itemnames			= density_names;
	s_forge.density.curvalue			= 1;	// NORMAL

	y += 22;
	s_forge.seed.generic.type		= MTYPE_FIELD;
	s_forge.seed.generic.flags		= QMF_NUMBERSONLY|QMF_PULSEIFFOCUS|QMF_SMALLFONT;
	s_forge.seed.generic.x			= x;
	s_forge.seed.generic.y			= y;
	s_forge.seed.generic.name		= "SEED:";
	s_forge.seed.generic.id			= ID_SEED;
	s_forge.seed.field.widthInChars	= 10;
	s_forge.seed.field.maxchars		= 9;

	y += 30;
	s_forge.randomize.generic.type		= MTYPE_PTEXT;
	s_forge.randomize.generic.flags		= QMF_LEFT_JUSTIFY|QMF_PULSEIFFOCUS;
	s_forge.randomize.generic.x			= x - 4 * SMALLCHAR_WIDTH;
	s_forge.randomize.generic.y			= y;
	s_forge.randomize.generic.id		= ID_RANDOM;
	s_forge.randomize.generic.callback	= ForgeMenu_Event;
	s_forge.randomize.string			= "> RANDOMIZE";
	s_forge.randomize.color				= forge_green;
	s_forge.randomize.style				= UI_LEFT|UI_SMALLFONT;

	// ---- FORGE (the hero) + BACK -------------------------------------
	y = PV_Y + PV_H + 10;
	s_forge.forge.generic.type		= MTYPE_PTEXT;
	s_forge.forge.generic.flags		= QMF_CENTER_JUSTIFY|QMF_PULSEIFFOCUS;
	s_forge.forge.generic.x			= 320;
	s_forge.forge.generic.y			= y;
	s_forge.forge.generic.id		= ID_FORGE;
	s_forge.forge.generic.callback	= ForgeMenu_Event;
	s_forge.forge.string			= "FORGE";
	s_forge.forge.color				= forge_amber;
	s_forge.forge.style				= UI_CENTER|UI_DROPSHADOW;

	y += 30;
	s_forge.back.generic.type		= MTYPE_PTEXT;
	s_forge.back.generic.flags		= QMF_CENTER_JUSTIFY|QMF_PULSEIFFOCUS;
	s_forge.back.generic.x			= 320;
	s_forge.back.generic.y			= y;
	s_forge.back.generic.id			= ID_BACK;
	s_forge.back.generic.callback	= ForgeMenu_Event;
	s_forge.back.string				= "BACK";
	s_forge.back.color				= forge_dim;
	s_forge.back.style				= UI_CENTER|UI_DROPSHADOW|UI_SMALLFONT;

	Menu_AddItem( &s_forge.menu, &s_forge.kind );
	Menu_AddItem( &s_forge.menu, &s_forge.archetype );
	Menu_AddItem( &s_forge.menu, &s_forge.difficulty );
	Menu_AddItem( &s_forge.menu, &s_forge.length );
	Menu_AddItem( &s_forge.menu, &s_forge.voidrise );
	Menu_AddItem( &s_forge.menu, &s_forge.graphics );
	Menu_AddItem( &s_forge.menu, &s_forge.size );
	Menu_AddItem( &s_forge.menu, &s_forge.density );
	Menu_AddItem( &s_forge.menu, &s_forge.seed );
	Menu_AddItem( &s_forge.menu, &s_forge.randomize );
	Menu_AddItem( &s_forge.menu, &s_forge.forge );
	Menu_AddItem( &s_forge.menu, &s_forge.back );

	ForgeMenu_Randomize();
	ForgeMenu_UpdateGrays();
}


/*
=================
UI_GenerateMenu
=================
*/
void UI_GenerateMenu( void ) {
	ForgeMenu_Init();
	trap_Key_SetCatcher( KEYCATCH_UI );
	UI_PushMenu( &s_forge.menu );
}
