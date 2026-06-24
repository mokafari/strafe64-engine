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
// cg_draw.c -- draw all of the graphical elements during
// active (after loading) gameplay

#include "cg_local.h"

#ifdef MISSIONPACK
#include "../ui/ui_shared.h"

// used for scoreboard
extern displayContextDef_t cgDC;
menuDef_t *menuScoreboard = NULL;
#else
int drawTeamOverlayModificationCount = -1;
#endif

int sortedTeamPlayers[TEAM_MAXOVERLAY];
int	numSortedTeamPlayers;

char systemChat[256];
char teamChat1[256];
char teamChat2[256];

#ifdef MISSIONPACK

int CG_Text_Width(const char *text, float scale, int limit) {
  int count,len;
	float out;
	glyphInfo_t *glyph;
	float useScale;
	const char *s = text;
	fontInfo_t *font = &cgDC.Assets.textFont;
	if (scale <= cg_smallFont.value) {
		font = &cgDC.Assets.smallFont;
	} else if (scale > cg_bigFont.value) {
		font = &cgDC.Assets.bigFont;
	}
	useScale = scale * font->glyphScale;
  out = 0;
  if (text) {
    len = strlen(text);
		if (limit > 0 && len > limit) {
			len = limit;
		}
		count = 0;
		while (s && *s && count < len) {
			if ( Q_IsColorString(s) ) {
				s += 2;
				continue;
			} else {
				glyph = &font->glyphs[*s & 255];
				out += glyph->xSkip;
				s++;
				count++;
			}
    }
  }
  return out * useScale;
}

int CG_Text_Height(const char *text, float scale, int limit) {
  int len, count;
	float max;
	glyphInfo_t *glyph;
	float useScale;
	const char *s = text;
	fontInfo_t *font = &cgDC.Assets.textFont;
	if (scale <= cg_smallFont.value) {
		font = &cgDC.Assets.smallFont;
	} else if (scale > cg_bigFont.value) {
		font = &cgDC.Assets.bigFont;
	}
	useScale = scale * font->glyphScale;
  max = 0;
  if (text) {
    len = strlen(text);
		if (limit > 0 && len > limit) {
			len = limit;
		}
		count = 0;
		while (s && *s && count < len) {
			if ( Q_IsColorString(s) ) {
				s += 2;
				continue;
			} else {
				glyph = &font->glyphs[*s & 255];
	      if (max < glyph->height) {
		      max = glyph->height;
			  }
				s++;
				count++;
			}
    }
  }
  return max * useScale;
}

void CG_Text_PaintChar(float x, float y, float width, float height, float scale, float s, float t, float s2, float t2, qhandle_t hShader) {
  float w, h;
  w = width * scale;
  h = height * scale;
  CG_AdjustFrom640( &x, &y, &w, &h );
  trap_R_DrawStretchPic( x, y, w, h, s, t, s2, t2, hShader );
}

void CG_Text_Paint(float x, float y, float scale, vec4_t color, const char *text, float adjust, int limit, int style) {
  int len, count;
	vec4_t newColor;
	glyphInfo_t *glyph;
	float useScale;
	fontInfo_t *font = &cgDC.Assets.textFont;
	if (scale <= cg_smallFont.value) {
		font = &cgDC.Assets.smallFont;
	} else if (scale > cg_bigFont.value) {
		font = &cgDC.Assets.bigFont;
	}
	useScale = scale * font->glyphScale;
  if (text) {
		const char *s = text;
		trap_R_SetColor( color );
		memcpy(&newColor[0], &color[0], sizeof(vec4_t));
    len = strlen(text);
		if (limit > 0 && len > limit) {
			len = limit;
		}
		count = 0;
		while (s && *s && count < len) {
			glyph = &font->glyphs[*s & 255];
      //int yadj = Assets.textFont.glyphs[text[i]].bottom + Assets.textFont.glyphs[text[i]].top;
      //float yadj = scale * (Assets.textFont.glyphs[text[i]].imageHeight - Assets.textFont.glyphs[text[i]].height);
			if ( Q_IsColorString( s ) ) {
				memcpy( newColor, g_color_table[ColorIndex(*(s+1))], sizeof( newColor ) );
				newColor[3] = color[3];
				trap_R_SetColor( newColor );
				s += 2;
				continue;
			} else {
				float yadj = useScale * glyph->top;
				if (style == ITEM_TEXTSTYLE_SHADOWED || style == ITEM_TEXTSTYLE_SHADOWEDMORE) {
					int ofs = style == ITEM_TEXTSTYLE_SHADOWED ? 1 : 2;
					colorBlack[3] = newColor[3];
					trap_R_SetColor( colorBlack );
					CG_Text_PaintChar(x + ofs, y - yadj + ofs, 
														glyph->imageWidth,
														glyph->imageHeight,
														useScale, 
														glyph->s,
														glyph->t,
														glyph->s2,
														glyph->t2,
														glyph->glyph);
					colorBlack[3] = 1.0;
					trap_R_SetColor( newColor );
				}
				CG_Text_PaintChar(x, y - yadj, 
													glyph->imageWidth,
													glyph->imageHeight,
													useScale, 
													glyph->s,
													glyph->t,
													glyph->s2,
													glyph->t2,
													glyph->glyph);
				// CG_DrawPic(x, y - yadj, scale * cgDC.Assets.textFont.glyphs[text[i]].imageWidth, scale * cgDC.Assets.textFont.glyphs[text[i]].imageHeight, cgDC.Assets.textFont.glyphs[text[i]].glyph);
				x += (glyph->xSkip * useScale) + adjust;
				s++;
				count++;
			}
    }
	  trap_R_SetColor( NULL );
  }
}


#endif

/*
==============
CG_DrawField

Draws large numbers for status bar and powerups
==============
*/
#ifndef MISSIONPACK
static void CG_DrawField (int x, int y, int width, int value) {
	char	num[16], *ptr;
	int		l;
	int		frame;

	if ( width < 1 ) {
		return;
	}

	// draw number string
	if ( width > 5 ) {
		width = 5;
	}

	switch ( width ) {
	case 1:
		value = value > 9 ? 9 : value;
		value = value < 0 ? 0 : value;
		break;
	case 2:
		value = value > 99 ? 99 : value;
		value = value < -9 ? -9 : value;
		break;
	case 3:
		value = value > 999 ? 999 : value;
		value = value < -99 ? -99 : value;
		break;
	case 4:
		value = value > 9999 ? 9999 : value;
		value = value < -999 ? -999 : value;
		break;
	}

	Com_sprintf (num, sizeof(num), "%i", value);
	l = strlen(num);
	if (l > width)
		l = width;
	x += 2 + CHAR_WIDTH*(width - l);

	ptr = num;
	while (*ptr && l)
	{
		if (*ptr == '-')
			frame = STAT_MINUS;
		else
			frame = *ptr -'0';

		CG_DrawPic( x,y, CHAR_WIDTH, CHAR_HEIGHT, cgs.media.numberShaders[frame] );
		x += CHAR_WIDTH;
		ptr++;
		l--;
	}
}
#endif // MISSIONPACK

/*
================
CG_Draw3DModel

================
*/
void CG_Draw3DModel( float x, float y, float w, float h, qhandle_t model, qhandle_t skin, vec3_t origin, vec3_t angles ) {
	refdef_t		refdef;
	refEntity_t		ent;

	if ( !cg_draw3dIcons.integer || !cg_drawIcons.integer ) {
		return;
	}

	CG_AdjustFrom640( &x, &y, &w, &h );

	memset( &refdef, 0, sizeof( refdef ) );

	memset( &ent, 0, sizeof( ent ) );
	AnglesToAxis( angles, ent.axis );
	VectorCopy( origin, ent.origin );
	ent.hModel = model;
	ent.customSkin = skin;
	ent.renderfx = RF_NOSHADOW;		// no stencil shadows

	refdef.rdflags = RDF_NOWORLDMODEL;

	AxisClear( refdef.viewaxis );

	refdef.fov_x = 30;
	refdef.fov_y = 30;

	refdef.x = x;
	refdef.y = y;
	refdef.width = w;
	refdef.height = h;

	refdef.time = cg.time;

	trap_R_ClearScene();
	trap_R_AddRefEntityToScene( &ent );
	trap_R_RenderScene( &refdef );
}

/*
================
CG_DrawHead

Used for both the status bar and the scoreboard
================
*/
void CG_DrawHead( float x, float y, float w, float h, int clientNum, vec3_t headAngles ) {
	clipHandle_t	cm;
	clientInfo_t	*ci;
	float			len;
	vec3_t			origin;
	vec3_t			mins, maxs;

	ci = &cgs.clientinfo[ clientNum ];

	if ( cg_draw3dIcons.integer ) {
		cm = ci->headModel;
		if ( !cm ) {
			return;
		}

		// offset the origin y and z to center the head
		trap_R_ModelBounds( cm, mins, maxs );

		origin[2] = -0.5 * ( mins[2] + maxs[2] );
		origin[1] = 0.5 * ( mins[1] + maxs[1] );

		// calculate distance so the head nearly fills the box
		// assume heads are taller than wide
		len = 0.7 * ( maxs[2] - mins[2] );		
		origin[0] = len / 0.268;	// len / tan( fov/2 )

		// allow per-model tweaking
		VectorAdd( origin, ci->headOffset, origin );

		CG_Draw3DModel( x, y, w, h, ci->headModel, ci->headSkin, origin, headAngles );
	} else if ( cg_drawIcons.integer ) {
		CG_DrawPic( x, y, w, h, ci->modelIcon );
	}

	// if they are deferred, draw a cross out
	if ( ci->deferred ) {
		CG_DrawPic( x, y, w, h, cgs.media.deferShader );
	}
}

/*
================
CG_DrawFlagModel

Used for both the status bar and the scoreboard
================
*/
void CG_DrawFlagModel( float x, float y, float w, float h, int team, qboolean force2D ) {
	qhandle_t		cm;
	float			len;
	vec3_t			origin, angles;
	vec3_t			mins, maxs;
	qhandle_t		handle;

	if ( !force2D && cg_draw3dIcons.integer ) {

		VectorClear( angles );

		cm = cgs.media.redFlagModel;

		// offset the origin y and z to center the flag
		trap_R_ModelBounds( cm, mins, maxs );

		origin[2] = -0.5 * ( mins[2] + maxs[2] );
		origin[1] = 0.5 * ( mins[1] + maxs[1] );

		// calculate distance so the flag nearly fills the box
		// assume heads are taller than wide
		len = 0.5 * ( maxs[2] - mins[2] );		
		origin[0] = len / 0.268;	// len / tan( fov/2 )

		angles[YAW] = 60 * sin( cg.time / 2000.0 );;

		if( team == TEAM_RED ) {
			handle = cgs.media.redFlagModel;
		} else if( team == TEAM_BLUE ) {
			handle = cgs.media.blueFlagModel;
		} else if( team == TEAM_FREE ) {
			handle = cgs.media.neutralFlagModel;
		} else {
			return;
		}
		CG_Draw3DModel( x, y, w, h, handle, 0, origin, angles );
	} else if ( cg_drawIcons.integer ) {
		gitem_t *item;

		if( team == TEAM_RED ) {
			item = BG_FindItemForPowerup( PW_REDFLAG );
		} else if( team == TEAM_BLUE ) {
			item = BG_FindItemForPowerup( PW_BLUEFLAG );
		} else if( team == TEAM_FREE ) {
			item = BG_FindItemForPowerup( PW_NEUTRALFLAG );
		} else {
			return;
		}
		if (item) {
		  CG_DrawPic( x, y, w, h, cg_items[ ITEM_INDEX(item) ].icon );
		}
	}
}

/*
================
CG_DrawStatusBarHead

================
*/
#ifndef MISSIONPACK

static void CG_DrawStatusBarHead( float x ) {
	vec3_t		angles;
	float		size, stretch;
	float		frac;

	VectorClear( angles );

	if ( cg.damageTime && cg.time - cg.damageTime < DAMAGE_TIME ) {
		frac = (float)(cg.time - cg.damageTime ) / DAMAGE_TIME;
		size = ICON_SIZE * 1.25 * ( 1.5 - frac * 0.5 );

		stretch = size - ICON_SIZE * 1.25;
		// kick in the direction of damage
		x -= stretch * 0.5 + cg.damageX * stretch * 0.5;

		cg.headStartYaw = 180 + cg.damageX * 45;

		cg.headEndYaw = 180 + 20 * cos( crandom()*M_PI );
		cg.headEndPitch = 5 * cos( crandom()*M_PI );

		cg.headStartTime = cg.time;
		cg.headEndTime = cg.time + 100 + random() * 2000;
	} else {
		if ( cg.time >= cg.headEndTime ) {
			// select a new head angle
			cg.headStartYaw = cg.headEndYaw;
			cg.headStartPitch = cg.headEndPitch;
			cg.headStartTime = cg.headEndTime;
			cg.headEndTime = cg.time + 100 + random() * 2000;

			cg.headEndYaw = 180 + 20 * cos( crandom()*M_PI );
			cg.headEndPitch = 5 * cos( crandom()*M_PI );
		}

		size = ICON_SIZE * 1.25;
	}

	// if the server was frozen for a while we may have a bad head start time
	if ( cg.headStartTime > cg.time ) {
		cg.headStartTime = cg.time;
	}

	frac = ( cg.time - cg.headStartTime ) / (float)( cg.headEndTime - cg.headStartTime );
	frac = frac * frac * ( 3 - 2 * frac );
	angles[YAW] = cg.headStartYaw + ( cg.headEndYaw - cg.headStartYaw ) * frac;
	angles[PITCH] = cg.headStartPitch + ( cg.headEndPitch - cg.headStartPitch ) * frac;

	CG_DrawHead( x, 480 - size, size, size, 
				cg.snap->ps.clientNum, angles );
}
#endif // MISSIONPACK

/*
================
CG_DrawStatusBarFlag

================
*/
#ifndef MISSIONPACK
static void CG_DrawStatusBarFlag( float x, int team ) {
	CG_DrawFlagModel( x, 480 - ICON_SIZE, ICON_SIZE, ICON_SIZE, team, qfalse );
}
#endif // MISSIONPACK

/*
================
CG_DrawTeamBackground

================
*/
void CG_DrawTeamBackground( int x, int y, int w, int h, float alpha, int team )
{
	vec4_t		hcolor;

	hcolor[3] = alpha;
	if ( team == TEAM_RED ) {
		hcolor[0] = 1;
		hcolor[1] = 0;
		hcolor[2] = 0;
	} else if ( team == TEAM_BLUE ) {
		hcolor[0] = 0;
		hcolor[1] = 0;
		hcolor[2] = 1;
	} else {
		return;
	}
	trap_R_SetColor( hcolor );
	CG_DrawPic( x, y, w, h, cgs.media.teamStatusBar );
	trap_R_SetColor( NULL );
}

/*
================
CG_DrawStatusBar

================
*/
#ifndef MISSIONPACK
static void CG_DrawStatusBar( void ) {
	int			color;
	centity_t	*cent;
	playerState_t	*ps;
	int			value;
	vec4_t		hcolor;
	vec3_t		angles;
	vec3_t		origin;

	static float colors[4][4] = { 
//		{ 0.2, 1.0, 0.2, 1.0 } , { 1.0, 0.2, 0.2, 1.0 }, {0.5, 0.5, 0.5, 1} };
		{ 1.0f, 0.69f, 0.0f, 1.0f },    // normal
		{ 1.0f, 0.2f, 0.2f, 1.0f },     // low health
		{ 0.5f, 0.5f, 0.5f, 1.0f },     // weapon firing
		{ 1.0f, 1.0f, 1.0f, 1.0f } };   // health > 100

	if ( cg_drawStatus.integer == 0 ) {
		return;
	}

	// draw the team background
	CG_DrawTeamBackground( 0, 420, 640, 60, 0.33f, cg.snap->ps.persistant[PERS_TEAM] );

	cent = &cg_entities[cg.snap->ps.clientNum];
	ps = &cg.snap->ps;

	VectorClear( angles );

	// draw any 3D icons first, so the changes back to 2D are minimized
	if ( cent->currentState.weapon && cg_weapons[ cent->currentState.weapon ].ammoModel ) {
		origin[0] = 70;
		origin[1] = 0;
		origin[2] = 0;
		angles[YAW] = 90 + 20 * sin( cg.time / 1000.0 );
		CG_Draw3DModel( CHAR_WIDTH*3 + TEXT_ICON_SPACE, 432, ICON_SIZE, ICON_SIZE,
					   cg_weapons[ cent->currentState.weapon ].ammoModel, 0, origin, angles );
	}

	CG_DrawStatusBarHead( 185 + CHAR_WIDTH*3 + TEXT_ICON_SPACE );

	if( cg.predictedPlayerState.powerups[PW_REDFLAG] ) {
		CG_DrawStatusBarFlag( 185 + CHAR_WIDTH*3 + TEXT_ICON_SPACE + ICON_SIZE, TEAM_RED );
	} else if( cg.predictedPlayerState.powerups[PW_BLUEFLAG] ) {
		CG_DrawStatusBarFlag( 185 + CHAR_WIDTH*3 + TEXT_ICON_SPACE + ICON_SIZE, TEAM_BLUE );
	} else if( cg.predictedPlayerState.powerups[PW_NEUTRALFLAG] ) {
		CG_DrawStatusBarFlag( 185 + CHAR_WIDTH*3 + TEXT_ICON_SPACE + ICON_SIZE, TEAM_FREE );
	}

	if ( ps->stats[ STAT_ARMOR ] ) {
		origin[0] = 90;
		origin[1] = 0;
		origin[2] = -10;
		angles[YAW] = ( cg.time & 2047 ) * 360 / 2048.0;
		CG_Draw3DModel( 370 + CHAR_WIDTH*3 + TEXT_ICON_SPACE, 432, ICON_SIZE, ICON_SIZE,
					   cgs.media.armorModel, 0, origin, angles );
	}
	//
	// ammo
	//
	if ( cent->currentState.weapon ) {
		value = ps->ammo[cent->currentState.weapon];
		if ( value > -1 ) {
			if ( cg.predictedPlayerState.weaponstate == WEAPON_FIRING
				&& cg.predictedPlayerState.weaponTime > 100 ) {
				// draw as dark grey when reloading
				color = 2;	// dark grey
			} else {
				if ( value >= 0 ) {
					color = 0;	// green
				} else {
					color = 1;	// red
				}
			}
			trap_R_SetColor( colors[color] );
			
			CG_DrawField (0, 432, 3, value);
			trap_R_SetColor( NULL );

			// if we didn't draw a 3D icon, draw a 2D icon for ammo
			if ( !cg_draw3dIcons.integer && cg_drawIcons.integer ) {
				qhandle_t	icon;

				icon = cg_weapons[ cg.predictedPlayerState.weapon ].ammoIcon;
				if ( icon ) {
					CG_DrawPic( CHAR_WIDTH*3 + TEXT_ICON_SPACE, 432, ICON_SIZE, ICON_SIZE, icon );
				}
			}
		}
	}

	//
	// health
	//
	value = ps->stats[STAT_HEALTH];
	if ( value > 100 ) {
		trap_R_SetColor( colors[3] );		// white
	} else if (value > 25) {
		trap_R_SetColor( colors[0] );	// green
	} else if (value > 0) {
		color = (cg.time >> 8) & 1;	// flash
		trap_R_SetColor( colors[color] );
	} else {
		trap_R_SetColor( colors[1] );	// red
	}

	// stretch the health up when taking damage
	CG_DrawField ( 185, 432, 3, value);
	CG_ColorForHealth( hcolor );
	trap_R_SetColor( hcolor );


	//
	// armor
	//
	value = ps->stats[STAT_ARMOR];
	if (value > 0 ) {
		trap_R_SetColor( colors[0] );
		CG_DrawField (370, 432, 3, value);
		trap_R_SetColor( NULL );
		// if we didn't draw a 3D icon, draw a 2D icon for armor
		if ( !cg_draw3dIcons.integer && cg_drawIcons.integer ) {
			CG_DrawPic( 370 + CHAR_WIDTH*3 + TEXT_ICON_SPACE, 432, ICON_SIZE, ICON_SIZE, cgs.media.armorIcon );
		}

	}
}
#endif

