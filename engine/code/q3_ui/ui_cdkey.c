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
/*
=======================================================================

STRAFE 64 LICENSE / ACTIVATION SCREEN

Replaces the stock Q3 16-character CD-key screen. Takes the long Ed25519
license key (S64-...), hands it to the engine's `license` command, and reports
the result by polling com_licensed. Built to be PASTED into, not typed: the
key is ~146 characters. Themed to match the MAGI/NERV main menu.

The menu keeps the historical UI_CDKeyMenu* symbol names so the Setup menu,
the cache pass, and the `ui_cdkey` console alias keep working unchanged.

=======================================================================
*/

#include "ui_local.h"

#define ID_KEYFIELD		10
#define ID_PASTE		11
#define ID_ACTIVATE		12
#define ID_BACK			13

// theme palette (mirrors ui_menu.c's MAGI look)
static vec4_t lic_amber  = { 1.00f, 0.62f, 0.05f, 1.00f };	// headline
static vec4_t lic_green  = { 0.45f, 1.00f, 0.55f, 1.00f };	// terminal green / OK
static vec4_t lic_grid   = { 0.10f, 0.55f, 0.22f, 0.16f };	// dim rule
static vec4_t lic_dim    = { 0.45f, 0.55f, 0.55f, 1.00f };	// labels
static vec4_t lic_redhot = { 1.00f, 0.16f, 0.22f, 1.00f };	// reject red
static vec4_t lic_panel  = { 0.04f, 0.09f, 0.07f, 0.55f };	// field backdrop
static vec4_t lic_back   = { 0.02f, 0.04f, 0.05f, 0.92f };	// fullscreen scrim

#define FIELD_X			70
#define FIELD_Y			236
#define FIELD_W			500

typedef struct {
	menuframework_s	menu;

	menufield_s		keyfield;
	menutext_s		paste;
	menutext_s		activate;
	menutext_s		back;

	int				submittedAt;	// uis.realtime of last ACTIVATE, or 0
} licenseMenuInfo_t;

static licenseMenuInfo_t	licenseMenuInfo;


/*
===============
UI_License_Submit

Persist the typed/pasted key and ask the engine to verify it. We cannot use
EXEC_NOW from the UI, so the result lands on com_licensed a few frames later;
the draw routine polls for it.
===============
*/
static void UI_License_Submit( void ) {
	char	cmd[MAX_EDIT_LINE + 16];
	char	*key = licenseMenuInfo.keyfield.field.buffer;

	if( !key[0] ) {
		return;
	}

	trap_Cvar_Set( "cl_licenseKey", key );
	Com_sprintf( cmd, sizeof( cmd ), "license %s\n", key );
	trap_Cmd_ExecuteText( EXEC_APPEND, cmd );

	licenseMenuInfo.submittedAt = uis.realtime;
}


/*
===============
UI_License_Event
===============
*/
static void UI_License_Event( void *ptr, int event ) {
	if( event != QM_ACTIVATED ) {
		return;
	}

	switch( ((menucommon_s*)ptr)->id ) {
	case ID_PASTE:
		MField_Paste( &licenseMenuInfo.keyfield.field );
		break;

	case ID_ACTIVATE:
		UI_License_Submit();
		break;

	case ID_BACK:
		UI_PopMenu();
		break;
	}
}


/*
===============
UI_License_DrawKey

Owner-draw for the key field: a dark panel with the key in the small font,
scrolled to keep the cursor visible (the key is far wider than the panel).
===============
*/
static void UI_License_DrawKey( void *self ) {
	menufield_s	*f = (menufield_s *)self;
	qboolean	focus;
	int			style;
	vec4_t		*color;

	focus = ( f->generic.parent->cursor == f->generic.menuPosition );

	UI_FillRect( FIELD_X - 8, FIELD_Y - 6, FIELD_W + 16, SMALLCHAR_HEIGHT + 12, lic_panel );
	UI_DrawRect( FIELD_X - 8, FIELD_Y - 6, FIELD_W + 16, SMALLCHAR_HEIGHT + 12,
		focus ? lic_amber : lic_grid );

	style = UI_SMALLFONT;
	color = focus ? &lic_amber : &lic_dim;
	if( focus ) {
		style |= UI_PULSE;	// MField_Draw renders the cursor when UI_PULSE is set
	}

	if( f->field.buffer[0] ) {
		MField_Draw( &f->field, FIELD_X, FIELD_Y, style, *color );
	} else if( !focus ) {
		UI_DrawString( FIELD_X, FIELD_Y, "S64-XXXXXX-XXXXXX- ...  paste your key here",
			UI_SMALLFONT, lic_dim );
	} else {
		MField_Draw( &f->field, FIELD_X, FIELD_Y, style, *color );
	}
}


