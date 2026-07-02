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
// cg_scoreboard -- the STRAFE 64 "COMBAT RECORD": a clean NERV/MAGI panel in
// the HUD's matrix font. Replaces the stock Q3/OpenArena board (3D heads,
// medal icons, giant char rows) with flat plates, an accent rail, colored
// pings and an (AI) tag for bots. No stock icon art is referenced at all.
//
#include "cg_local.h"

// palette — matches the NERV HUD in cg_draw.c
static vec4_t sb_amber  = { 1.00f, 0.60f, 0.06f, 1.00f };
static vec4_t sb_cyan   = { 0.32f, 0.86f, 1.00f, 1.00f };
static vec4_t sb_green  = { 0.40f, 1.00f, 0.50f, 1.00f };
static vec4_t sb_red    = { 1.00f, 0.12f, 0.16f, 1.00f };
static vec4_t sb_blue   = { 0.35f, 0.55f, 1.00f, 1.00f };
static vec4_t sb_dim    = { 0.55f, 0.42f, 0.20f, 1.00f };
static vec4_t sb_white  = { 0.92f, 0.94f, 0.96f, 1.00f };
static vec4_t sb_plate  = { 0.01f, 0.02f, 0.03f, 0.72f };
static vec4_t sb_rowme  = { 1.00f, 0.60f, 0.06f, 0.14f };

#define SB_X		70
#define SB_W		500
#define SB_ROW_H	18
#define SB_CELL		1.15f
#define SB_HEAD_CELL 0.95f

/*
=================
CG_SBText

Matrix-font text with a per-call alpha fold (fade), keeping the palette.
=================
*/
static void CG_SBText( float x, float y, const char *s, float cell,
					   const float *color, float fade ) {
	vec4_t c;

	c[0] = color[0]; c[1] = color[1]; c[2] = color[2]; c[3] = color[3] * fade;
	CG_DrawMatrixString( x, y, s, cell, c );
}

static void CG_SBTextRight( float xRight, float y, const char *s, float cell,
							const float *color, float fade ) {
	CG_SBText( xRight - CG_MatrixStringWidth( s, cell ), y, s, cell, color, fade );
}

static void CG_SBTextCenter( float xMid, float y, const char *s, float cell,
							 const float *color, float fade ) {
	CG_SBText( xMid - CG_MatrixStringWidth( s, cell ) / 2, y, s, cell, color, fade );
}

/*
=================
CG_SBPlate

Panel plate with a 2px accent rail across the top.
=================
*/
static void CG_SBPlate( float x, float y, float w, float h,
						const float *accent, float fade ) {
	vec4_t c;

	c[0] = sb_plate[0]; c[1] = sb_plate[1]; c[2] = sb_plate[2];
	c[3] = sb_plate[3] * fade;
	CG_FillRect( x, y, w, h, c );
	c[0] = accent[0]; c[1] = accent[1]; c[2] = accent[2]; c[3] = 0.9f * fade;
	CG_FillRect( x, y, w, 2, c );
}

