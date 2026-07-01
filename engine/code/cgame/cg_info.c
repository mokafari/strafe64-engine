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
// cg_info.c -- display information while data is being loading

#include "cg_local.h"

#define MAX_LOADING_PLAYER_ICONS	16
#define MAX_LOADING_ITEM_ICONS		26

static int			loadingPlayerIconCount;
static int			loadingItemIconCount;
static qhandle_t	loadingPlayerIcons[MAX_LOADING_PLAYER_ICONS];
static qhandle_t	loadingItemIcons[MAX_LOADING_ITEM_ICONS];


/*
===================
CG_DrawLoadingIcons
===================
*/
static void CG_DrawLoadingIcons( void ) {
	int		n;
	int		x, y;

	for( n = 0; n < loadingPlayerIconCount; n++ ) {
		x = 16 + n * 78;
		y = 324-40;
		CG_DrawPic( x, y, 64, 64, loadingPlayerIcons[n] );
	}

	for( n = 0; n < loadingItemIconCount; n++ ) {
		y = 400-40;
		if( n >= 13 ) {
			y += 40;
		}
		x = 16 + n % 13 * 48;
		CG_DrawPic( x, y, 32, 32, loadingItemIcons[n] );
	}
}


/*
======================
CG_LoadingString

======================
*/
void CG_LoadingString( const char *s ) {
	Q_strncpyz( cg.infoScreenText, s, sizeof( cg.infoScreenText ) );

	trap_UpdateScreen();
}

/*
===================
CG_LoadingItem
===================
*/
void CG_LoadingItem( int itemNum ) {
	gitem_t		*item;

	item = &bg_itemlist[itemNum];
	
	if ( item->icon && loadingItemIconCount < MAX_LOADING_ITEM_ICONS ) {
		loadingItemIcons[loadingItemIconCount++] = trap_R_RegisterShaderNoMip( item->icon );
	}

	CG_LoadingString( item->pickup_name );
}

/*
===================
CG_LoadingClient
===================
*/
void CG_LoadingClient( int clientNum ) {
	const char		*info;
	char			*skin;
	char			personality[MAX_QPATH];
	char			model[MAX_QPATH];
	char			iconName[MAX_QPATH];

	info = CG_ConfigString( CS_PLAYERS + clientNum );

	if ( loadingPlayerIconCount < MAX_LOADING_PLAYER_ICONS ) {
		Q_strncpyz( model, Info_ValueForKey( info, "model" ), sizeof( model ) );
		skin = strrchr( model, '/' );
		if ( skin ) {
			*skin++ = '\0';
		} else {
			skin = "default";
		}

		Com_sprintf( iconName, MAX_QPATH, "models/players/%s/icon_%s.tga", model, skin );
		
		loadingPlayerIcons[loadingPlayerIconCount] = trap_R_RegisterShaderNoMip( iconName );
		if ( !loadingPlayerIcons[loadingPlayerIconCount] ) {
			Com_sprintf( iconName, MAX_QPATH, "models/players/characters/%s/icon_%s.tga", model, skin );
			loadingPlayerIcons[loadingPlayerIconCount] = trap_R_RegisterShaderNoMip( iconName );
		}
		if ( !loadingPlayerIcons[loadingPlayerIconCount] ) {
			Com_sprintf( iconName, MAX_QPATH, "models/players/%s/icon_%s.tga", DEFAULT_MODEL, "default" );
			loadingPlayerIcons[loadingPlayerIconCount] = trap_R_RegisterShaderNoMip( iconName );
		}
		if ( loadingPlayerIcons[loadingPlayerIconCount] ) {
			loadingPlayerIconCount++;
		}
	}

	Q_strncpyz( personality, Info_ValueForKey( info, "n" ), sizeof(personality) );
	Q_CleanStr( personality );

	if( cgs.gametype == GT_SINGLE_PLAYER ) {
		trap_S_RegisterSound( va( "sound/player/announce/%s.wav", personality ), qtrue );
	}

	CG_LoadingString( personality );
}