/*
===========================================================================================

  UPPER RIGHT CORNER

===========================================================================================
*/

/*
================
CG_DrawAttacker

================
*/
static float CG_DrawAttacker( float y ) {
	int			t;
	const char	*info;
	const char	*name;
	int			clientNum;

	if ( cg.predictedPlayerState.stats[STAT_HEALTH] <= 0 ) {
		return y;
	}

	if ( !cg.attackerTime ) {
		return y;
	}

	clientNum = cg.predictedPlayerState.persistant[PERS_ATTACKER];
	if ( clientNum < 0 || clientNum >= MAX_CLIENTS || clientNum == cg.snap->ps.clientNum ) {
		return y;
	}

	if ( !cgs.clientinfo[clientNum].infoValid ) {
		cg.attackerTime = 0;
		return y;
	}

	t = cg.time - cg.attackerTime;
	if ( t > ATTACKER_HEAD_TIME ) {
		cg.attackerTime = 0;
		return y;
	}

	// NERV: drop the 3D head — show the last attacker's tag in alert red
	info = CG_ConfigString( CS_PLAYERS + clientNum );
	name = Info_ValueForKey( info, "n" );
	{
		char	clean[64];
		vec4_t	ac;
		int		nw;

		Q_strncpyz( clean, name, sizeof( clean ) );
		Q_CleanStr( clean );
		ac[0] = 1.0f; ac[1] = 0.12f; ac[2] = 0.16f; ac[3] = 1.0f;
		nw = CG_MatrixStringWidth( clean, 1.3f );
		CG_DrawMatrixString( 632 - nw, y, clean, 1.3f, ac );
	}

	return y + 16;
}

/*
==================
CG_DrawSnapshot
==================
*/
static float CG_DrawSnapshot( float y ) {
	char		*s;
	int			w;

	s = va( "time:%i snap:%i cmd:%i", cg.snap->serverTime, 
		cg.latestSnapshotNum, cgs.serverCommandSequence );
	w = CG_DrawStrlen( s ) * BIGCHAR_WIDTH;

	CG_DrawBigString( 635 - w, y + 2, s, 1.0F);

	return y + BIGCHAR_HEIGHT + 4;
}

/*
==================
CG_DrawFPS
==================
*/
#define	FPS_FRAMES	4
static float CG_DrawFPS( float y ) {
	char		*s;
	int			w;
	static int	previousTimes[FPS_FRAMES];
	static int	index;
	int		i, total;
	int		fps;
	static	int	previous;
	int		t, frameTime;

	// don't use serverTime, because that will be drifting to
	// correct for internet lag changes, timescales, timedemos, etc
	t = trap_Milliseconds();
	frameTime = t - previous;
	previous = t;

	previousTimes[index % FPS_FRAMES] = frameTime;
	index++;
	if ( index > FPS_FRAMES ) {
		// average multiple frames together to smooth changes out a bit
		total = 0;
		for ( i = 0 ; i < FPS_FRAMES ; i++ ) {
			total += previousTimes[i];
		}
		if ( !total ) {
			total = 1;
		}
		fps = 1000 * FPS_FRAMES / total;

		s = va( "%ifps", fps );
		w = CG_DrawStrlen( s ) * BIGCHAR_WIDTH;

		CG_DrawBigString( 635 - w, y + 2, s, 1.0F);
	}

	return y + BIGCHAR_HEIGHT + 4;
}

/*
=================
CG_DrawTimer
=================
*/
static float CG_DrawTimer( float y ) {
	char		*s;
	int			w;
	int			mins, seconds, tens;
	int			msec;

	msec = cg.time - cgs.levelStartTime;

	seconds = msec / 1000;
	mins = seconds / 60;
	seconds -= mins * 60;
	tens = seconds / 10;
	seconds -= tens * 10;

	s = va( "%i:%i%i", mins, tens, seconds );
	w = CG_DrawStrlen( s ) * BIGCHAR_WIDTH;

	CG_DrawBigString( 635 - w, y + 2, s, 1.0F);

	return y + BIGCHAR_HEIGHT + 4;
}


/*
=================
CG_DrawTeamOverlay
=================
*/

static float CG_DrawTeamOverlay( float y, qboolean right, qboolean upper ) {
	int x, w, h, xx;
	int i, j, len;
	const char *p;
	vec4_t		hcolor;
	int pwidth, lwidth;
	int plyrs;
	char st[16];
	clientInfo_t *ci;
	gitem_t	*item;
	int ret_y, count;

	if ( !cg_drawTeamOverlay.integer ) {
		return y;
	}

	if ( cg.snap->ps.persistant[PERS_TEAM] != TEAM_RED && cg.snap->ps.persistant[PERS_TEAM] != TEAM_BLUE ) {
		return y; // Not on any team
	}

	plyrs = 0;

	// max player name width
	pwidth = 0;
	count = (numSortedTeamPlayers > 8) ? 8 : numSortedTeamPlayers;
	for (i = 0; i < count; i++) {
		ci = cgs.clientinfo + sortedTeamPlayers[i];
		if ( ci->infoValid && ci->team == cg.snap->ps.persistant[PERS_TEAM]) {
			plyrs++;
			len = CG_DrawStrlen(ci->name);
			if (len > pwidth)
				pwidth = len;
		}
	}

	if (!plyrs)
		return y;

	if (pwidth > TEAM_OVERLAY_MAXNAME_WIDTH)
		pwidth = TEAM_OVERLAY_MAXNAME_WIDTH;

	// max location name width
	lwidth = 0;
	for (i = 1; i < MAX_LOCATIONS; i++) {
		p = CG_ConfigString(CS_LOCATIONS + i);
		if (p && *p) {
			len = CG_DrawStrlen(p);
			if (len > lwidth)
				lwidth = len;
		}
	}

	if (lwidth > TEAM_OVERLAY_MAXLOCATION_WIDTH)
		lwidth = TEAM_OVERLAY_MAXLOCATION_WIDTH;

	w = (pwidth + lwidth + 4 + 7) * TINYCHAR_WIDTH;

	if ( right )
		x = 640 - w;
	else
		x = 0;

	h = plyrs * TINYCHAR_HEIGHT;

	if ( upper ) {
		ret_y = y + h;
	} else {
		y -= h;
		ret_y = y;
	}

	if ( cg.snap->ps.persistant[PERS_TEAM] == TEAM_RED ) {
		hcolor[0] = 1.0f;
		hcolor[1] = 0.0f;
		hcolor[2] = 0.0f;
		hcolor[3] = 0.33f;
	} else { // if ( cg.snap->ps.persistant[PERS_TEAM] == TEAM_BLUE )
		hcolor[0] = 0.0f;
		hcolor[1] = 0.0f;
		hcolor[2] = 1.0f;
		hcolor[3] = 0.33f;
	}
	trap_R_SetColor( hcolor );
	CG_DrawPic( x, y, w, h, cgs.media.teamStatusBar );
	trap_R_SetColor( NULL );

	for (i = 0; i < count; i++) {
		ci = cgs.clientinfo + sortedTeamPlayers[i];
		if ( ci->infoValid && ci->team == cg.snap->ps.persistant[PERS_TEAM]) {

			hcolor[0] = hcolor[1] = hcolor[2] = hcolor[3] = 1.0;

			xx = x + TINYCHAR_WIDTH;

			CG_DrawStringExt( xx, y,
				ci->name, hcolor, qfalse, qfalse,
				TINYCHAR_WIDTH, TINYCHAR_HEIGHT, TEAM_OVERLAY_MAXNAME_WIDTH);

			if (lwidth) {
				p = CG_ConfigString(CS_LOCATIONS + ci->location);
				if (!p || !*p)
					p = "unknown";
//				len = CG_DrawStrlen(p);
//				if (len > lwidth)
//					len = lwidth;

//				xx = x + TINYCHAR_WIDTH * 2 + TINYCHAR_WIDTH * pwidth + 
//					((lwidth/2 - len/2) * TINYCHAR_WIDTH);
				xx = x + TINYCHAR_WIDTH * 2 + TINYCHAR_WIDTH * pwidth;
				CG_DrawStringExt( xx, y,
					p, hcolor, qfalse, qfalse, TINYCHAR_WIDTH, TINYCHAR_HEIGHT,
					TEAM_OVERLAY_MAXLOCATION_WIDTH);
			}

			CG_GetColorForHealth( ci->health, ci->armor, hcolor );

			Com_sprintf (st, sizeof(st), "%3i %3i", ci->health,	ci->armor);

			xx = x + TINYCHAR_WIDTH * 3 + 
				TINYCHAR_WIDTH * pwidth + TINYCHAR_WIDTH * lwidth;

			CG_DrawStringExt( xx, y,
				st, hcolor, qfalse, qfalse,
				TINYCHAR_WIDTH, TINYCHAR_HEIGHT, 0 );

			// draw weapon icon
			xx += TINYCHAR_WIDTH * 3;

			if ( cg_weapons[ci->curWeapon].weaponIcon ) {
				CG_DrawPic( xx, y, TINYCHAR_WIDTH, TINYCHAR_HEIGHT, 
					cg_weapons[ci->curWeapon].weaponIcon );
			} else {
				CG_DrawPic( xx, y, TINYCHAR_WIDTH, TINYCHAR_HEIGHT, 
					cgs.media.deferShader );
			}

			// Draw powerup icons
			if (right) {
				xx = x;
			} else {
				xx = x + w - TINYCHAR_WIDTH;
			}
			for (j = 0; j <= PW_NUM_POWERUPS; j++) {
				if (ci->powerups & (1 << j)) {

					item = BG_FindItemForPowerup( j );

					if (item) {
						CG_DrawPic( xx, y, TINYCHAR_WIDTH, TINYCHAR_HEIGHT, 
						trap_R_RegisterShader( item->icon ) );
						if (right) {
							xx -= TINYCHAR_WIDTH;
						} else {
							xx += TINYCHAR_WIDTH;
						}
					}
				}
			}

			y += TINYCHAR_HEIGHT;
		}
	}

	return ret_y;
//#endif
}


/*
=====================
CG_DrawUpperRight

=====================
*/
static void CG_DrawUpperRight(stereoFrame_t stereoFrame)
{
	float	y;

	y = 0;

	if ( cgs.gametype >= GT_TEAM && cg_drawTeamOverlay.integer == 1 ) {
		y = CG_DrawTeamOverlay( y, qtrue, qtrue );
	} 
	if ( cg_drawSnapshot.integer ) {
		y = CG_DrawSnapshot( y );
	}
	if (cg_drawFPS.integer && (stereoFrame == STEREO_CENTER || stereoFrame == STEREO_RIGHT)) {
		y = CG_DrawFPS( y );
	}
	if ( cg_drawTimer.integer ) {
		y = CG_DrawTimer( y );
	}
	if ( cg_drawAttacker.integer ) {
		CG_DrawAttacker( y );
	}

}

/*
===========================================================================================

  LOWER RIGHT CORNER

===========================================================================================
*/

/*
=================
CG_DrawScores

Draw the small two score display
=================
*/
#ifndef MISSIONPACK
static float CG_DrawScores( float y ) {
	const char	*s;
	int			s1, s2, score;
	int			x, w;
	int			v;
	vec4_t		color;
	float		y1;
	gitem_t		*item;

	s1 = cgs.scores1;
	s2 = cgs.scores2;

	y -=  BIGCHAR_HEIGHT + 8;

	y1 = y;

	// draw from the right side to left
	if ( cgs.gametype >= GT_TEAM ) {
		x = 640;
		color[0] = 0.0f;
		color[1] = 0.0f;
		color[2] = 1.0f;
		color[3] = 0.33f;
		s = va( "%2i", s2 );
		w = CG_DrawStrlen( s ) * BIGCHAR_WIDTH + 8;
		x -= w;
		CG_FillRect( x, y-4,  w, BIGCHAR_HEIGHT+8, color );
		if ( cg.snap->ps.persistant[PERS_TEAM] == TEAM_BLUE ) {
			CG_DrawPic( x, y-4, w, BIGCHAR_HEIGHT+8, cgs.media.selectShader );
		}
		CG_DrawBigString( x + 4, y, s, 1.0F);

		if ( cgs.gametype == GT_CTF ) {
			// Display flag status
			item = BG_FindItemForPowerup( PW_BLUEFLAG );

			if (item) {
				y1 = y - BIGCHAR_HEIGHT - 8;
				if( cgs.blueflag >= 0 && cgs.blueflag <= 2 ) {
					CG_DrawPic( x, y1-4, w, BIGCHAR_HEIGHT+8, cgs.media.blueFlagShader[cgs.blueflag] );
				}
			}
		}
		color[0] = 1.0f;
		color[1] = 0.0f;
		color[2] = 0.0f;
		color[3] = 0.33f;
		s = va( "%2i", s1 );
		w = CG_DrawStrlen( s ) * BIGCHAR_WIDTH + 8;
		x -= w;
		CG_FillRect( x, y-4,  w, BIGCHAR_HEIGHT+8, color );
		if ( cg.snap->ps.persistant[PERS_TEAM] == TEAM_RED ) {
			CG_DrawPic( x, y-4, w, BIGCHAR_HEIGHT+8, cgs.media.selectShader );
		}
		CG_DrawBigString( x + 4, y, s, 1.0F);

		if ( cgs.gametype == GT_CTF ) {
			// Display flag status
			item = BG_FindItemForPowerup( PW_REDFLAG );

			if (item) {
				y1 = y - BIGCHAR_HEIGHT - 8;
				if( cgs.redflag >= 0 && cgs.redflag <= 2 ) {
					CG_DrawPic( x, y1-4, w, BIGCHAR_HEIGHT+8, cgs.media.redFlagShader[cgs.redflag] );
				}
			}
		}

		if ( cgs.gametype >= GT_CTF ) {
			v = cgs.capturelimit;
		} else {
			v = cgs.fraglimit;
		}
		if ( v ) {
			s = va( "%2i", v );
			w = CG_DrawStrlen( s ) * BIGCHAR_WIDTH + 8;
			x -= w;
			CG_DrawBigString( x + 4, y, s, 1.0F);
		}

	} else {
		qboolean	spectator;

		x = 640;
		score = cg.snap->ps.persistant[PERS_SCORE];
		spectator = ( cg.snap->ps.persistant[PERS_TEAM] == TEAM_SPECTATOR );

		// always show your score in the second box if not in first place
		if ( s1 != score ) {
			s2 = score;
		}
		if ( s2 != SCORE_NOT_PRESENT ) {
			s = va( "%2i", s2 );
			w = CG_DrawStrlen( s ) * BIGCHAR_WIDTH + 8;
			x -= w;
			if ( !spectator && score == s2 && score != s1 ) {
				color[0] = 1.0f;
				color[1] = 0.0f;
				color[2] = 0.0f;
				color[3] = 0.33f;
				CG_FillRect( x, y-4,  w, BIGCHAR_HEIGHT+8, color );
				CG_DrawPic( x, y-4, w, BIGCHAR_HEIGHT+8, cgs.media.selectShader );
			} else {
				color[0] = 0.5f;
				color[1] = 0.5f;
				color[2] = 0.5f;
				color[3] = 0.33f;
				CG_FillRect( x, y-4,  w, BIGCHAR_HEIGHT+8, color );
			}	
			CG_DrawBigString( x + 4, y, s, 1.0F);
		}

		// first place
		if ( s1 != SCORE_NOT_PRESENT ) {
			s = va( "%2i", s1 );
			w = CG_DrawStrlen( s ) * BIGCHAR_WIDTH + 8;
			x -= w;
			if ( !spectator && score == s1 ) {
				color[0] = 0.0f;
				color[1] = 0.0f;
				color[2] = 1.0f;
				color[3] = 0.33f;
				CG_FillRect( x, y-4,  w, BIGCHAR_HEIGHT+8, color );
				CG_DrawPic( x, y-4, w, BIGCHAR_HEIGHT+8, cgs.media.selectShader );
			} else {
				color[0] = 0.5f;
				color[1] = 0.5f;
				color[2] = 0.5f;
				color[3] = 0.33f;
				CG_FillRect( x, y-4,  w, BIGCHAR_HEIGHT+8, color );
			}	
			CG_DrawBigString( x + 4, y, s, 1.0F);
		}

		if ( cgs.fraglimit ) {
			s = va( "%2i", cgs.fraglimit );
			w = CG_DrawStrlen( s ) * BIGCHAR_WIDTH + 8;
			x -= w;
			CG_DrawBigString( x + 4, y, s, 1.0F);
		}

	}

	return y1 - 8;
}
#endif // MISSIONPACK