/*
=================
CG_SBClientRow

One pilot line: #  NAME (AI)  SCORE  PING  TIME  ACC
=================
*/
static void CG_SBClientRow( const score_t *score, int place, float x, float y,
							float w, float fade ) {
	clientInfo_t	*ci;
	char			buf[64];
	char			name[26];
	float			*pc;

	if ( score->client < 0 || score->client >= cgs.maxclients ) {
		return;
	}
	ci = &cgs.clientinfo[ score->client ];
	if ( !ci->infoValid ) {
		return;
	}

	// the local pilot's row gets a highlight plate
	if ( score->client == cg.snap->ps.clientNum ) {
		vec4_t hc;
		hc[0] = sb_rowme[0]; hc[1] = sb_rowme[1]; hc[2] = sb_rowme[2];
		hc[3] = sb_rowme[3] * fade;
		CG_FillRect( x + 2, y - 2, w - 4, SB_ROW_H - 2, hc );
	}

	// place
	Com_sprintf( buf, sizeof( buf ), "%i", place );
	CG_SBTextRight( x + 34, y, buf, SB_CELL, sb_dim, fade );

	// name (colour codes are skipped by the font) + AI tag
	Q_strncpyz( name, ci->name, sizeof( name ) );
	CG_SBText( x + 46, y, name, SB_CELL, sb_white, fade );
	if ( ci->botSkill > 0 && ci->botSkill <= 5 ) {
		CG_SBText( x + 46 + CG_MatrixStringWidth( name, SB_CELL ) + 8, y,
				   "(AI)", 0.9f, sb_dim, fade );
	}

	// score ("*" = perfect, shown at intermission like the stock medal)
	if ( cg.snap->ps.pm_type == PM_INTERMISSION && score->perfect ) {
		Com_sprintf( buf, sizeof( buf ), "%i*", score->score );
	} else {
		Com_sprintf( buf, sizeof( buf ), "%i", score->score );
	}
	CG_SBTextRight( x + w - 150, y, buf, SB_CELL, sb_amber, fade );

	// ping: green/amber/red bands; bots have none
	if ( ci->botSkill > 0 ) {
		CG_SBTextRight( x + w - 92, y, "-", SB_CELL, sb_dim, fade );
	} else if ( score->ping < 0 ) {
		CG_SBTextRight( x + w - 92, y, "CNCT", 0.9f, sb_dim, fade );
	} else {
		pc = ( score->ping < 60 ) ? sb_green
		   : ( score->ping < 150 ) ? sb_amber : sb_red;
		Com_sprintf( buf, sizeof( buf ), "%i", score->ping );
		CG_SBTextRight( x + w - 92, y, buf, SB_CELL, pc, fade );
	}

	// time in match (minutes)
	Com_sprintf( buf, sizeof( buf ), "%i", score->time );
	CG_SBTextRight( x + w - 48, y, buf, SB_CELL, sb_cyan, fade );

	// accuracy
	Com_sprintf( buf, sizeof( buf ), "%i%%", score->accuracy );
	CG_SBTextRight( x + w - 8, y, buf, SB_CELL, sb_dim, fade );
}

/*
=================
CG_SBColumnHeads
=================
*/
static void CG_SBColumnHeads( float x, float y, float w, float fade ) {
	CG_SBTextRight( x + 34, y, "#", SB_HEAD_CELL, sb_dim, fade );
	CG_SBText( x + 46, y, "PILOT", SB_HEAD_CELL, sb_dim, fade );
	CG_SBTextRight( x + w - 150, y, "SCORE", SB_HEAD_CELL, sb_dim, fade );
	CG_SBTextRight( x + w - 92, y, "PING", SB_HEAD_CELL, sb_dim, fade );
	CG_SBTextRight( x + w - 48, y, "TIME", SB_HEAD_CELL, sb_dim, fade );
	CG_SBTextRight( x + w - 8, y, "ACC", SB_HEAD_CELL, sb_dim, fade );
}

/*
=================
CG_SBListTeam

Draw every scoreboard entry belonging to `team` (or all, when team == -1).
Returns the y after the last row.
=================
*/
static float CG_SBListTeam( int team, float x, float y, float w,
							float yMax, float fade ) {
	int		i, place = 0, skipped = 0;
	const score_t *score;

	for ( i = 0; i < cg.numScores; i++ ) {
		score = &cg.scores[i];
		if ( team != -1 && score->team != team ) {
			continue;
		}
		if ( score->team == TEAM_SPECTATOR ) {
			continue;
		}
		place++;
		if ( y + SB_ROW_H > yMax ) {
			skipped++;
			continue;
		}
		CG_SBClientRow( score, place, x, y, w, fade );
		y += SB_ROW_H;
	}
	if ( skipped ) {
		char buf[32];
		Com_sprintf( buf, sizeof( buf ), "+%i MORE", skipped );
		CG_SBText( x + 46, y, buf, 0.9f, sb_dim, fade );
		y += SB_ROW_H;
	}
	return y;
}

