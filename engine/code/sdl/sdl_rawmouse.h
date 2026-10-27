/*
===========================================================================
Copyright (C) 2026 STRAFE 64

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.
===========================================================================
*/

// Raw (unaccelerated) mouse deltas, bypassing SDL. Only macOS has a backend;
// elsewhere Init fails and in_mouse 2 falls back to SDL relative mode.

#ifndef SDL_RAWMOUSE_H
#define SDL_RAWMOUSE_H

#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"

#ifdef __APPLE__
qboolean	IN_RawMouse_Init( void );
void		IN_RawMouse_Shutdown( void );
void		IN_RawMouse_Read( int *dx, int *dy, int *reports );
#else
static ID_INLINE qboolean IN_RawMouse_Init( void ) { return qfalse; }
static ID_INLINE void IN_RawMouse_Shutdown( void ) { }
static ID_INLINE void IN_RawMouse_Read( int *dx, int *dy, int *reports ) { *dx = *dy = 0; if( reports ) *reports = 0; }
#endif

#endif