/*
===============
UI_License_MenuDraw

Full-screen themed backdrop + headline + live status, drawn under the items.
===============
*/
static void UI_License_MenuDraw( void ) {
	qboolean	licensed;
	float		j;

	// fullscreen scrim so this reads as its own screen
	UI_FillRect( 0, 0, 640, 480, lic_back );

	// chromatic headline, matching the main-menu wordmark treatment: a red
	// ghost that stutters a couple px, with the amber wordmark on top
	j = ( ( uis.realtime / 90 ) % 22 == 0 ) ? 2.0f : 0.0f;
	UI_DrawProportionalString( 320 - 3 + j, 64, "ACCESS KEY", UI_CENTER, lic_redhot );
	UI_DrawProportionalString( 320, 64, "ACCESS KEY", UI_CENTER, lic_amber );

	UI_DrawString( 320, 108, "MAGI-01  / /  LICENSE PROTOCOL  / /  ACTIVATE TO DEPLOY",
		UI_CENTER|UI_SMALLFONT, lic_green );

	UI_FillRect( 70, 150, 500, 1, lic_grid );
	UI_DrawString( 76, 154, "[  ENTER LICENSE  ]", UI_LEFT|UI_SMALLFONT, lic_green );

	UI_DrawString( 320, 188, "Paste the key from your purchase e-mail.",
		UI_CENTER|UI_SMALLFONT, lic_dim );
	UI_DrawString( 320, 206, "CTRL+V  or  SHIFT+INS  to paste,  or click  PASTE.",
		UI_CENTER|UI_SMALLFONT, lic_dim );

	// live status line, polled from the engine verifier
	licensed = trap_Cvar_VariableValue( "com_licensed" ) ? qtrue : qfalse;
	if( licensed ) {
		int tier = (int)trap_Cvar_VariableValue( "com_licenseTier" );
		UI_DrawString( 320, 318, va( "ACTIVATED   / /   TIER %i   / /   ACCESS GRANTED", tier ),
			UI_CENTER|UI_SMALLFONT, lic_green );
		UI_DrawString( 320, 340, "press  BACK  to launch", UI_CENTER|UI_SMALLFONT, lic_dim );
	} else if( licenseMenuInfo.submittedAt ) {
		if( uis.realtime - licenseMenuInfo.submittedAt < 400 ) {
			UI_DrawString( 320, 318, "VERIFYING...", UI_CENTER|UI_SMALLFONT, lic_amber );
		} else {
			UI_DrawString( 320, 318, "KEY REJECTED   / /   CHECK THE KEY AND RETRY",
				UI_CENTER|UI_SMALLFONT, lic_redhot );
		}
	} else {
		UI_DrawString( 320, 318, "UNREGISTERED   / /   AWAITING KEY",
			UI_CENTER|UI_SMALLFONT, lic_dim );
	}

	UI_DrawString( 22, 452, "SYS 6.66   //   baseoa   //   LICENSE   //   NERV",
		UI_LEFT|UI_SMALLFONT, lic_green );
	UI_DrawString( 618, 452, "PROJECT  No.666", UI_RIGHT|UI_SMALLFONT, lic_redhot );

	// draw the menu items (field + buttons) on top
	Menu_Draw( &licenseMenuInfo.menu );
}


/*
===============
UI_CDKeyMenu_Cache
===============
*/
void UI_CDKeyMenu_Cache( void ) {
	// nothing to precache; this screen is drawn procedurally
}