/*
================
CG_DrawPowerups
================
*/
#ifndef MISSIONPACK
static float CG_DrawPowerups( float y ) {
	int		sorted[MAX_POWERUPS];
	int		sortedTime[MAX_POWERUPS];
	int		i, j, k;
	int		active;
	playerState_t	*ps;
	int		t;
	gitem_t	*item;
	int		x;
	int		color;
	float	size;
	float	f;
	static float colors[2][4] = { 
    { 0.2f, 1.0f, 0.2f, 1.0f } , 
    { 1.0f, 0.2f, 0.2f, 1.0f } 
  };

	ps = &cg.snap->ps;

	if ( ps->stats[STAT_HEALTH] <= 0 ) {
		return y;
	}

	// sort the list by time remaining
	active = 0;
	for ( i = 0 ; i < MAX_POWERUPS ; i++ ) {
		if ( !ps->powerups[ i ] ) {
			continue;
		}

		// ZOID--don't draw if the power up has unlimited time
		// This is true of the CTF flags
		if ( ps->powerups[ i ] == INT_MAX ) {
			continue;
		}

		t = ps->powerups[ i ] - cg.time;
		if ( t <= 0 ) {
			continue;
		}

		// insert into the list
		for ( j = 0 ; j < active ; j++ ) {
			if ( sortedTime[j] >= t ) {
				for ( k = active - 1 ; k >= j ; k-- ) {
					sorted[k+1] = sorted[k];
					sortedTime[k+1] = sortedTime[k];
				}
				break;
			}
		}
		sorted[j] = i;
		sortedTime[j] = t;
		active++;
	}

	// draw the icons and timers
	x = 640 - ICON_SIZE - CHAR_WIDTH * 2;
	for ( i = 0 ; i < active ; i++ ) {
		item = BG_FindItemForPowerup( sorted[i] );

    if (item) {

		  color = 1;

		  y -= ICON_SIZE;

		  trap_R_SetColor( colors[color] );
		  CG_DrawField( x, y, 2, sortedTime[ i ] / 1000 );

		  t = ps->powerups[ sorted[i] ];
		  if ( t - cg.time >= POWERUP_BLINKS * POWERUP_BLINK_TIME ) {
			  trap_R_SetColor( NULL );
		  } else {
			  vec4_t	modulate;

			  f = (float)( t - cg.time ) / POWERUP_BLINK_TIME;
			  f -= (int)f;
			  modulate[0] = modulate[1] = modulate[2] = modulate[3] = f;
			  trap_R_SetColor( modulate );
		  }

		  if ( cg.powerupActive == sorted[i] && 
			  cg.time - cg.powerupTime < PULSE_TIME ) {
			  f = 1.0 - ( ( (float)cg.time - cg.powerupTime ) / PULSE_TIME );
			  size = ICON_SIZE * ( 1.0 + ( PULSE_SCALE - 1.0 ) * f );
		  } else {
			  size = ICON_SIZE;
		  }

		  CG_DrawPic( 640 - size, y + ICON_SIZE / 2 - size / 2, 
			  size, size, trap_R_RegisterShader( item->icon ) );
    }
	}
	trap_R_SetColor( NULL );

	return y;
}
#endif // MISSIONPACK

/*
=====================
CG_DrawLowerRight

=====================
*/
#ifndef MISSIONPACK
static void CG_DrawLowerRight( void ) {
	float	y;

	y = 480 - ICON_SIZE;

	if ( cgs.gametype >= GT_TEAM && cg_drawTeamOverlay.integer == 2 ) {
		y = CG_DrawTeamOverlay( y, qtrue, qfalse );
	} 

	// frag scores are deathmatch clutter on the speedrun HUD — skip them
	CG_DrawPowerups( y );
}
#endif // MISSIONPACK

/*
===================
CG_DrawPickupItem
===================
*/
#ifndef MISSIONPACK
static int CG_DrawPickupItem( int y ) {
	int		value;
	float	*fadeColor;

	if ( cg.snap->ps.stats[STAT_HEALTH] <= 0 ) {
		return y;
	}

	y -= ICON_SIZE;

	value = cg.itemPickup;
	if ( value ) {
		fadeColor = CG_FadeColor( cg.itemPickupTime, 3000 );
		if ( fadeColor ) {
			CG_RegisterItemVisuals( value );
			trap_R_SetColor( fadeColor );
			CG_DrawPic( 8, y, ICON_SIZE, ICON_SIZE, cg_items[ value ].icon );
			CG_DrawBigString( ICON_SIZE + 16, y + (ICON_SIZE/2 - BIGCHAR_HEIGHT/2), bg_itemlist[ value ].pickup_name, fadeColor[0] );
			trap_R_SetColor( NULL );
		}
	}
	
	return y;
}
#endif // MISSIONPACK

/*
=====================
CG_DrawLowerLeft

=====================
*/
#ifndef MISSIONPACK
static void CG_DrawLowerLeft( void ) {
	float	y;

	y = 480 - ICON_SIZE;

	if ( cgs.gametype >= GT_TEAM && cg_drawTeamOverlay.integer == 3 ) {
		y = CG_DrawTeamOverlay( y, qfalse, qfalse );
	} 


	CG_DrawPickupItem( y );
}
#endif // MISSIONPACK


//===========================================================================================

/*
=================
CG_DrawTeamInfo
=================
*/
#ifndef MISSIONPACK
static void CG_DrawTeamInfo( void ) {
	int h;
	int i;
	vec4_t		hcolor;
	int		chatHeight;

#define CHATLOC_Y 420 // bottom end
#define CHATLOC_X 0

	if (cg_teamChatHeight.integer < TEAMCHAT_HEIGHT)
		chatHeight = cg_teamChatHeight.integer;
	else
		chatHeight = TEAMCHAT_HEIGHT;
	if (chatHeight <= 0)
		return; // disabled

	if (cgs.teamLastChatPos != cgs.teamChatPos) {
		if (cg.time - cgs.teamChatMsgTimes[cgs.teamLastChatPos % chatHeight] > cg_teamChatTime.integer) {
			cgs.teamLastChatPos++;
		}

		h = (cgs.teamChatPos - cgs.teamLastChatPos) * TINYCHAR_HEIGHT;

		if ( cgs.clientinfo[cg.clientNum].team == TEAM_RED ) {
			hcolor[0] = 1.0f;
			hcolor[1] = 0.0f;
			hcolor[2] = 0.0f;
			hcolor[3] = 0.33f;
		} else if ( cgs.clientinfo[cg.clientNum].team == TEAM_BLUE ) {
			hcolor[0] = 0.0f;
			hcolor[1] = 0.0f;
			hcolor[2] = 1.0f;
			hcolor[3] = 0.33f;
		} else {
			hcolor[0] = 0.0f;
			hcolor[1] = 1.0f;
			hcolor[2] = 0.0f;
			hcolor[3] = 0.33f;
		}

		trap_R_SetColor( hcolor );
		CG_DrawPic( CHATLOC_X, CHATLOC_Y - h, 640, h, cgs.media.teamStatusBar );
		trap_R_SetColor( NULL );

		hcolor[0] = hcolor[1] = hcolor[2] = 1.0f;
		hcolor[3] = 1.0f;

		for (i = cgs.teamChatPos - 1; i >= cgs.teamLastChatPos; i--) {
			CG_DrawStringExt( CHATLOC_X + TINYCHAR_WIDTH, 
				CHATLOC_Y - (cgs.teamChatPos - i)*TINYCHAR_HEIGHT, 
				cgs.teamChatMsgs[i % chatHeight], hcolor, qfalse, qfalse,
				TINYCHAR_WIDTH, TINYCHAR_HEIGHT, 0 );
		}
	}
}
#endif // MISSIONPACK

/*
===================
CG_DrawHoldableItem
===================
*/
#ifndef MISSIONPACK
static void CG_DrawHoldableItem( void ) { 
	int		value;

	value = cg.snap->ps.stats[STAT_HOLDABLE_ITEM];
	if ( value ) {
		CG_RegisterItemVisuals( value );
		CG_DrawPic( 640-ICON_SIZE, (SCREEN_HEIGHT-ICON_SIZE)/2, ICON_SIZE, ICON_SIZE, cg_items[ value ].icon );
	}

}
#endif // MISSIONPACK

#ifdef MISSIONPACK
/*
===================
CG_DrawPersistantPowerup
===================
*/
#if 0 // sos001208 - DEAD
static void CG_DrawPersistantPowerup( void ) { 
	int		value;

	value = cg.snap->ps.stats[STAT_PERSISTANT_POWERUP];
	if ( value ) {
		CG_RegisterItemVisuals( value );
		CG_DrawPic( 640-ICON_SIZE, (SCREEN_HEIGHT-ICON_SIZE)/2 - ICON_SIZE, ICON_SIZE, ICON_SIZE, cg_items[ value ].icon );
	}
}
#endif
#endif // MISSIONPACK


/*
===================
CG_DrawReward
===================
*/
static void CG_DrawReward( void ) { 
	float	*color;
	int		i, count;
	float	x, y;
	char	buf[32];

	if ( !cg_drawRewards.integer ) {
		return;
	}

	color = CG_FadeColor( cg.rewardTime, REWARD_TIME );
	if ( !color ) {
		if (cg.rewardStack > 0) {
			for(i = 0; i < cg.rewardStack; i++) {
				cg.rewardSound[i] = cg.rewardSound[i+1];
				cg.rewardShader[i] = cg.rewardShader[i+1];
				cg.rewardCount[i] = cg.rewardCount[i+1];
			}
			cg.rewardTime = cg.time;
			cg.rewardStack--;
			color = CG_FadeColor( cg.rewardTime, REWARD_TIME );
			trap_S_StartLocalSound(cg.rewardSound[0], CHAN_ANNOUNCER);
		} else {
			return;
		}
	}

	trap_R_SetColor( color );

	/*
	count = cg.rewardCount[0]/10;				// number of big rewards to draw

	if (count) {
		y = 4;
		x = 320 - count * ICON_SIZE;
		for ( i = 0 ; i < count ; i++ ) {
			CG_DrawPic( x, y, (ICON_SIZE*2)-4, (ICON_SIZE*2)-4, cg.rewardShader[0] );
			x += (ICON_SIZE*2);
		}
	}

	count = cg.rewardCount[0] - count*10;		// number of small rewards to draw
	*/

	if ( cg.rewardCount[0] >= 10 ) {
		y = 56;
		x = 320 - ICON_SIZE/2;
		CG_DrawPic( x, y, ICON_SIZE-4, ICON_SIZE-4, cg.rewardShader[0] );
		Com_sprintf(buf, sizeof(buf), "%d", cg.rewardCount[0]);
		x = ( SCREEN_WIDTH - SMALLCHAR_WIDTH * CG_DrawStrlen( buf ) ) / 2;
		CG_DrawStringExt( x, y+ICON_SIZE, buf, color, qfalse, qtrue,
								SMALLCHAR_WIDTH, SMALLCHAR_HEIGHT, 0 );
	}
	else {

		count = cg.rewardCount[0];

		y = 56;
		x = 320 - count * ICON_SIZE/2;
		for ( i = 0 ; i < count ; i++ ) {
			CG_DrawPic( x, y, ICON_SIZE-4, ICON_SIZE-4, cg.rewardShader[0] );
			x += ICON_SIZE;
		}
	}
	trap_R_SetColor( NULL );
}


/*
===============================================================================

LAGOMETER

===============================================================================
*/

#define	LAG_SAMPLES		128


typedef struct {
	int		frameSamples[LAG_SAMPLES];
	int		frameCount;
	int		snapshotFlags[LAG_SAMPLES];
	int		snapshotSamples[LAG_SAMPLES];
	int		snapshotCount;
} lagometer_t;

lagometer_t		lagometer;

/*
==============
CG_AddLagometerFrameInfo

Adds the current interpolate / extrapolate bar for this frame
==============
*/
void CG_AddLagometerFrameInfo( void ) {
	int			offset;

	offset = cg.time - cg.latestSnapshotTime;
	lagometer.frameSamples[ lagometer.frameCount & ( LAG_SAMPLES - 1) ] = offset;
	lagometer.frameCount++;
}

/*
==============
CG_AddLagometerSnapshotInfo

Each time a snapshot is received, log its ping time and
the number of snapshots that were dropped before it.

Pass NULL for a dropped packet.
==============
*/
void CG_AddLagometerSnapshotInfo( snapshot_t *snap ) {
	// dropped packet
	if ( !snap ) {
		lagometer.snapshotSamples[ lagometer.snapshotCount & ( LAG_SAMPLES - 1) ] = -1;
		lagometer.snapshotCount++;
		return;
	}

	// add this snapshot's info
	lagometer.snapshotSamples[ lagometer.snapshotCount & ( LAG_SAMPLES - 1) ] = snap->ping;
	lagometer.snapshotFlags[ lagometer.snapshotCount & ( LAG_SAMPLES - 1) ] = snap->snapFlags;
	lagometer.snapshotCount++;
}

/*
==============
CG_DrawDisconnect

Should we draw something differnet for long lag vs no packets?
==============
*/
static void CG_DrawDisconnect( void ) {
	float		x, y;
	int			cmdNum;
	usercmd_t	cmd;
	const char		*s;
	int			w;

	// draw the phone jack if we are completely past our buffers
	cmdNum = trap_GetCurrentCmdNumber() - CMD_BACKUP + 1;
	trap_GetUserCmd( cmdNum, &cmd );
	if ( cmd.serverTime <= cg.snap->ps.commandTime
		|| cmd.serverTime > cg.time ) {	// special check for map_restart
		return;
	}

	// also add text in center of screen
	s = "Connection Interrupted";
	w = CG_DrawStrlen( s ) * BIGCHAR_WIDTH;
	CG_DrawBigString( 320 - w/2, 100, s, 1.0F);

	// blink the icon
	if ( ( cg.time >> 9 ) & 1 ) {
		return;
	}

#ifdef MISSIONPACK
	x = 640 - 48;
	y = 480 - 144;
#else
	x = 640 - 48;
	y = 480 - 48;
#endif

	CG_DrawPic( x, y, 48, 48, trap_R_RegisterShader("gfx/2d/net.tga" ) );
}


#define	MAX_LAGOMETER_PING	900
#define	MAX_LAGOMETER_RANGE	300

/*
==============
CG_DrawLagometer
==============
*/
static void CG_DrawLagometer( void ) {
	int		a, x, y, i;
	float	v;
	float	ax, ay, aw, ah, mid, range;
	int		color;
	float	vscale;

	if ( !cg_lagometer.integer || cgs.localServer ) {
		CG_DrawDisconnect();
		return;
	}

	//
	// draw the graph
	//
#ifdef MISSIONPACK
	x = 640 - 48;
	y = 480 - 144;
#else
	x = 640 - 48;
	y = 480 - 48;
#endif

	trap_R_SetColor( NULL );
	CG_DrawPic( x, y, 48, 48, cgs.media.lagometerShader );

	ax = x;
	ay = y;
	aw = 48;
	ah = 48;
	CG_AdjustFrom640( &ax, &ay, &aw, &ah );

	color = -1;
	range = ah / 3;
	mid = ay + range;

	vscale = range / MAX_LAGOMETER_RANGE;

	// draw the frame interpoalte / extrapolate graph
	for ( a = 0 ; a < aw ; a++ ) {
		i = ( lagometer.frameCount - 1 - a ) & (LAG_SAMPLES - 1);
		v = lagometer.frameSamples[i];
		v *= vscale;
		if ( v > 0 ) {
			if ( color != 1 ) {
				color = 1;
				trap_R_SetColor( g_color_table[ColorIndex(COLOR_YELLOW)] );
			}
			if ( v > range ) {
				v = range;
			}
			trap_R_DrawStretchPic ( ax + aw - a, mid - v, 1, v, 0, 0, 0, 0, cgs.media.whiteShader );
		} else if ( v < 0 ) {
			if ( color != 2 ) {
				color = 2;
				trap_R_SetColor( g_color_table[ColorIndex(COLOR_BLUE)] );
			}
			v = -v;
			if ( v > range ) {
				v = range;
			}
			trap_R_DrawStretchPic( ax + aw - a, mid, 1, v, 0, 0, 0, 0, cgs.media.whiteShader );
		}
	}

	// draw the snapshot latency / drop graph
	range = ah / 2;
	vscale = range / MAX_LAGOMETER_PING;

	for ( a = 0 ; a < aw ; a++ ) {
		i = ( lagometer.snapshotCount - 1 - a ) & (LAG_SAMPLES - 1);
		v = lagometer.snapshotSamples[i];
		if ( v > 0 ) {
			if ( lagometer.snapshotFlags[i] & SNAPFLAG_RATE_DELAYED ) {
				if ( color != 5 ) {
					color = 5;	// YELLOW for rate delay
					trap_R_SetColor( g_color_table[ColorIndex(COLOR_YELLOW)] );
				}
			} else {
				if ( color != 3 ) {
					color = 3;
					trap_R_SetColor( g_color_table[ColorIndex(COLOR_GREEN)] );
				}
			}
			v = v * vscale;
			if ( v > range ) {
				v = range;
			}
			trap_R_DrawStretchPic( ax + aw - a, ay + ah - v, 1, v, 0, 0, 0, 0, cgs.media.whiteShader );
		} else if ( v < 0 ) {
			if ( color != 4 ) {
				color = 4;		// RED for dropped snapshots
				trap_R_SetColor( g_color_table[ColorIndex(COLOR_RED)] );
			}
			trap_R_DrawStretchPic( ax + aw - a, ay + ah - range, 1, range, 0, 0, 0, 0, cgs.media.whiteShader );
		}
	}

	trap_R_SetColor( NULL );

	if ( cg_nopredict.integer || cg_synchronousClients.integer ) {
		CG_DrawBigString( x, y, "snc", 1.0 );
	}

	CG_DrawDisconnect();
}