/*
=================
CG_SBSpectators
=================
*/
static void CG_SBSpectators( float y, float fade ) {
	char	line[256];
	int		i, n = 0;

	line[0] = '\0';
	for ( i = 0; i < cg.numScores; i++ ) {
		clientInfo_t *ci;
		if ( cg.scores[i].team != TEAM_SPECTATOR ) {
			continue;
		}
		if ( cg.scores[i].client < 0 || cg.scores[i].client >= cgs.maxclients ) {
			continue;
		}
		ci = &cgs.clientinfo[ cg.scores[i].client ];
		if ( !ci->infoValid ) {
			continue;
		}
		if ( n++ ) {
			Q_strcat( line, sizeof( line ), "  " );
		}
		Q_strcat( line, sizeof( line ), ci->name );
		if ( strlen( line ) > 60 ) {
			break;
		}
	}
	if ( n ) {
		char full[300];
		Com_sprintf( full, sizeof( full ), "SPEC: %s", line );
		CG_SBTextCenter( 320, y, full, 0.9f, sb_dim, fade );
	}
}

/*
=================
CG_DrawOldScoreboard

The COMBAT RECORD. Entry contract matches the stock board: qfalse when
paused / warmup-hidden / suppressed by the death MISSION REPORT.
=================
*/
qboolean CG_DrawOldScoreboard( void ) {
	float	fade, y, yMax;
	float	*fadeColor;
	char	buf[128];
	const char *info;

	// don't draw anything if the menu or console is up
	if ( cg_paused.integer ) {
		cg.deferredPlayerLoading = 0;
		return qfalse;
	}

	if ( cgs.gametype == GT_SINGLE_PLAYER && cg.predictedPlayerState.pm_type == PM_INTERMISSION ) {
		cg.deferredPlayerLoading = 0;
		return qfalse;
	}

	// don't draw scoreboard during death while warming up
	if ( cg.warmup && !cg.showScores ) {
		return qfalse;
	}

	// STRAFE 64: on a normal death the NERV MISSION REPORT owns the screen —
	// only show the record when the scores key is held. Intermission shows it.
	if ( cg.predictedPlayerState.pm_type == PM_DEAD && !cg.showScores ) {
		return qfalse;
	}

	if ( cg.showScores || cg.predictedPlayerState.pm_type == PM_DEAD ||
		 cg.predictedPlayerState.pm_type == PM_INTERMISSION ) {
		fade = 1.0f;
	} else {
		fadeColor = CG_FadeColor( cg.scoreFadeTime, FADE_TIME );
		if ( !fadeColor ) {
			cg.deferredPlayerLoading = 0;
			cg.killerName[0] = 0;
			return qfalse;
		}
		fade = fadeColor[3];
	}

	// no 3D heads on this board — nothing to defer-load
	cg.deferredPlayerLoading = 0;

	// fragged-by line above the record
	y = 44;
	if ( cg.killerName[0] ) {
		Com_sprintf( buf, sizeof( buf ), "CUT DOWN BY %s", cg.killerName );
		CG_SBTextCenter( 320, y, buf, 1.2f, sb_red, fade );
	}
	y = 66;

	// header
	CG_SBTextCenter( 320, y, "COMBAT RECORD", 1.7f, sb_amber, fade );
	y += 22;

	// map + limits
	info = CG_ConfigString( CS_SERVERINFO );
	Com_sprintf( buf, sizeof( buf ), "%s", Info_ValueForKey( info, "mapname" ) );
	Q_strupr( buf );
	if ( cgs.fraglimit && cgs.gametype < GT_CTF ) {
		Com_sprintf( buf + strlen( buf ), sizeof( buf ) - strlen( buf ),
					 "  /  FRAG LIMIT %i", cgs.fraglimit );
	}
	if ( cgs.timelimit ) {
		Com_sprintf( buf + strlen( buf ), sizeof( buf ) - strlen( buf ),
					 "  /  %i MIN", cgs.timelimit );
	}
	CG_SBTextCenter( 320, y, buf, 0.95f, sb_dim, fade );
	y += 24;

	yMax = 414;

	if ( cgs.gametype >= GT_TEAM ) {
		// two team panels, red left / blue right — sized to the longer roster
		float	wHalf = ( SB_W - 10 ) / 2;
		float	xR = SB_X, xB = SB_X + wHalf + 10;
		float	yTop = y, yR, yB, h;
		int		i, nR = 0, nB = 0, rows;

		for ( i = 0; i < cg.numScores; i++ ) {
			if ( cg.scores[i].team == TEAM_RED )  nR++;
			if ( cg.scores[i].team == TEAM_BLUE ) nB++;
		}
		rows = ( nR > nB ) ? nR : nB;
		h = 34 + rows * SB_ROW_H + 8;
		if ( yTop + h > yMax ) h = yMax - yTop;

		CG_SBPlate( xR, yTop, wHalf, h, sb_red, fade );
		CG_SBPlate( xB, yTop, wHalf, h, sb_blue, fade );

		Com_sprintf( buf, sizeof( buf ), "RED  %i", cg.teamScores[0] );
		CG_SBText( xR + 10, yTop + 8, buf, 1.4f, sb_red, fade );
		Com_sprintf( buf, sizeof( buf ), "BLUE  %i", cg.teamScores[1] );
		CG_SBTextRight( xB + wHalf - 10, yTop + 8, buf, 1.4f, sb_blue, fade );

		yR = CG_SBListTeam( TEAM_RED,  xR, yTop + 34, wHalf, yTop + h - 6, fade );
		yB = CG_SBListTeam( TEAM_BLUE, xB, yTop + 34, wHalf, yTop + h - 6, fade );
		y = ( yR > yB ) ? yR : yB;
	} else {
		// one panel, sized to the roster
		float	h;
		int		i, rows = 0;

		for ( i = 0; i < cg.numScores; i++ ) {
			if ( cg.scores[i].team != TEAM_SPECTATOR ) rows++;
		}
		h = 30 + rows * SB_ROW_H + 8;
		if ( y + h > yMax ) h = yMax - y;

		CG_SBPlate( SB_X, y, SB_W, h, sb_amber, fade );
		CG_SBColumnHeads( SB_X, y + 8, SB_W, fade );
		y = CG_SBListTeam( -1, SB_X, y + 30, SB_W, y + h - 6, fade );
	}

	CG_SBSpectators( 424, fade );

	// load any changed model info while the board is up (stock behaviour)
	CG_LoadDeferredPlayers();
	return qtrue;
}