/*
====================
CG_DrawInformation

Draw all the status / pacifier stuff during level loading
====================
*/
void CG_DrawInformation( void ) {
	const char	*s;
	const char	*info;
	const char	*sysInfo;
	int			y;
	int			value;
	qhandle_t	levelshot;
	qhandle_t	detail;
	char		buf[1024];
	char		map[64];
	vec4_t		band   = { 0.01f, 0.02f, 0.03f, 0.78f };
	vec4_t		amber  = { 1.00f, 0.60f, 0.06f, 1.00f };
	vec4_t		cyan   = { 0.32f, 0.86f, 1.00f, 1.00f };
	vec4_t		dim    = { 0.55f, 0.42f, 0.20f, 1.00f };
	vec4_t		red    = { 1.00f, 0.12f, 0.16f, 1.00f };
	float		w;

	info = CG_ConfigString( CS_SERVERINFO );
	sysInfo = CG_ConfigString( CS_SYSTEMINFO );

	s = Info_ValueForKey( info, "mapname" );
	levelshot = trap_R_RegisterShaderNoMip( va( "levelshots/%s.tga", s ) );
	if ( !levelshot ) {
		levelshot = trap_R_RegisterShaderNoMip( "menu/art/unknownmap" );
	}
	trap_R_SetColor( NULL );
	CG_DrawPic( 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, levelshot );

	// blend a detail texture over it
	detail = trap_R_RegisterShader( "levelShotDetail" );
	trap_R_DrawStretchPic( 0, 0, cgs.glconfig.vidWidth, cgs.glconfig.vidHeight, 0, 0, 2.5, 2, detail );

	// NERV frame: dark bands top and bottom so the readouts float clean
	CG_FillRect( 0, 0, SCREEN_WIDTH, 118, band );
	CG_FillRect( 0, 118, SCREEN_WIDTH, 2, amber );
	CG_FillRect( 0, 360, SCREEN_WIDTH, 2, amber );
	CG_FillRect( 0, 362, SCREEN_WIDTH, 118, band );

	// draw the icons of things as they are loaded
	CG_DrawLoadingIcons();

	// top band: wordmark + map designation
	w = CG_MatrixStringWidth( "STRAFE 64", 1.0f );
	CG_DrawMatrixString( 320 - w / 2, 14, "STRAFE 64", 1.0f, dim );

	Q_strncpyz( map, Info_ValueForKey( info, "mapname" ), sizeof( map ) );
	Q_strupr( map );
	w = CG_MatrixStringWidth( map, 2.2f );
	CG_DrawMatrixString( 320 - w / 2, 34, map, 2.2f, amber );

	// the first 150 rows are reserved for the client connection
	// screen to write into
	if ( cg.infoScreenText[0] ) {
		s = va( "LOADING / %s", cg.infoScreenText );
	} else {
		s = "AWAITING SNAPSHOT";
	}
	w = CG_MatrixStringWidth( s, 1.1f );
	CG_DrawMatrixString( 320 - w / 2, 92, s, 1.1f, cyan );

	// bottom band: mission parameters
	y = 372;

	// don't print server lines if playing a local game
	trap_Cvar_VariableStringBuffer( "sv_running", buf, sizeof( buf ) );
	if ( !atoi( buf ) ) {
		// server hostname
		Q_strncpyz( buf, Info_ValueForKey( info, "sv_hostname" ), sizeof( buf ) );
		Q_CleanStr( buf );
		w = CG_MatrixStringWidth( buf, 1.1f );
		CG_DrawMatrixString( 320 - w / 2, y, buf, 1.1f, cyan );
		y += 16;

		// server-specific message of the day
		s = CG_ConfigString( CS_MOTD );
		if ( s[0] ) {
			w = CG_MatrixStringWidth( s, 1.0f );
			CG_DrawMatrixString( 320 - w / 2, y, s, 1.0f, dim );
			y += 16;
		}
	}

	// map-specific message (long map name)
	s = CG_ConfigString( CS_MESSAGE );
	if ( s[0] ) {
		w = CG_MatrixStringWidth( s, 1.0f );
		CG_DrawMatrixString( 320 - w / 2, y, s, 1.0f, dim );
		y += 16;
	}

	// gametype + limits on one mission-parameter line
	switch ( cgs.gametype ) {
	case GT_FFA:			s = "FREE FOR ALL";		break;
	case GT_SINGLE_PLAYER:	s = "SINGLE PLAYER";	break;
	case GT_TOURNAMENT:		s = "DUEL";				break;
	case GT_TEAM:			s = "TEAM DEATHMATCH";	break;
	case GT_CTF:			s = "CAPTURE THE FLAG";	break;
#ifdef MISSIONPACK
	case GT_1FCTF:			s = "ONE FLAG CTF";		break;
	case GT_OBELISK:		s = "OVERLOAD";			break;
	case GT_HARVESTER:		s = "HARVESTER";		break;
#endif
	default:				s = "UNKNOWN";			break;
	}
	Q_strncpyz( buf, s, sizeof( buf ) );

	value = atoi( Info_ValueForKey( info, "timelimit" ) );
	if ( value ) {
		Com_sprintf( buf + strlen( buf ), sizeof( buf ) - strlen( buf ),
					 "  /  %i MIN", value );
	}
	if ( cgs.gametype < GT_CTF ) {
		value = atoi( Info_ValueForKey( info, "fraglimit" ) );
		if ( value ) {
			Com_sprintf( buf + strlen( buf ), sizeof( buf ) - strlen( buf ),
						 "  /  FRAG LIMIT %i", value );
		}
	} else {
		value = atoi( Info_ValueForKey( info, "capturelimit" ) );
		if ( value ) {
			Com_sprintf( buf + strlen( buf ), sizeof( buf ) - strlen( buf ),
						 "  /  CAPTURE LIMIT %i", value );
		}
	}
	w = CG_MatrixStringWidth( buf, 1.1f );
	CG_DrawMatrixString( 320 - w / 2, y, buf, 1.1f, amber );
	y += 18;

	// warnings
	s = Info_ValueForKey( sysInfo, "sv_cheats" );
	if ( s[0] == '1' ) {
		w = CG_MatrixStringWidth( "CHEATS ARE ENABLED", 1.0f );
		CG_DrawMatrixString( 320 - w / 2, y, "CHEATS ARE ENABLED", 1.0f, red );
	}
}