/*
===============================================================================

CENTER PRINTING

===============================================================================
*/


/*
==============
CG_CenterPrint

Called for important messages that should stay in the center of the screen
for a few moments
==============
*/
void CG_CenterPrint( const char *str, int y, int charWidth ) {
	char	*s;

	Q_strncpyz( cg.centerPrint, str, sizeof(cg.centerPrint) );

	cg.centerPrintTime = cg.time;
	cg.centerPrintY = y;
	cg.centerPrintCharWidth = charWidth;

	// count the number of lines for centering
	cg.centerPrintLines = 1;
	s = cg.centerPrint;
	while( *s ) {
		if (*s == '\n')
			cg.centerPrintLines++;
		s++;
	}
}


/*
===================
CG_DrawCenterString
===================
*/
static void CG_DrawCenterString( void ) {
	char	*start;
	int		l;
	int		x, y, w;
#ifdef MISSIONPACK
	int h;
#endif
	float	*color;

	if ( !cg.centerPrintTime ) {
		return;
	}

	color = CG_FadeColor( cg.centerPrintTime, 1000 * cg_centertime.value );
	if ( !color ) {
		return;
	}

	trap_R_SetColor( color );

	start = cg.centerPrint;

	y = cg.centerPrintY - cg.centerPrintLines * BIGCHAR_HEIGHT / 2;

	while ( 1 ) {
		char linebuffer[1024];

		for ( l = 0; l < 50; l++ ) {
			if ( !start[l] || start[l] == '\n' ) {
				break;
			}
			linebuffer[l] = start[l];
		}
		linebuffer[l] = 0;

#ifdef MISSIONPACK
		w = CG_Text_Width(linebuffer, 0.5, 0);
		h = CG_Text_Height(linebuffer, 0.5, 0);
		x = (SCREEN_WIDTH - w) / 2;
		CG_Text_Paint(x, y + h, 0.5, color, linebuffer, 0, 0, ITEM_TEXTSTYLE_SHADOWEDMORE);
		y += h + 6;
#else
		{
			float	cell = cg.centerPrintCharWidth * 0.2f;
			vec4_t	tc;

			tc[0] = 1.0f; tc[1] = 0.6f; tc[2] = 0.06f; tc[3] = color[3];	// NERV amber, keep fade
			w = CG_MatrixStringWidth( linebuffer, cell );
			x = ( SCREEN_WIDTH - w ) / 2;
			CG_DrawMatrixString( x, y, linebuffer, cell, tc );
			y += 7 * cell + 4;
		}
#endif
		while ( *start && ( *start != '\n' ) ) {
			start++;
		}
		if ( !*start ) {
			break;
		}
		start++;
	}

	trap_R_SetColor( NULL );
}



/*
================================================================================

CROSSHAIR

================================================================================
*/


/*
=================
CG_DrawCrosshair
=================
*/
static void CG_DrawCrosshair(void)
{
	float		w, h;
	qhandle_t	hShader;
	float		f;
	float		x, y;
	int			ca;

	if ( !cg_drawCrosshair.integer ) {
		return;
	}

	if ( cg.snap->ps.persistant[PERS_TEAM] == TEAM_SPECTATOR) {
		return;
	}

	if ( cg.renderingThirdPerson ) {
		return;
	}

	// set color based on health
	if ( cg_crosshairHealth.integer ) {
		vec4_t		hcolor;

		CG_ColorForHealth( hcolor );
		trap_R_SetColor( hcolor );
	} else {
		trap_R_SetColor( NULL );
	}

	w = h = cg_crosshairSize.value;

	// pulse the size of the crosshair when picking up items
	f = cg.time - cg.itemPickupBlendTime;
	if ( f > 0 && f < ITEM_BLOB_TIME ) {
		f /= ITEM_BLOB_TIME;
		w *= ( 1 + f );
		h *= ( 1 + f );
	}

	x = cg_crosshairX.integer;
	y = cg_crosshairY.integer;
	CG_AdjustFrom640( &x, &y, &w, &h );

	ca = cg_drawCrosshair.integer;
	if (ca < 0) {
		ca = 0;
	}
	hShader = cgs.media.crosshairShader[ ca % NUM_CROSSHAIRS ];

	trap_R_DrawStretchPic( x + cg.refdef.x + 0.5 * (cg.refdef.width - w), 
		y + cg.refdef.y + 0.5 * (cg.refdef.height - h), 
		w, h, 0, 0, 1, 1, hShader );

	trap_R_SetColor( NULL );
}

/*
=================
CG_DrawCrosshair3D
=================
*/
static void CG_DrawCrosshair3D(void)
{
	float		w;
	qhandle_t	hShader;
	float		f;
	int			ca;

	trace_t trace;
	vec3_t endpos;
	float stereoSep, zProj, maxdist, xmax;
	char rendererinfos[128];
	refEntity_t ent;

	if ( !cg_drawCrosshair.integer ) {
		return;
	}

	if ( cg.snap->ps.persistant[PERS_TEAM] == TEAM_SPECTATOR) {
		return;
	}

	if ( cg.renderingThirdPerson ) {
		return;
	}

	w = cg_crosshairSize.value;

	// pulse the size of the crosshair when picking up items
	f = cg.time - cg.itemPickupBlendTime;
	if ( f > 0 && f < ITEM_BLOB_TIME ) {
		f /= ITEM_BLOB_TIME;
		w *= ( 1 + f );
	}

	ca = cg_drawCrosshair.integer;
	if (ca < 0) {
		ca = 0;
	}
	hShader = cgs.media.crosshairShader[ ca % NUM_CROSSHAIRS ];

	// Use a different method rendering the crosshair so players don't see two of them when
	// focusing their eyes at distant objects with high stereo separation
	// We are going to trace to the next shootable object and place the crosshair in front of it.

	// first get all the important renderer information
	trap_Cvar_VariableStringBuffer("r_zProj", rendererinfos, sizeof(rendererinfos));
	zProj = atof(rendererinfos);
	trap_Cvar_VariableStringBuffer("r_stereoSeparation", rendererinfos, sizeof(rendererinfos));
	stereoSep = zProj / atof(rendererinfos);
	
	xmax = zProj * tan(cg.refdef.fov_x * M_PI / 360.0f);
	
	// let the trace run through until a change in stereo separation of the crosshair becomes less than one pixel.
	maxdist = cgs.glconfig.vidWidth * stereoSep * zProj / (2 * xmax);
	VectorMA(cg.refdef.vieworg, maxdist, cg.refdef.viewaxis[0], endpos);
	CG_Trace(&trace, cg.refdef.vieworg, NULL, NULL, endpos, 0, MASK_SHOT);
	
	memset(&ent, 0, sizeof(ent));
	ent.reType = RT_SPRITE;
	ent.renderfx = RF_DEPTHHACK | RF_CROSSHAIR;
	
	VectorCopy(trace.endpos, ent.origin);
	
	// scale the crosshair so it appears the same size for all distances
	ent.radius = w / 640 * xmax * trace.fraction * maxdist / zProj;
	ent.customShader = hShader;

	trap_R_AddRefEntityToScene(&ent);
}



/*
=================
CG_DrawHitMarker

A brief "+"-bracket confirmation when you damage someone, tinted by the
speed at the moment of impact: amber normally, alert-red when you hit
fast enough to be lethal. Makes speed = damage legible at the point of
aim, and gives baseq3 the hit feedback it never had.
=================
*/
static void CG_DrawHitMarker( void ) {
	float			frac, alpha, len, gap;
	vec4_t			col;
	float			*base;
	int				dt;
	static vec4_t	hm_amber = { 1.00f, 0.60f, 0.06f, 1.00f };
	static vec4_t	hm_red   = { 1.00f, 0.12f, 0.16f, 1.00f };

	if ( !cg.hitMarkerTime ) {
		return;
	}
	dt = cg.time - cg.hitMarkerTime;
	if ( dt < 0 || dt > 200 ) {
		return;
	}

	frac = 1.0f - (float)dt / 200.0f;		// fade out
	alpha = 0.85f * frac;

	base = cg.hitMarkerFast ? hm_red : hm_amber;
	col[0] = base[0];
	col[1] = base[1];
	col[2] = base[2];
	col[3] = alpha;

	len = 6.0f;
	gap = 6.0f + ( 1.0f - frac ) * 4.0f;	// ticks drift outward as they fade

	CG_FillRect( 320 - 1, 240 - gap - len, 2, len, col );	// top
	CG_FillRect( 320 - 1, 240 + gap, 2, len, col );			// bottom
	CG_FillRect( 320 - gap - len, 240 - 1, len, 2, col );	// left
	CG_FillRect( 320 + gap, 240 - 1, len, 2, col );			// right
}


/*
=================
CG_DrawDamageDirection

Momentary red arc on a ring around the crosshair pointing toward whoever
just hit you — baseq3 only flashed the whole screen red, leaving you
blind to the threat's bearing. Knowing where to turn is knowing where to
keep moving.
=================
*/
static void CG_DrawDamageDirection( void ) {
	float			frac, alpha, a, cx, cy, w;
	vec4_t			col;
	int				dt, i;
	static vec4_t	dd_red = { 1.00f, 0.12f, 0.16f, 1.00f };
	const float		R = 90.0f;

	if ( !cg.dmgDirTime ) {
		return;
	}
	dt = cg.time - cg.dmgDirTime;
	if ( dt < 0 || dt > 1000 ) {
		return;
	}

	frac = 1.0f - (float)dt / 1000.0f;
	alpha = 0.7f * frac;

	col[0] = dd_red[0];
	col[1] = dd_red[1];
	col[2] = dd_red[2];

	// three ticks form a short arc centred on the attacker's bearing;
	// ahead = top, left = left (screen-relative snapshot at hit time)
	for ( i = -1 ; i <= 1 ; i++ ) {
		a = cg.dmgDirAngle + i * 0.18f;		// ~10 degrees apart
		w = ( i == 0 ) ? 12.0f : 8.0f;
		cx = 320.0f - R * sin( a ) - w * 0.5f;
		cy = 240.0f - R * cos( a ) - w * 0.5f;
		col[3] = ( i == 0 ) ? alpha : alpha * 0.6f;
		CG_FillRect( cx, cy, w, w, col );
	}
}


/*
=================
CG_DrawFragFlash

A brief amber bloom over the scene when a kill heals you, so the frag
reward (health + armor on every kill) reads as a surge of life rather
than a silent stat change. Subtle and quick — a pop, not a wash.
=================
*/
static void CG_DrawFragFlash( void ) {
	float			frac;
	vec4_t			col;
	int				dt;
	static vec4_t	ff_amber = { 1.00f, 0.60f, 0.06f, 1.00f };

	if ( !cg.fragFlashTime ) {
		return;
	}
	dt = cg.time - cg.fragFlashTime;
	if ( dt < 0 || dt > 250 ) {
		return;
	}

	frac = 1.0f - (float)dt / 250.0f;
	col[0] = ff_amber[0];
	col[1] = ff_amber[1];
	col[2] = ff_amber[2];
	col[3] = 0.16f * frac;		// faint pop, never obscures the fight
	CG_FillRect( 0, 0, 640, 480, col );
}


/*
=================
CG_ScanForCrosshairEntity
=================
*/
static void CG_ScanForCrosshairEntity( void ) {
	trace_t		trace;
	vec3_t		start, end;
	int			content;

	VectorCopy( cg.refdef.vieworg, start );
	VectorMA( start, 131072, cg.refdef.viewaxis[0], end );

	CG_Trace( &trace, start, vec3_origin, vec3_origin, end, 
		cg.snap->ps.clientNum, CONTENTS_SOLID|CONTENTS_BODY );
	if ( trace.entityNum >= MAX_CLIENTS ) {
		return;
	}

	// if the player is in fog, don't show it
	content = CG_PointContents( trace.endpos, 0 );
	if ( content & CONTENTS_FOG ) {
		return;
	}

	// if the player is invisible, don't show it
	if ( cg_entities[ trace.entityNum ].currentState.powerups & ( 1 << PW_INVIS ) ) {
		return;
	}

	// update the fade timer
	cg.crosshairClientNum = trace.entityNum;
	cg.crosshairClientTime = cg.time;
}


/*
=====================
CG_DrawCrosshairNames
=====================
*/
static void CG_DrawCrosshairNames( void ) {
	float		*color;
	char		*name;
	float		w;

	if ( !cg_drawCrosshair.integer ) {
		return;
	}
	if ( !cg_drawCrosshairNames.integer ) {
		return;
	}
	if ( cg.renderingThirdPerson ) {
		return;
	}

	// scan the known entities to see if the crosshair is sighted on one
	CG_ScanForCrosshairEntity();

	// draw the name of the player being looked at
	color = CG_FadeColor( cg.crosshairClientTime, 1000 );
	if ( !color ) {
		trap_R_SetColor( NULL );
		return;
	}

	name = cgs.clientinfo[ cg.crosshairClientNum ].name;
#ifdef MISSIONPACK
	color[3] *= 0.5f;
	w = CG_Text_Width(name, 0.3f, 0);
	CG_Text_Paint( 320 - w / 2, 190, 0.3f, color, name, 0, 0, ITEM_TEXTSTYLE_SHADOWED);
#else
	{
		char	clean[64];
		vec4_t	cc;

		Q_strncpyz( clean, name, sizeof( clean ) );
		Q_CleanStr( clean );
		cc[0] = 1.0f; cc[1] = 0.6f; cc[2] = 0.06f; cc[3] = color[3] * 0.6f;
		w = CG_MatrixStringWidth( clean, 1.4f );
		CG_DrawMatrixString( 320 - w / 2, 168, clean, 1.4f, cc );
	}
#endif
	trap_R_SetColor( NULL );
}


//==============================================================================

/*
=================
CG_DrawSpectator
=================
*/
static void CG_DrawSpectator(void) {
	CG_DrawBigString(320 - 9 * 8, 440, "SPECTATOR", 1.0F);
	if ( cgs.gametype == GT_TOURNAMENT ) {
		CG_DrawBigString(320 - 15 * 8, 460, "waiting to play", 1.0F);
	}
	else if ( cgs.gametype >= GT_TEAM ) {
		CG_DrawBigString(320 - 39 * 8, 460, "press ESC and use the JOIN menu to play", 1.0F);
	}
}

/*
=================
CG_DrawVote
=================
*/
static void CG_DrawVote(void) {
	char	*s;
	int		sec;

	if ( !cgs.voteTime ) {
		return;
	}

	// play a talk beep whenever it is modified
	if ( cgs.voteModified ) {
		cgs.voteModified = qfalse;
		trap_S_StartLocalSound( cgs.media.talkSound, CHAN_LOCAL_SOUND );
	}

	sec = ( VOTE_TIME - ( cg.time - cgs.voteTime ) ) / 1000;
	if ( sec < 0 ) {
		sec = 0;
	}
#ifdef MISSIONPACK
	s = va("VOTE(%i):%s yes:%i no:%i", sec, cgs.voteString, cgs.voteYes, cgs.voteNo);
	CG_DrawSmallString( 0, 58, s, 1.0F );
	s = "or press ESC then click Vote";
	CG_DrawSmallString( 0, 58 + SMALLCHAR_HEIGHT + 2, s, 1.0F );
#else
	s = va("VOTE(%i):%s yes:%i no:%i", sec, cgs.voteString, cgs.voteYes, cgs.voteNo );
	CG_DrawSmallString( 0, 58, s, 1.0F );
#endif
}

/*
=================
CG_DrawTeamVote
=================
*/
static void CG_DrawTeamVote(void) {
	char	*s;
	int		sec, cs_offset;

	if ( cgs.clientinfo[cg.clientNum].team == TEAM_RED )
		cs_offset = 0;
	else if ( cgs.clientinfo[cg.clientNum].team == TEAM_BLUE )
		cs_offset = 1;
	else
		return;

	if ( !cgs.teamVoteTime[cs_offset] ) {
		return;
	}

	// play a talk beep whenever it is modified
	if ( cgs.teamVoteModified[cs_offset] ) {
		cgs.teamVoteModified[cs_offset] = qfalse;
		trap_S_StartLocalSound( cgs.media.talkSound, CHAN_LOCAL_SOUND );
	}

	sec = ( VOTE_TIME - ( cg.time - cgs.teamVoteTime[cs_offset] ) ) / 1000;
	if ( sec < 0 ) {
		sec = 0;
	}
	s = va("TEAMVOTE(%i):%s yes:%i no:%i", sec, cgs.teamVoteString[cs_offset],
							cgs.teamVoteYes[cs_offset], cgs.teamVoteNo[cs_offset] );
	CG_DrawSmallString( 0, 90, s, 1.0F );
}