/*
================
CG_CenterGiantLine
================
*/
static void CG_CenterGiantLine( float y, const char *string ) {
	CG_SBTextCenter( 320, y, string, 2.2f, sb_amber, 1.0f );
}

/*
=================
CG_DrawTourneyScoreboard

Draw the oversized scoreboard for tournaments
=================
*/
void CG_DrawTourneyScoreboard( void ) {
	const char		*s;
	int				min, tens, ones;
	clientInfo_t	*ci;
	int				y;
	int				i;

	// request more scores regularly
	if ( cg.scoresRequestTime + 2000 < cg.time ) {
		cg.scoresRequestTime = cg.time;
		trap_SendClientCommand( "score" );
	}

	// print the message of the day
	s = CG_ConfigString( CS_MOTD );
	if ( !s[0] ) {
		s = "STRAFE 64";
	}
	CG_CenterGiantLine( 8, s );

	// print the current time
	min = ( cg.time - cgs.levelStartTime ) / 60000;
	tens = ( ( cg.time - cgs.levelStartTime ) / 10000 ) % 6;
	ones = ( ( cg.time - cgs.levelStartTime ) / 1000 ) % 10;
	CG_SBTextCenter( 320, 64, va( "%i:%i%i", min, tens, ones ), 1.6f, sb_cyan, 1.0f );

	y = 120;

	if ( cgs.gametype >= GT_TEAM ) {
		CG_SBText( 80, y, va( "RED  %i", cg.teamScores[0] ), 2.0f, sb_red, 1.0f );
		y += 40;
		CG_SBText( 80, y, va( "BLUE  %i", cg.teamScores[1] ), 2.0f, sb_blue, 1.0f );
	} else {
		for ( i = 0; i < MAX_CLIENTS; i++ ) {
			ci = &cgs.clientinfo[i];
			if ( !ci->infoValid ) {
				continue;
			}
			if ( ci->team != TEAM_FREE ) {
				continue;
			}
			CG_SBText( 80, y, va( "%-4i %s", ci->score, ci->name ), 2.0f, sb_white, 1.0f );
			y += 40;
			if ( y > 400 ) {
				break;
			}
		}
	}
}
