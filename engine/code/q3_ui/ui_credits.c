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

CREDITS  —  STRAFE 64

The stock id Software / ioquake3 roll has been replaced with a themed
NERV/MAGI sign-off. It is skippable: any key drops straight to quit.
The engine attribution below keeps faith with the GPL it ships under.

=======================================================================
*/


#include "ui_local.h"


typedef struct {
	menuframework_s	menu;
} creditsmenu_t;

static creditsmenu_t	s_credits;

// NERV/MAGI palette (mirrors ui_menu.c)
static vec4_t cr_amber = { 1.00f, 0.62f, 0.05f, 1.00f };
static vec4_t cr_green = { 0.45f, 1.00f, 0.55f, 1.00f };
static vec4_t cr_dim   = { 0.45f, 0.55f, 0.55f, 1.00f };
static vec4_t cr_red   = { 1.00f, 0.16f, 0.22f, 1.00f };


/*
=================
UI_CreditMenu_Key

Skippable: any key (mouse or keyboard) quits the game immediately.
=================
*/
static sfxHandle_t UI_CreditMenu_Key( int key ) {
	// ignore the character-repeat events, act on the key itself
	if( key & K_CHAR_FLAG ) {
		return 0;
	}
	trap_Cmd_ExecuteText( EXEC_APPEND, "quit\n" );
	return 0;
}


/*
===============
UI_CreditMenu_Draw

The NERV canvas is already painted behind this fullscreen menu; layer the
STRAFE 64 sign-off on top.
===============
*/
static void UI_CreditMenu_Draw( void ) {
	vec4_t	cyan = { 0.20f, 0.95f, 1.00f, 0.55f };
	vec4_t	red  = { 1.00f, 0.18f, 0.30f, 0.55f };
	int		y;

	// chromatic-split STRAFE 64 wordmark (matches the main menu)
	UI_DrawProportionalString( 320 - 3, 96, "STRAFE 64", UI_CENTER, red );
	UI_DrawProportionalString( 320 + 3, 96, "STRAFE 64", UI_CENTER, cyan );
	UI_DrawProportionalString( 320,     96, "STRAFE 64", UI_CENTER, cr_amber );

	UI_DrawString( 320, 140, "MAGI-01  / /  PROJECT  No.666  / /  SPEED IS LIFE",
		UI_CENTER|UI_SMALLFONT, cr_green );

	y = 210;
	UI_DrawString( 320, y, "DESIGN  -  CODE  -  WORLDS", UI_CENTER|UI_SMALLFONT, cr_dim );
	y += 22;
	UI_DrawProportionalString( 320, y, "GUSTAV LILJESTEN", UI_CENTER, cr_amber );

	y += 60;
	UI_DrawString( 320, y, "MOVEMENT IS THE WEAPON  / /  THE FLOOR IS NEVER SAFE",
		UI_CENTER|UI_SMALLFONT, cr_green );

	// engine attribution — STRAFE 64 runs on ioquake3 / id Tech 3 (GPLv2)
	UI_DrawString( 320, 404,
		"built on ioquake3  /  id Tech 3 engine  -  GNU GPL v2",
		UI_CENTER|UI_SMALLFONT, cr_dim );

	UI_DrawString( 320, 446, "PRESS ANY KEY TO TERMINATE",
		UI_CENTER|UI_SMALLFONT, cr_red );
}


/*
===============
UI_CreditMenu
===============
*/
void UI_CreditMenu( void ) {
	memset( &s_credits, 0 ,sizeof(s_credits) );

	s_credits.menu.draw = UI_CreditMenu_Draw;
	s_credits.menu.key = UI_CreditMenu_Key;
	s_credits.menu.fullscreen = qtrue;
	UI_PushMenu ( &s_credits.menu );
}