static qboolean CG_DrawScoreboard( void ) {
#ifdef MISSIONPACK
	static qboolean firstTime = qtrue;

	if (menuScoreboard) {
		menuScoreboard->window.flags &= ~WINDOW_FORCED;
	}
	if (cg_paused.integer) {
		cg.deferredPlayerLoading = 0;
		firstTime = qtrue;
		return qfalse;
	}

	// should never happen in Team Arena
	if (cgs.gametype == GT_SINGLE_PLAYER && cg.predictedPlayerState.pm_type == PM_INTERMISSION ) {
		cg.deferredPlayerLoading = 0;
		firstTime = qtrue;
		return qfalse;
	}

	// don't draw scoreboard during death while warmup up
	if ( cg.warmup && !cg.showScores ) {
		return qfalse;
	}

	if ( cg.showScores || cg.predictedPlayerState.pm_type == PM_DEAD || cg.predictedPlayerState.pm_type == PM_INTERMISSION ) {
	} else {
		if ( !CG_FadeColor( cg.scoreFadeTime, FADE_TIME ) ) {
			// next time scoreboard comes up, don't print killer
			cg.deferredPlayerLoading = 0;
			cg.killerName[0] = 0;
			firstTime = qtrue;
			return qfalse;
		}
	}

	if (menuScoreboard == NULL) {
		if ( cgs.gametype >= GT_TEAM ) {
			menuScoreboard = Menus_FindByName("teamscore_menu");
		} else {
			menuScoreboard = Menus_FindByName("score_menu");
		}
	}

	if (menuScoreboard) {
		if (firstTime) {
			CG_SetScoreSelection(menuScoreboard);
			firstTime = qfalse;
		}
		Menu_Paint(menuScoreboard, qtrue);
	}

	// load any models that have been deferred
	if ( ++cg.deferredPlayerLoading > 10 ) {
		CG_LoadDeferredPlayers();
	}

	return qtrue;
#else
	return CG_DrawOldScoreboard();
#endif
}

/*
=================
CG_DrawIntermission
=================
*/
static void CG_DrawIntermission( void ) {
//	int key;
#ifdef MISSIONPACK
	//if (cg_singlePlayer.integer) {
	//	CG_DrawCenterString();
	//	return;
	//}
#else
	if ( cgs.gametype == GT_SINGLE_PLAYER ) {
		CG_DrawCenterString();
		return;
	}
#endif
	cg.scoreFadeTime = cg.time;
	cg.scoreBoardShowing = CG_DrawScoreboard();
}

/*
=================
CG_DrawFollow
=================
*/
static qboolean CG_DrawFollow( void ) {
	float		x;
	vec4_t		color;
	const char	*name;

	if ( !(cg.snap->ps.pm_flags & PMF_FOLLOW) ) {
		return qfalse;
	}
	color[0] = 1;
	color[1] = 1;
	color[2] = 1;
	color[3] = 1;


	CG_DrawBigString( 320 - 9 * 8, 24, "following", 1.0F );

	name = cgs.clientinfo[ cg.snap->ps.clientNum ].name;

	x = 0.5 * ( 640 - GIANT_WIDTH * CG_DrawStrlen( name ) );

	CG_DrawStringExt( x, 40, name, color, qtrue, qtrue, GIANT_WIDTH, GIANT_HEIGHT, 0 );

	return qtrue;
}



/*
=================
CG_DrawAmmoWarning
=================
*/
static void CG_DrawAmmoWarning( void ) {
	const char	*s;
	int			w;

	if ( cg_drawAmmoWarning.integer == 0 ) {
		return;
	}

	if ( !cg.lowAmmoWarning ) {
		return;
	}

	// no ammo nag for the melee kit (sword/gauntlet) or any infinite-ammo weapon —
	// it's not "out of ammo", it's the blade. Keeps the slow-mo melee frame clean.
	{
		int wp = cg.snap->ps.weapon;
		if ( wp == WP_GAUNTLET || wp == WP_SWORD || cg.snap->ps.ammo[wp] < 0 ) {
			return;
		}
	}

	if ( cg.lowAmmoWarning == 2 ) {
		s = "OUT OF AMMO";
	} else {
		s = "LOW AMMO WARNING";
	}
	w = CG_DrawStrlen( s ) * BIGCHAR_WIDTH;
	CG_DrawBigString(320 - w / 2, 64, s, 1.0F);
}


#ifdef MISSIONPACK
/*
=================
CG_DrawProxWarning
=================
*/
static void CG_DrawProxWarning( void ) {
	char s [32];
	int			w;
  static int proxTime;
  int proxTick;

	if( !(cg.snap->ps.eFlags & EF_TICKING ) ) {
    proxTime = 0;
		return;
	}

  if (proxTime == 0) {
    proxTime = cg.time;
  }

  proxTick = 10 - ((cg.time - proxTime) / 1000);

  if (proxTick > 0 && proxTick <= 5) {
    Com_sprintf(s, sizeof(s), "INTERNAL COMBUSTION IN: %i", proxTick);
  } else {
    Com_sprintf(s, sizeof(s), "YOU HAVE BEEN MINED");
  }

	w = CG_DrawStrlen( s ) * BIGCHAR_WIDTH;
	CG_DrawBigStringColor( 320 - w / 2, 64 + BIGCHAR_HEIGHT, s, g_color_table[ColorIndex(COLOR_RED)] );
}
#endif


/*
=================
CG_DrawWarmup
=================
*/
static void CG_DrawWarmup( void ) {
	int			w;
	int			sec;
	int			i;
#ifdef MISSIONPACK
	float		scale;
#else
	int			cw;
#endif
	clientInfo_t	*ci1, *ci2;
	const char	*s;

	sec = cg.warmup;
	if ( !sec ) {
		return;
	}

	if ( sec < 0 ) {
		s = "Waiting for players";		
		w = CG_DrawStrlen( s ) * BIGCHAR_WIDTH;
		CG_DrawBigString(320 - w / 2, 24, s, 1.0F);
		cg.warmupCount = 0;
		return;
	}

	if (cgs.gametype == GT_TOURNAMENT) {
		// find the two active players
		ci1 = NULL;
		ci2 = NULL;
		for ( i = 0 ; i < cgs.maxclients ; i++ ) {
			if ( cgs.clientinfo[i].infoValid && cgs.clientinfo[i].team == TEAM_FREE ) {
				if ( !ci1 ) {
					ci1 = &cgs.clientinfo[i];
				} else {
					ci2 = &cgs.clientinfo[i];
				}
			}
		}

		if ( ci1 && ci2 ) {
			s = va( "%s vs %s", ci1->name, ci2->name );
#ifdef MISSIONPACK
			w = CG_Text_Width(s, 0.6f, 0);
			CG_Text_Paint(320 - w / 2, 60, 0.6f, colorWhite, s, 0, 0, ITEM_TEXTSTYLE_SHADOWEDMORE);
#else
			w = CG_DrawStrlen( s );
			if ( w > 640 / GIANT_WIDTH ) {
				cw = 640 / w;
			} else {
				cw = GIANT_WIDTH;
			}
			CG_DrawStringExt( 320 - w * cw/2, 20,s, colorWhite, 
					qfalse, qtrue, cw, (int)(cw * 1.5f), 0 );
#endif
		}
	} else {
		if ( cgs.gametype == GT_FFA ) {
			s = "Free For All";
		} else if ( cgs.gametype == GT_TEAM ) {
			s = "Team Deathmatch";
		} else if ( cgs.gametype == GT_CTF ) {
			s = "Capture the Flag";
#ifdef MISSIONPACK
		} else if ( cgs.gametype == GT_1FCTF ) {
			s = "One Flag CTF";
		} else if ( cgs.gametype == GT_OBELISK ) {
			s = "Overload";
		} else if ( cgs.gametype == GT_HARVESTER ) {
			s = "Harvester";
#endif
		} else {
			s = "";
		}
#ifdef MISSIONPACK
		w = CG_Text_Width(s, 0.6f, 0);
		CG_Text_Paint(320 - w / 2, 90, 0.6f, colorWhite, s, 0, 0, ITEM_TEXTSTYLE_SHADOWEDMORE);
#else
		w = CG_DrawStrlen( s );
		if ( w > 640 / GIANT_WIDTH ) {
			cw = 640 / w;
		} else {
			cw = GIANT_WIDTH;
		}
		CG_DrawStringExt( 320 - w * cw/2, 25,s, colorWhite, 
				qfalse, qtrue, cw, (int)(cw * 1.1f), 0 );
#endif
	}

	sec = ( sec - cg.time ) / 1000;
	if ( sec < 0 ) {
		cg.warmup = 0;
		sec = 0;
	}
	s = va( "Starts in: %i", sec + 1 );
	if ( sec != cg.warmupCount ) {
		cg.warmupCount = sec;
		switch ( sec ) {
		case 0:
			trap_S_StartLocalSound( cgs.media.count1Sound, CHAN_ANNOUNCER );
			break;
		case 1:
			trap_S_StartLocalSound( cgs.media.count2Sound, CHAN_ANNOUNCER );
			break;
		case 2:
			trap_S_StartLocalSound( cgs.media.count3Sound, CHAN_ANNOUNCER );
			break;
		default:
			break;
		}
	}

#ifdef MISSIONPACK
	switch ( cg.warmupCount ) {
	case 0:
		scale = 0.54f;
		break;
	case 1:
		scale = 0.51f;
		break;
	case 2:
		scale = 0.48f;
		break;
	default:
		scale = 0.45f;
		break;
	}

	w = CG_Text_Width(s, scale, 0);
	CG_Text_Paint(320 - w / 2, 125, scale, colorWhite, s, 0, 0, ITEM_TEXTSTYLE_SHADOWEDMORE);
#else
	switch ( cg.warmupCount ) {
	case 0:
		cw = 28;
		break;
	case 1:
		cw = 24;
		break;
	case 2:
		cw = 20;
		break;
	default:
		cw = 16;
		break;
	}

	w = CG_DrawStrlen( s );
	CG_DrawStringExt( 320 - w * cw/2, 70, s, colorWhite, 
			qfalse, qtrue, cw, (int)(cw * 1.5), 0 );
#endif
}

//==================================================================================
#ifdef MISSIONPACK
/* 
=================
CG_DrawTimedMenus
=================
*/
void CG_DrawTimedMenus( void ) {
	if (cg.voiceTime) {
		int t = cg.time - cg.voiceTime;
		if ( t > 2500 ) {
			Menus_CloseByName("voiceMenu");
			trap_Cvar_Set("cl_conXOffset", "0");
			cg.voiceTime = 0;
		}
	}
}
#endif
/*
==================
STRAFE 64 / NERV-MAGI HUD identity

A single amber-on-black terminal palette ties the readouts together,
escalating to alert-red under pressure. CG_NervPanel draws the dark
translucent backing plate with L-shaped corner brackets — the targeting
reticle frame of the Eva interface.
==================
*/
static vec4_t nerv_amber  = { 1.00f, 0.60f, 0.06f, 1.00f };	// MAGI ops
static vec4_t nerv_orange = { 1.00f, 0.42f, 0.05f, 1.00f };	// elevated
static vec4_t nerv_red    = { 1.00f, 0.12f, 0.16f, 1.00f };	// alert
static vec4_t nerv_green  = { 0.40f, 1.00f, 0.50f, 1.00f };	// nominal/go
static vec4_t nerv_dim    = { 0.55f, 0.42f, 0.20f, 1.00f };	// idle
static vec4_t nerv_cyan   = { 0.32f, 0.86f, 1.00f, 1.00f };	// MAGI teal — nominal readouts
static vec4_t nerv_fill   = { 0.02f, 0.03f, 0.04f, 0.55f };	// panel plate

static void CG_NervPanel( float x, float y, float w, float h, const float *accent ) {
	// No borders, no plates — the HUD is clean floating LED text (the matrix font
	// carries a 1px drop shadow for legibility). Kept as a no-op so the call sites
	// stay put. (Gustav's call 2026-06-16: good font, no frames — keep it minimal.)
	(void)x; (void)y; (void)w; (void)h; (void)accent;
}

/*
==================
CG_DrawMatrixString / CG_MatrixStringWidth

A 5x7 LED dot-matrix font drawn as quads — the MAGI/NERV terminal readout.
Crisp at any scale and any color with no texture: each lit cell is a small
quad inset by a thin gutter so it reads as an LED grid. `cell` is the size
of one LED in 640x480 virtual units; a glyph is 5 cells wide, 7 tall, with
a one-cell gap between characters. This is what replaces the chunky bitmap
charset and the pre-baked number textures on the STRAFE 64 HUD.
==================
*/
static const char cg_matrixOrder[] =
	" 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ:.-/%+!>";
static const unsigned char cg_matrixGlyph[45][7] = {
	{0,0,0,0,0,0,0},					// (space)
	{0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},	// 0
	{0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},	// 1
	{0x0E,0x11,0x01,0x02,0x04,0x08,0x1F},	// 2
	{0x1F,0x02,0x04,0x02,0x01,0x11,0x0E},	// 3
	{0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},	// 4
	{0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},	// 5
	{0x06,0x08,0x10,0x1E,0x11,0x11,0x0E},	// 6
	{0x1F,0x01,0x02,0x04,0x08,0x08,0x08},	// 7
	{0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},	// 8
	{0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C},	// 9
	{0x0E,0x11,0x11,0x1F,0x11,0x11,0x11},	// A
	{0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},	// B
	{0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},	// C
	{0x1C,0x12,0x11,0x11,0x11,0x12,0x1C},	// D
	{0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},	// E
	{0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},	// F
	{0x0E,0x11,0x10,0x17,0x11,0x11,0x0F},	// G
	{0x11,0x11,0x11,0x1F,0x11,0x11,0x11},	// H
	{0x0E,0x04,0x04,0x04,0x04,0x04,0x0E},	// I
	{0x07,0x02,0x02,0x02,0x02,0x12,0x0C},	// J
	{0x11,0x12,0x14,0x18,0x14,0x12,0x11},	// K
	{0x10,0x10,0x10,0x10,0x10,0x10,0x1F},	// L
	{0x11,0x1B,0x15,0x15,0x11,0x11,0x11},	// M
	{0x11,0x11,0x19,0x15,0x13,0x11,0x11},	// N
	{0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},	// O
	{0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},	// P
	{0x0E,0x11,0x11,0x11,0x15,0x12,0x0D},	// Q
	{0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},	// R
	{0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E},	// S
	{0x1F,0x04,0x04,0x04,0x04,0x04,0x04},	// T
	{0x11,0x11,0x11,0x11,0x11,0x11,0x0E},	// U
	{0x11,0x11,0x11,0x11,0x11,0x0A,0x04},	// V
	{0x11,0x11,0x11,0x15,0x15,0x1B,0x11},	// W
	{0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},	// X
	{0x11,0x11,0x0A,0x04,0x04,0x04,0x04},	// Y
	{0x1F,0x01,0x02,0x04,0x08,0x10,0x1F},	// Z
	{0x00,0x04,0x04,0x00,0x04,0x04,0x00},	// :
	{0x00,0x00,0x00,0x00,0x00,0x0C,0x0C},	// .
	{0x00,0x00,0x00,0x1F,0x00,0x00,0x00},	// -
	{0x01,0x02,0x02,0x04,0x08,0x08,0x10},	// /
	{0x19,0x1A,0x02,0x04,0x08,0x0B,0x13},	// %
	{0x00,0x04,0x04,0x1F,0x04,0x04,0x00},	// +
	{0x04,0x04,0x04,0x04,0x04,0x00,0x04},	// !
	{0x10,0x08,0x04,0x02,0x04,0x08,0x10}	// >
};

/* HUD text. PRIMARY: the Share Tech Mono TrueType font (fonts/strafe64.ttf, OFL)
   via the engine's FreeType atlas — clean anti-aliased mono, the NERV terminal look
   as designed (needs the renderer built with USE_FREETYPE=ON). FALLBACK: if the
   build has no FreeType (trap_R_RegisterFont returns an empty font), the procedural
   5x7 LED dot-matrix from the cg_matrixGlyph tables above, so the HUD can never go
   invisible. `cell` scales both to ~the same height. */
static int CG_MatrixGlyphIndex( char c ) {
	int i;
	if ( c >= 'a' && c <= 'z' ) {
		c -= 32;
	}
	for ( i = 0; cg_matrixOrder[i]; i++ ) {
		if ( cg_matrixOrder[i] == c ) {
			return i;
		}
	}
	return -1;
}

static void CG_LedDrawString( float x, float y, const char *s, float cell, const float *color ) {
	const char	*p;
	float		lit = cell * 0.82f, cx = x;
	vec4_t		shadow;

	shadow[0] = shadow[1] = shadow[2] = 0.0f;
	shadow[3] = ( color ? color[3] : 1.0f ) * 0.7f;
	for ( p = s; *p; p++ ) {
		int idx = CG_MatrixGlyphIndex( *p ), row, col;
		if ( idx < 0 ) {
			cx += 6.0f * cell;
			continue;
		}
		for ( row = 0; row < 7; row++ ) {
			unsigned char bits = cg_matrixGlyph[idx][row];
			for ( col = 0; col < 5; col++ ) {
				if ( bits & ( 0x10 >> col ) ) {
					float lx = cx + col * cell, ly = y + row * cell;
					CG_FillRect( lx + 1.0f, ly + 1.0f, lit, lit, shadow );
					CG_FillRect( lx, ly, lit, lit, color );
				}
			}
		}
		cx += 6.0f * cell;
	}
}

static fontInfo_t	cg_hudFont;
static qboolean		cg_hudFontReg = qfalse;

static qboolean CG_HudFontReady( void ) {
	if ( !cg_hudFontReg ) {
		trap_R_RegisterFont( "fonts/strafe64.ttf", 48, &cg_hudFont );
		cg_hudFontReg = qtrue;
	}
	return cg_hudFont.glyphScale > 0.0f;		// 0 => no FreeType -> LED fallback
}

int CG_MatrixStringWidth( const char *s, float cell ) {
	float	scale, w = 0.0f;

	if ( !CG_HudFontReady() ) {
		int n = 0; while ( s[n] ) n++;
		return (int)( n * 6.0f * cell );
	}
	scale = cell * 0.16f * cg_hudFont.glyphScale;
	while ( *s ) {
		w += cg_hudFont.glyphs[ *s & 255 ].xSkip * scale;
		s++;
	}
	return (int)w;
}