/*
===============
UI_CDKeyMenu_Init
===============
*/
static void UI_CDKeyMenu_Init( void ) {
	UI_CDKeyMenu_Cache();

	memset( &licenseMenuInfo, 0, sizeof(licenseMenuInfo) );
	licenseMenuInfo.menu.wrapAround = qtrue;
	licenseMenuInfo.menu.fullscreen = qtrue;
	licenseMenuInfo.menu.draw       = UI_License_MenuDraw;

	licenseMenuInfo.keyfield.generic.type			= MTYPE_FIELD;
	licenseMenuInfo.keyfield.generic.flags			= QMF_NODEFAULTINIT;
	licenseMenuInfo.keyfield.generic.ownerdraw		= UI_License_DrawKey;
	licenseMenuInfo.keyfield.generic.x				= FIELD_X;
	licenseMenuInfo.keyfield.generic.y				= FIELD_Y;
	licenseMenuInfo.keyfield.generic.left			= FIELD_X - 8;
	licenseMenuInfo.keyfield.generic.top			= FIELD_Y - 6;
	licenseMenuInfo.keyfield.generic.right			= FIELD_X + FIELD_W + 8;
	licenseMenuInfo.keyfield.generic.bottom			= FIELD_Y + SMALLCHAR_HEIGHT + 6;
	licenseMenuInfo.keyfield.field.widthInChars		= FIELD_W / SMALLCHAR_WIDTH;
	licenseMenuInfo.keyfield.field.maxchars			= MAX_EDIT_LINE - 1;

	licenseMenuInfo.paste.generic.type				= MTYPE_PTEXT;
	licenseMenuInfo.paste.generic.flags				= QMF_CENTER_JUSTIFY|QMF_PULSEIFFOCUS;
	licenseMenuInfo.paste.generic.id				= ID_PASTE;
	licenseMenuInfo.paste.generic.callback			= UI_License_Event;
	licenseMenuInfo.paste.generic.x					= 170;
	licenseMenuInfo.paste.generic.y					= 286;
	licenseMenuInfo.paste.string					= "PASTE";
	licenseMenuInfo.paste.color						= lic_green;
	licenseMenuInfo.paste.style						= UI_CENTER|UI_SMALLFONT;

	licenseMenuInfo.activate.generic.type			= MTYPE_PTEXT;
	licenseMenuInfo.activate.generic.flags			= QMF_CENTER_JUSTIFY|QMF_PULSEIFFOCUS;
	licenseMenuInfo.activate.generic.id				= ID_ACTIVATE;
	licenseMenuInfo.activate.generic.callback		= UI_License_Event;
	licenseMenuInfo.activate.generic.x				= 320;
	licenseMenuInfo.activate.generic.y				= 286;
	licenseMenuInfo.activate.string					= "ACTIVATE";
	licenseMenuInfo.activate.color					= lic_amber;
	licenseMenuInfo.activate.style					= UI_CENTER|UI_SMALLFONT;

	licenseMenuInfo.back.generic.type				= MTYPE_PTEXT;
	licenseMenuInfo.back.generic.flags				= QMF_CENTER_JUSTIFY|QMF_PULSEIFFOCUS;
	licenseMenuInfo.back.generic.id					= ID_BACK;
	licenseMenuInfo.back.generic.callback			= UI_License_Event;
	licenseMenuInfo.back.generic.x					= 470;
	licenseMenuInfo.back.generic.y					= 286;
	licenseMenuInfo.back.string						= "BACK";
	licenseMenuInfo.back.color						= lic_dim;
	licenseMenuInfo.back.style						= UI_CENTER|UI_SMALLFONT;

	Menu_AddItem( &licenseMenuInfo.menu, &licenseMenuInfo.keyfield );
	Menu_AddItem( &licenseMenuInfo.menu, &licenseMenuInfo.paste );
	Menu_AddItem( &licenseMenuInfo.menu, &licenseMenuInfo.activate );
	Menu_AddItem( &licenseMenuInfo.menu, &licenseMenuInfo.back );

	// prefill with any stored key so the buyer can see / re-activate it
	trap_Cvar_VariableStringBuffer( "cl_licenseKey",
		licenseMenuInfo.keyfield.field.buffer, MAX_EDIT_LINE );

	// start focus on the field
	Menu_SetCursorToItem( &licenseMenuInfo.menu, &licenseMenuInfo.keyfield );
}


/*
===============
UI_CDKeyMenu
===============
*/
void UI_CDKeyMenu( void ) {
	UI_CDKeyMenu_Init();
	UI_PushMenu( &licenseMenuInfo.menu );
}


/*
===============
UI_CDKeyMenu_f
===============
*/
void UI_CDKeyMenu_f( void ) {
	UI_CDKeyMenu();
}