void CG_DrawMatrixString( float x, float y, const char *s, float cell, const float *color ) {
	const glyphInfo_t	*g;
	const char			*p;
	float				scale, ax, ay, aw, ah, cx;
	int					pass;
	vec4_t				shadow;

	if ( !CG_HudFontReady() ) {
		CG_LedDrawString( x, y, s, cell, color );		// no FreeType -> LED dots
		return;
	}
	scale = cell * 0.16f * cg_hudFont.glyphScale;
	y += 6.0f * cell;		// call sites pass the top edge; TrueType y is the baseline
	shadow[0] = shadow[1] = shadow[2] = 0.0f;
	shadow[3] = ( color ? color[3] : 1.0f ) * 0.7f;
	for ( pass = 0; pass < 2; pass++ ) {
		float off = pass ? 0.0f : 1.0f;		// pass 0 = drop shadow
		trap_R_SetColor( pass ? color : shadow );
		cx = x;
		for ( p = s; *p; p++ ) {
			g = &cg_hudFont.glyphs[ *p & 255 ];
			ax = cx + off;
			ay = y - g->top * scale + off;
			aw = g->imageWidth * scale;
			ah = g->imageHeight * scale;
			CG_AdjustFrom640( &ax, &ay, &aw, &ah );
			trap_R_DrawStretchPic( ax, ay, aw, ah, g->s, g->t, g->s2, g->t2, g->glyph );
			cx += g->xSkip * scale;
		}
	}
	trap_R_SetColor( NULL );
}

/*
==================
CG_Flow

The single normalized [0,1] momentum value the whole client reads from:
0 at run speed (g_speed, 320), 1 at the 960 ups ceiling where the FOV,
the speed-damage curve and the rail one-shot all top out. A live bhop
streak nudges it up so a chained run keeps the world lit through a brief
dip. Drives dynamic FOV (cg_view.c) and the flow color wash below, so
every "speed = everything" feedback channel shares one curve.
==================
*/
float CG_Flow( void ) {
	playerState_t	*ps;
	float			speed, flow;

	ps = &cg.predictedPlayerState;
	speed = sqrt( ps->velocity[0] * ps->velocity[0]
		+ ps->velocity[1] * ps->velocity[1] );

	flow = ( speed - 320.0f ) / 640.0f;
	flow += 0.05f * ( ps->stats[STAT_BHOP_STREAK] - 1 );
	if ( flow < 0.0f ) {
		flow = 0.0f;
	} else if ( flow > 1.0f ) {
		flow = 1.0f;
	}
	return flow;
}

/*
==================
CG_DrawFlowColor

The world reads from flow: at low momentum a cold wash drains the scene
toward gray-black — "the world dies if you stop" — and as flow climbs to
the 960 ceiling it clears entirely so the N64 vertex colors blaze. Drawn
over the 3D scene, under the HUD. This is the cgame-only phase-1 proxy
for the renderer-level saturation grade the PSX pass will own.
==================
*/
static void CG_DrawFlowColor( void ) {
	float	alpha;
	vec4_t	wash;

	if ( !cg_flowColor.integer ) {
		return;
	}

	alpha = ( 1.0f - CG_Flow() ) * 0.28f;	// gentle drain — keep the world readable
	if ( alpha <= 0.0f ) {
		return;
	}

	wash[0] = 0.05f;	// cold, faintly blue — toward the void, not toward fog
	wash[1] = 0.06f;
	wash[2] = 0.09f;
	wash[3] = alpha;
	CG_FillRect( 0, 0, 640, 480, wash );
}

/*
==================
CG_DrawStillness

The contrast to the flat flow wash. A uniform gray veil reads as dead and
dull; this gives the dying frame depth: as momentum bleeds away, a cold
void-blue vignette creeps in from the screen edges (darkest at the rim,
accumulating in the corners) instead of flattening the whole frame evenly.
The world doesn't just dim when you stop — it closes in. Scales with lost
momentum (1 - flow); drawn right after CG_DrawFlowColor, under the HUD.
==================
*/
static void CG_DrawStillness( void ) {
	float	still;
	vec4_t	col;
	int		i;
	// nested cold bands: { thickness, edge alpha } — outer-most last
	static const float bandW[3] = { 16.0f, 46.0f, 96.0f };
	static const float bandA[3] = { 0.55f, 0.28f, 0.12f };

	// the cold edge vignette read as a heavy black border framing the screen
	// (constant in bullet-time, where you're often still) — OFF by default for the
	// clean full-bleed look; `cg_stillVignette 1` brings it back.
	if ( !cg_stillVignette.integer || !cg_flowColor.integer ) {
		return;
	}

	still = 1.0f - CG_Flow();
	if ( still <= 0.04f ) {
		return;
	}

	// cold vignette — void-blue, accumulates at the corners for depth
	col[0] = 0.02f; col[1] = 0.03f; col[2] = 0.06f;
	for ( i = 0; i < 3; i++ ) {
		float w = bandW[i];
		col[3] = bandA[i] * still;
		CG_FillRect( 0,       0,       640, w,   col );	// top
		CG_FillRect( 0,       480 - w, 640, w,   col );	// bottom
		CG_FillRect( 0,       0,       w,   480, col );	// left
		CG_FillRect( 640 - w, 0,       w,   480, col );	// right
	}
}

/*
==================
CG_DrawStrafeMeter

Direct feedback for the one mechanic the whole game rests on: the air
wishspeed cap (pm_wishSpeedClamp = 30). While A/D-only strafing in the air
you only add velocity along wishdir up to the 30 cap, so you gain speed only
while (velocity . wishdir) < 30 — i.e. while the angle between your velocity
and your aim exceeds acos(30/|v|). That threshold climbs toward 90 deg as you
speed up.

The OPTIMUM is not 90 deg. Per pmove tick PM_Accelerate adds at most
  A = pm_strafeAccelerate * (pmove_msec/1000) * wishspeed
along wishdir, then clamps so (v.wishdir) never passes the 30 cap. Maximising
|v'| over the angle puts the peak exactly at the tick boundary, where
(v.wishdir) = wishspeed - A, i.e. cos(phi_opt) = (30 - A)/|v|. That sits a few
degrees inside the dead-zone edge (always > acos(30/|v|)) and creeps toward —
but never reaches — 90 deg as you speed up. The fixed-90 model only holds in
the limit of infinite accel; this tracks the real per-tick optimum instead.

A is timescale-INDEPENDENT: PM_AirMove feeds PM_Accelerate accel/timeScale,
but PM_Accelerate then scales by frametime = (scaled game-clock msec)/1000 ~
realMsec*timeScale, so the timeScale cancels and the real-time accel is the
same in bullet-time as at full speed. (Dividing A by timescale here was a bug
that pushed phi_opt past 90 deg in slowmo — telling you to aim into the loss
zone, where you bleed speed instead of gaining it.)

Past ~90 deg you start adding velocity AGAINST your motion: (v.wishdir) is
still under the cap so PM_Accelerate keeps pushing, but the push now shortens
|v|. Gain only holds up to phi_hi = acos(-A/(2|v|)), a hair past 90; beyond
that the band is speed LOSS, not gain, so it's drawn red, not green.

A horizontal strip under the crosshair:
  - center      = your velocity heading (wishdir aligned with motion, 0 gain)
  - +/- ticks   = the perfect-strafe targets at +/-phi_opt (bright green),
    which shift with speed / framerate to follow the real optimum
  - red dead-zone = angles where (v.wishdir) >= 30 so you gain nothing; it
    WIDENS as you speed up — the skill ceiling made visible
  - green band  = where you actually accelerate
  - needle      = your current velocity<->wishdir angle; green when gaining
    (brightest at phi_opt), red when bleeding speed inside the dead-zone

Shown airborne while holding a pure strafe — the state where the cap is the
game. Eases in so it never pops.
==================
*/
static void CG_DrawStrafeMeter( void ) {
	static float	vis = 0.0f;
	playerState_t	*ps;
	usercmd_t		cmd;
	int				cmdNum;
	vec3_t			forward, right;
	float			fmove, smove, wl;
	float			wishdir[2], vdir[2];
	float			speed, dot, phi, thetaMin, crossz, prox;
	float			tickAccel, phiOpt, phiHi, cosHi;
	float			cx, half, y0, barH, x;
	vec4_t			col;
	qboolean		active;

	if ( !cg_strafeHelper.integer ) {
		return;
	}

	ps = &cg.predictedPlayerState;
	speed = sqrt( ps->velocity[0]*ps->velocity[0] + ps->velocity[1]*ps->velocity[1] );

	cmdNum = trap_GetCurrentCmdNumber();
	trap_GetUserCmd( cmdNum, &cmd );
	fmove = cmd.forwardmove;
	smove = cmd.rightmove;

	// the cap-bound skill mode: airborne, pure left/right strafe, above the cap
	active = ( ps->groundEntityNum == ENTITYNUM_NONE
		&& fmove == 0 && smove != 0 && speed > 30.0f );

	vis += ( ( active ? 1.0f : 0.0f ) - vis ) * 0.18f;	// ease in/out
	if ( !active || vis < 0.02f ) {
		return;
	}

	// wishdir from view + strafe key, flattened (same as PM_AirMove)
	AngleVectors( ps->viewangles, forward, right, NULL );
	forward[2] = 0; right[2] = 0;
	VectorNormalize( forward ); VectorNormalize( right );
	wishdir[0] = forward[0]*fmove + right[0]*smove;
	wishdir[1] = forward[1]*fmove + right[1]*smove;
	wl = sqrt( wishdir[0]*wishdir[0] + wishdir[1]*wishdir[1] );
	if ( wl < 0.001f ) {
		return;
	}
	wishdir[0] /= wl; wishdir[1] /= wl;

	vdir[0] = ps->velocity[0] / speed;
	vdir[1] = ps->velocity[1] / speed;

	// signed angle velocity -> wishdir, degrees [-180,180]
	dot = vdir[0]*wishdir[0] + vdir[1]*wishdir[1];
	if ( dot > 1.0f ) { dot = 1.0f; } else if ( dot < -1.0f ) { dot = -1.0f; }
	// crossz > 0 means wishdir is CCW of velocity (the player's left); we want
	// the needle on the side the player is actually pushing toward, so a
	// rightward wishdir reads as +phi (right of the strip). Hence the flip.
	crossz = vdir[0]*wishdir[1] - vdir[1]*wishdir[0];
	phi = (float)acos( dot ) * 180.0f / M_PI;
	if ( crossz > 0 ) { phi = -phi; }

	thetaMin = (float)acos( 30.0f / speed ) * 180.0f / M_PI;	// gain threshold

	// true per-tick optimum, from the SHARED helper the bots also strafe at, so
	// what the meter teaches and what the AI does can't drift. tickAccel = max
	// along-wishdir add before the 30 cap clips; phiOpt = the peak-gain angle.
	// Timescale-independent by construction (see PM_OptimalStrafeAngle).
	tickAccel = PM_StrafeTickAccel( pmove_msec.integer * 0.001f );
	phiOpt = PM_OptimalStrafeAngle( speed, pmove_msec.integer * 0.001f );	// (thetaMin, 90)

	// upper edge of real speed GAIN: past here the push points enough against
	// |v| that magnitude shrinks even though (v.wishdir) is still under the cap.
	cosHi = -tickAccel / ( 2.0f * speed );
	if ( cosHi > 1.0f ) { cosHi = 1.0f; } else if ( cosHi < -1.0f ) { cosHi = -1.0f; }
	phiHi = (float)acos( cosHi ) * 180.0f / M_PI;	// a hair past 90

	cx = 320.0f; half = 130.0f; y0 = 366.0f; barH = 7.0f;
#define SX(P) ( cx + ( (P) / 180.0f ) * half )

	// base track
	col[0]=0.10f; col[1]=0.12f; col[2]=0.16f; col[3]=0.55f*vis;
	CG_FillRect( cx - half, y0, 2.0f*half, barH, col );

	// green gain band on the strafing side (thetaMin .. phiHi) — only where |v|
	// actually grows; the stretch past phiHi is speed loss and gets the red wash
	col[0]=0.40f; col[1]=1.00f; col[2]=0.50f; col[3]=0.16f*vis;
	if ( phi < 0 ) {
		CG_FillRect( SX(-phiHi), y0, SX(-thetaMin)-SX(-phiHi), barH, col );
	} else {
		CG_FillRect( SX(thetaMin), y0, SX(phiHi)-SX(thetaMin), barH, col );
	}

	// red loss band (phiHi .. 180): still "accelerating" along wishdir, but the
	// push fights your motion so speed bleeds — aiming here is the trap
	col[0]=1.00f; col[1]=0.12f; col[2]=0.16f; col[3]=0.18f*vis;
	if ( phi < 0 ) {
		CG_FillRect( SX(-180.0f), y0, SX(-phiHi)-SX(-180.0f), barH, col );
	} else {
		CG_FillRect( SX(phiHi), y0, SX(180.0f)-SX(phiHi), barH, col );
	}

	// red dead-zone (|phi| < thetaMin) — widens with speed
	col[0]=1.00f; col[1]=0.12f; col[2]=0.16f; col[3]=0.22f*vis;
	CG_FillRect( SX(-thetaMin), y0, SX(thetaMin)-SX(-thetaMin), barH, col );

	// +/-phiOpt optimal ticks — the real per-tick peak, not a fixed 90
	col[0]=0.40f; col[1]=1.00f; col[2]=0.50f; col[3]=0.85f*vis;
	CG_FillRect( SX(-phiOpt)-1.0f, y0-3.0f, 2.0f, barH+6.0f, col );
	CG_FillRect( SX( phiOpt)-1.0f, y0-3.0f, 2.0f, barH+6.0f, col );

	// velocity reference (center)
	col[0]=0.55f; col[1]=0.42f; col[2]=0.20f; col[3]=0.5f*vis;
	CG_FillRect( cx-1.0f, y0-1.0f, 2.0f, barH+2.0f, col );

	// the needle
	x = SX( phi );
	if ( x < cx-half ) { x = cx-half; } else if ( x > cx+half ) { x = cx+half; }
	if ( fabs( phi ) > thetaMin && fabs( phi ) < phiHi ) {
		prox = 1.0f - (float)fabs( fabs(phi) - phiOpt ) / 90.0f;	// 1 at phi_opt
		if ( prox < 0.0f ) { prox = 0.0f; }
		col[0]=0.40f; col[1]=1.00f; col[2]=0.50f; col[3]=(0.55f+0.45f*prox)*vis;
	} else {
		// dead zone (no gain) or past phiHi (bleeding speed) — both are red
		col[0]=1.00f; col[1]=0.12f; col[2]=0.16f; col[3]=0.95f*vis;
	}
	CG_FillRect( x-1.5f, y0-5.0f, 3.0f, barH+10.0f, col );

#undef SX
}

/*
==================
CG_Corruption

How corrupted the world is right now, [0,1] — the glitch sibling of
CG_Flow. The max of three pressures: lost momentum (the world frays if
you stop), the rising void closing in, and a hard spike when you take a
hit. The single value the glitch layer reads, scaled by cg_glitchAmount.
==================
*/
static float CG_Corruption( void ) {
	float	c, t, dist;

	// no ambient glitch: the datamosh is a burst effect, not a constant wash.
	// It fires only on real threats — the void closing in and taking a hit —
	// below. Standing still (which the slow-mo direction makes the common
	// case) should leave the screen clean.
	c = 0.0f;

	// the rising void: ramps in over the last 600 units of altitude
	if ( cgs.voidActive ) {
		dist = cg.predictedPlayerState.origin[2] - CG_VoidZ();
		if ( dist < 600.0f ) {
			t = ( 600.0f - dist ) / 600.0f;
			if ( t > c ) {
				c = t;
			}
		}
	}

	// a hit spikes corruption hard (damageValue caps at 10), decaying
	// over DAMAGE_TIME
	if ( cg.damageTime && cg.time - cg.damageTime < DAMAGE_TIME ) {
		t = ( cg.damageValue / 10.0f )
			* ( 1.0f - (float)( cg.time - cg.damageTime ) / DAMAGE_TIME );
		if ( t > c ) {
			c = t;
		}
	}

	c *= cg_glitchAmount.value;
	if ( c < 0.0f ) {
		c = 0.0f;
	} else if ( c > 1.0f ) {
		c = 1.0f;
	}
	return c;
}

/*
==================
CG_DrawGlitch

Cgame-only corruption glitch: datamosh macroblocks, scanline tears and a
chromatic flash, all re-jittered every frame and scaled by CG_Corruption.
This is the dissolving-world aesthetic on the GL1/PSX path — it only adds
quads, never resamples the framebuffer, so true RGB-split / vertex jitter
wait for the GL2 phase-2 renderer pass. Drawn over the scene, under the
HUD so the ops readout stays legible through the noise.
==================
*/
static void CG_DrawGlitch( void ) {
	static vec4_t	pal[6] = {
		{ 0.00f, 1.00f, 1.00f, 1.0f },	// cyan
		{ 1.00f, 0.00f, 1.00f, 1.0f },	// magenta
		{ 0.20f, 0.40f, 1.00f, 1.0f },	// void blue
		{ 1.00f, 0.60f, 0.06f, 1.0f },	// MAGI amber
		{ 0.40f, 1.00f, 0.50f, 1.0f },	// terminal green
		{ 0.02f, 0.02f, 0.04f, 1.0f }	// dropout black
	};
	float	corruption, y, h, w, x;
	int		i, n;
	vec4_t	col;

	if ( !cg_glitch.integer ) {
		return;
	}

	corruption = CG_Corruption();
	if ( corruption <= 0.02f ) {
		return;
	}

	// datamosh macroblocks — count grows with the square, so a little
	// corruption is a few stray blocks and full corruption is a storm
	n = (int)( corruption * corruption * 18.0f );
	for ( i = 0; i < n; i++ ) {
		Vector4Copy( pal[ (int)( random() * 6.0f ) % 6 ], col );
		col[3] = 0.25f + random() * 0.35f;
		x = random() * 640.0f;
		y = random() * 480.0f;
		w = 8.0f + random() * 56.0f;
		h = 5.0f + random() * 22.0f;
		CG_FillRect( x, y, w, h, col );
	}

	// horizontal scanline tears — thin full-width strips that strobe
	n = (int)( corruption * 6.0f );
	for ( i = 0; i < n; i++ ) {
		if ( random() < 0.5f ) {
			Vector4Copy( pal[5], col );			// dropout black
			col[3] = 0.5f;
		} else {
			Vector4Copy( pal[ (int)( random() * 3.0f ) % 3 ], col );
			col[3] = 0.3f;
		}
		y = random() * 480.0f;
		h = 1.0f + random() * 3.0f;
		CG_FillRect( 0, y, 640, h, col );
	}

	// chromatic flash at heavy corruption: a faint full-screen channel
	// wash alternating per frame, the suggestion of RGB tearing without a
	// real framebuffer split
	if ( corruption > 0.6f ) {
		Vector4Copy( ( cg.time / 50 ) & 1 ? pal[0] : pal[1], col );
		col[3] = ( corruption - 0.6f ) * 0.35f;
		CG_FillRect( 0, 0, 640, 480, col );
	}
}

/*
==================
CG_DrawSpeedLines

Tunnel motion streaks that rush inward from the four screen edges, longer
and denser the higher CG_Flow climbs — the "going fast feels fast" juice
on top of the FOV stretch. Re-jittered every frame so they read as motion
blur. Axis-aligned quads (GL1/cgame), over the scene and under the HUD.
==================
*/
static void CG_DrawSpeedLines( void ) {
	float	flow, intensity, len, t;
	int		i, n;
	vec4_t	col;

	if ( !cg_speedLines.integer ) {
		return;
	}

	flow = CG_Flow();
	if ( flow < 0.35f ) {
		return;				// only at real speed — off the screen otherwise
	}
	intensity = ( flow - 0.35f ) / 0.65f;

	col[0] = 0.6f;			// cool white-cyan, the speed color
	col[1] = 0.9f;
	col[2] = 1.0f;

	n = (int)( 2 + intensity * 6 );
	for ( i = 0; i < n; i++ ) {
		col[3] = ( 0.06f + random() * 0.10f ) * intensity;
		len = ( 30.0f + random() * 130.0f ) * intensity;

		// left / right edges: horizontal streaks raking inward
		t = random() * 480.0f;
		CG_FillRect( 0, t, len, 1.0f + random() * 2.0f, col );
		t = random() * 480.0f;
		CG_FillRect( 640.0f - len, t, len, 1.0f + random() * 2.0f, col );

		// top / bottom edges: shorter vertical streaks (screen is wider)
		len *= 0.75f;
		t = random() * 640.0f;
		CG_FillRect( t, 0, 1.0f + random() * 2.0f, len, col );
		t = random() * 640.0f;
		CG_FillRect( t, 480.0f - len, 1.0f + random() * 2.0f, len, col );
	}
}

/*
==================
CG_DrawSpeedVignette

Peripheral flow cue: amber side-glows that bloom in from the screen
edges as the speed-damage multiplier climbs, deepening to alert-red at
the cap. The ambient, felt counterpart to the speedometer's number —
you sense flow in your periphery before you read it.
==================
*/
static void CG_DrawSpeedVignette( void ) {
	float			speed, scale, alpha, w, a;
	vec4_t			col;
	float			*base;
	playerState_t	*ps;
	int				i;

	if ( !cg_drawSpeed.integer ) {
		return;
	}

	ps = &cg.predictedPlayerState;
	speed = sqrt( ps->velocity[0] * ps->velocity[0]
		+ ps->velocity[1] * ps->velocity[1] );
	if ( speed <= 320.0f ) {
		return;		// only above run speed, where the multiplier exceeds 1.0
	}

	// same curve as the speedometer
	scale = 1.0f + ( speed / 320.0f - 1.0f ) * 0.5f;
	if ( scale > 2.0f ) {
		scale = 2.0f;
	}

	alpha = ( scale - 1.0f ) * 0.30f;	// 0 at 1.0x, ~0.30 at the 2.0x cap
	if ( alpha <= 0.0f ) {
		return;
	}

	base = ( scale < 1.7f ) ? nerv_amber : nerv_red;

	// three nested edge bars per side fake a soft inward glow
	for ( i = 0 ; i < 3 ; i++ ) {
		w = 36.0f - i * 11.0f;			// 36, 25, 14 wide
		a = alpha * ( 1.0f - i * 0.30f );
		col[0] = base[0];
		col[1] = base[1];
		col[2] = base[2];
		col[3] = a;
		CG_FillRect( 0, 0, w, 480, col );			// left
		CG_FillRect( 640 - w, 0, w, 480, col );		// right
	}
}

/*
==================
FLOW combo — style multiplier + run score

Movement is the game, so movement scores. A live multiplier climbs as you
chain tricks (bhops, wall jumps, double jumps, wall runs) and sustain speed,
and decays the moment you coast or stop. Style points pour in at speed × the
multiplier, so the fantasy is "stay in flow". Pure cgame: reads the already-
networked movement stats, no protocol change. Drives the juice too (shake,
fov punch, speed lines).
==================
*/
#define COMBO_MAX	10.0f

static void CG_ComboEvent( const char *name, float bump ) {
	cg.comboMult += bump;
	if ( cg.comboMult > COMBO_MAX ) {
		cg.comboMult = COMBO_MAX;
	}
	cg.comboEventTime = cg.time;
	if ( name && name[0] ) {
		Q_strncpyz( cg.trickName, name, sizeof( cg.trickName ) );
		cg.trickTime = cg.time;
	}
	if ( cg.viewShake < 0.45f ) {
		cg.viewShake = 0.45f;		// a tap of juice per trick
	}
}

static void CG_UpdateCombo( void ) {
	playerState_t	*ps = &cg.predictedPlayerState;
	float			speed, dt;
	int				streak, race;

	dt = cg.frametime * 0.001f;
	if ( dt <= 0.0f ) dt = 0.001f;
	if ( dt > 0.1f )  dt = 0.1f;		// clamp across hitches

	// juice always eases back out
	cg.viewShake -= dt * 6.0f;
	if ( cg.viewShake < 0.0f ) cg.viewShake = 0.0f;
	cg.fovPunch -= cg.fovPunch * dt * 6.0f;
	if ( cg.fovPunch < 0.05f ) cg.fovPunch = 0.0f;

	// a fresh run (leaving the start pad) zeroes the style score
	race = ps->stats[STAT_RACE_START];
	if ( race != cg.comboRaceSeen ) {
		if ( race && !cg.comboRaceSeen ) {
			cg.styleScore = 0.0f;
		}
		cg.comboRaceSeen = race;
	}

	// dead / spectating / intermission: drop the combo, keep the run score,
	// and resync the edge trackers so we don't false-trigger on respawn
	if ( ps->pm_type != PM_NORMAL || ps->stats[STAT_HEALTH] <= 0 ) {
		cg.comboMult = 1.0f;
		cg.comboMilestone = 1;
		cg.comboBhopSeen = ps->stats[STAT_BHOP_STREAK];
		cg.comboWalljumpSeen = ps->stats[STAT_WALLJUMP_COUNT];
		cg.comboAirjumpSeen = ps->stats[STAT_AIRJUMP_COUNT];
		cg.comboScoreSeen = ps->persistant[PERS_SCORE];
		return;
	}

	if ( cg.comboMult < 1.0f ) {
		cg.comboMult = 1.0f;
	}

	speed = sqrt( ps->velocity[0] * ps->velocity[0]
		+ ps->velocity[1] * ps->velocity[1] );

	// --- trick edges feed the multiplier ---
	streak = ps->stats[STAT_BHOP_STREAK];
	if ( streak > cg.comboBhopSeen && streak >= 2 ) {
		CG_ComboEvent( va( "BHOP x%i", streak ), 0.08f );
	}
	cg.comboBhopSeen = streak;

	if ( ps->stats[STAT_WALLJUMP_COUNT] > cg.comboWalljumpSeen ) {
		CG_ComboEvent( "WALL JUMP", 0.25f );
	}
	cg.comboWalljumpSeen = ps->stats[STAT_WALLJUMP_COUNT];

	if ( ps->stats[STAT_AIRJUMP_COUNT] > cg.comboAirjumpSeen ) {
		CG_ComboEvent( "DOUBLE JUMP", 0.25f );
	}
	cg.comboAirjumpSeen = ps->stats[STAT_AIRJUMP_COUNT];

	if ( ps->stats[STAT_WALLRUN] != 0 ) {
		cg.comboMult += 0.6f * dt;
		if ( cg.comboMult > COMBO_MAX ) cg.comboMult = COMBO_MAX;
		cg.comboEventTime = cg.time;
		if ( cg.time - cg.trickTime > 500 ) {
			CG_ComboEvent( "WALL RUN", 0.0f );
		}
	}

	// frag: persistent score went up
	if ( ps->persistant[PERS_SCORE] > cg.comboScoreSeen ) {
		CG_ComboEvent( "FRAG", 0.75f );
		cg.styleScore += 250.0f * cg.comboMult;
		cg.fovPunch = 6.0f;
		cg.viewShake = 1.2f;
	}
	cg.comboScoreSeen = ps->persistant[PERS_SCORE];

	// sustained speed both feeds and keeps the combo alive
	if ( speed > 500.0f ) {
		cg.comboMult += 0.30f * dt;
		if ( cg.comboMult > COMBO_MAX ) cg.comboMult = COMBO_MAX;
		cg.comboEventTime = cg.time;
	}

	// style pours in at speed * multiplier while you're moving
	if ( speed > 200.0f ) {
		cg.styleScore += dt * ( speed * 0.02f ) * cg.comboMult;
	}
	if ( (int)cg.styleScore > cg.styleBest ) {
		cg.styleBest = (int)cg.styleScore;
	}
	if ( (int)cg.styleScore > cg.lifeStylePeak ) {
		cg.lifeStylePeak = (int)cg.styleScore;	// this run's style for the report
	}

	// announcer at each whole multiplier milestone
	if ( (int)cg.comboMult > cg.comboMilestone && (int)cg.comboMult >= 3 ) {
		cg.comboMilestone = (int)cg.comboMult;
		if ( cg.comboMilestone >= 7 ) {
			trap_S_StartLocalSound( cgs.media.holyShitSound, CHAN_ANNOUNCER );
		} else if ( cg.comboMilestone >= 5 ) {
			trap_S_StartLocalSound( cgs.media.excellentSound, CHAN_ANNOUNCER );
		} else {
			trap_S_StartLocalSound( cgs.media.impressiveSound, CHAN_ANNOUNCER );
		}
	}

	// decay: lose the multiplier when you stop feeding it, fast if you've
	// actually stopped on the ground
	if ( cg.time - cg.comboEventTime > 1000 ) {
		float rate = ( speed < 200.0f && ps->groundEntityNum != ENTITYNUM_NONE )
			? 3.0f : 1.2f;
		cg.comboMult -= rate * dt;
		if ( cg.comboMult <= 1.0f ) {
			cg.comboMult = 1.0f;
			cg.comboMilestone = 1;
		}
	}

	// landing punch (reuse the engine's landing detection)
	if ( cg.landTime != cg.comboLandTime ) {
		cg.comboLandTime = cg.landTime;
		if ( cg.landChange < -7.0f ) {
			float m = -cg.landChange / 16.0f;
			if ( m > 1.5f ) m = 1.5f;
			if ( m > cg.viewShake ) cg.viewShake = m;
			if ( m * 4.0f > cg.fovPunch ) cg.fovPunch = m * 4.0f;
		}
	}

	// hit shake when you take damage
	if ( cg.damageTime != cg.comboDamageSeen ) {
		cg.comboDamageSeen = cg.damageTime;
		if ( cg.viewShake < 1.0f ) cg.viewShake = 1.0f;
	}
}

static void CG_DrawCombo( void ) {
	char			str[32];
	int				w, age;
	float			*color;
	float			y = 56.0f;		// top-right stack, off the crosshair

	// live chain multiplier — cyan nominal, heating amber→orange→red as it climbs
	if ( cg.comboMult >= 1.05f ) {
		if      ( cg.comboMult < 1.6f ) color = nerv_cyan;
		else if ( cg.comboMult < 2.5f ) color = nerv_amber;
		else if ( cg.comboMult < 4.0f ) color = nerv_orange;
		else                            color = nerv_red;

		Com_sprintf( str, sizeof( str ), "X%.1f", cg.comboMult );
		w = CG_MatrixStringWidth( str, 2.2f );
		CG_DrawMatrixString( 632 - w, y, str, 2.2f, color );
		y += 22.0f;
	}

	// run style score — cyan readout under the multiplier
	if ( cg.styleScore > 0.0f ) {
		Com_sprintf( str, sizeof( str ), "STYLE %i", (int)cg.styleScore );
		w = CG_MatrixStringWidth( str, 1.4f );
		CG_DrawMatrixString( 632 - w, y, str, 1.4f, nerv_cyan );
		y += 18.0f;
	}

	// trick callout — momentary, flashing green, under the group
	age = cg.time - cg.trickTime;
	if ( age >= 0 && age < 700 && cg.trickName[0] ) {
		vec4_t	tc;

		Vector4Copy( nerv_green, tc );
		tc[3] = 1.0f - age / 700.0f;
		w = CG_MatrixStringWidth( cg.trickName, 1.2f );
		CG_DrawMatrixString( 632 - w, y, cg.trickName, 1.2f, tc );
	}
}

/*
==================
CG_DrawSpeedMeter

Speed readout under the crosshair: ups, the damage multiplier the
server will apply (mirrors the g_speedDamage curve), and hop streak
==================
*/
static void CG_DrawSpeedMeter( void ) {
	char			str[64];
	char			pkbuf[24];
	float			speed, scale;
	int				streak, x, w;
	float			*color;
	playerState_t	*ps;

	if ( !cg_drawSpeed.integer ) {
		return;
	}

	ps = &cg.predictedPlayerState;
	speed = sqrt( ps->velocity[0] * ps->velocity[0]
		+ ps->velocity[1] * ps->velocity[1] );

	// track this life's top speed — the number a speedrunner chases
	if ( (int)speed > cg.peakSpeed ) {
		cg.peakSpeed = (int)speed;
	}

	// session top-speed milestones: every 100 ups from 500 up,
	// with an escalating announcer
	if ( speed >= 500 ) {
		int		tier;

		tier = (int)( speed / 100 );
		if ( tier > cg.speedTier ) {
			cg.speedTier = tier;
			CG_CenterPrint( va( "%i UPS!", tier * 100 ), 120, BIGCHAR_WIDTH );
			if ( tier >= 10 ) {
				trap_S_StartLocalSound( cgs.media.holyShitSound, CHAN_ANNOUNCER );
			} else if ( tier >= 7 ) {
				trap_S_StartLocalSound( cgs.media.excellentSound, CHAN_ANNOUNCER );
			} else {
				trap_S_StartLocalSound( cgs.media.impressiveSound, CHAN_ANNOUNCER );
			}
		}
	}

	// mirror the server's g_speedDamage curve
	scale = speed / 320.0f;
	if ( scale < 1.0f ) {
		scale = 0.85f + 0.15f * scale;
	} else {
		scale = 1.0f + ( scale - 1.0f ) * 0.5f;
		if ( scale > 2.0f ) {
			scale = 2.0f;
		}
	}

	// cyan at nominal, escalating amber→orange→red as the speed-damage multiplier
	// climbs ("move or die" — warm = the hot zone)
	if ( scale < 0.999f ) {
		color = nerv_dim;
	} else if ( scale < 1.3f ) {
		color = nerv_cyan;
	} else if ( scale < 1.6f ) {
		color = nerv_amber;
	} else if ( scale < 1.9f ) {
		color = nerv_orange;
	} else {
		color = nerv_red;
	}

	streak = ps->stats[STAT_BHOP_STREAK];
	if ( streak > cg.peakStreak ) {
		cg.peakStreak = streak;		// longest chain this run
	}

	// minimal: just the speed, fading in as you move and brightening with the
	// damage multiplier — no labels, no panel. Peak/record live on the finish
	// screen, not in your face during the run.
	{
		float	a = CG_Flow();
		vec4_t	col2;

		if ( a <= 0.02f ) {
			return;					// at rest the readout vanishes
		}
		Com_sprintf( str, sizeof( str ), "%i", (int)speed );
		Vector4Copy( color, col2 );
		col2[3] = 0.30f + 0.70f * a;
		w = CG_MatrixStringWidth( str, 2.4f );
		CG_DrawMatrixString( 320 - w / 2, 412, str, 2.4f, col2 );
	}
}

/*
=================
CG_Draw2D
=================
*/
/*
==================
CG_DrawRaceTimer

Live run clock while STAT_RACE_START is stamped, with the ghost's best
underneath as the pace to beat.
==================
*/
static void CG_DrawRaceTimer( void ) {
	char			str[64];
	const char		*lbl = "- ELAPSED -";
	int				stat, ms, best, x, w;

	stat = cg.snap->ps.stats[STAT_RACE_START];
	if ( !stat ) {
		return;
	}

	ms = cg.time - ( cgs.levelStartTime + ( stat - 1 ) * 100 );
	if ( ms < 0 ) {
		ms = 0;
	}
	Com_sprintf( str, sizeof( str ), "%i:%02i.%i",
		ms / 60000, ( ms / 1000 ) % 60, ( ms % 1000 ) / 100 );
	w = CG_MatrixStringWidth( str, 3.0f );
	x = 320 - w / 2;
	CG_NervPanel( x - 14, 24, w + 28, 7 * 3.0f + 22, nerv_cyan );
	CG_DrawMatrixString( 320 - CG_MatrixStringWidth( lbl, 1.4f ) / 2, 28,
		lbl, 1.4f, nerv_cyan );
	CG_DrawMatrixString( x, 40, str, 3.0f, nerv_cyan );

	best = CG_GhostBestMs();
	if ( best ) {
		Com_sprintf( str, sizeof( str ), "TARGET %i:%02i.%03i",
			best / 60000, ( best / 1000 ) % 60, best % 1000 );
		x = 320 - CG_MatrixStringWidth( str, 1.4f ) / 2;
		CG_DrawMatrixString( x, 24 + 7 * 3 + 24, str, 1.4f, nerv_green );
	}
}

/*
==================
CG_DrawVoidWarning

Distance readout to the rising void, going red and screaming when it
gets close.
==================
*/
static void CG_DrawVoidWarning( void ) {
	char			str[64];
	float			dist;
	int				x, w;
	float			*color;

	if ( !cgs.voidActive ) {
		return;
	}

	dist = cg.predictedPlayerState.origin[2] - CG_VoidZ();
	if ( dist > 1200 ) {
		return;		// far enough that the void is someone else's problem
	}

	if ( dist > 600 ) {
		color = nerv_dim;
	} else if ( dist > 300 ) {
		color = nerv_amber;
	} else {
		// alert klaxon flash
		color = ( cg.time / 200 ) & 1 ? nerv_red : nerv_amber;
	}

	Com_sprintf( str, sizeof( str ), "VOID COLLAPSE  %im", (int)dist );
	w = CG_MatrixStringWidth( str, 1.8f );
	x = 320 - w / 2;
	CG_NervPanel( x - 10, 396, w + 20, 7 * 1.8f + 8, color );
	CG_DrawMatrixString( x, 400, str, 1.8f, color );
}

/*
==================
CG_DrawVitals

The clean NERV bottom HUD: INTEGRITY / ARMOR / AMMO as TrueType readouts in
bracket panels, each with a tinted flat icon (pulled from game-icons, CC-BY)
and a segmented capacity bar. Replaces the vanilla pre-baked number/icon
status bar.
==================
*/
static qhandle_t	cg_icoHealth, cg_icoArmor, cg_icoAmmo, cg_icoReticle;
static qboolean		cg_icoReg = qfalse;

static void CG_HudIcons( void ) {
	if ( cg_icoReg ) {
		return;
	}
	cg_icoHealth  = trap_R_RegisterShaderNoMip( "strafe64/hud/health" );
	cg_icoArmor   = trap_R_RegisterShaderNoMip( "strafe64/hud/armor" );
	cg_icoAmmo    = trap_R_RegisterShaderNoMip( "strafe64/hud/ammo" );
	cg_icoReticle = trap_R_RegisterShaderNoMip( "strafe64/hud/reticle" );
	cg_icoReg = qtrue;
}

static void CG_DrawVitals( void ) {
	playerState_t	*ps;
	int				hp, ar, ammo, wp, maxhp, w;
	float			*hc, *amc;
	char			num[24];

	CG_HudIcons();

	ps = &cg.snap->ps;
	hp = ps->stats[STAT_HEALTH];
	ar = ps->stats[STAT_ARMOR];
	maxhp = ps->stats[STAT_MAX_HEALTH];
	if ( maxhp <= 0 ) {
		maxhp = 100;
	}
	wp = ps->weapon;
	ammo = ps->ammo[wp];

	// health — cyan nominal, heating to amber→red as it drops (alert = warm)
	if ( hp > maxhp / 2 ) {
		hc = nerv_cyan;
	} else if ( hp > maxhp / 4 ) {
		hc = nerv_amber;
	} else {
		hc = ( ( cg.time / 200 ) & 1 ) ? nerv_red : nerv_orange;
	}
	trap_R_SetColor( hc );
	CG_DrawPic( 24, 451, 15, 15, cg_icoHealth );
	trap_R_SetColor( NULL );
	Com_sprintf( num, sizeof( num ), "%i", hp );
	CG_DrawMatrixString( 44, 452, num, 2.0f, hc );

	// armor — cyan nominal, only when you actually have some
	if ( ar > 0 ) {
		trap_R_SetColor( nerv_cyan );
		CG_DrawPic( 104, 451, 15, 15, cg_icoArmor );
		trap_R_SetColor( NULL );
		Com_sprintf( num, sizeof( num ), "%i", ar );
		CG_DrawMatrixString( 124, 452, num, 2.0f, nerv_cyan );
	}

	// ammo — cyan nominal, flashing red when low (alert). finite weapons only
	// (grapple/vectorgun carry no counter, so the corner stays clean)
	if ( ammo >= 0 ) {
		amc = ( ammo <= 5 && ( ( cg.time / 200 ) & 1 ) ) ? nerv_red : nerv_cyan;
		Com_sprintf( num, sizeof( num ), "%i", ammo );
		w = CG_MatrixStringWidth( num, 2.0f );
		CG_DrawMatrixString( 616 - w, 452, num, 2.0f, amc );
		trap_R_SetColor( amc );
		CG_DrawPic( 616 - w - 21, 451, 15, 15, cg_icoAmmo );
		trap_R_SetColor( NULL );
	}
}

/*
==================
CG_DrawMissionReport

STRAFE 64 run scorecard. Death ends a run, so while dead we replace the frag
board with a NERV-terminal readout of how the run went: top speed (the
daily-speedrun currency), style banked, the map's standing record, and a rank
graded on this run's peak speed. This is the "one more run" payoff — the
number you want to beat next attempt. Speed is still valid here; CG_Respawn
clears peakSpeed only on the spawn-count change.
==================
*/
/*
==================
CG_DrawMutator

Top-centre readout of the active run mutator (cgs.mutator, from serverinfo) so
the twist is always legible. Minimal: just the name in the matrix font, tinted
by feel — green floaty, amber fast, red heavy.
==================
*/
static void CG_DrawMutator( void ) {
	const char	*name;
	const float	*col;
	int			w;

	switch ( cgs.mutator ) {
	case 1:  name = "LOW GRAVITY"; col = nerv_green; break;
	case 2:  name = "RUSH";        col = nerv_amber; break;
	case 3:  name = "HEAVY";       col = nerv_red;   break;
	case 4:  name = "VECTORGUN";   col = nerv_amber; break;
	default: return;
	}
	w = CG_MatrixStringWidth( name, 1.1f );
	CG_DrawMatrixString( 320 - w / 2, 30, name, 1.1f, col );
}

/*
==================
CG_DrawLatticeBracket

Persistent top-left readout of the LATTICE bracket state (round + this-heat /
advanced / still-queued counts), from CS_LATTICEHEAT. The server centerprints the
transitions ("NEXT HEAT" / "FINAL"); this is the always-on tournament scoreboard.
==================
*/
static void CG_DrawLatticeBracket( void ) {
	char	line[64];
	vec4_t	col;
	int		w;

	if ( !cgs.lattice || cgs.latticeRound <= 0 ) {
		return;		// not a bracket match (single heat or mode off)
	}
	// top-RIGHT (the top-left column is the obituary notify feed). NB: the LED
	// matrix font (CG_DrawMatrixString) renders nothing in this build, so this
	// uses the stock big/small string draws. The final = one heat, nobody queued
	// and nobody yet advanced this round.
	if ( cgs.latticeLeft == 0 && cgs.latticeAdv == 0 ) {
		Q_strncpyz( line, "FINAL", sizeof( line ) );
		Vector4Copy( nerv_amber, col );
	} else {
		Com_sprintf( line, sizeof( line ), "ROUND %i", cgs.latticeRound );
		Vector4Copy( nerv_green, col );
	}
	w = CG_DrawStrlen( line ) * BIGCHAR_WIDTH;
	CG_DrawBigStringColor( 632 - w, 6, line, col );

	Com_sprintf( line, sizeof( line ), "HEAT %i  ADV %i  LEFT %i",
		cgs.latticeHeatSize, cgs.latticeAdv, cgs.latticeLeft );
	w = CG_DrawStrlen( line ) * SMALLCHAR_WIDTH;
	Vector4Copy( nerv_amber, col );
	CG_DrawSmallStringColor( 632 - w, 26, line, col );
}

// mirrors g_shop[] in g_cmds.c — kept in sync by hand (cgame has no game header)
static const struct { const char *name; int cost; int weapon; } cg_shopItems[] = {
	{ "ROCKET", 400,  WP_ROCKET_LAUNCHER },
	{ "RAIL",   700,  WP_RAILGUN },
	{ "BFG",   1200,  WP_BFG },
	{ "ARMOR",  300,  WP_NONE },
	{ "HEAL",   200,  WP_NONE },
	{ "AIRJUMP",800,  WP_NONE },
};
#define CG_SHOP_COUNT ((int)(ARRAY_LEN(cg_shopItems)))

static void CG_DrawMissionReport( void ) {
	float		cy;
	int			i, w, record, peak, style, score, wpns, bestlap;
	const char	*rank;
	vec4_t		accent;
	vec4_t		plate = { 0.02f, 0.03f, 0.04f, 0.78f };
	char		line[64];

	peak   = cg.peakSpeed;
	style  = cg.styleBest;
	record = cg.mapSpeedRecord.integer;
	score  = cg.snap->ps.persistant[PERS_SCORE];
	wpns   = cg.snap->ps.stats[STAT_WEAPONS];

	// rank graded against this map's PAR (best-bot calibration, cgp_<map>) when set,
	// so a tight map's S-rank is earned at a lower speed than a fast one's; falls
	// back to absolute thresholds when uncalibrated (par 0). S = beat par.
	{
		int par = cg.mapPar.integer;
		int sThr = par > 0 ? par              : 900;
		int aThr = par > 0 ? (int)(par*0.85f) : 700;
		int bThr = par > 0 ? (int)(par*0.70f) : 500;
		if      ( peak >= sThr ) { rank = "S RANK"; Vector4Copy( nerv_green,  accent ); }
		else if ( peak >= aThr ) { rank = "A RANK"; Vector4Copy( nerv_amber,  accent ); }
		else if ( peak >= bThr ) { rank = "B RANK"; Vector4Copy( nerv_orange, accent ); }
		else                     { rank = "C RANK"; Vector4Copy( nerv_dim,    accent ); }
	}

	// backing plate + accent rule (the targeting-frame look)
	CG_FillRect( 320 - 152, 104, 304, 324, plate );
	CG_FillRect( 320 - 152, 104, 304, 2, accent );
	CG_FillRect( 320 - 152, 426, 304, 2, accent );

	cy = 120;
	w = CG_MatrixStringWidth( "MISSION REPORT", 1.5f );
	CG_DrawMatrixString( 320 - w/2, cy, "MISSION REPORT", 1.5f, nerv_amber );
	cy += 34;

	Com_sprintf( line, sizeof(line), "TOP SPEED  %i UPS", peak );
	w = CG_MatrixStringWidth( line, 1.4f );
	CG_DrawMatrixString( 320 - w/2, cy, line, 1.4f, nerv_amber );
	cy += 20;

	Com_sprintf( line, sizeof(line), "STYLE  %i  (PB %i)", cg.lifeStylePeak, style );
	w = CG_MatrixStringWidth( line, 1.4f );
	CG_DrawMatrixString( 320 - w/2, cy, line, 1.4f, nerv_amber );
	cy += 20;

	Com_sprintf( line, sizeof(line), "MAP BEST   %i UPS", record );
	w = CG_MatrixStringWidth( line, 1.4f );
	CG_DrawMatrixString( 320 - w/2, cy, line, 1.4f, nerv_dim );
	cy += 20;

	// PAR (best-bot calibration) — the rank target; only shown once calibrated
	if ( cg.mapPar.integer > 0 ) {
		Com_sprintf( line, sizeof(line), "PAR        %i UPS", cg.mapPar.integer );
		w = CG_MatrixStringWidth( line, 1.4f );
		CG_DrawMatrixString( 320 - w/2, cy, line, 1.4f,
			peak >= cg.mapPar.integer ? nerv_green : nerv_dim );
		cy += 20;
	}

	// best lap time this map (the speedrun currency), from the saved ghost
	bestlap = CG_GhostBestMs();
	if ( bestlap > 0 ) {
		Com_sprintf( line, sizeof(line), "BEST LAP   %i:%02i.%03i",
			bestlap / 60000, ( bestlap / 1000 ) % 60, bestlap % 1000 );
	} else {
		Com_sprintf( line, sizeof(line), "BEST LAP   --:--" );
	}
	w = CG_MatrixStringWidth( line, 1.4f );
	CG_DrawMatrixString( 320 - w/2, cy, line, 1.4f,
		bestlap > 0 ? nerv_green : nerv_dim );
	cy += 30;

	w = CG_MatrixStringWidth( rank, 2.2f );
	CG_DrawMatrixString( 320 - w/2, cy, rank, 2.2f, accent );
	cy += 34;

	// --- loadout shop: spend the banked score before you respawn ---------
	Com_sprintf( line, sizeof(line), "SCORE  %i", score );
	w = CG_MatrixStringWidth( line, 1.5f );
	CG_DrawMatrixString( 320 - w/2, cy, line, 1.5f, nerv_green );
	cy += 22;
	for ( i = 0; i < CG_SHOP_COUNT; i++ ) {
		qboolean	owned  = ( cg_shopItems[i].weapon != WP_NONE
						&& ( wpns & ( 1 << cg_shopItems[i].weapon ) ) );
		qboolean	afford = ( score >= cg_shopItems[i].cost );
		const float	*col   = owned ? nerv_amber : ( afford ? nerv_green : nerv_dim );
		if ( owned ) {
			Com_sprintf( line, sizeof(line), "%-7s OWNED", cg_shopItems[i].name );
		} else {
			Com_sprintf( line, sizeof(line), "%-7s %5i", cg_shopItems[i].name,
				cg_shopItems[i].cost );
		}
		CG_DrawMatrixString( 320 - 64, cy, line, 1.2f, col );
		cy += 16;
	}
	cy += 6;
	w = CG_MatrixStringWidth( "type buy <item>  -  FIRE TO RUN AGAIN", 1.0f );
	CG_DrawMatrixString( 320 - w/2, cy, "type buy <item>  -  FIRE TO RUN AGAIN",
		1.0f, nerv_dim );
}

static void CG_Draw2D(stereoFrame_t stereoFrame)
{
#ifdef MISSIONPACK
	if (cgs.orderPending && cg.time > cgs.orderTime) {
		CG_CheckOrderPending();
	}
#endif
	// if we are taking a levelshot for the menu, don't draw anything
	if ( cg.levelShot ) {
		return;
	}

	// FLOW combo: advance the multiplier, run score and juice every frame
	CG_UpdateCombo();

	if ( cg_draw2D.integer == 0 ) {
		return;
	}

	if ( cg.snap->ps.pm_type == PM_INTERMISSION ) {
		CG_DrawIntermission();
		return;
	}

/*
	if (cg.cameraMode) {
		return;
	}
*/
	if ( cg.snap->ps.persistant[PERS_TEAM] == TEAM_SPECTATOR ) {
		CG_DrawSpectator();

		if(stereoFrame == STEREO_CENTER)
			CG_DrawCrosshair();

		CG_DrawCrosshairNames();
		CG_DrawLatticeBracket();	// benched pilots watch the bracket too
	} else {
		// don't draw any status if dead or the scoreboard is being explicitly shown
		if ( !cg.showScores && cg.snap->ps.stats[STAT_HEALTH] > 0 ) {

			// the world drains toward gray when momentum is lost, then
			// corrupts as the void closes or you take a hit — both drawn
			// over the scene, under the HUD
			CG_DrawFlowColor();
			CG_DrawStillness();
			CG_DrawGlitch();
			CG_DrawSpeedLines();
			CG_DrawStrafeMeter();
			CG_DrawFragFlash();

#ifdef MISSIONPACK
			if ( cg_drawStatus.integer ) {
				Menu_PaintAll();
				CG_DrawTimedMenus();
			}
#else
			CG_DrawVitals();
#endif
      
			CG_DrawAmmoWarning();

#ifdef MISSIONPACK
			CG_DrawProxWarning();
#endif      
			if(stereoFrame == STEREO_CENTER)
				CG_DrawCrosshair();
			CG_DrawHitMarker();
			CG_DrawDamageDirection();
			CG_DrawCrosshairNames();
			CG_DrawWeaponSelect();

#ifndef MISSIONPACK
			CG_DrawHoldableItem();
#else
			//CG_DrawPersistantPowerup();
#endif
			CG_DrawReward();
			CG_DrawSpeedVignette();
			CG_DrawSpeedMeter();
			CG_DrawRaceTimer();
			CG_DrawVoidWarning();
			CG_DrawCombo();
			CG_DrawMutator();
			CG_DrawLatticeBracket();
		}
	}

	if ( cgs.gametype >= GT_TEAM ) {
#ifndef MISSIONPACK
		CG_DrawTeamInfo();
#endif
	}

	CG_DrawVote();
	CG_DrawTeamVote();

	CG_DrawLagometer();

#ifdef MISSIONPACK
	if (!cg_paused.integer) {
		CG_DrawUpperRight(stereoFrame);
	}
#else
	CG_DrawUpperRight(stereoFrame);
#endif

#ifndef MISSIONPACK
	CG_DrawLowerRight();
	CG_DrawLowerLeft();
#endif

	if ( !CG_DrawFollow() ) {
		CG_DrawWarmup();
	}

	// don't draw center string if scoreboard is up
	cg.scoreBoardShowing = CG_DrawScoreboard();
	if ( !cg.scoreBoardShowing) {
		CG_DrawCenterString();
	}

	// STRAFE 64: death ends the run — show the scorecard payoff on top of all
	if ( cg.snap->ps.persistant[PERS_TEAM] != TEAM_SPECTATOR
		&& cg.snap->ps.pm_type != PM_INTERMISSION
		&& cg.snap->ps.stats[STAT_HEALTH] <= 0 ) {
		CG_DrawMissionReport();
	}
}


/*
=====================
CG_DrawActive

Perform all drawing needed to completely fill the screen
=====================
*/
void CG_DrawActive( stereoFrame_t stereoView ) {
	// optionally draw the info screen instead
	if ( !cg.snap ) {
		CG_DrawInformation();
		return;
	}

	// optionally draw the tournement scoreboard instead
	if ( cg.snap->ps.persistant[PERS_TEAM] == TEAM_SPECTATOR &&
		( cg.snap->ps.pm_flags & PMF_SCOREBOARD ) ) {
		CG_DrawTourneyScoreboard();
		return;
	}

	// clear around the rendered view if sized down
	CG_TileClear();

	if(stereoView != STEREO_CENTER)
		CG_DrawCrosshair3D();

	// draw 3D view
	trap_R_RenderScene( &cg.refdef );

	// draw status bar and other floating elements
 	CG_Draw2D(stereoView);
}



