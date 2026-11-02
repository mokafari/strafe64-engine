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
// cg_players.c -- handle the media and animation for player entities
#include "cg_local.h"

char	*cg_customSoundNames[MAX_CUSTOM_SOUNDS] = {
	"*death1.wav",
	"*death2.wav",
	"*death3.wav",
	"*jump1.wav",
	"*pain25_1.wav",
	"*pain50_1.wav",
	"*pain75_1.wav",
	"*pain100_1.wav",
	"*falling1.wav",
	"*gasp.wav",
	"*drown.wav",
	"*fall1.wav",
	"*taunt.wav"
};


/*
================
CG_CustomSound

================
*/
sfxHandle_t	CG_CustomSound( int clientNum, const char *soundName ) {
	clientInfo_t *ci;
	int			i;

	if ( soundName[0] != '*' ) {
		return trap_S_RegisterSound( soundName, qfalse );
	}

	if ( clientNum < 0 || clientNum >= MAX_CLIENTS ) {
		clientNum = 0;
	}
	ci = &cgs.clientinfo[ clientNum ];

	for ( i = 0 ; i < MAX_CUSTOM_SOUNDS && cg_customSoundNames[i] ; i++ ) {
		if ( !strcmp( soundName, cg_customSoundNames[i] ) ) {
			return ci->sounds[i];
		}
	}

	CG_Error( "Unknown custom sound: %s", soundName );
	return 0;
}



/*
=============================================================================

CLIENT INFO

=============================================================================
*/

/*
======================
CG_ParseAnimationFile

Read a configuration file containing animation counts and rates
models/players/visor/animation.cfg, etc
======================
*/
static qboolean	CG_ParseAnimationFile( const char *filename, clientInfo_t *ci ) {
	char		*text_p, *prev;
	int			len;
	int			i;
	char		*token;
	float		fps;
	int			skip;
	char		text[20000];
	fileHandle_t	f;
	animation_t *animations;

	animations = ci->animations;

	// load the file
	len = trap_FS_FOpenFile( filename, &f, FS_READ );
	if ( len <= 0 ) {
		return qfalse;
	}
	if ( len >= sizeof( text ) - 1 ) {
		CG_Printf( "File %s too long\n", filename );
		trap_FS_FCloseFile( f );
		return qfalse;
	}
	trap_FS_Read( text, len, f );
	text[len] = 0;
	trap_FS_FCloseFile( f );

	// parse the text
	text_p = text;
	skip = 0;	// quite the compiler warning

	ci->footsteps = FOOTSTEP_NORMAL;
	VectorClear( ci->headOffset );
	ci->gender = GENDER_MALE;
	ci->fixedlegs = qfalse;
	ci->fixedtorso = qfalse;

	// read optional parameters
	while ( 1 ) {
		prev = text_p;	// so we can unget
		token = COM_Parse( &text_p );
		if ( !token[0] ) {
			break;
		}
		if ( !Q_stricmp( token, "footsteps" ) ) {
			token = COM_Parse( &text_p );
			if ( !token[0] ) {
				break;
			}
			if ( !Q_stricmp( token, "default" ) || !Q_stricmp( token, "normal" ) ) {
				ci->footsteps = FOOTSTEP_NORMAL;
			} else if ( !Q_stricmp( token, "boot" ) ) {
				ci->footsteps = FOOTSTEP_BOOT;
			} else if ( !Q_stricmp( token, "flesh" ) ) {
				ci->footsteps = FOOTSTEP_FLESH;
			} else if ( !Q_stricmp( token, "mech" ) ) {
				ci->footsteps = FOOTSTEP_MECH;
			} else if ( !Q_stricmp( token, "energy" ) ) {
				ci->footsteps = FOOTSTEP_ENERGY;
			} else {
				CG_Printf( "Bad footsteps parm in %s: %s\n", filename, token );
			}
			continue;
		} else if ( !Q_stricmp( token, "headoffset" ) ) {
			for ( i = 0 ; i < 3 ; i++ ) {
				token = COM_Parse( &text_p );
				if ( !token[0] ) {
					break;
				}
				ci->headOffset[i] = atof( token );
			}
			continue;
		} else if ( !Q_stricmp( token, "sex" ) ) {
			token = COM_Parse( &text_p );
			if ( !token[0] ) {
				break;
			}
			if ( token[0] == 'f' || token[0] == 'F' ) {
				ci->gender = GENDER_FEMALE;
			} else if ( token[0] == 'n' || token[0] == 'N' ) {
				ci->gender = GENDER_NEUTER;
			} else {
				ci->gender = GENDER_MALE;
			}
			continue;
		} else if ( !Q_stricmp( token, "fixedlegs" ) ) {
			ci->fixedlegs = qtrue;
			continue;
		} else if ( !Q_stricmp( token, "fixedtorso" ) ) {
			ci->fixedtorso = qtrue;
			continue;
		}

		// if it is a number, start parsing animations
		if ( token[0] >= '0' && token[0] <= '9' ) {
			text_p = prev;	// unget the token
			break;
		}
		Com_Printf( "unknown token '%s' in %s\n", token, filename );
	}

	// read information for each frame
	for ( i = 0 ; i < MAX_ANIMATIONS ; i++ ) {

		token = COM_Parse( &text_p );
		if ( !token[0] ) {
			if( i >= TORSO_GETFLAG && i <= TORSO_NEGATIVE ) {
				animations[i].firstFrame = animations[TORSO_GESTURE].firstFrame;
				animations[i].frameLerp = animations[TORSO_GESTURE].frameLerp;
				animations[i].initialLerp = animations[TORSO_GESTURE].initialLerp;
				animations[i].loopFrames = animations[TORSO_GESTURE].loopFrames;
				animations[i].numFrames = animations[TORSO_GESTURE].numFrames;
				animations[i].reversed = qfalse;
				animations[i].flipflop = qfalse;
				continue;
			}
			break;
		}
		animations[i].firstFrame = atoi( token );
		// leg only frames are adjusted to not count the upper body only frames
		if ( i == LEGS_WALKCR ) {
			skip = animations[LEGS_WALKCR].firstFrame - animations[TORSO_GESTURE].firstFrame;
		}
		if ( i >= LEGS_WALKCR && i<TORSO_GETFLAG) {
			animations[i].firstFrame -= skip;
		}

		token = COM_Parse( &text_p );
		if ( !token[0] ) {
			break;
		}
		animations[i].numFrames = atoi( token );

		animations[i].reversed = qfalse;
		animations[i].flipflop = qfalse;
		// if numFrames is negative the animation is reversed
		if (animations[i].numFrames < 0) {
			animations[i].numFrames = -animations[i].numFrames;
			animations[i].reversed = qtrue;
		}

		token = COM_Parse( &text_p );
		if ( !token[0] ) {
			break;
		}
		animations[i].loopFrames = atoi( token );

		token = COM_Parse( &text_p );
		if ( !token[0] ) {
			break;
		}
		fps = atof( token );
		if ( fps == 0 ) {
			fps = 1;
		}
		animations[i].frameLerp = 1000 / fps;
		animations[i].initialLerp = 1000 / fps;
	}

	if ( i != MAX_ANIMATIONS ) {
		CG_Printf( "Error parsing animation file: %s\n", filename );
		return qfalse;
	}

	// crouch backward animation
	memcpy(&animations[LEGS_BACKCR], &animations[LEGS_WALKCR], sizeof(animation_t));
	animations[LEGS_BACKCR].reversed = qtrue;
	// walk backward animation
	memcpy(&animations[LEGS_BACKWALK], &animations[LEGS_WALK], sizeof(animation_t));
	animations[LEGS_BACKWALK].reversed = qtrue;
	// flag moving fast
	animations[FLAG_RUN].firstFrame = 0;
	animations[FLAG_RUN].numFrames = 16;
	animations[FLAG_RUN].loopFrames = 16;
	animations[FLAG_RUN].frameLerp = 1000 / 15;
	animations[FLAG_RUN].initialLerp = 1000 / 15;
	animations[FLAG_RUN].reversed = qfalse;
	// flag not moving or moving slowly
	animations[FLAG_STAND].firstFrame = 16;
	animations[FLAG_STAND].numFrames = 5;
	animations[FLAG_STAND].loopFrames = 0;
	animations[FLAG_STAND].frameLerp = 1000 / 20;
	animations[FLAG_STAND].initialLerp = 1000 / 20;
	animations[FLAG_STAND].reversed = qfalse;
	// flag speeding up
	animations[FLAG_STAND2RUN].firstFrame = 16;
	animations[FLAG_STAND2RUN].numFrames = 5;
	animations[FLAG_STAND2RUN].loopFrames = 1;
	animations[FLAG_STAND2RUN].frameLerp = 1000 / 15;
	animations[FLAG_STAND2RUN].initialLerp = 1000 / 15;
	animations[FLAG_STAND2RUN].reversed = qtrue;
	//
	// new anims changes
	//
//	animations[TORSO_GETFLAG].flipflop = qtrue;
//	animations[TORSO_GUARDBASE].flipflop = qtrue;
//	animations[TORSO_PATROL].flipflop = qtrue;
//	animations[TORSO_AFFIRMATIVE].flipflop = qtrue;
//	animations[TORSO_NEGATIVE].flipflop = qtrue;
	//
	return qtrue;
}

/*
==========================
CG_FileExists
==========================
*/
static qboolean	CG_FileExists(const char *filename) {
	int len;

	len = trap_FS_FOpenFile( filename, NULL, FS_READ );
	if (len>0) {
		return qtrue;
	}
	return qfalse;
}

/*
==========================
CG_FindClientModelFile
==========================
*/
static qboolean	CG_FindClientModelFile( char *filename, int length, clientInfo_t *ci, const char *teamName, const char *modelName, const char *skinName, const char *base, const char *ext ) {
	char *team, *charactersFolder;
	int i;

	if ( cgs.gametype >= GT_TEAM ) {
		switch ( ci->team ) {
			case TEAM_BLUE: {
				team = "blue";
				break;
			}
			default: {
				team = "red";
				break;
			}
		}
	}
	else {
		team = "default";
	}
	charactersFolder = "";
	while(1) {
		for ( i = 0; i < 2; i++ ) {
			if ( i == 0 && teamName && *teamName ) {
				//								"models/players/characters/james/stroggs/lower_lily_red.skin"
				Com_sprintf( filename, length, "models/players/%s%s/%s%s_%s_%s.%s", charactersFolder, modelName, teamName, base, skinName, team, ext );
			}
			else {
				//								"models/players/characters/james/lower_lily_red.skin"
				Com_sprintf( filename, length, "models/players/%s%s/%s_%s_%s.%s", charactersFolder, modelName, base, skinName, team, ext );
			}
			if ( CG_FileExists( filename ) ) {
				return qtrue;
			}
			if ( cgs.gametype >= GT_TEAM ) {
				if ( i == 0 && teamName && *teamName ) {
					//								"models/players/characters/james/stroggs/lower_red.skin"
					Com_sprintf( filename, length, "models/players/%s%s/%s%s_%s.%s", charactersFolder, modelName, teamName, base, team, ext );
				}
				else {
					//								"models/players/characters/james/lower_red.skin"
					Com_sprintf( filename, length, "models/players/%s%s/%s_%s.%s", charactersFolder, modelName, base, team, ext );
				}
			}
			else {
				if ( i == 0 && teamName && *teamName ) {
					//								"models/players/characters/james/stroggs/lower_lily.skin"
					Com_sprintf( filename, length, "models/players/%s%s/%s%s_%s.%s", charactersFolder, modelName, teamName, base, skinName, ext );
				}
				else {
					//								"models/players/characters/james/lower_lily.skin"
					Com_sprintf( filename, length, "models/players/%s%s/%s_%s.%s", charactersFolder, modelName, base, skinName, ext );
				}
			}
			if ( CG_FileExists( filename ) ) {
				return qtrue;
			}
			if ( !teamName || !*teamName ) {
				break;
			}
		}
		// if tried the heads folder first
		if ( charactersFolder[0] ) {
			break;
		}
		charactersFolder = "characters/";
	}

	return qfalse;
}

/*
==========================
CG_FindClientHeadFile
==========================
*/
static qboolean	CG_FindClientHeadFile( char *filename, int length, clientInfo_t *ci, const char *teamName, const char *headModelName, const char *headSkinName, const char *base, const char *ext ) {
	char *team, *headsFolder;
	int i;

	if ( cgs.gametype >= GT_TEAM ) {
		switch ( ci->team ) {
			case TEAM_BLUE: {
				team = "blue";
				break;
			}
			default: {
				team = "red";
				break;
			}
		}
	}
	else {
		team = "default";
	}

	if ( headModelName[0] == '*' ) {
		headsFolder = "heads/";
		headModelName++;
	}
	else {
		headsFolder = "";
	}
	while(1) {
		for ( i = 0; i < 2; i++ ) {
			if ( i == 0 && teamName && *teamName ) {
				Com_sprintf( filename, length, "models/players/%s%s/%s/%s%s_%s.%s", headsFolder, headModelName, headSkinName, teamName, base, team, ext );
			}
			else {
				Com_sprintf( filename, length, "models/players/%s%s/%s/%s_%s.%s", headsFolder, headModelName, headSkinName, base, team, ext );
			}
			if ( CG_FileExists( filename ) ) {
				return qtrue;
			}
			if ( cgs.gametype >= GT_TEAM ) {
				if ( i == 0 &&  teamName && *teamName ) {
					Com_sprintf( filename, length, "models/players/%s%s/%s%s_%s.%s", headsFolder, headModelName, teamName, base, team, ext );
				}
				else {
					Com_sprintf( filename, length, "models/players/%s%s/%s_%s.%s", headsFolder, headModelName, base, team, ext );
				}
			}
			else {
				if ( i == 0 && teamName && *teamName ) {
					Com_sprintf( filename, length, "models/players/%s%s/%s%s_%s.%s", headsFolder, headModelName, teamName, base, headSkinName, ext );
				}
				else {
					Com_sprintf( filename, length, "models/players/%s%s/%s_%s.%s", headsFolder, headModelName, base, headSkinName, ext );
				}
			}
			if ( CG_FileExists( filename ) ) {
				return qtrue;
			}
			if ( !teamName || !*teamName ) {
				break;
			}
		}
		// if tried the heads folder first
		if ( headsFolder[0] ) {
			break;
		}
		headsFolder = "heads/";
	}

	return qfalse;
}

/*
==========================
CG_RegisterClientSkin
==========================
*/
static qboolean	CG_RegisterClientSkin( clientInfo_t *ci, const char *teamName, const char *modelName, const char *skinName, const char *headModelName, const char *headSkinName ) {
	char filename[MAX_QPATH];

	/*
	Com_sprintf( filename, sizeof( filename ), "models/players/%s/%slower_%s.skin", modelName, teamName, skinName );
	ci->legsSkin = trap_R_RegisterSkin( filename );
	if (!ci->legsSkin) {
		Com_sprintf( filename, sizeof( filename ), "models/players/characters/%s/%slower_%s.skin", modelName, teamName, skinName );
		ci->legsSkin = trap_R_RegisterSkin( filename );
		if (!ci->legsSkin) {
			Com_Printf( "Leg skin load failure: %s\n", filename );
		}
	}


	Com_sprintf( filename, sizeof( filename ), "models/players/%s/%supper_%s.skin", modelName, teamName, skinName );
	ci->torsoSkin = trap_R_RegisterSkin( filename );
	if (!ci->torsoSkin) {
		Com_sprintf( filename, sizeof( filename ), "models/players/characters/%s/%supper_%s.skin", modelName, teamName, skinName );
		ci->torsoSkin = trap_R_RegisterSkin( filename );
		if (!ci->torsoSkin) {
			Com_Printf( "Torso skin load failure: %s\n", filename );
		}
	}
	*/
	if ( CG_FindClientModelFile( filename, sizeof(filename), ci, teamName, modelName, skinName, "lower", "skin" ) ) {
		ci->legsSkin = trap_R_RegisterSkin( filename );
	}
	if (!ci->legsSkin) {
		Com_Printf( "Leg skin load failure: %s\n", filename );
	}

	if ( CG_FindClientModelFile( filename, sizeof(filename), ci, teamName, modelName, skinName, "upper", "skin" ) ) {
		ci->torsoSkin = trap_R_RegisterSkin( filename );
	}
	if (!ci->torsoSkin) {
		Com_Printf( "Torso skin load failure: %s\n", filename );
	}

	if ( CG_FindClientHeadFile( filename, sizeof(filename), ci, teamName, headModelName, headSkinName, "head", "skin" ) ) {
		ci->headSkin = trap_R_RegisterSkin( filename );
	}
	if (!ci->headSkin) {
		Com_Printf( "Head skin load failure: %s\n", filename );
	}

	// if any skins failed to load
	if ( !ci->legsSkin || !ci->torsoSkin || !ci->headSkin ) {
		return qfalse;
	}
	return qtrue;
}

/*
==========================
CG_RegisterClientModelname
==========================
*/
static qboolean CG_RegisterClientModelname( clientInfo_t *ci, const char *modelName, const char *skinName, const char *headModelName, const char *headSkinName, const char *teamName ) {
	char	filename[MAX_QPATH];
	const char		*headName;
	char newTeamName[MAX_QPATH];

	if ( headModelName[0] == '\0' ) {
		headName = modelName;
	}
	else {
		headName = headModelName;
	}
	Com_sprintf( filename, sizeof( filename ), "models/players/%s/lower.md3", modelName );
	ci->legsModel = trap_R_RegisterModel( filename );
	if ( !ci->legsModel ) {
		Com_sprintf( filename, sizeof( filename ), "models/players/characters/%s/lower.md3", modelName );
		ci->legsModel = trap_R_RegisterModel( filename );
		if ( !ci->legsModel ) {
			Com_Printf( "Failed to load model file %s\n", filename );
			return qfalse;
		}
	}

	Com_sprintf( filename, sizeof( filename ), "models/players/%s/upper.md3", modelName );
	ci->torsoModel = trap_R_RegisterModel( filename );
	if ( !ci->torsoModel ) {
		Com_sprintf( filename, sizeof( filename ), "models/players/characters/%s/upper.md3", modelName );
		ci->torsoModel = trap_R_RegisterModel( filename );
		if ( !ci->torsoModel ) {
			Com_Printf( "Failed to load model file %s\n", filename );
			return qfalse;
		}
	}

	if( headName[0] == '*' ) {
		Com_sprintf( filename, sizeof( filename ), "models/players/heads/%s/%s.md3", &headModelName[1], &headModelName[1] );
	}
	else {
		Com_sprintf( filename, sizeof( filename ), "models/players/%s/head.md3", headName );
	}
	ci->headModel = trap_R_RegisterModel( filename );
	// if the head model could not be found and we didn't load from the heads folder try to load from there
	if ( !ci->headModel && headName[0] != '*' ) {
		Com_sprintf( filename, sizeof( filename ), "models/players/heads/%s/%s.md3", headModelName, headModelName );
		ci->headModel = trap_R_RegisterModel( filename );
	}
	if ( !ci->headModel ) {
		Com_Printf( "Failed to load model file %s\n", filename );
		return qfalse;
	}

	// if any skins failed to load, return failure
	if ( !CG_RegisterClientSkin( ci, teamName, modelName, skinName, headName, headSkinName ) ) {
		if ( teamName && *teamName) {
			Com_Printf( "Failed to load skin file: %s : %s : %s, %s : %s\n", teamName, modelName, skinName, headName, headSkinName );
			if( ci->team == TEAM_BLUE ) {
				Com_sprintf(newTeamName, sizeof(newTeamName), "%s/", DEFAULT_BLUETEAM_NAME);
			}
			else {
				Com_sprintf(newTeamName, sizeof(newTeamName), "%s/", DEFAULT_REDTEAM_NAME);
			}
			if ( !CG_RegisterClientSkin( ci, newTeamName, modelName, skinName, headName, headSkinName ) ) {
				Com_Printf( "Failed to load skin file: %s : %s : %s, %s : %s\n", newTeamName, modelName, skinName, headName, headSkinName );
				return qfalse;
			}
		} else {
			Com_Printf( "Failed to load skin file: %s : %s, %s : %s\n", modelName, skinName, headName, headSkinName );
			return qfalse;
		}
	}

	// load the animations
	Com_sprintf( filename, sizeof( filename ), "models/players/%s/animation.cfg", modelName );
	if ( !CG_ParseAnimationFile( filename, ci ) ) {
		Com_sprintf( filename, sizeof( filename ), "models/players/characters/%s/animation.cfg", modelName );
		if ( !CG_ParseAnimationFile( filename, ci ) ) {
			Com_Printf( "Failed to load animation file %s\n", filename );
			return qfalse;
		}
	}

	if ( CG_FindClientHeadFile( filename, sizeof(filename), ci, teamName, headName, headSkinName, "icon", "skin" ) ) {
		ci->modelIcon = trap_R_RegisterShaderNoMip( filename );
	}
	else if ( CG_FindClientHeadFile( filename, sizeof(filename), ci, teamName, headName, headSkinName, "icon", "tga" ) ) {
		ci->modelIcon = trap_R_RegisterShaderNoMip( filename );
	}

	if ( !ci->modelIcon ) {
		return qfalse;
	}

	return qtrue;
}

/*
====================
CG_ColorFromString
====================
*/
static void CG_ColorFromString( const char *v, vec3_t color ) {
	int val;

	VectorClear( color );

	val = atoi( v );

	if ( val < 1 || val > 7 ) {
		VectorSet( color, 1, 1, 1 );
		return;
	}

	if ( val & 1 ) {
		color[2] = 1.0f;
	}
	if ( val & 2 ) {
		color[1] = 1.0f;
	}
	if ( val & 4 ) {
		color[0] = 1.0f;
	}
}

/*
===================
CG_LoadClientInfo

Load it now, taking the disk hits.
This will usually be deferred to a safe time
===================
*/
static void CG_LoadClientInfo( int clientNum, clientInfo_t *ci ) {
	const char	*dir, *fallback;
	int			i, modelloaded;
	const char	*s;
	char		teamname[MAX_QPATH];

	teamname[0] = 0;
#ifdef MISSIONPACK
	if( cgs.gametype >= GT_TEAM) {
		if( ci->team == TEAM_BLUE ) {
			Q_strncpyz(teamname, cg_blueTeamName.string, sizeof(teamname) );
		} else {
			Q_strncpyz(teamname, cg_redTeamName.string, sizeof(teamname) );
		}
	}
	if( teamname[0] ) {
		strcat( teamname, "/" );
	}
#endif
	modelloaded = qtrue;
	if ( !CG_RegisterClientModelname( ci, ci->modelName, ci->skinName, ci->headModelName, ci->headSkinName, teamname ) ) {
		if ( cg_buildScript.integer ) {
			CG_Error( "CG_RegisterClientModelname( %s, %s, %s, %s %s ) failed", ci->modelName, ci->skinName, ci->headModelName, ci->headSkinName, teamname );
		}

		// fall back to default team name
		if( cgs.gametype >= GT_TEAM) {
			// keep skin name
			if( ci->team == TEAM_BLUE ) {
				Q_strncpyz(teamname, DEFAULT_BLUETEAM_NAME, sizeof(teamname) );
			} else {
				Q_strncpyz(teamname, DEFAULT_REDTEAM_NAME, sizeof(teamname) );
			}
			if ( !CG_RegisterClientModelname( ci, DEFAULT_TEAM_MODEL, ci->skinName, DEFAULT_TEAM_HEAD, ci->skinName, teamname ) ) {
				CG_Error( "DEFAULT_TEAM_MODEL / skin (%s/%s) failed to register", DEFAULT_TEAM_MODEL, ci->skinName );
			}
		} else {
			if ( !CG_RegisterClientModelname( ci, DEFAULT_MODEL, "default", DEFAULT_MODEL, "default", teamname ) ) {
				CG_Error( "DEFAULT_MODEL (%s) failed to register", DEFAULT_MODEL );
			}
		}
		modelloaded = qfalse;
	}

	ci->newAnims = qfalse;
	if ( ci->torsoModel ) {
		orientation_t tag;
		// if the torso model has the "tag_flag"
		if ( trap_R_LerpTag( &tag, ci->torsoModel, 0, 0, 1, "tag_flag" ) ) {
			ci->newAnims = qtrue;
		}
	}

	// sounds
	dir = ci->modelName;
	fallback = (cgs.gametype >= GT_TEAM) ? DEFAULT_TEAM_MODEL : DEFAULT_MODEL;

	for ( i = 0 ; i < MAX_CUSTOM_SOUNDS ; i++ ) {
		s = cg_customSoundNames[i];
		if ( !s ) {
			break;
		}
		ci->sounds[i] = 0;
		// if the model didn't load use the sounds of the default model
		if (modelloaded) {
			ci->sounds[i] = trap_S_RegisterSound( va("sound/player/%s/%s", dir, s + 1), qfalse );
		}
		if ( !ci->sounds[i] ) {
			ci->sounds[i] = trap_S_RegisterSound( va("sound/player/%s/%s", fallback, s + 1), qfalse );
		}
	}

	ci->deferred = qfalse;

	// reset any existing players and bodies, because they might be in bad
	// frames for this new model
	for ( i = 0 ; i < MAX_GENTITIES ; i++ ) {
		if ( cg_entities[i].currentState.clientNum == clientNum
			&& cg_entities[i].currentState.eType == ET_PLAYER ) {
			CG_ResetPlayerEntity( &cg_entities[i] );
		}
	}
}

/*
======================
CG_CopyClientInfoModel
======================
*/
static void CG_CopyClientInfoModel( clientInfo_t *from, clientInfo_t *to ) {
	VectorCopy( from->headOffset, to->headOffset );
	to->footsteps = from->footsteps;
	to->gender = from->gender;

	to->legsModel = from->legsModel;
	to->legsSkin = from->legsSkin;
	to->torsoModel = from->torsoModel;
	to->torsoSkin = from->torsoSkin;
	to->headModel = from->headModel;
	to->headSkin = from->headSkin;
	to->modelIcon = from->modelIcon;

	to->newAnims = from->newAnims;

	memcpy( to->animations, from->animations, sizeof( to->animations ) );
	memcpy( to->sounds, from->sounds, sizeof( to->sounds ) );
}

/*
======================
CG_ScanForExistingClientInfo
======================
*/
static qboolean CG_ScanForExistingClientInfo( clientInfo_t *ci ) {
	int		i;
	clientInfo_t	*match;

	for ( i = 0 ; i < cgs.maxclients ; i++ ) {
		match = &cgs.clientinfo[ i ];
		if ( !match->infoValid ) {
			continue;
		}
		if ( match->deferred ) {
			continue;
		}
		if ( !Q_stricmp( ci->modelName, match->modelName )
			&& !Q_stricmp( ci->skinName, match->skinName )
			&& !Q_stricmp( ci->headModelName, match->headModelName )
			&& !Q_stricmp( ci->headSkinName, match->headSkinName ) 
			&& !Q_stricmp( ci->blueTeam, match->blueTeam ) 
			&& !Q_stricmp( ci->redTeam, match->redTeam )
			&& (cgs.gametype < GT_TEAM || ci->team == match->team) ) {
			// this clientinfo is identical, so use its handles

			ci->deferred = qfalse;

			CG_CopyClientInfoModel( match, ci );

			return qtrue;
		}
	}

	// nothing matches, so defer the load
	return qfalse;
}

/*
======================
CG_SetDeferredClientInfo

We aren't going to load it now, so grab some other
client's info to use until we have some spare time.
======================
*/
static void CG_SetDeferredClientInfo( int clientNum, clientInfo_t *ci ) {
	int		i;
	clientInfo_t	*match;

	// if someone else is already the same models and skins we
	// can just load the client info
	for ( i = 0 ; i < cgs.maxclients ; i++ ) {
		match = &cgs.clientinfo[ i ];
		if ( !match->infoValid || match->deferred ) {
			continue;
		}
		if ( Q_stricmp( ci->skinName, match->skinName ) ||
			 Q_stricmp( ci->modelName, match->modelName ) ||
//			 Q_stricmp( ci->headModelName, match->headModelName ) ||
//			 Q_stricmp( ci->headSkinName, match->headSkinName ) ||
			 (cgs.gametype >= GT_TEAM && ci->team != match->team) ) {
			continue;
		}
		// just load the real info cause it uses the same models and skins
		CG_LoadClientInfo( clientNum, ci );
		return;
	}

	// if we are in teamplay, only grab a model if the skin is correct
	if ( cgs.gametype >= GT_TEAM ) {
		for ( i = 0 ; i < cgs.maxclients ; i++ ) {
			match = &cgs.clientinfo[ i ];
			if ( !match->infoValid || match->deferred ) {
				continue;
			}
			if ( Q_stricmp( ci->skinName, match->skinName ) ||
				(cgs.gametype >= GT_TEAM && ci->team != match->team) ) {
				continue;
			}
			ci->deferred = qtrue;
			CG_CopyClientInfoModel( match, ci );
			return;
		}
		// load the full model, because we don't ever want to show
		// an improper team skin.  This will cause a hitch for the first
		// player, when the second enters.  Combat shouldn't be going on
		// yet, so it shouldn't matter
		CG_LoadClientInfo( clientNum, ci );
		return;
	}

	// find the first valid clientinfo and grab its stuff
	for ( i = 0 ; i < cgs.maxclients ; i++ ) {
		match = &cgs.clientinfo[ i ];
		if ( !match->infoValid ) {
			continue;
		}

		ci->deferred = qtrue;
		CG_CopyClientInfoModel( match, ci );
		return;
	}

	// we should never get here...
	CG_Printf( "CG_SetDeferredClientInfo: no valid clients!\n" );

	CG_LoadClientInfo( clientNum, ci );
}


/*
======================
CG_NewClientInfo
======================
*/
void CG_NewClientInfo( int clientNum ) {
	clientInfo_t *ci;
	clientInfo_t newInfo;
	const char	*configstring;
	const char	*v;
	char		*slash;

	if ( clientNum < 0 || clientNum >= MAX_CLIENTS ) {
		CG_Error( "CG_NewClientInfo: bad clientNum %i", clientNum );
		return;
	}

	ci = &cgs.clientinfo[clientNum];

	configstring = CG_ConfigString( clientNum + CS_PLAYERS );
	if ( !configstring[0] ) {
		memset( ci, 0, sizeof( *ci ) );
		return;		// player just left
	}

	// build into a temp buffer so the defer checks can use
	// the old value
	memset( &newInfo, 0, sizeof( newInfo ) );

	// isolate the player's name
	v = Info_ValueForKey(configstring, "n");
	Q_strncpyz( newInfo.name, v, sizeof( newInfo.name ) );

	// colors
	v = Info_ValueForKey( configstring, "c1" );
	CG_ColorFromString( v, newInfo.color1 );

	newInfo.c1RGBA[0] = 255 * newInfo.color1[0];
	newInfo.c1RGBA[1] = 255 * newInfo.color1[1];
	newInfo.c1RGBA[2] = 255 * newInfo.color1[2];
	newInfo.c1RGBA[3] = 255;

	v = Info_ValueForKey( configstring, "c2" );
	CG_ColorFromString( v, newInfo.color2 );

	newInfo.c2RGBA[0] = 255 * newInfo.color2[0];
	newInfo.c2RGBA[1] = 255 * newInfo.color2[1];
	newInfo.c2RGBA[2] = 255 * newInfo.color2[2];
	newInfo.c2RGBA[3] = 255;

	// bot skill
	v = Info_ValueForKey( configstring, "skill" );
	newInfo.botSkill = atoi( v );

	// handicap
	v = Info_ValueForKey( configstring, "hc" );
	newInfo.handicap = atoi( v );

	// wins
	v = Info_ValueForKey( configstring, "w" );
	newInfo.wins = atoi( v );

	// losses
	v = Info_ValueForKey( configstring, "l" );
	newInfo.losses = atoi( v );

	// team
	v = Info_ValueForKey( configstring, "t" );
	newInfo.team = atoi( v );

	// team task
	v = Info_ValueForKey( configstring, "tt" );
	newInfo.teamTask = atoi(v);

	// team leader
	v = Info_ValueForKey( configstring, "tl" );
	newInfo.teamLeader = atoi(v);

	v = Info_ValueForKey( configstring, "g_redteam" );
	Q_strncpyz(newInfo.redTeam, v, MAX_TEAMNAME);

	v = Info_ValueForKey( configstring, "g_blueteam" );
	Q_strncpyz(newInfo.blueTeam, v, MAX_TEAMNAME);

	// model
	v = Info_ValueForKey( configstring, "model" );
	if ( cg_forceModel.integer ) {
		// forcemodel makes everyone use a single model
		// to prevent load hitches
		char modelStr[MAX_QPATH];
		char *skin;

		if( cgs.gametype >= GT_TEAM ) {
			Q_strncpyz( newInfo.modelName, DEFAULT_TEAM_MODEL, sizeof( newInfo.modelName ) );
			Q_strncpyz( newInfo.skinName, "default", sizeof( newInfo.skinName ) );
		} else {
			trap_Cvar_VariableStringBuffer( "model", modelStr, sizeof( modelStr ) );
			if ( ( skin = strchr( modelStr, '/' ) ) == NULL) {
				skin = "default";
			} else {
				*skin++ = 0;
			}

			Q_strncpyz( newInfo.skinName, skin, sizeof( newInfo.skinName ) );
			Q_strncpyz( newInfo.modelName, modelStr, sizeof( newInfo.modelName ) );
		}

		if ( cgs.gametype >= GT_TEAM ) {
			// keep skin name
			slash = strchr( v, '/' );
			if ( slash ) {
				Q_strncpyz( newInfo.skinName, slash + 1, sizeof( newInfo.skinName ) );
			}
		}
	} else {
		Q_strncpyz( newInfo.modelName, v, sizeof( newInfo.modelName ) );

		slash = strchr( newInfo.modelName, '/' );
		if ( !slash ) {
			// modelName didn not include a skin name
			Q_strncpyz( newInfo.skinName, "default", sizeof( newInfo.skinName ) );
		} else {
			Q_strncpyz( newInfo.skinName, slash + 1, sizeof( newInfo.skinName ) );
			// truncate modelName
			*slash = 0;
		}
	}

	// head model
	v = Info_ValueForKey( configstring, "hmodel" );
	if ( cg_forceModel.integer ) {
		// forcemodel makes everyone use a single model
		// to prevent load hitches
		char modelStr[MAX_QPATH];
		char *skin;

		if( cgs.gametype >= GT_TEAM ) {
			Q_strncpyz( newInfo.headModelName, DEFAULT_TEAM_HEAD, sizeof( newInfo.headModelName ) );
			Q_strncpyz( newInfo.headSkinName, "default", sizeof( newInfo.headSkinName ) );
		} else {
			trap_Cvar_VariableStringBuffer( "headmodel", modelStr, sizeof( modelStr ) );
			if ( ( skin = strchr( modelStr, '/' ) ) == NULL) {
				skin = "default";
			} else {
				*skin++ = 0;
			}

			Q_strncpyz( newInfo.headSkinName, skin, sizeof( newInfo.headSkinName ) );
			Q_strncpyz( newInfo.headModelName, modelStr, sizeof( newInfo.headModelName ) );
		}

		if ( cgs.gametype >= GT_TEAM ) {
			// keep skin name
			slash = strchr( v, '/' );
			if ( slash ) {
				Q_strncpyz( newInfo.headSkinName, slash + 1, sizeof( newInfo.headSkinName ) );
			}
		}
	} else {
		Q_strncpyz( newInfo.headModelName, v, sizeof( newInfo.headModelName ) );

		slash = strchr( newInfo.headModelName, '/' );
		if ( !slash ) {
			// modelName didn not include a skin name
			Q_strncpyz( newInfo.headSkinName, "default", sizeof( newInfo.headSkinName ) );
		} else {
			Q_strncpyz( newInfo.headSkinName, slash + 1, sizeof( newInfo.headSkinName ) );
			// truncate modelName
			*slash = 0;
		}
	}

	// scan for an existing clientinfo that matches this modelname
	// so we can avoid loading checks if possible
	if ( !CG_ScanForExistingClientInfo( &newInfo ) ) {
		qboolean	forceDefer;

		forceDefer = trap_MemoryRemaining() < 4000000;

		// if we are defering loads, just have it pick the first valid
		if ( forceDefer || (cg_deferPlayers.integer && !cg_buildScript.integer && !cg.loading ) ) {
			// keep whatever they had if it won't violate team skins
			CG_SetDeferredClientInfo( clientNum, &newInfo );
			// if we are low on memory, leave them with this model
			if ( forceDefer ) {
				CG_Printf( "Memory is low. Using deferred model.\n" );
				newInfo.deferred = qfalse;
			}
		} else {
			CG_LoadClientInfo( clientNum, &newInfo );
		}
	}

	// replace whatever was there with the new one
	newInfo.infoValid = qtrue;
	*ci = newInfo;
}



/*
======================
CG_LoadDeferredPlayers

Called each frame when a player is dead
and the scoreboard is up
so deferred players can be loaded
======================
*/
void CG_LoadDeferredPlayers( void ) {
	int		i;
	clientInfo_t	*ci;

	// scan for a deferred player to load
	for ( i = 0, ci = cgs.clientinfo ; i < cgs.maxclients ; i++, ci++ ) {
		if ( ci->infoValid && ci->deferred ) {
			// if we are low on memory, leave it deferred
			if ( trap_MemoryRemaining() < 4000000 ) {
				CG_Printf( "Memory is low. Using deferred model.\n" );
				ci->deferred = qfalse;
				continue;
			}
			CG_LoadClientInfo( i, ci );
//			break;
		}
	}
}

/*
=============================================================================

PLAYER ANIMATION

=============================================================================
*/


/*
===============
CG_SetLerpFrameAnimation

may include ANIM_TOGGLEBIT
===============
*/
static void CG_SetLerpFrameAnimation( clientInfo_t *ci, lerpFrame_t *lf, int newAnimation ) {
	animation_t	*anim;

	lf->animationNumber = newAnimation;
	newAnimation &= ~ANIM_TOGGLEBIT;

	if ( newAnimation < 0 || newAnimation >= MAX_TOTALANIMATIONS ) {
		CG_Error( "Bad animation number: %i", newAnimation );
	}

	anim = &ci->animations[ newAnimation ];

	lf->animation = anim;
	lf->animationTime = lf->frameTime + anim->initialLerp;

	if ( cg_debugAnim.integer ) {
		CG_Printf( "Anim: %i\n", newAnimation );
	}
}

/*
===============
CG_RunLerpFrame

Sets cg.snap, cg.oldFrame, and cg.backlerp
cg.time should be between oldFrameTime and frameTime after exit
===============
*/
static void CG_RunLerpFrame( clientInfo_t *ci, lerpFrame_t *lf, int newAnimation, float speedScale ) {
	int			f, numFrames;
	animation_t	*anim;

	// debugging tool to get no animations
	if ( cg_animSpeed.integer == 0 ) {
		lf->oldFrame = lf->frame = lf->backlerp = 0;
		return;
	}

	// see if the animation sequence is switching
	if ( newAnimation != lf->animationNumber || !lf->animation ) {
		CG_SetLerpFrameAnimation( ci, lf, newAnimation );
	}

	// if we have passed the current frame, move it to
	// oldFrame and calculate a new frame
	if ( cg.time >= lf->frameTime ) {
		lf->oldFrame = lf->frame;
		lf->oldFrameTime = lf->frameTime;

		// get the next frame based on the animation
		anim = lf->animation;
		if ( !anim->frameLerp ) {
			return;		// shouldn't happen
		}
		if ( cg.time < lf->animationTime ) {
			lf->frameTime = lf->animationTime;		// initial lerp
		} else {
			lf->frameTime = lf->oldFrameTime + anim->frameLerp;
		}
		f = ( lf->frameTime - lf->animationTime ) / anim->frameLerp;
		f *= speedScale;		// adjust for haste, etc

		numFrames = anim->numFrames;
		if (anim->flipflop) {
			numFrames *= 2;
		}
		if ( f >= numFrames ) {
			f -= numFrames;
			if ( anim->loopFrames ) {
				f %= anim->loopFrames;
				f += anim->numFrames - anim->loopFrames;
			} else {
				f = numFrames - 1;
				// the animation is stuck at the end, so it
				// can immediately transition to another sequence
				lf->frameTime = cg.time;
			}
		}
		if ( anim->reversed ) {
			lf->frame = anim->firstFrame + anim->numFrames - 1 - f;
		}
		else if (anim->flipflop && f>=anim->numFrames) {
			lf->frame = anim->firstFrame + anim->numFrames - 1 - (f%anim->numFrames);
		}
		else {
			lf->frame = anim->firstFrame + f;
		}
		if ( cg.time > lf->frameTime ) {
			lf->frameTime = cg.time;
			if ( cg_debugAnim.integer ) {
				CG_Printf( "Clamp lf->frameTime\n");
			}
		}
	}

	if ( lf->frameTime > cg.time + 200 ) {
		lf->frameTime = cg.time;
	}

	if ( lf->oldFrameTime > cg.time ) {
		lf->oldFrameTime = cg.time;
	}
	// calculate current lerp value
	if ( lf->frameTime == lf->oldFrameTime ) {
		lf->backlerp = 0;
	} else {
		lf->backlerp = 1.0 - (float)( cg.time - lf->oldFrameTime ) / ( lf->frameTime - lf->oldFrameTime );
	}
}


/*
===============
CG_ClearLerpFrame
===============
*/
static void CG_ClearLerpFrame( clientInfo_t *ci, lerpFrame_t *lf, int animationNumber ) {
	lf->frameTime = lf->oldFrameTime = cg.time;
	CG_SetLerpFrameAnimation( ci, lf, animationNumber );
	lf->oldFrame = lf->frame = lf->animation->firstFrame;
}


/*
===============
CG_PlayerAnimation
===============
*/
static void CG_PlayerAnimation( centity_t *cent, int *legsOld, int *legs, float *legsBackLerp,
						int *torsoOld, int *torso, float *torsoBackLerp ) {
	clientInfo_t	*ci;
	int				clientNum;
	float			speedScale;

	clientNum = cent->currentState.clientNum;

	if ( cg_noPlayerAnims.integer ) {
		*legsOld = *legs = *torsoOld = *torso = 0;
		return;
	}

	if ( cent->currentState.powerups & ( 1 << PW_HASTE ) ) {
		speedScale = 1.5;
	} else {
		speedScale = 1;
	}

	ci = &cgs.clientinfo[ clientNum ];

	// STRAFE 64: freeze the legs into a static crouch while sliding. The server
	// keeps playing the crouch-WALK shuffle (PM_Footsteps has no slide anim), and
	// that shuffle is what makes a slide read as a crouch-walk. Holding the still
	// crouch-idle pose, plus the CG_SlidePose recline, sells the ground slide.
	if ( ( cent->currentState.eFlags & EF_SLIDING ) && cg_slidePose.integer ) {
		CG_RunLerpFrame( ci, &cent->pe.legs, LEGS_IDLECR, speedScale );
	} else if ( cent->pe.legs.yawing && ( cent->currentState.legsAnim & ~ANIM_TOGGLEBIT ) == LEGS_IDLE ) {
		// do the shuffle turn frames locally
		CG_RunLerpFrame( ci, &cent->pe.legs, LEGS_TURN, speedScale );
	} else {
		CG_RunLerpFrame( ci, &cent->pe.legs, cent->currentState.legsAnim, speedScale );
	}

	*legsOld = cent->pe.legs.oldFrame;
	*legs = cent->pe.legs.frame;
	*legsBackLerp = cent->pe.legs.backlerp;

	CG_RunLerpFrame( ci, &cent->pe.torso, cent->currentState.torsoAnim, speedScale );

	*torsoOld = cent->pe.torso.oldFrame;
	*torso = cent->pe.torso.frame;
	*torsoBackLerp = cent->pe.torso.backlerp;
}

/*
=============================================================================

PLAYER ANGLES

=============================================================================
*/

/*
==================
CG_SwingAngles
==================
*/
static void CG_SwingAngles( float destination, float swingTolerance, float clampTolerance,
					float speed, float *angle, qboolean *swinging ) {
	float	swing;
	float	move;
	float	scale;

	if ( !*swinging ) {
		// see if a swing should be started
		swing = AngleSubtract( *angle, destination );
		if ( swing > swingTolerance || swing < -swingTolerance ) {
			*swinging = qtrue;
		}
	}

	if ( !*swinging ) {
		return;
	}
	
	// modify the speed depending on the delta
	// so it doesn't seem so linear
	swing = AngleSubtract( destination, *angle );
	scale = fabs( swing );
	if ( scale < swingTolerance * 0.5 ) {
		scale = 0.5;
	} else if ( scale < swingTolerance ) {
		scale = 1.0;
	} else {
		scale = 2.0;
	}

	// swing towards the destination angle
	if ( swing >= 0 ) {
		move = cg.frametime * scale * speed;
		if ( move >= swing ) {
			move = swing;
			*swinging = qfalse;
		}
		*angle = AngleMod( *angle + move );
	} else if ( swing < 0 ) {
		move = cg.frametime * scale * -speed;
		if ( move <= swing ) {
			move = swing;
			*swinging = qfalse;
		}
		*angle = AngleMod( *angle + move );
	}

	// clamp to no more than tolerance
	swing = AngleSubtract( destination, *angle );
	if ( swing > clampTolerance ) {
		*angle = AngleMod( destination - (clampTolerance - 1) );
	} else if ( swing < -clampTolerance ) {
		*angle = AngleMod( destination + (clampTolerance - 1) );
	}
}

/*
=================
CG_AddPainTwitch
=================
*/
static void CG_AddPainTwitch( centity_t *cent, vec3_t torsoAngles ) {
	int		t;
	float	f;

	t = cg.time - cent->pe.painTime;
	if ( t >= PAIN_TWITCH_TIME ) {
		return;
	}

	f = 1.0 - (float)t / PAIN_TWITCH_TIME;

	if ( cent->pe.painDirection ) {
		torsoAngles[ROLL] += 20 * f;
	} else {
		torsoAngles[ROLL] -= 20 * f;
	}
}


/*
===============
CG_PlayerAngles

Handles separate torso motion

  legs pivot based on direction of movement

  head always looks exactly at cent->lerpAngles

  if motion < 20 degrees, show in head only
  if < 45 degrees, also show in torso
===============
*/
static void CG_PlayerAngles( centity_t *cent, vec3_t legs[3], vec3_t torso[3], vec3_t head[3] ) {
	vec3_t		legsAngles, torsoAngles, headAngles;
	float		dest;
	static	int	movementOffsets[8] = { 0, 22, 45, -22, 0, 22, -45, -22 };
	vec3_t		velocity;
	float		speed;
	int			dir, clientNum;
	clientInfo_t	*ci;

	VectorCopy( cent->lerpAngles, headAngles );
	headAngles[YAW] = AngleMod( headAngles[YAW] );
	VectorClear( legsAngles );
	VectorClear( torsoAngles );

	// --------- yaw -------------

	// allow yaw to drift a bit
	if ( ( cent->currentState.legsAnim & ~ANIM_TOGGLEBIT ) != LEGS_IDLE 
		|| ((cent->currentState.torsoAnim & ~ANIM_TOGGLEBIT) != TORSO_STAND 
		&& (cent->currentState.torsoAnim & ~ANIM_TOGGLEBIT) != TORSO_STAND2)) {
		// if not standing still, always point all in the same direction
		cent->pe.torso.yawing = qtrue;	// always center
		cent->pe.torso.pitching = qtrue;	// always center
		cent->pe.legs.yawing = qtrue;	// always center
	}

	// adjust legs for movement dir
	if ( cent->currentState.eFlags & EF_DEAD ) {
		// don't let dead bodies twitch
		dir = 0;
	} else {
		dir = cent->currentState.angles2[YAW];
		if ( dir < 0 || dir > 7 ) {
			CG_Error( "Bad player movement angle" );
		}
	}
	legsAngles[YAW] = headAngles[YAW] + movementOffsets[ dir ];
	torsoAngles[YAW] = headAngles[YAW] + 0.25 * movementOffsets[ dir ];

	// torso
	CG_SwingAngles( torsoAngles[YAW], 25, 90, cg_swingSpeed.value, &cent->pe.torso.yawAngle, &cent->pe.torso.yawing );
	CG_SwingAngles( legsAngles[YAW], 40, 90, cg_swingSpeed.value, &cent->pe.legs.yawAngle, &cent->pe.legs.yawing );

	torsoAngles[YAW] = cent->pe.torso.yawAngle;
	legsAngles[YAW] = cent->pe.legs.yawAngle;


	// --------- pitch -------------

	// only show a fraction of the pitch angle in the torso
	if ( headAngles[PITCH] > 180 ) {
		dest = (-360 + headAngles[PITCH]) * 0.75f;
	} else {
		dest = headAngles[PITCH] * 0.75f;
	}
	CG_SwingAngles( dest, 15, 30, 0.1f, &cent->pe.torso.pitchAngle, &cent->pe.torso.pitching );
	torsoAngles[PITCH] = cent->pe.torso.pitchAngle;

	//
	clientNum = cent->currentState.clientNum;
	if ( clientNum >= 0 && clientNum < MAX_CLIENTS ) {
		ci = &cgs.clientinfo[ clientNum ];
		if ( ci->fixedtorso ) {
			torsoAngles[PITCH] = 0.0f;
		}
	}

	// --------- roll -------------


	// lean towards the direction of travel
	VectorCopy( cent->currentState.pos.trDelta, velocity );
	speed = VectorNormalize( velocity );
	if ( speed ) {
		vec3_t	axis[3];
		float	side;

		speed *= 0.05f;

		AnglesToAxis( legsAngles, axis );
		side = speed * DotProduct( velocity, axis[1] );
		legsAngles[ROLL] -= side;

		side = speed * DotProduct( velocity, axis[0] );
		legsAngles[PITCH] += side;
	}

	//
	clientNum = cent->currentState.clientNum;
	if ( clientNum >= 0 && clientNum < MAX_CLIENTS ) {
		ci = &cgs.clientinfo[ clientNum ];
		if ( ci->fixedlegs ) {
			legsAngles[YAW] = torsoAngles[YAW];
			legsAngles[PITCH] = 0.0f;
			legsAngles[ROLL] = 0.0f;
		}
	}

	// pain twitch
	CG_AddPainTwitch( cent, torsoAngles );

	// pull the angles back out of the hierarchial chain
	AnglesSubtract( headAngles, torsoAngles, headAngles );
	AnglesSubtract( torsoAngles, legsAngles, torsoAngles );
	AnglesToAxis( legsAngles, legs );
	AnglesToAxis( torsoAngles, torso );
	AnglesToAxis( headAngles, head );
}


//==========================================================================

/*
===============
CG_HasteTrail
===============
*/
static void CG_HasteTrail( centity_t *cent ) {
	localEntity_t	*smoke;
	vec3_t			origin;
	int				anim;

	if ( cent->trailTime > cg.time ) {
		return;
	}
	anim = cent->pe.legs.animationNumber & ~ANIM_TOGGLEBIT;
	if ( anim != LEGS_RUN && anim != LEGS_BACK ) {
		return;
	}

	cent->trailTime += 100;
	if ( cent->trailTime < cg.time ) {
		cent->trailTime = cg.time;
	}

	VectorCopy( cent->lerpOrigin, origin );
	origin[2] -= 16;

	smoke = CG_SmokePuff( origin, vec3_origin, 
				  8, 
				  1, 1, 1, 1,
				  500, 
				  cg.time,
				  0,
				  0,
				  cgs.media.hastePuffShader );

	// use the optimized local entity add
	smoke->leType = LE_SCALE_FADE;
}

#ifdef MISSIONPACK
/*
===============
CG_BreathPuffs
===============
*/
static void CG_BreathPuffs( centity_t *cent, refEntity_t *head) {
	clientInfo_t *ci;
	vec3_t up, origin;
	int contents;

	ci = &cgs.clientinfo[ cent->currentState.number ];

	if (!cg_enableBreath.integer) {
		return;
	}
	if ( cent->currentState.number == cg.snap->ps.clientNum && !cg.renderingThirdPerson) {
		return;
	}
	if ( cent->currentState.eFlags & EF_DEAD ) {
		return;
	}
	contents = CG_PointContents( head->origin, 0 );
	if ( contents & ( CONTENTS_WATER | CONTENTS_SLIME | CONTENTS_LAVA ) ) {
		return;
	}
	if ( ci->breathPuffTime > cg.time ) {
		return;
	}

	VectorSet( up, 0, 0, 8 );
	VectorMA(head->origin, 8, head->axis[0], origin);
	VectorMA(origin, -4, head->axis[2], origin);
	CG_SmokePuff( origin, up, 16, 1, 1, 1, 0.66f, 1500, cg.time, cg.time + 400, LEF_PUFF_DONT_SCALE, cgs.media.shotgunSmokePuffShader );
	ci->breathPuffTime = cg.time + 2000;
}

/*
===============
CG_DustTrail
===============
*/
static void CG_DustTrail( centity_t *cent ) {
	int				anim;
	vec3_t end, vel;
	trace_t tr;

	if (!cg_enableDust.integer)
		return;

	if ( cent->dustTrailTime > cg.time ) {
		return;
	}

	anim = cent->pe.legs.animationNumber & ~ANIM_TOGGLEBIT;
	if ( anim != LEGS_LANDB && anim != LEGS_LAND ) {
		return;
	}

	cent->dustTrailTime += 40;
	if ( cent->dustTrailTime < cg.time ) {
		cent->dustTrailTime = cg.time;
	}

	VectorCopy(cent->currentState.pos.trBase, end);
	end[2] -= 64;
	CG_Trace( &tr, cent->currentState.pos.trBase, NULL, NULL, end, cent->currentState.number, MASK_PLAYERSOLID );

	if ( !(tr.surfaceFlags & SURF_DUST) )
		return;

	VectorCopy( cent->currentState.pos.trBase, end );
	end[2] -= 16;

	VectorSet(vel, 0, 0, -30);
	CG_SmokePuff( end, vel,
				  24,
				  .8f, .8f, 0.7f, 0.33f,
				  500,
				  cg.time,
				  0,
				  0,
				  cgs.media.dustPuffShader );
}

#endif

/*
===============
CG_SlideTrail

STRAFE 64: ground dust kicked up behind a crouch-slide. The slide is a
signature movement state but had only camera juice (FOV punch + lean) and no
particle signature. Driven off the networked EF_SLIDING flag so it draws on
every pilot (and bots), not just the viewing client. Puffs spawn at the
trailing edge of the slide, kicked up and back along the motion vector, scaling
with slide speed. (CG_DustTrail above is MISSIONPACK-only; this is always built.)
===============
*/
static void CG_SlideTrail( centity_t *cent ) {
	vec3_t		end, vel, dir;
	trace_t		tr;
	float		speed;

	if ( !cg_slideDust.integer ) {
		return;
	}
	if ( !( cent->currentState.eFlags & EF_SLIDING ) ) {
		return;
	}
	if ( cent->dustTrailTime > cg.time ) {
		return;		// rate-limit (shared with CG_DustTrail, which isn't built here)
	}
	cent->dustTrailTime = cg.time + 35;

	// must be over ground for dust to make sense
	VectorCopy( cent->lerpOrigin, end );
	end[2] -= 64;
	CG_Trace( &tr, cent->lerpOrigin, NULL, NULL, end, cent->currentState.number, MASK_PLAYERSOLID );
	if ( tr.fraction == 1.0f ) {
		return;
	}

	// horizontal motion vector — puffs trail BEHIND the slide
	VectorCopy( cent->currentState.pos.trDelta, dir );
	dir[2] = 0;
	speed = VectorNormalize( dir );

	VectorCopy( tr.endpos, end );
	end[2] += 2;
	VectorMA( end, -16.0f, dir, end );		// behind the feet

	vel[0] = -dir[0] * 40.0f + crandom() * 20.0f;
	vel[1] = -dir[1] * 40.0f + crandom() * 20.0f;
	vel[2] = 30.0f + random() * 30.0f;		// kick up off the floor

	CG_SmokePuff( end, vel,
				  8.0f + speed * 0.02f,			// faster slide => bigger plume
				  0.80f, 0.85f, 0.95f, 0.45f,	// cool synth-grey
				  400,
				  cg.time, 0, 0,
				  cgs.media.smokePuffShader );
}

/*
===============
CG_TrailItem
===============
*/
static void CG_TrailItem( centity_t *cent, qhandle_t hModel ) {
	refEntity_t		ent;
	vec3_t			angles;
	vec3_t			axis[3];

	VectorCopy( cent->lerpAngles, angles );
	angles[PITCH] = 0;
	angles[ROLL] = 0;
	AnglesToAxis( angles, axis );

	memset( &ent, 0, sizeof( ent ) );
	VectorMA( cent->lerpOrigin, -16, axis[0], ent.origin );
	ent.origin[2] += 16;
	angles[YAW] += 90;
	AnglesToAxis( angles, ent.axis );

	ent.hModel = hModel;
	trap_R_AddRefEntityToScene( &ent );
}


/*
===============
CG_PlayerFlag
===============
*/
static void CG_PlayerFlag( centity_t *cent, qhandle_t hSkin, refEntity_t *torso ) {
	clientInfo_t	*ci;
	refEntity_t	pole;
	refEntity_t	flag;
	vec3_t		angles, dir;
	int			legsAnim, flagAnim, updateangles;
	float		angle, d;

	// show the flag pole model
	memset( &pole, 0, sizeof(pole) );
	pole.hModel = cgs.media.flagPoleModel;
	VectorCopy( torso->lightingOrigin, pole.lightingOrigin );
	pole.shadowPlane = torso->shadowPlane;
	pole.renderfx = torso->renderfx;
	CG_PositionEntityOnTag( &pole, torso, torso->hModel, "tag_flag" );
	trap_R_AddRefEntityToScene( &pole );

	// show the flag model
	memset( &flag, 0, sizeof(flag) );
	flag.hModel = cgs.media.flagFlapModel;
	flag.customSkin = hSkin;
	VectorCopy( torso->lightingOrigin, flag.lightingOrigin );
	flag.shadowPlane = torso->shadowPlane;
	flag.renderfx = torso->renderfx;

	VectorClear(angles);

	updateangles = qfalse;
	legsAnim = cent->currentState.legsAnim & ~ANIM_TOGGLEBIT;
	if( legsAnim == LEGS_IDLE || legsAnim == LEGS_IDLECR ) {
		flagAnim = FLAG_STAND;
	} else if ( legsAnim == LEGS_WALK || legsAnim == LEGS_WALKCR ) {
		flagAnim = FLAG_STAND;
		updateangles = qtrue;
	} else {
		flagAnim = FLAG_RUN;
		updateangles = qtrue;
	}

	if ( updateangles ) {

		VectorCopy( cent->currentState.pos.trDelta, dir );
		// add gravity
		dir[2] += 100;
		VectorNormalize( dir );
		d = DotProduct(pole.axis[2], dir);
		// if there is enough movement orthogonal to the flag pole
		if (fabs(d) < 0.9) {
			//
			d = DotProduct(pole.axis[0], dir);
			if (d > 1.0f) {
				d = 1.0f;
			}
			else if (d < -1.0f) {
				d = -1.0f;
			}
			angle = acos(d);

			d = DotProduct(pole.axis[1], dir);
			if (d < 0) {
				angles[YAW] = 360 - angle * 180 / M_PI;
			}
			else {
				angles[YAW] = angle * 180 / M_PI;
			}
			if (angles[YAW] < 0)
				angles[YAW] += 360;
			if (angles[YAW] > 360)
				angles[YAW] -= 360;

			//vectoangles( cent->currentState.pos.trDelta, tmpangles );
			//angles[YAW] = tmpangles[YAW] + 45 - cent->pe.torso.yawAngle;
			// change the yaw angle
			CG_SwingAngles( angles[YAW], 25, 90, 0.15f, &cent->pe.flag.yawAngle, &cent->pe.flag.yawing );
		}

		/*
		d = DotProduct(pole.axis[2], dir);
		angle = Q_acos(d);

		d = DotProduct(pole.axis[1], dir);
		if (d < 0) {
			angle = 360 - angle * 180 / M_PI;
		}
		else {
			angle = angle * 180 / M_PI;
		}
		if (angle > 340 && angle < 20) {
			flagAnim = FLAG_RUNUP;
		}
		if (angle > 160 && angle < 200) {
			flagAnim = FLAG_RUNDOWN;
		}
		*/
	}

	// set the yaw angle
	angles[YAW] = cent->pe.flag.yawAngle;
	// lerp the flag animation frames
	ci = &cgs.clientinfo[ cent->currentState.clientNum ];
	CG_RunLerpFrame( ci, &cent->pe.flag, flagAnim, 1 );
	flag.oldframe = cent->pe.flag.oldFrame;
	flag.frame = cent->pe.flag.frame;
	flag.backlerp = cent->pe.flag.backlerp;

	AnglesToAxis( angles, flag.axis );
	CG_PositionRotatedEntityOnTag( &flag, &pole, pole.hModel, "tag_flag" );

	trap_R_AddRefEntityToScene( &flag );
}


#ifdef MISSIONPACK
/*
===============
CG_PlayerTokens
===============
*/
static void CG_PlayerTokens( centity_t *cent, int renderfx ) {
	int			tokens, i, j;
	float		angle;
	refEntity_t	ent;
	vec3_t		dir, origin;
	skulltrail_t *trail;
	if ( cent->currentState.number >= MAX_CLIENTS ) {
		return;
	}
	trail = &cg.skulltrails[cent->currentState.number];
	tokens = cent->currentState.generic1;
	if ( !tokens ) {
		trail->numpositions = 0;
		return;
	}

	if ( tokens > MAX_SKULLTRAIL ) {
		tokens = MAX_SKULLTRAIL;
	}

	// add skulls if there are more than last time
	for (i = 0; i < tokens - trail->numpositions; i++) {
		for (j = trail->numpositions; j > 0; j--) {
			VectorCopy(trail->positions[j-1], trail->positions[j]);
		}
		VectorCopy(cent->lerpOrigin, trail->positions[0]);
	}
	trail->numpositions = tokens;

	// move all the skulls along the trail
	VectorCopy(cent->lerpOrigin, origin);
	for (i = 0; i < trail->numpositions; i++) {
		VectorSubtract(trail->positions[i], origin, dir);
		if (VectorNormalize(dir) > 30) {
			VectorMA(origin, 30, dir, trail->positions[i]);
		}
		VectorCopy(trail->positions[i], origin);
	}

	memset( &ent, 0, sizeof( ent ) );
	if( cgs.clientinfo[ cent->currentState.clientNum ].team == TEAM_BLUE ) {
		ent.hModel = cgs.media.redCubeModel;
	} else {
		ent.hModel = cgs.media.blueCubeModel;
	}
	ent.renderfx = renderfx;

	VectorCopy(cent->lerpOrigin, origin);
	for (i = 0; i < trail->numpositions; i++) {
		VectorSubtract(origin, trail->positions[i], ent.axis[0]);
		ent.axis[0][2] = 0;
		VectorNormalize(ent.axis[0]);
		VectorSet(ent.axis[2], 0, 0, 1);
		CrossProduct(ent.axis[0], ent.axis[2], ent.axis[1]);

		VectorCopy(trail->positions[i], ent.origin);
		angle = (((cg.time + 500 * MAX_SKULLTRAIL - 500 * i) / 16) & 255) * (M_PI * 2) / 255;
		ent.origin[2] += sin(angle) * 10;
		trap_R_AddRefEntityToScene( &ent );
		VectorCopy(trail->positions[i], origin);
	}
}
#endif


/*
===============
CG_PlayerPowerups
===============
*/
static void CG_PlayerPowerups( centity_t *cent, refEntity_t *torso ) {
	int		powerups;
	clientInfo_t	*ci;

	powerups = cent->currentState.powerups;
	if ( !powerups ) {
		return;
	}

	// quad gives a dlight
	if ( powerups & ( 1 << PW_QUAD ) ) {
		trap_R_AddLightToScene( cent->lerpOrigin, 200 + (rand()&31), 0.2f, 0.2f, 1 );
	}

	// flight plays a looped sound
	if ( powerups & ( 1 << PW_FLIGHT ) ) {
		trap_S_AddLoopingSound( cent->currentState.number, cent->lerpOrigin, vec3_origin, cgs.media.flightSound );
	}

	ci = &cgs.clientinfo[ cent->currentState.clientNum ];
	// redflag
	if ( powerups & ( 1 << PW_REDFLAG ) ) {
		if (ci->newAnims) {
			CG_PlayerFlag( cent, cgs.media.redFlagFlapSkin, torso );
		}
		else {
			CG_TrailItem( cent, cgs.media.redFlagModel );
		}
		trap_R_AddLightToScene( cent->lerpOrigin, 200 + (rand()&31), 1.0, 0.2f, 0.2f );
	}

	// blueflag
	if ( powerups & ( 1 << PW_BLUEFLAG ) ) {
		if (ci->newAnims){
			CG_PlayerFlag( cent, cgs.media.blueFlagFlapSkin, torso );
		}
		else {
			CG_TrailItem( cent, cgs.media.blueFlagModel );
		}
		trap_R_AddLightToScene( cent->lerpOrigin, 200 + (rand()&31), 0.2f, 0.2f, 1.0 );
	}

	// neutralflag
	if ( powerups & ( 1 << PW_NEUTRALFLAG ) ) {
		if (ci->newAnims) {
			CG_PlayerFlag( cent, cgs.media.neutralFlagFlapSkin, torso );
		}
		else {
			CG_TrailItem( cent, cgs.media.neutralFlagModel );
		}
		trap_R_AddLightToScene( cent->lerpOrigin, 200 + (rand()&31), 1.0, 1.0, 1.0 );
	}

	// haste leaves smoke trails
	if ( powerups & ( 1 << PW_HASTE ) ) {
		CG_HasteTrail( cent );
	}
}


/*
===============
CG_PlayerFloatSprite

Float a sprite over the player's head
===============
*/
static void CG_PlayerFloatSprite( centity_t *cent, qhandle_t shader ) {
	int				rf;
	refEntity_t		ent;

	if ( cent->currentState.number == cg.snap->ps.clientNum && !cg.renderingThirdPerson ) {
		rf = RF_THIRD_PERSON;		// only show in mirrors
	} else {
		rf = 0;
	}

	memset( &ent, 0, sizeof( ent ) );
	VectorCopy( cent->lerpOrigin, ent.origin );
	ent.origin[2] += 48;
	ent.reType = RT_SPRITE;
	ent.customShader = shader;
	ent.radius = 10;
	ent.renderfx = rf;
	ent.shaderRGBA[0] = 255;
	ent.shaderRGBA[1] = 255;
	ent.shaderRGBA[2] = 255;
	ent.shaderRGBA[3] = 255;
	trap_R_AddRefEntityToScene( &ent );
}



/*
===============
CG_PlayerSprites

Float sprites over the player's head
===============
*/
static void CG_PlayerSprites( centity_t *cent ) {
	int		team;

	if ( cent->currentState.eFlags & EF_CONNECTION ) {
		CG_PlayerFloatSprite( cent, cgs.media.connectionShader );
		return;
	}

	if ( cent->currentState.eFlags & EF_TALK ) {
		CG_PlayerFloatSprite( cent, cgs.media.balloonShader );
		return;
	}

	if ( cent->currentState.eFlags & EF_AWARD_IMPRESSIVE ) {
		CG_PlayerFloatSprite( cent, cgs.media.medalImpressive );
		return;
	}

	if ( cent->currentState.eFlags & EF_AWARD_EXCELLENT ) {
		CG_PlayerFloatSprite( cent, cgs.media.medalExcellent );
		return;
	}

	if ( cent->currentState.eFlags & EF_AWARD_GAUNTLET ) {
		CG_PlayerFloatSprite( cent, cgs.media.medalGauntlet );
		return;
	}

	if ( cent->currentState.eFlags & EF_AWARD_DEFEND ) {
		CG_PlayerFloatSprite( cent, cgs.media.medalDefend );
		return;
	}

	if ( cent->currentState.eFlags & EF_AWARD_ASSIST ) {
		CG_PlayerFloatSprite( cent, cgs.media.medalAssist );
		return;
	}

	if ( cent->currentState.eFlags & EF_AWARD_CAP ) {
		CG_PlayerFloatSprite( cent, cgs.media.medalCapture );
		return;
	}

	team = cgs.clientinfo[ cent->currentState.clientNum ].team;
	if ( !(cent->currentState.eFlags & EF_DEAD) && 
		cg.snap->ps.persistant[PERS_TEAM] == team &&
		cgs.gametype >= GT_TEAM) {
		if (cg_drawFriend.integer) {
			CG_PlayerFloatSprite( cent, cgs.media.friendShader );
		}
		return;
	}
}

/*
===============
CG_PlayerShadow

Returns the Z component of the surface being shadowed

  should it return a full plane instead of a Z?
===============
*/
#define	SHADOW_DISTANCE		128
static qboolean CG_PlayerShadow( centity_t *cent, float *shadowPlane ) {
	vec3_t		end, mins = {-15, -15, 0}, maxs = {15, 15, 2};
	trace_t		trace;
	float		alpha;

	*shadowPlane = 0;

	if ( cg_shadows.integer == 0 ) {
		return qfalse;
	}

	// no shadows when invisible
	if ( cent->currentState.powerups & ( 1 << PW_INVIS ) ) {
		return qfalse;
	}

	// send a trace down from the player to the ground
	VectorCopy( cent->lerpOrigin, end );
	end[2] -= SHADOW_DISTANCE;

	trap_CM_BoxTrace( &trace, cent->lerpOrigin, end, mins, maxs, 0, MASK_PLAYERSOLID );

	// no shadow if too high
	if ( trace.fraction == 1.0 || trace.startsolid || trace.allsolid ) {
		return qfalse;
	}

	*shadowPlane = trace.endpos[2] + 1;

	if ( cg_shadows.integer != 1 ) {	// no mark for stencil or projection shadows
		return qtrue;
	}

	// fade the shadow out with height
	alpha = 1.0 - trace.fraction;

	// hack / FPE - bogus planes?
	//assert( DotProduct( trace.plane.normal, trace.plane.normal ) != 0.0f ) 

	// add the mark as a temporary, so it goes directly to the renderer
	// without taking a spot in the cg_marks array
	CG_ImpactMark( cgs.media.shadowMarkShader, trace.endpos, trace.plane.normal, 
		cent->pe.legs.yawAngle, alpha,alpha,alpha,1, qfalse, 24, qtrue );

	return qtrue;
}


/*
===============
CG_PlayerSplash

Draw a mark at the water surface
===============
*/
static void CG_PlayerSplash( centity_t *cent ) {
	vec3_t		start, end;
	trace_t		trace;
	int			contents;
	polyVert_t	verts[4];

	if ( !cg_shadows.integer ) {
		return;
	}

	VectorCopy( cent->lerpOrigin, end );
	end[2] -= 24;

	// if the feet aren't in liquid, don't make a mark
	// this won't handle moving water brushes, but they wouldn't draw right anyway...
	contents = CG_PointContents( end, 0 );
	if ( !( contents & ( CONTENTS_WATER | CONTENTS_SLIME | CONTENTS_LAVA ) ) ) {
		return;
	}

	VectorCopy( cent->lerpOrigin, start );
	start[2] += 32;

	// if the head isn't out of liquid, don't make a mark
	contents = CG_PointContents( start, 0 );
	if ( contents & ( CONTENTS_SOLID | CONTENTS_WATER | CONTENTS_SLIME | CONTENTS_LAVA ) ) {
		return;
	}

	// trace down to find the surface
	trap_CM_BoxTrace( &trace, start, end, NULL, NULL, 0, ( CONTENTS_WATER | CONTENTS_SLIME | CONTENTS_LAVA ) );

	if ( trace.fraction == 1.0 ) {
		return;
	}

	// create a mark polygon
	VectorCopy( trace.endpos, verts[0].xyz );
	verts[0].xyz[0] -= 32;
	verts[0].xyz[1] -= 32;
	verts[0].st[0] = 0;
	verts[0].st[1] = 0;
	verts[0].modulate[0] = 255;
	verts[0].modulate[1] = 255;
	verts[0].modulate[2] = 255;
	verts[0].modulate[3] = 255;

	VectorCopy( trace.endpos, verts[1].xyz );
	verts[1].xyz[0] -= 32;
	verts[1].xyz[1] += 32;
	verts[1].st[0] = 0;
	verts[1].st[1] = 1;
	verts[1].modulate[0] = 255;
	verts[1].modulate[1] = 255;
	verts[1].modulate[2] = 255;
	verts[1].modulate[3] = 255;

	VectorCopy( trace.endpos, verts[2].xyz );
	verts[2].xyz[0] += 32;
	verts[2].xyz[1] += 32;
	verts[2].st[0] = 1;
	verts[2].st[1] = 1;
	verts[2].modulate[0] = 255;
	verts[2].modulate[1] = 255;
	verts[2].modulate[2] = 255;
	verts[2].modulate[3] = 255;

	VectorCopy( trace.endpos, verts[3].xyz );
	verts[3].xyz[0] += 32;
	verts[3].xyz[1] -= 32;
	verts[3].st[0] = 1;
	verts[3].st[1] = 0;
	verts[3].modulate[0] = 255;
	verts[3].modulate[1] = 255;
	verts[3].modulate[2] = 255;
	verts[3].modulate[3] = 255;

	trap_R_AddPolyToScene( cgs.media.wakeMarkShader, 4, verts );
}



/*
===============
CG_AddRefEntityWithPowerups

Adds a piece with modifications or duplications for powerups
Also called by CG_Missile for quad rockets, but nobody can tell...
===============
*/
void CG_AddRefEntityWithPowerups( refEntity_t *ent, entityState_t *state, int team ) {

	if ( state->powerups & ( 1 << PW_INVIS ) ) {
		ent->customShader = cgs.media.invisShader;
		trap_R_AddRefEntityToScene( ent );
	} else {
		/*
		if ( state->eFlags & EF_KAMIKAZE ) {
			if (team == TEAM_BLUE)
				ent->customShader = cgs.media.blueKamikazeShader;
			else
				ent->customShader = cgs.media.redKamikazeShader;
			trap_R_AddRefEntityToScene( ent );
		}
		else {*/
			trap_R_AddRefEntityToScene( ent );
		//}

		if ( state->powerups & ( 1 << PW_QUAD ) )
		{
			if (team == TEAM_RED)
				ent->customShader = cgs.media.redQuadShader;
			else
				ent->customShader = cgs.media.quadShader;
			trap_R_AddRefEntityToScene( ent );
		}
		if ( state->powerups & ( 1 << PW_REGEN ) ) {
			if ( ( ( cg.time / 100 ) % 10 ) == 1 ) {
				ent->customShader = cgs.media.regenShader;
				trap_R_AddRefEntityToScene( ent );
			}
		}
		if ( state->powerups & ( 1 << PW_BATTLESUIT ) ) {
			ent->customShader = cgs.media.battleSuitShader;
			trap_R_AddRefEntityToScene( ent );
		}
	}
}

/*
=================
CG_LightVerts
=================
*/
int CG_LightVerts( vec3_t normal, int numVerts, polyVert_t *verts )
{
	int				i, j;
	float			incoming;
	vec3_t			ambientLight;
	vec3_t			lightDir;
	vec3_t			directedLight;

	trap_R_LightForPoint( verts[0].xyz, ambientLight, directedLight, lightDir );

	for (i = 0; i < numVerts; i++) {
		incoming = DotProduct (normal, lightDir);
		if ( incoming <= 0 ) {
			verts[i].modulate[0] = ambientLight[0];
			verts[i].modulate[1] = ambientLight[1];
			verts[i].modulate[2] = ambientLight[2];
			verts[i].modulate[3] = 255;
			continue;
		} 
		j = ( ambientLight[0] + incoming * directedLight[0] );
		if ( j > 255 ) {
			j = 255;
		}
		verts[i].modulate[0] = j;

		j = ( ambientLight[1] + incoming * directedLight[1] );
		if ( j > 255 ) {
			j = 255;
		}
		verts[i].modulate[1] = j;

		j = ( ambientLight[2] + incoming * directedLight[2] );
		if ( j > 255 ) {
			j = 255;
		}
		verts[i].modulate[2] = j;

		verts[i].modulate[3] = 255;
	}
	return qtrue;
}

/*
===============
CG_AxisRoll

Roll an MD3 axis triad about one of its own axes by `degrees`. about=0 rolls
about forward (bank left/right), 1 about left (pitch fwd/back), 2 about up (yaw).
The two perpendicular axes are rotated in place; the spin axis is untouched.
===============
*/
static void CG_AxisRoll( vec3_t axis[3], int about, float degrees ) {
	vec3_t	a1, a2;
	int		i1 = ( about + 1 ) % 3;
	int		i2 = ( about + 2 ) % 3;

	RotatePointAroundVector( a1, axis[about], axis[i1], degrees );
	RotatePointAroundVector( a2, axis[about], axis[i2], degrees );
	VectorCopy( a1, axis[i1] );
	VectorCopy( a2, axis[i2] );
}

/*
===============
CG_WallGripDetect

Cosmetic, client-side wall detection for the grip pose. Independent of the
server's STAT_WALLRUN (which isn't networked for remote players), so it lights
up for bots and other clients too. Returns a signed target: + wall on the
right, - wall on the left, 0 = no grip. Magnitude scales with proximity.
===============
*/
static float CG_WallGripDetect( centity_t *cent ) {
	vec3_t		angles, forward, right, start, end, vel;
	vec3_t		mins = { -6, -6, -6 }, maxs = { 6, 6, 6 };
	trace_t		tr;
	float		speed;
	int			i;

	if ( !cg_wallGrip.integer ) {
		return 0;
	}
	// only airborne bodies grip a wall
	if ( cent->currentState.groundEntityNum != ENTITYNUM_NONE ) {
		return 0;
	}

	// horizontal speed gate (authoritative for self, networked velocity for others)
	if ( cent->currentState.number == cg.snap->ps.clientNum ) {
		VectorCopy( cg.predictedPlayerState.velocity, vel );
	} else {
		VectorCopy( cent->currentState.pos.trDelta, vel );
	}
	vel[2] = 0;
	speed = VectorLength( vel );
	if ( speed < 200.0f ) {
		return 0;
	}

	// probe left and right of the body's facing for a near-vertical wall
	VectorClear( angles );
	angles[YAW] = cent->lerpAngles[YAW];
	AngleVectors( angles, forward, right, NULL );

	VectorCopy( cent->lerpOrigin, start );
	start[2] += 24.0f;	// mid-torso height

	for ( i = 0; i < 2; i++ ) {
		float	s = i ? -1.0f : 1.0f;	// +1 right first, then left

		VectorMA( start, s * 30.0f, right, end );
		CG_Trace( &tr, start, mins, maxs, end, cent->currentState.number, MASK_PLAYERSOLID );

		if ( tr.fraction < 1.0f && !tr.allsolid
			&& fabs( tr.plane.normal[2] ) < 0.3f ) {
			return s * ( 1.0f - tr.fraction );	// closer wall -> stronger lean
		}
	}
	return 0;
}

/*
===============
CG_WallGripPose

Lean the whole body onto a gripped wall: bank the legs into it, hunch and bank
the torso a touch harder, keep the head a little more upright. Layered on the
MD3 axes from CG_PlayerAngles before the tag chain composes them, so torso and
head inherit the legs bank and add their own. Eased per-entity so attach and
release roll smoothly rather than snap.
===============
*/
static void CG_WallGripPose( centity_t *cent, vec3_t legs[3], vec3_t torso[3], vec3_t head[3] ) {
	float	target, g, k;

	target = CG_WallGripDetect( cent );
	cent->pe.wallGrip += ( target - cent->pe.wallGrip ) * 0.2f;
	g = cent->pe.wallGrip;
	if ( g > -0.01f && g < 0.01f ) {
		return;
	}

	k = cg_wallGripScale.value;

	// bank the body toward the wall (g>0 = wall on right -> top leans right).
	// legs carry the whole-body lean; torso/head inherit it through the tag
	// chain and add their own. head banks less so the gaze stays readable.
	CG_AxisRoll( legs,  0, -14.0f * g * k );			// roll about forward
	CG_AxisRoll( torso, 0,  -8.0f * g * k );			// extra torso bank
	CG_AxisRoll( torso, 1,   7.0f * fabs( g ) * k );	// hunch forward into the run
	CG_AxisRoll( head,  0,   6.0f * g * k );			// counter-roll: head stays upright
}

/*
===============
CG_TriggerAcrobatic

STRAFE 64: kick off a cosmetic full-body flip. Purely visual — it spins the
rendered body, never the physics or the view. Only a BACKWARD air-jump
backflips: the dash no longer rolls and a forward/neutral air-jump stays flat
(both read as disorienting in the forward run). Kept narrow on purpose, easy to
re-broaden later.

kind: 0 = air-jump
===============
*/
void CG_TriggerAcrobatic( centity_t *cent, int kind ) {
	vec3_t	fwd, vel, ang;
	float	fdot, sp;

	if ( !cg_acrobatics.integer ) {
		return;
	}

	VectorCopy( cent->currentState.pos.trDelta, vel );
	vel[2] = 0;
	sp = VectorNormalize( vel );

	VectorClear( ang );
	ang[YAW] = cent->lerpAngles[YAW];
	AngleVectors( ang, fwd, NULL, NULL );
	fdot = ( sp > 1.0f ) ? DotProduct( vel, fwd ) : 1.0f;

	// only a clear back-pedalling air-jump backflips; forward / neutral jumps and
	// the dash do nothing
	if ( kind != 0 || fdot >= -0.2f ) {
		return;
	}
	cent->pe.flipAxis = 1;			// somersault about the side axis
	cent->pe.flipDir  = -1.0f;		// backflip (rotate the body backward, heels over head)
	cent->pe.flipDuration = 600;
	cent->pe.flipStartTime = cg.time;
}

/*
===============
CG_AcrobaticPose

Apply the active flip / roll: a full 360 spin of the whole body about one axis
over flipDuration, eased so it accelerates off the move and lands cleanly back
at neutral. Rolling the LEGS axis spins everything — torso and head hang off the
legs tag and inherit the rotation. Layered on the MD3 axes exactly like the
wall-grip and slide poses.
===============
*/
static void CG_AcrobaticPose( centity_t *cent, vec3_t legs[3] ) {
	int		dt;
	float	p, e, deg;

	if ( !cent->pe.flipStartTime ) {
		return;
	}
	dt = cg.time - cent->pe.flipStartTime;
	if ( dt < 0 || dt >= cent->pe.flipDuration ) {
		cent->pe.flipStartTime = 0;				// done — back to neutral
		return;
	}

	p = (float)dt / (float)cent->pe.flipDuration;	// 0..1
	e = p * p * ( 3.0f - 2.0f * p );				// smoothstep: at p=1, e=1 -> full 360 = neutral
	deg = 360.0f * cent->pe.flipDir * e;
	CG_AxisRoll( legs, cent->pe.flipAxis, deg );
}

/*
===============
CG_TriggerKick

STRAFE 64: kick off the melee-kick body animation (EV_KICK). Grounded it's the
snap-roundhouse pose (CG_KickPose) layered on the forced LEGS_JUMP/TORSO_ATTACK2
frames the game set; airborne it ALSO rides the acrobatic flip system for a full
kung-fu tornado spin about the body's up axis, spinning INTO the kick and landing
back at neutral. parm = EV_KICK eventParm (1 airborne, 2 hit, 4 ninja).
===============
*/
void CG_TriggerKick( centity_t *cent, int parm ) {
	cent->pe.kickTime  = cg.time;
	cent->pe.kickFlags = parm;

	if ( parm & 1 ) {
		cent->pe.flipAxis = 2;			// tornado: spin about the body's up axis
		// spin through the kicking side — alternate by client so a crowd of
		// kicking assassins doesn't pirouette in lockstep
		cent->pe.flipDir  = ( cent->currentState.number & 1 ) ? -1.0f : 1.0f;
		cent->pe.flipDuration = 450;
		cent->pe.flipStartTime = cg.time;
	}
}

/*
===============
CG_KickPose

STRAFE 64: the kick itself, on the body. Grounded: a snap roundhouse — hips whip
through the target, torso counter-rotates square, lean back off the raised leg,
head stays locked on the mark. Airborne: the tornado spin (flip system) does the
turning, this just lays the body back into the flying-kick line. Strike envelope
snaps out in the first third and eases home, matching the 350ms server anim
commit. Same axis-overlay approach as the other poses.
===============
*/
#define KICK_POSE_MS	350
static void CG_KickPose( centity_t *cent, vec3_t legs[3], vec3_t torso[3], vec3_t head[3] ) {
	int		dt;
	float	f, amt;

	if ( !cent->pe.kickTime ) {
		return;
	}
	dt = cg.time - cent->pe.kickTime;
	if ( dt < 0 || dt >= KICK_POSE_MS ) {
		cent->pe.kickTime = 0;					// done — back to neutral
		return;
	}

	f = (float)dt / (float)KICK_POSE_MS;
	// strike envelope: snap out over the first third, ease back home
	amt = ( f < 0.32f ) ? ( f / 0.32f ) : ( 1.0f - ( f - 0.32f ) / 0.68f );
	amt = amt * amt * ( 3.0f - 2.0f * amt );	// smooth both ends

	if ( cent->pe.kickFlags & 1 ) {
		// flying kick: lay the body back so the boots lead the spin
		CG_AxisRoll( legs,  1, -22.0f * amt );
		CG_AxisRoll( torso, 1, -10.0f * amt );
		return;
	}

	// grounded snap roundhouse
	CG_AxisRoll( legs,  2, -38.0f * amt );		// hips whip through the target
	CG_AxisRoll( legs,  1, -14.0f * amt );		// lean back off the raised leg
	CG_AxisRoll( torso, 2,  26.0f * amt );		// counter-twist keeps the chest square
	CG_AxisRoll( head,  2,  10.0f * amt );		// eyes stay on the victim
}

/*
===============
CG_SwordSwingPose

STRAFE 64: drive the WHOLE BODY through a sword swing, not just the weapon. The
stock TORSO_ATTACK frame is a flat arm poke; layered on top of it we twist the
torso through the swing's quadrant arc, lean into vertical cuts, plant the hips
and lunge the legs into the strike, and lead the gaze with the head — a
wound-spring that uncoils along the same start->end line the blade cuts. Holding
block instead settles the body into a guard stance leaning toward the threat.
Same axis-overlay approach as the wall-grip / slide poses.
===============
*/
static void CG_SwordSwingPose( centity_t *cent, vec3_t legs[3], vec3_t torso[3], vec3_t head[3] ) {
	int		dt;
	float	f, amt, r, u, sr, su, er, eu;
	float	twist, pitch, lunge;

	if ( cent->currentState.weapon != WP_SWORD ) {
		return;
	}

	// --- guard stance: hunch into the block and twist toward the side you're
	// covering (from lateral velocity), so a held guard reads braced, not idle ---
	if ( cent->currentState.eFlags & EF_BLOCKING ) {
		vec3_t	vel, right, ang;
		float	side;

		if ( cent->currentState.number == cg.snap->ps.clientNum ) {
			VectorCopy( cg.predictedPlayerState.velocity, vel );
		} else {
			VectorCopy( cent->currentState.pos.trDelta, vel );
		}
		VectorClear( ang );
		ang[YAW] = cent->lerpAngles[YAW];
		AngleVectors( ang, NULL, right, NULL );
		side = DotProduct( vel, right ) * 0.02f;
		if ( side > 12.0f )  side = 12.0f;
		if ( side < -12.0f ) side = -12.0f;

		CG_AxisRoll( torso, 2, side );		// twist toward the defended side
		CG_AxisRoll( torso, 1, 8.0f );		// hunch forward into the guard
		CG_AxisRoll( legs,  1, 5.0f );
		CG_AxisRoll( head,  1, -6.0f );		// keep the gaze up over the blade
		return;
	}

	dt = cg.time - cent->swordSwingTime;
	if ( dt < 0 || dt >= SWORD_SWING_MS + SWORD_RECOVER_MS ) {
		return;
	}

	if ( dt < SWORD_SWING_MS ) {
		f   = CG_SwordSwingFactor( dt / (float)SWORD_SWING_MS );
		amt = 1.0f;
	} else {
		// follow-through: hold the end pose and ease the body back to neutral
		float rf = ( dt - SWORD_SWING_MS ) / (float)SWORD_RECOVER_MS;
		f   = 1.0f;
		amt = ( 1.0f - rf ) * ( 1.0f - rf );
	}

	BG_SwordQuadDir( cent->swordStartQuad, &sr, &su );
	BG_SwordQuadDir( cent->swordEndQuad,   &er, &eu );
	r = sr + ( er - sr ) * f;		// screen-space blade position along the arc
	u = su + ( eu - su ) * f;

	twist = 46.0f * r * amt;				// torso yaw through a horizontal sweep
	pitch = 22.0f * ( -u ) * amt;			// back at wind-up, forward through a vertical cut
	lunge = 12.0f * amt * ( f > 0.0f ? f : 0.0f );	// step the legs into the strike

	// wound-spring: hips plant + lunge, torso carries the twist, head leads the gaze
	CG_AxisRoll( legs,  1, lunge );
	CG_AxisRoll( legs,  2, -0.25f * twist );
	CG_AxisRoll( torso, 2, twist );
	CG_AxisRoll( torso, 1, pitch );
	CG_AxisRoll( head,  2, -0.30f * twist );
	CG_AxisRoll( head,  1, -0.50f * pitch );
}

/*
===============
CG_SlidePose

STRAFE 64: throw the whole body into a ground slide, the slide counterpart of
CG_WallGripPose. Driven off the networked EF_SLIDING flag (so it reads on bots
and remote pilots too, no client-side detector needed), eased per-entity so
entry and recovery roll smoothly rather than snap.

A crouch pose alone reads as a crouch-walk, so this kicks the legs out in front,
reclines the torso back so the butt drops toward the deck, and keeps the head up
looking ahead — an Apex / Titanfall baseball slide. The matching vertical drop
of the body onto the ground is applied to legs.origin back in CG_Player (the tag
chain hangs off it). Banks into the carve from lateral velocity so the body
echoes the slide camera roll. Layered on the MD3 axes from CG_PlayerAngles
before the tag chain composes torso and head, exactly like the wall-grip pose.
===============
*/
// pitch about the side axis (axis 1): per the wall-grip hunch, +deg pitches the
// top forward, -deg leans it back. Legs kick out in front, torso reclines back to
// the deck, head counter-pitches up so the gaze stays level and readable.
#define SLIDE_LEG_PITCH		-50.0f
#define SLIDE_TORSO_PITCH	-30.0f
#define SLIDE_HEAD_PITCH	 60.0f
#define SLIDE_DROP			  9.0f		// units the body sinks onto the deck

static void CG_SlidePose( centity_t *cent, vec3_t legs[3], vec3_t torso[3], vec3_t head[3] ) {
	float	target, s, k, lean, side;
	vec3_t	angles, right, vel;

	if ( !cg_slidePose.integer ) {
		return;
	}

	// ease the recline amount toward 1 while sliding, back to 0 on release
	target = ( cent->currentState.eFlags & EF_SLIDING ) ? 1.0f : 0.0f;
	cent->pe.slide += ( target - cent->pe.slide ) * 0.18f;
	s = cent->pe.slide;
	if ( s < 0.01f ) {
		cent->pe.slideLean = 0;
		return;
	}

	// signed bank from lateral velocity, matching the slide camera carve.
	// authoritative predicted velocity for our own body, networked trDelta for others.
	if ( cent->currentState.number == cg.snap->ps.clientNum ) {
		VectorCopy( cg.predictedPlayerState.velocity, vel );
	} else {
		VectorCopy( cent->currentState.pos.trDelta, vel );
	}
	VectorClear( angles );
	angles[YAW] = cent->lerpAngles[YAW];
	AngleVectors( angles, NULL, right, NULL );
	side = DotProduct( vel, right );
	target = side * 0.0016f;				// lateral speed -> signed lean fraction
	if ( target > 1.0f ) {
		target = 1.0f;
	} else if ( target < -1.0f ) {
		target = -1.0f;
	}
	cent->pe.slideLean += ( target - cent->pe.slideLean ) * 0.16f;
	lean = cent->pe.slideLean;

	k = cg_slidePoseScale.value;

	CG_AxisRoll( legs,  1, SLIDE_LEG_PITCH   * s * k );	// kick the legs out in front
	CG_AxisRoll( torso, 1, SLIDE_TORSO_PITCH * s * k );	// recline the torso back to the deck
	CG_AxisRoll( head,  1, SLIDE_HEAD_PITCH  * s * k );	// head holds level, looking ahead

	// bank into the carve (roll about forward), the body echoing the camera roll.
	CG_AxisRoll( legs,  0, -14.0f * lean * s * k );
	CG_AxisRoll( torso, 0,  -6.0f * lean * s * k );
	CG_AxisRoll( head,  0,   8.0f * lean * s * k );		// head counter-rolls upright
}

/*
===============
CG_DashGlitchGhost

STRAFE 64: a short, audio-reactive chromatic after-image left on an air-dash
(EV_DOUBLE_JUMP). Re-renders the pilot's body a few steps back along the dash
velocity, split into red / cyan copies on the additive ghost shader and fading
over the dash window — a glitchy neon speed-trail flourish on the key movement.
===============
*/
#define DASH_GLITCH_MS		300
#define DASH_GHOST_STEPS	5

static void CG_DashGlitchGhost( centity_t *cent, const refEntity_t *legs,
		const refEntity_t *torso, const refEntity_t *head ) {
	int					dt = cg.time - cent->dashGlitchTime;
	float				life, audio, bright, speed, gap, fringe;
	vec3_t				vel;
	const refEntity_t	*parts[3];
	int					step, p, c;

	if ( cg_dashGlitch.value <= 0 || cent->dashGlitchTime <= 0
			|| dt < 0 || dt > DASH_GLITCH_MS ) {
		return;
	}
	// nothing to ghost on our own first-person body
	if ( cent->currentState.number == cg.snap->ps.clientNum && !cg.renderingThirdPerson ) {
		return;
	}

	life   = 1.0f - dt / (float)DASH_GLITCH_MS;					// 1 -> 0 over the window
	audio  = 1.0f + 1.4f * au_level.value + 1.0f * au_bass.value;	// pump on the track
	bright = life * cg_dashGlitch.value;

	// trail axis: directly BEHIND the dash, along the full velocity (an air-dash
	// can climb/dive, so keep z) — copies stack back down the motion line.
	VectorCopy( cent->currentState.pos.trDelta, vel );
	speed = VectorNormalize( vel );
	if ( speed < 1.0f ) {
		VectorSet( vel, 1, 0, 0 );			// degenerate: shove along x
	}

	gap    = ( 13.0f + 0.022f * speed ) * cg_dashGlitch.value;	// strobe spacing behind
	fringe = 3.0f + 3.0f * audio;								// chromatic split ALONG the trail

	parts[0] = legs; parts[1] = torso; parts[2] = head;

	for ( step = 1 ; step <= DASH_GHOST_STEPS ; step++ ) {
		// STROBE: discrete frozen frames trailing behind. Brightness fades down the
		// trail AND every other frame punches dim, so the after-image flickers like
		// a strobe instead of a smooth smear.
		float	fade   = 1.0f - ( step - 1 ) / (float)DASH_GHOST_STEPS;
		float	strobe = ( step & 1 ) ? 1.0f : 0.40f;	// alternate bright / dim
		float	f       = bright * fade * strobe;
		float	back    = gap * step;

		if ( f <= 0.0f ) {
			continue;
		}
		for ( p = 0 ; p < 3 ; p++ ) {
			// chromatic aberration: red shoved a touch further back, cyan a touch
			// nearer, so each strobed frame carries a red/cyan fringe down the trail.
			for ( c = 0 ; c < 2 ; c++ ) {
				refEntity_t	g = *parts[p];
				float		off = back + ( ( c == 0 ) ? fringe : -fringe );

				g.customShader = cgs.media.glitchGhostShader;
				g.renderfx &= ~RF_SHADOW_PLANE;		// ghosts cast no blob shadow
				if ( c == 0 ) {						// red, trailing edge
					g.shaderRGBA[0] = (byte)( 255 * f );
					g.shaderRGBA[1] = (byte)(  20 * f );
					g.shaderRGBA[2] = (byte)(  50 * f );
				} else {							// cyan, leading edge
					g.shaderRGBA[0] = (byte)(  30 * f );
					g.shaderRGBA[1] = (byte)( 200 * f );
					g.shaderRGBA[2] = (byte)( 255 * f );
				}
				g.shaderRGBA[3] = 255;				// additive: fade lives in RGB
				VectorMA( g.origin, -off, vel, g.origin );
				VectorMA( g.lightingOrigin, -off, vel, g.lightingOrigin );
				trap_R_AddRefEntityToScene( &g );
			}
		}
	}
}

/*
===============
CG_GlowPilotCount

How many ET_PLAYER entities are in the current snapshot — i.e. how many
cg_playerGlow lights will be added this frame. Cached per cg.time so the O(N)
scan runs once a frame, not once per player. Used to soft-clamp the combined
glow so a crowd of pilots doesn't sum to white (see the glow block in CG_Player).
===============
*/
int CG_GlowPilotCount( void ) {
	static int	cachedTime = -1;
	static int	cachedCount = 1;
	int			i, n;

	if ( cg.time == cachedTime ) {
		return cachedCount;
	}
	cachedTime = cg.time;
	n = 0;
	for ( i = 0 ; i < cg.snap->numEntities ; i++ ) {
		if ( cg.snap->entities[i].eType == ET_PLAYER ) {
			n++;
		}
	}
	if ( n < 1 ) {
		n = 1;
	}
	cachedCount = n;
	return n;
}

/*
===============
CG_Player
===============
*/
#include "cg_skelclips.h"

/*
===============
CG_SkeletalPlayer

cg_skeletal 1: one IQM body in place of the legs/torso/head MD3s. Loops are
picked from the movement state and run at a rate matched to ground speed;
take-offs, landings, flips and dashes play as one-shots on top. The body leans
into acceleration and rolls into walls, and the chest follows the view pitch.
===============
*/
#define SKEL_WALL_TILT		46.0f
#define SKEL_WALL_LEG		26.0f	// furthest a foot goes from the hip, about 85% of the leg
#define SKEL_WALL_CLIP_OFF	17.0f	// body stand-off from the wall in the mocap wall run
#define SKEL_WALL_STEP		15.0f	// planted foot travels +-this along the wall
#define SKEL_WALL_REACH		5.0f	// stride centre sits ahead of the hips: the back leg never locks
#define SKEL_SPRINT_SPEED	230.0f	// ground speed the sprint clip reads right at

// second-order follow: overshoots a little, then settles. Acceleration-limited
// by construction, so a stepped target can't kick the body (f 3.5 Hz, damping 0.6)
static void CG_SkelSecondOrder( float *y, float *yd, float x, float dt ) {
	const float	w = 2 * M_PI * 3.5f, z = 0.6f;

	*yd += dt * ( w * w * ( x - *y ) - 2 * z * w * *yd );
	*y += dt * *yd;
}

/*
===============
CG_SkelWallStride

Wall-run gait, fitted to the real surface. The body hangs out from the wall
(SKEL_WALL_TILT, pivoting at mid-body) and the spine curves back so the head
stays near upright. Each foot has a true stance, planted and sliding backward
at ground speed, then a swing, lifted clear and carried forward. Soles turn flat
to the wall while planted. a = cycle angle.
===============
*/
static void CG_SkelFootFlat( refEntity_t *ent, const char *foot, const vec3_t off, float weight ) {
	orientation_t	rest, tag;
	vec3_t			n, want, axis;
	float			ang;
	int				i, best = 0;

	if ( !trap_R_LerpTag( &rest, ent->hModel, 0, 0, 0, foot )
		|| !trap_R_BoneTag( &tag, ent->hModel, ent->frame, ent->oldframe, ent->backlerp, ent->bonePose, foot ) ) {
		return;
	}
	for ( i = 1; i < 3; i++ ) {		// which joint axis is the sole normal at rest
		if ( fabs( rest.axis[i][2] ) > fabs( rest.axis[best][2] ) ) {
			best = i;
		}
	}
	VectorScale( tag.axis[best], rest.axis[best][2] > 0 ? 1.0f : -1.0f, n );
	for ( i = 0; i < 3; i++ ) {
		want[i] = DotProduct( off, ent->axis[i] );
	}
	CrossProduct( n, want, axis );
	if ( VectorNormalize( axis ) < 0.001f ) {
		return;
	}
	ang = acos( Com_Clamp( -1, 1, DotProduct( n, want ) ) ) * weight;
	ent->bonePose = CG_BoneRotation( ent, ent->bonePose, foot, &tag, axis, ang );
}

// faster runs bound: longer stride, longer flight, so cadence stays human
// 1 running level along the wall, 0 climbing or dropping
static float CG_SkelWallFlat( float vz ) {
	return 1.0f - Com_Clamp( 0, 1, fabs( vz ) / 170.0f );
}

static float CG_SkelWallStance( float speed ) {
	return Com_Clamp( 0.2f, 0.35f, 110.0f / ( speed > 1 ? speed : 1 ) );
}

static float CG_SkelWallStep( float speed ) {
	return Com_Clamp( SKEL_WALL_STEP, 20.0f, SKEL_WALL_STEP + ( speed - 320.0f ) * 0.015f );
}

void CG_SkelWallStride( refEntity_t *ent, const vec3_t wallPt, const vec3_t moveLeft, const vec3_t vel, int wall, float a, float weight ) {
	float	pace = sqrt( vel[0] * vel[0] + vel[1] * vel[1] ), stance, step;
	static const char	*spine[3] = { "spine_01", "spine_02", "spine_03" };
	orientation_t	tag;
	vec3_t			fwd, pt, off, anim, up = { 0, 0, 1 }, mfwd = { 1, 0, 0 };
	float			u, along, lift;
	int				i, leg;

	// the stride runs along the actual path: up the wall while rising, down it
	// while sliding off, so a planted foot holds still in the world
	VectorSet( fwd, moveLeft[1], -moveLeft[0], 0 );
	VectorScale( fwd, sqrt( vel[0] * vel[0] + vel[1] * vel[1] ), fwd );
	fwd[2] = vel[2];
	if ( VectorNormalize( fwd ) < 1.0f ) {
		VectorSet( fwd, moveLeft[1], -moveLeft[0], 0 );
	}
	VectorScale( moveLeft, -wall, off );
	ent->origin[2] += 2.5f * cos( 2.0f * a ) * weight;
	VectorCopy( ent->origin, ent->oldorigin );

	for ( i = 0; i < 3; i++ ) {
		if ( trap_R_LerpTag( &tag, ent->hModel, ent->oldframe, ent->frame, 1.0f - ent->backlerp, spine[i] ) ) {
			// curve back to upright, rocking over the leg that's down
			ent->bonePose = CG_BoneRotation( ent, ent->bonePose, spine[i], &tag, mfwd,
				DEG2RAD( -wall * ( 9.0f + 3.0f * sin( 2.0f * a ) ) * weight ) );
			if ( i == 1 ) {
				ent->bonePose = CG_BoneRotation( ent, ent->bonePose, spine[i], &tag, up, DEG2RAD( 18.0f ) * sin( a ) * weight );
			}
		}
	}

	// the arm away from the wall flies out for balance, swinging with the stride
	{
		const char	*up_a = wall > 0 ? "upperarm_r" : "upperarm_l";
		const char	*lo_a = wall > 0 ? "lowerarm_r" : "lowerarm_l";
		const char	*ha_a = wall > 0 ? "hand_r" : "hand_l";
		vec3_t		sh, hand, t;

		if ( CG_JointWorld( ent, up_a, sh ) && CG_JointWorld( ent, ha_a, hand ) ) {
			VectorMA( sh, 24.0f, off, t );
			VectorMA( t, -4.0f + 15.0f * sin( a ), fwd, t );
			t[2] -= 3.0f + 6.0f * cos( 2.0f * a );
			VectorSubtract( t, hand, t );
			VectorMA( hand, weight, t, t );
			ent->bonePose = CG_LimbToWorld( ent, ent->bonePose, up_a, lo_a, ha_a, t );
		}
	}

	// Running, not walking: each foot is down for SKEL_WALL_STANCE of the cycle
	// and there's a flight phase between steps. Cadence is set by speed (see the
	// sprint rate) so the planted foot holds still on the wall. Feet sit high and
	// close to the hips: at wall-hug distance that's what keeps the knees bent.
	stance = CG_SkelWallStance( pace );
	step = CG_SkelWallStep( pace );
	for ( leg = 0; leg < 2; leg++ ) {
		float	bend;

		u = fmod( a / ( 2.0f * M_PI ) + leg * 0.5f, 1.0f );
		if ( u < 0 ) {
			u += 1.0f;
		}
		if ( u < stance ) {		// stance: planted, travels backward
			along = step - 2.0f * step * ( u / stance );
			lift = 0;
		} else {							// swing: heel to hip, then reach forward
			float	t = ( u - stance ) / ( 1.0f - stance );

			// foot speed at the seams, partly matched: all of it overshoots into a hard reversal
			float	m = -0.8f * step / stance * ( 1.0f - stance );

			// Hermite from foot-off to foot-down, so the foot eases through each change
			along = ( 2*t*t*t - 3*t*t + 1 ) * -step + ( t*t*t - 2*t*t + t ) * m
				+ ( -2*t*t*t + 3*t*t ) * step + ( t*t*t - t*t ) * m;
			lift = sin( t * M_PI );
			lift *= lift;
		}
		bend = 1.0f - Com_Clamp( 0, 1, along * along / ( step * step ) );	// mid-stance the knee takes the load
		VectorCopy( wallPt, pt );
		VectorMA( pt, along + SKEL_WALL_REACH, fwd, pt );
		VectorMA( pt, 2.0f + 11.0f * lift + 3.0f * bend * ( 1.0f - lift ), off, pt );
		pt[2] += -13.0f + 7.0f * CG_SkelWallFlat( vel[2] ) + 11.0f * lift + 3.0f * bend * ( 1.0f - lift );
		// never past a bent knee: a target out of reach locks the leg dead straight
		if ( CG_JointWorld( ent, leg ? "thigh_r" : "thigh_l", anim ) ) {
			vec3_t	reach;
			float	dist;

			VectorSubtract( pt, anim, reach );
			dist = VectorLength( reach );
			// a soft limit: a hard stop would put a corner in the foot's path
			if ( dist > SKEL_WALL_LEG - 4.0f ) {
				float	d0 = SKEL_WALL_LEG - 4.0f;

				VectorMA( anim, ( d0 + 4.0f * ( 1.0f - exp( -( dist - d0 ) / 4.0f ) ) ) / dist, reach, pt );
			}
		}
		if ( weight < 1.0f && CG_JointWorld( ent, leg ? "foot_r" : "foot_l", anim ) ) {
			VectorSubtract( pt, anim, pt );
			VectorMA( anim, weight, pt, pt );
		}
		ent->bonePose = CG_LimbToWorld( ent, ent->bonePose, leg ? "thigh_r" : "thigh_l",
			leg ? "calf_r" : "calf_l", leg ? "foot_r" : "foot_l", pt );
		CG_SkelFootFlat( ent, leg ? "foot_r" : "foot_l", off, ( 1.0f - lift ) * weight );
	}
}

#include "cg_robotscarf.h"

// Keep cloth outside a capsule following the animated torso. The radius includes
// the ribbon's half-width, so its edges also clear the body.
static void CG_ScarfCollide( vec3_t p, vec3_t velocity, const vec3_t a,
        const vec3_t b, const vec3_t back, float radius ) {
    vec3_t axis, delta, closest;
    float length2, along, distance, inward;
    VectorSubtract( b, a, axis );
    VectorSubtract( p, a, delta );
    length2 = DotProduct( axis, axis );
    along = length2 > 0.001f ? Com_Clamp( 0, 1, DotProduct( delta, axis ) / length2 ) : 0;
    VectorMA( a, along, axis, closest );
    VectorSubtract( p, closest, delta );
    distance = VectorNormalize( delta );
    if ( distance >= radius ) return;
    if ( distance < 0.001f ) VectorCopy( back, delta );
    VectorMA( closest, radius, delta, p );
    inward = DotProduct( velocity, delta );
    if ( inward < 0 ) VectorMA( velocity, -inward, delta, velocity );
}

static void CG_SkelScarf( refEntity_t *ent, playerEntity_t *pe, int renderfx ) {
    vec3_t root, neck, pelvis, back, wind, flutter, d, target, socketSide;
    orientation_t socketTag;
    vec3_t sides[8], ridge[8];
    float widths[8], ripple, dt = Com_Clamp( 0, 0.1f, cg.animDelta );
    qboolean robot = !Q_stricmp( cg_skelBody.string, "models/s64/robot.iqm" );
    qboolean collide;
    qhandle_t shader;
    int i, j, step, steps, pass, strip;
    float h;
    polyVert_t v[4];

    if ( ( renderfx & RF_THIRD_PERSON ) || !CG_JointWorld( ent, "neck_01", neck ) ) return;
    collide = robot && CG_JointWorld( ent, "pelvis", pelvis );
    VectorScale( ent->axis[0], -1, back );
    VectorCopy( neck, root );
    if ( robot ) {
        if ( !trap_R_BoneTag( &socketTag, ent->hModel, ent->frame, ent->oldframe,
                ent->backlerp, ent->bonePose, "neck_01" ) ) return;
        VectorCopy( socketTag.origin, target );
        VectorClear( d );
        VectorClear( wind );
        for ( i = 0; i < 3; i++ ) {
            VectorMA( target, robotScarfSocket[i], socketTag.axis[i], target );
            VectorMA( d, robotScarfSide[i], socketTag.axis[i], d );
            VectorMA( wind, robotScarfBack[i], socketTag.axis[i], wind );
        }
        VectorCopy( ent->origin, root );
        VectorClear( socketSide );
        VectorClear( back );
        for ( i = 0; i < 3; i++ ) {
            VectorMA( root, target[i], ent->axis[i], root );
            VectorMA( socketSide, d[i], ent->axis[i], socketSide );
            VectorMA( back, wind[i], ent->axis[i], back );
        }
        VectorNormalize( socketSide );
        VectorNormalize( back );
        // The short folded cuff uses the same fabric as the tail, avoiding
        // the armour's normal map and bright character rim on this small join.
        {
            vec3_t joinWorld[16], local;
            polyVert_t face[3];
            int vertex, triangle, axis;
            qhandle_t joinShader = trap_R_RegisterShader( "models/s64/robot_cloth_join" );
            for ( vertex = 0; vertex < 16; vertex++ ) {
                VectorCopy( socketTag.origin, local );
                for ( axis = 0; axis < 3; axis++ )
                    VectorMA( local, robotScarfJoinVerts[vertex][axis], socketTag.axis[axis], local );
                VectorCopy( ent->origin, joinWorld[vertex] );
                for ( axis = 0; axis < 3; axis++ )
                    VectorMA( joinWorld[vertex], local[axis], ent->axis[axis], joinWorld[vertex] );
            }
            for ( triangle = 0; triangle < sizeof(robotScarfJoinTris)/sizeof(robotScarfJoinTris[0]); triangle++ ) {
                memset( face, 0, sizeof(face) );
                for ( vertex = 0; vertex < 3; vertex++ ) {
                    int n = robotScarfJoinTris[triangle][vertex];
                    VectorCopy( joinWorld[n], face[vertex].xyz );
                    face[vertex].st[0] = ( n % 4 ) / 3.0f;
                    face[vertex].st[1] = ( n / 4 ) / 3.0f;
                    face[vertex].modulate[0] = 190;
                    face[vertex].modulate[1] = 130;
                    face[vertex].modulate[2] = 40;
                    face[vertex].modulate[3] = 255;
                }
                trap_R_AddPolyToScene( joinShader, 3, face );
            }
        }
    }
    VectorSubtract( root, pe->skelScarf[0], d );
    if ( !pe->skelScarfTime || cg.time < pe->skelScarfTime ||
            cg.time - pe->skelScarfTime > 500 || VectorLength( d ) > 64 ) {
        for ( i = 0; i < 8; i++ ) {
            VectorMA( root, -3.6f * i, ent->axis[2], pe->skelScarf[i] );
            VectorClear( pe->skelScarfOld[i] );
        }
    }
    pe->skelScarfTime = cg.time;
    VectorScale( pe->skelVelF, -0.0035f, wind );
    wind[2] -= 1;
    VectorMA( wind, 0.18f, back, wind );
    VectorNormalize( wind );
    VectorCopy( robot ? socketSide : ent->axis[1], flutter );
    ripple = Com_Clamp( 0.04f, 0.55f, VectorLength( pe->skelVelF ) * 0.0012f );
    steps = (int)ceil( dt * 120.0f );
    if ( steps < 1 ) steps = 1;
    h = dt / steps;
    for ( step = 0; step < steps; step++ ) {
        VectorCopy( root, pe->skelScarf[0] );
        for ( i = 1; i < 8; i++ ) {
            float w = 34.0f - i * 3.0f;
            VectorMA( pe->skelScarf[i-1], 3.6f, wind, target );
            VectorMA( target, ripple * i * sin( cg.time * 0.006f - i * 0.8f ), flutter, target );
            VectorSubtract( target, pe->skelScarf[i], d );
            VectorMA( pe->skelScarfOld[i], w*w*h, d, pe->skelScarfOld[i] );
            VectorScale( pe->skelScarfOld[i], 1.0f/(1.0f+2.0f*0.6f*w*h), pe->skelScarfOld[i] );
            VectorMA( pe->skelScarf[i], h, pe->skelScarfOld[i], pe->skelScarf[i] );
        }
        for ( pass = 0; pass < 4; pass++ ) {
            for ( i = 1; i < 8; i++ ) {
                VectorSubtract( pe->skelScarf[i], pe->skelScarf[i-1], d );
                if ( VectorNormalize( d ) > 0.001f )
                    VectorMA( pe->skelScarf[i-1], 3.6f, d, pe->skelScarf[i] );
                if ( collide ) CG_ScarfCollide( pe->skelScarf[i], pe->skelScarfOld[i],
                        pelvis, neck, back, 10.5f );
            }
        }
    }
    // Shared edges and a shallow central fold. Width follows the body rather
    // than billboarding toward the camera, so orbiting cannot push cloth inside.
    for ( i = 0; i < 8; i++ ) {
        VectorSubtract( pe->skelScarf[i < 7 ? i+1 : i], pe->skelScarf[i > 0 ? i-1 : i], d );
        if ( VectorNormalize( d ) < 0.001f ) VectorScale( ent->axis[2], -1, d );
        VectorMA( ent->axis[1], -DotProduct( ent->axis[1], d ), d, sides[i] );
        if ( VectorNormalize( sides[i] ) < 0.01f ) {
            CrossProduct( d, back, sides[i] );
            VectorNormalize( sides[i] );
        }
        if ( robot && i == 0 ) VectorCopy( socketSide, sides[i] );
        widths[i] = ( robot ? 2.4f : 1.5f ) * ( 1.0f - i * 0.055f );
        VectorMA( pe->skelScarf[i], ( i ? 0.35f : 0.0f ) * sin( i * 0.9f + cg.time * 0.002f ), back, ridge[i] );
    }
    shader = robot ? trap_R_RegisterShader( "models/s64/robot_cloth" ) : cgs.media.whiteShader;
    for ( i = 0; i < 7; i++ ) for ( strip = 0; strip < 2; strip++ ) {
        memset( v, 0, sizeof(v) );
        for ( j = 0; j < 4; j++ ) {
            int n = i + ( j >= 2 );
            int edge = strip + ( j == 1 || j == 2 );
            if ( edge == 1 ) VectorCopy( ridge[n], v[j].xyz );
            else VectorMA( pe->skelScarf[n], edge == 0 ? widths[n] : -widths[n], sides[n], v[j].xyz );
            v[j].st[0] = edge * 0.5f;
            v[j].st[1] = n / 7.0f;
            v[j].modulate[0] = robot ? (strip ? 174 : 200) : 200;
            v[j].modulate[1] = robot ? (strip ? 116 : 140) : 24;
            v[j].modulate[2] = robot ? 40 : 40;
            v[j].modulate[3] = 255;
        }
        trap_R_AddPolyToScene( shader, 4, v );
    }
}

// the flip clip runs on into a recovery the game doesn't want: stop at the tuck-out
static float CG_SkelEnd( int clip ) {
	if ( clip == SKEL_RUN_FLIP ) {
		return 37.0f;
	}
	if ( clip == SKEL_NINJAJUMP_START ) {
		return 20.0f;			// the leap is done; the rest is a long hold in the air
	}
	if ( clip == SKEL_JUMP_START ) {
		return 22.0f;
	}
	if ( clip == SKEL_HARD_LAND ) {
		return 42.0f;			// up off the knee; the clip's long stand-up isn't needed
	}
	return skelClips[clip].count - 1;
}

static qboolean CG_SkelOneShot( int clip ) {
	return !skelClips[clip].loop;
}

// which side a wall is on (+1 left, -1 right, 0 none), for wall runs
static int CG_SkelWallSide( centity_t *cent, const vec3_t left, const vec3_t vel, vec3_t point ) {
	trace_t	tr;
	vec3_t	end;
	int		side;

	for ( side = 1; side >= -1; side -= 2 ) {
		VectorMA( cent->lerpOrigin, 40 * side, left, end );
		CG_Trace( &tr, cent->lerpOrigin, NULL, NULL, end, cent->currentState.number, MASK_SOLID );
		// running along it, not flying into or away from it
		if ( tr.fraction < 1.0f && fabs( tr.plane.normal[2] ) < 0.3f
			&& fabs( DotProduct( tr.plane.normal, vel ) ) <= 0.5f * VectorLength( vel ) ) {
			VectorCopy( tr.endpos, point );
			return side;
		}
	}
	return 0;
}

static void CG_SkeletalPlayer( centity_t *cent, vec3_t axis[3], int renderfx ) {
	qhandle_t		model;
	entityState_t	*s = &cent->currentState;
	playerEntity_t	*pe = &cent->pe;
	refEntity_t		ent;
	vec3_t			angles, accel, vel;
	float			speed, rate, fade, fadeMs, k, dt, wantPitch, wantRoll, up;
	int				clip, f, cur, legs = s->legsAnim & ~ANIM_TOGGLEBIT;
	int				wall = 0, ground = ( s->groundEntityNum != ENTITYNUM_NONE );
	qboolean		held, wallrun, grapple;
	vec3_t			moveLeft;
	char			dbg[8];
	int				hold = 0;
	qboolean		kicking, back, near;
	float			wallW = 0, pivot = 0, gait;

	// the fine detail is for bodies you can see the feet of
	{
		vec3_t	d;

		VectorSubtract( cent->lerpOrigin, cg.refdef.vieworg, d );
		near = s->clientNum == cg.snap->ps.clientNum || VectorLength( d ) < 320.0f;
	}
	model = cgs.media.skelModel;
	dt = cg.animDelta;
	VectorCopy( s->pos.trDelta, vel );
	speed = sqrt( vel[0] * vel[0] + vel[1] * vel[1] );
	// acceleration from a filtered velocity, per second: not per frame, and not
	// the step a network snapshot makes
	{
		vec3_t	before;
		float	vk = 1.0f - exp( -dt * 14.0f );

		VectorCopy( pe->skelVelF, before );
		VectorMA( before, vk, vel, pe->skelVelF );
		VectorMA( pe->skelVelF, -vk, before, pe->skelVelF );
		VectorSubtract( pe->skelVelF, before, accel );
		VectorScale( accel, dt > 0.0000001f ? 1.0f / dt : 0, accel );
	}
	gait = sqrt( pe->skelVelF[0] * pe->skelVelF[0] + pe->skelVelF[1] * pe->skelVelF[1] );
	cur = pe->skelClip;
	// the game's own wall-run state, where we have it (the local or followed player)
	wallrun = ( s->frame & 3 ) != 0;
	grapple = ( s->frame & 4 ) != 0;
	if ( s->clientNum == cg.snap->ps.clientNum ) {
		wallrun = cg.predictedPlayerState.stats[STAT_WALLRUN] != 0;
		grapple = ( cg.predictedPlayerState.pm_flags & PMF_GRAPPLE_PULL ) != 0;
	}
	if ( speed > 1 ) {
		VectorSet( moveLeft, -vel[1] / speed, vel[0] / speed, 0 );
	} else {
		VectorCopy( axis[1], moveLeft );
	}
	held = CG_SkelOneShot( cur ) && pe->skelPhase < CG_SkelEnd( cur ) - 0.5f;

	// take-offs and flips belong to the air: touching down releases them at once
	if ( ground && ( cur == SKEL_RUN_FLIP || cur == SKEL_NINJAJUMP_START || cur == SKEL_JUMP_START ) ) {
		held = qfalse;
	}

	// once the impact is absorbed, moving off breaks out of a landing
	if ( ( cur == SKEL_HARD_LAND || cur == SKEL_FALL_ROLL ) && pe->skelPhase > 18 && gait > 120 ) {
		held = qfalse;
	}

	// loops: what the body is doing right now
	if ( ( s->eFlags & EF_SLIDING ) && speed > ( cur == SKEL_SLIDE_LOOP ? 120 : 170 ) ) {
		clip = SKEL_SLIDE_LOOP;
	} else if ( grapple ) {
		clip = SKEL_NINJAJUMP_IDLE_LOOP;
	} else if ( wallrun ) {
		wall = CG_SkelWallSide( cent, moveLeft, vec3_origin, pe->skelWallPt );
		clip = SKEL_SPRINT_LOOP;
		if ( cg_skelWallClip.integer && wall ) {
			clip = wall > 0 ? SKEL_WALL_RUN_L : SKEL_WALL_RUN_R;
		}
	} else if ( !ground ) {
		if ( pe->skelGround ) {
			pe->skelAirStart = cg.time;
		}
		// bunny hops are part of the run: stay in stride through short air time
		if ( cur == SKEL_SPRINT_LOOP && cg.time - pe->skelAirStart < ( speed > 300 ? 650 : 350 ) ) {
			clip = SKEL_SPRINT_LOOP;
		} else {
			clip = speed > 500 ? SKEL_NINJAJUMP_IDLE_LOOP : SKEL_JUMP_LOOP;
		}
	} else if ( legs == LEGS_IDLECR || legs == LEGS_WALKCR || legs == LEGS_BACKCR || ( s->eFlags & EF_SLIDING ) ) {
		clip = speed > 15 ? SKEL_CROUCH_FWD_LOOP : SKEL_CROUCH_IDLE_LOOP;
	} else if ( gait > ( cur == SKEL_SPRINT_LOOP ? 160 : 210 ) ) {
		clip = SKEL_SPRINT_LOOP;		// filtered: a landing's one-frame dip can't change gait
	} else if ( gait > ( cur == SKEL_WALK_LOOP || cur == SKEL_SPRINT_LOOP ? 15 : 35 ) ) {
		clip = SKEL_WALK_LOOP;
	} else {
		clip = SKEL_IDLE_LOOP;
	}

	// one-shots: the transitions that sell the move. They hold until done
	// unless a stronger action interrupts them.
	if ( pe->skelEvent ) {
		if ( pe->skelEvent == EV_DASH ) {
			pe->skelDashTime = cg.time;		// a body lunge, not a clip: the dash isn't a cut
			if ( ground ) {
				pe->skelDropV = 170.0f;		// drive down into it, the spring recoils out
			}
		} else if ( pe->skelEvent == EV_VAULT ) {
			vec3_t	f, start, stop;
			trace_t	tr;

			clip = SKEL_CLIMBUP_1M;
			// find the real lip for the hands
			VectorSet( f, moveLeft[1], -moveLeft[0], 0 );
			VectorCopy( cent->lerpOrigin, start );
			VectorMA( start, 34, f, start );
			start[2] += pe->skelEventParm + 40;
			VectorCopy( start, stop );
			stop[2] -= pe->skelEventParm + 70;
			CG_Trace( &tr, start, NULL, NULL, stop, s->number, MASK_SOLID );
			pe->skelVaultHit = tr.fraction < 1.0f && !tr.startsolid;
			VectorCopy( tr.endpos, pe->skelVaultPt );
			VectorCopy( moveLeft, pe->skelVaultSide );
		} else if ( pe->skelEvent == EV_DOUBLE_JUMP ) {
			// a repeat press mid-flip lets the flip finish instead of restarting it
			clip = cur == SKEL_RUN_FLIP && pe->skelPhase < 30 ? cur : SKEL_RUN_FLIP;
		} else if ( pe->skelEvent == EV_WALLJUMP ) {
			clip = SKEL_NINJAJUMP_START;
		} else if ( speed < 200 ) {
			clip = SKEL_JUMP_START;
		}
		// a flip in progress runs out: restarting it snaps the body upright mid-spin
		if ( ( pe->skelEvent == EV_WALLJUMP || pe->skelEvent == EV_DOUBLE_JUMP )
				&& cg.time - pe->skelFlipTime >= 450 ) {
			if ( pe->skelEvent == EV_WALLJUMP ) {	// the double jump's flip is now a clip
				pe->skelFlipTime = cg.time;
			}
			pe->skelFlipKind = pe->skelEvent;
		}
		pe->skelEvent = 0;
		held = qfalse;
	} else if ( ground && !pe->skelGround ) {
		// at real speed there's no stopping: the landing spring absorbs it and the
		// run carries on. Rolls and the heavy landing are for arriving with less pace
		if ( pe->skelVel[2] < -650 && speed > 200 && speed < 480 ) {
			clip = SKEL_FALL_ROLL;
			held = qfalse;
		} else if ( pe->skelVel[2] < -600 && speed <= 200 ) {
			clip = SKEL_HARD_LAND;
			held = qfalse;
		} else if ( pe->skelVel[2] < -300 && speed < 200 ) {
			clip = SKEL_JUMP_LAND;
			held = qfalse;
		}
	} else if ( clip == SKEL_SLIDE_LOOP && cur != SKEL_SLIDE_LOOP && cur != SKEL_SLIDE_START ) {
		clip = SKEL_SLIDE_START;
	} else if ( clip != SKEL_SLIDE_LOOP && cur == SKEL_SLIDE_LOOP ) {
		clip = SKEL_SLIDE_EXIT;
	}
	if ( held ) {
		clip = cur;
	}
	// loops hold for a moment: a gait that lasts a few frames is just noise
	if ( clip != cur && !CG_SkelOneShot( clip ) && !CG_SkelOneShot( cur ) && cg.time - pe->skelStart < 140
			&& ( clip == SKEL_WALK_LOOP || clip == SKEL_SPRINT_LOOP || clip == SKEL_IDLE_LOOP ) ) {
		clip = cur;
	}

	// advance; fast moves get quick fades, settling moves soft ones
	if ( clip != cur ) {
		trap_Cvar_VariableStringBuffer( "cg_skelDebug", dbg, sizeof( dbg ) );
		if ( atoi( dbg ) ) {
			Com_Printf( "SKEL t=%d cl=%d %d->%d speed=%.0f vz=%.0f ground=%d wall=%d\n",
				cg.time, s->clientNum, cur, clip, speed, vel[2], ground, wall );
		}
		{
			int		fromF = (int)pe->skelPhase, toF;
			float	fromB = 1.0f - ( pe->skelPhase - fromF ), toB;
			int		fromFirst = skelClips[cur].first, fromN = skelClips[cur].count;

			pe->skelClip = clip;
			// walk and sprint share a gait: keep the foot that's already down
			if ( ( clip == SKEL_SPRINT_LOOP && cur == SKEL_WALK_LOOP ) || ( clip == SKEL_WALK_LOOP && cur == SKEL_SPRINT_LOOP ) ) {
				pe->skelPhase *= (float)( skelClips[clip].count - 1 ) / ( skelClips[cur].count - 1 );
			} else {
				pe->skelPhase = clip == SKEL_RUN_FLIP ? 9 : clip == SKEL_FALL_ROLL ? 4 : clip == SKEL_HARD_LAND ? 3 : 0;
			}
			toF = (int)pe->skelPhase;
			toB = 1.0f - ( pe->skelPhase - toF );
			trap_R_BoneInertiaCapture( model, s->clientNum,
				fromFirst + ( fromF + 1 < fromN ? fromF + 1 : fromF ), fromFirst + fromF, fromB,
				skelClips[clip].first + ( toF + 1 < skelClips[clip].count ? toF + 1 : toF ), skelClips[clip].first + toF, toB,
				fromFirst + ( fromF + 2 < fromN ? fromF + 2 : fromN - 1 ),
				skelClips[clip].first + ( toF + 2 < skelClips[clip].count ? toF + 2 : skelClips[clip].count - 1 ) );
			pe->skelStart = cg.time;
			// snappy into action, soft into rest
			pe->skelInertK = ( clip == SKEL_IDLE_LOOP || clip == SKEL_CROUCH_IDLE_LOOP ) ? 9.0f
				: ( clip == SKEL_SPRINT_LOOP || clip == SKEL_WALK_LOOP ) ? 11.0f : 15.0f;
		}
	}
	rate = 1.0f;
	if ( clip == SKEL_SPRINT_LOOP ) {
		rate = Com_Clamp( 0.8f, 3.0f, ( speed > 1 ? speed : SKEL_SPRINT_SPEED ) / SKEL_SPRINT_SPEED );
		if ( wall ) {
			// cycle length on the wall = stride / stance fraction, so the foot sticks
			float	cycle = 2.0f * CG_SkelWallStep( gait ) / CG_SkelWallStance( gait );

			rate = Com_Clamp( 1.0f, 2.6f, speed / cycle * ( skelClips[clip].count - 1 ) / 30.0f );
		}
	} else if ( clip == SKEL_WALK_LOOP ) {
		rate = Com_Clamp( 0.7f, 1.8f, speed / 110.0f );
	} else if ( CG_SkelOneShot( clip ) && clip != SKEL_SWORD_BLOCK ) {
		rate = clip == SKEL_RUN_FLIP ? 1.7f : clip == SKEL_NINJAJUMP_START || clip == SKEL_JUMP_START ? 1.5f
			: clip == SKEL_FALL_ROLL || clip == SKEL_HARD_LAND ? 1.6f : 1.2f;
	}
	// speed steps (a hop, a network update) mustn't kick the cycle's tempo
	pe->skelRate += ( rate - pe->skelRate ) * ( 1.0f - exp( -dt * 7.0f ) );
	if ( skelClips[clip].loop ) {
		rate = pe->skelRate;
	}
	// moving backward: same gait, cycle played in reverse
	{
		float	along = speed > 20 ? DotProduct( vel, axis[0] ) / speed : 0;

		if ( along < -0.4f ) {
			pe->skelBack = qtrue;
		} else if ( along > -0.1f ) {
			pe->skelBack = qfalse;
		}
	}
	back = pe->skelBack && ground && ( clip == SKEL_SPRINT_LOOP || clip == SKEL_WALK_LOOP );
	pe->skelPhase += dt * 30.0f * ( back ? -rate : rate );
	if ( clip == SKEL_WALL_RUN_L || clip == SKEL_WALL_RUN_R ) {
		// the wall stretch of the clip repeats for as long as the ride lasts
		if ( pe->skelPhase > 21.0f ) {
			pe->skelPhase = 11.0f + fmod( pe->skelPhase - 21.0f, 10.0f );
		}
	} else if ( skelClips[clip].loop ) {
		pe->skelPhase = fmod( pe->skelPhase, skelClips[clip].count - 1 );
		if ( pe->skelPhase < 0 ) {
			pe->skelPhase += skelClips[clip].count - 1;
		}
	} else if ( pe->skelPhase > CG_SkelEnd( clip ) - 0.01f ) {
		pe->skelPhase = CG_SkelEnd( clip ) - 0.01f;
	}
	f = (int)pe->skelPhase;

	// lean: into acceleration, and hard over against a wall; eased so it flows
	wantPitch = Com_Clamp( -12, 16, speed * 0.02f + DotProduct( accel, axis[0] ) * 0.006f );
	if ( back ) {
		wantPitch = -wantPitch * 0.5f;
	}
	wantPitch += pe->skelDrop * 1.1f;		// fold over the knees as the hips sink
	wantRoll = Com_Clamp( -14, 14, DotProduct( accel, axis[1] ) * -0.008f );
	if ( cg.time - pe->skelDashTime < 220 ) {
		wantPitch = 24.0f;
	}
	if ( wall ) {
		// flat runs lay the body over further than climbs: the legs have to drive
		// sideways into the wall, not stand down it
		wantRoll = cg_skelWallClip.integer ? 0 : wall * ( SKEL_WALL_TILT + 16.0f * CG_SkelWallFlat( pe->skelVelF[2] ) );
		// lean along the actual path: into a climb, back on the way down
		wantPitch = Com_Clamp( -10, 14, RAD2DEG( atan2( vel[2], speed > 1 ? speed : 1 ) ) * 0.3f + 3.0f );
	}
	if ( !ground && !wall ) {
		wantPitch *= 0.5f;
	}
	// the tilt target itself is rate limited: attaching steps it by 60 degrees
	{
		float	maxStep = 240.0f * dt;

		pe->skelRollT += Com_Clamp( -maxStep, maxStep, wantRoll - pe->skelRollT );
		wantRoll = pe->skelRollT;
	}
	if ( dt > 0.0000001f ) {
		float	want[2], h = dt < 0.03f ? dt : 0.03f;
		int		a;

		want[0] = wantPitch;
		want[1] = wantRoll;
		for ( a = 0; a < 2; a++ ) {
			CG_SkelSecondOrder( &pe->skelLean[a], &pe->skelLeanV[a], want[a], h );
		}
	}

	memset( &ent, 0, sizeof( ent ) );
	ent.hModel = model;
	VectorCopy( cent->lerpOrigin, ent.origin );
	ent.origin[2] -= 24;
	// the mocap's feet reach out to the wall: stand the body off it so they land on
	// the surface instead of through it
	pe->skelWallOff += ( ( wall ? ( cg_skelWallClip.integer ? 1.0f : 0.7f * CG_SkelWallFlat( pe->skelVelF[2] ) ) : 0.0f )
		- pe->skelWallOff ) * ( 1.0f - exp( -dt * 14.0f ) );
	if ( pe->skelWallOff > 0.01f ) {
		float	away = pe->skelWallSide * -SKEL_WALL_CLIP_OFF * pe->skelWallOff;

		VectorMA( ent.origin, away, moveLeft, ent.origin );
	}
	if ( wall ) {
		pe->skelWallSide = wall;
	}
	VectorCopy( ent.origin, ent.oldorigin );
	VectorCopy( cent->lerpOrigin, ent.lightingOrigin );
	angles[PITCH] = pe->skelLean[0];
	vectoangles( axis[0], accel );		// accel is spent; reuse for the facing
	// the body turns to its facing (or along the wall) instead of snapping there
	{
		float	want = wall ? RAD2DEG( atan2( vel[1], vel[0] ) ) : accel[YAW];

		pe->skelBodyYaw = AngleNormalize180( pe->skelBodyYaw
			+ AngleNormalize180( want - pe->skelBodyYaw ) * ( 1.0f - exp( -dt * ( wall ? 12.0f : 24.0f ) ) ) );
		angles[YAW] = pe->skelBodyYaw;
	}
	angles[ROLL] = pe->skelLean[1];
	// authored in code: spins around the body's centre. Air and wall jumps flip or
	// corkscrew; a sword cut in the air becomes a whole-body spin
	{
		// one spin at a time: whatever starts one owns the body until it completes,
		// because restarting snaps the body upright from mid-rotation
		float	spin = 0, ft;
		int		spinAxis;

		if ( !pe->skelSpinMs || cg.time - pe->skelSpinStart >= pe->skelSpinMs ) {
			pe->skelSpinMs = 0;
			// a small rotating set, so no two in a row match: yaw corkscrews either way
			// and a barrel roll. Air cuts only spin on a finisher, and every other one
			static const struct { int axis; float dir; int ms; } moves[4] = {
				{ YAW, 1, 520 }, { ROLL, 1, 560 }, { YAW, -1, 480 }, { ROLL, -1, 600 } };
			int		pick = -1;

			if ( pe->skelFlipTime == cg.time ) {
				pick = pe->skelSpinNext++;
			} else if ( pe->kickTime && ( pe->kickFlags & 1 ) && cg.time - pe->kickTime < 40 ) {
				pick = pe->flipDir > 0 ? 0 : 2;
			} else if ( !ground && !wall && s->weapon == WP_SWORD && cent->swordSwingStep % 3 == 2
					&& cg.time >= cent->swordSwingTime && cg.time - cent->swordSwingTime < 60
					&& cent->swordSwingTime != pe->skelSpinSwing ) {
				pe->skelSpinSwing = cent->swordSwingTime;
				if ( ( pe->skelSpinCut++ & 1 ) == 0 ) {
					pick = pe->skelSpinNext++;
				}
			}
			if ( pick >= 0 ) {
				pe->skelSpinMs = moves[pick & 3].ms;
				pe->skelSpinAxis = moves[pick & 3].axis;
				pe->skelSpinDir = moves[pick & 3].dir;
			}
			if ( pe->skelSpinMs ) {
				pe->skelSpinStart = cg.time;
			}
		}
		spinAxis = pe->skelSpinAxis;
		if ( pe->skelSpinMs ) {
			ft = ( cg.animTime - pe->skelSpinStart ) / (float)pe->skelSpinMs;
			spin = ft * ft * ft * ( ft * ( ft * 6 - 15 ) + 10 ) * 360.0f * pe->skelSpinDir;
		}
		angles[spinAxis] += spin;
		AnglesToAxis( angles, ent.axis );
		pivot = fabs( sin( DEG2RAD( spin ) * 0.5f ) );
		{
			float	wp = Com_Clamp( 0, 1, fabs( pe->skelLean[1] ) / SKEL_WALL_TILT );

			// follows the eased tilt on the way out too: if this drops with the wall the
			// body swings around its feet in a frame
			wallW = wp * wp * ( 3 - 2 * wp );
			pe->skelWallW += ( ( wall ? 1.0f : 0.0f ) - pe->skelWallW ) * ( 1.0f - exp( -dt * ( wall ? 10.0f : 9.0f ) ) );
			wallW = wall ? wallW : wallW * pe->skelWallW;
			if ( wallW > pivot ) {
				pivot = wallW;
			}
		}
	}
	ent.renderfx = renderfx | RF_LIGHTING_ORIGIN;
	ent.oldframe = skelClips[clip].first + f;
	ent.frame = f + 1 < skelClips[clip].count ? ent.oldframe + 1 : ent.oldframe;
	ent.backlerp = 1.0f - ( pe->skelPhase - f );

	// spins and wall tilt turn around the pelvis of the pose being shown, held at
	// the body's centre, eased in so nothing jumps
	if ( pivot > 0.001f ) {
		orientation_t	tag;
		vec3_t			mid;
		int				i;

		if ( trap_R_LerpTag( &tag, model, ent.oldframe, ent.frame, 1.0f - ent.backlerp, "pelvis" ) ) {
			VectorCopy( cent->lerpOrigin, mid );
			mid[2] += 8;
			for ( i = 0; i < 3; i++ ) {
				VectorMA( mid, -tag.origin[i], ent.axis[i], mid );
			}
			VectorSubtract( mid, ent.origin, mid );
			VectorMA( ent.origin, pivot, mid, ent.origin );
			VectorCopy( ent.origin, ent.oldorigin );
		}
	}

	// inertial transition: the offset from the old pose decays critically
	{
		float	it = ( cg.animTime - pe->skelStart ) * 0.001f * pe->skelInertK;

		// offset and its velocity on a critically damped spring (Bollo's inertialization)
		float	sec = ( cg.animTime - pe->skelStart ) * 0.001f;

		ent.bonePose = trap_R_BoneInertia( model, ent.bonePose, s->clientNum,
			it < 8.0f ? ( 1.0f + it ) * exp( -it ) : 0, it < 8.0f ? sec * exp( -it ) : 0 );
	}

	{
		int		first = skelClips[clip].first, n = skelClips[clip].count;
		int		before = f > 0 ? first + f - 1 : ( skelClips[clip].loop ? first + n - 2 : first );
		int		after = f + 2 < n ? first + f + 2 : ( skelClips[clip].loop ? first + 1 : first + n - 1 );

		ent.bonePose = trap_R_BoneCubic( model, ent.bonePose, before, after );
	}

	// sword on an upper-body layer: arms cut while the legs keep moving
	{
		int		sclip = -1;
		float	sphase = 0, want = 0, sb;
		int		sf;

		if ( s->weapon == WP_SWORD && ( s->eFlags & EF_BLOCKING ) ) {
			sclip = SKEL_SWORD_BLOCK;
			sphase = skelClips[sclip].count * 0.5f;
			want = 1;
		} else if ( s->weapon == WP_SWORD && cg.time >= cent->swordSwingTime
				&& cg.time - cent->swordSwingTime < 480 ) {
			sclip = SKEL_SWORD_REGULAR_A + ( cent->swordSwingStep % 3 );
			sphase = ( cg.animTime - cent->swordSwingTime ) / 480.0f * ( skelClips[sclip].count - 1 );	// whole clip fits the swing
			want = 1;
		} else if ( cg.time - pe->skelDashTime < 260 ) {
			sclip = SKEL_NINJAJUMP_IDLE_LOOP;	// arms sweep back through the burst
			sphase = 12;
			want = 0.45f;
		}
		pe->skelUpper += ( want - pe->skelUpper ) * ( 1.0f - exp( -dt * ( want > 0 ? 26.0f : 11.0f ) ) );
		if ( sclip >= 0 ) {
			float	nphase = Com_Clamp( 0, skelClips[sclip].count - 1.01f, sphase );

			// one cut into the next: carry the arm across instead of cutting to it
			if ( pe->skelUpper > 0.1f && ( sclip != pe->skelClipUp || nphase < pe->skelPhaseUp ) ) {
				int		of = (int)pe->skelPhaseUp, nf = (int)nphase;
				int		oa = skelClips[pe->skelClipUp].first + of, na = skelClips[sclip].first + nf;

				trap_R_BoneInertiaCapture( model, s->clientNum + 64, oa + 1, oa, 1.0f - ( pe->skelPhaseUp - of ),
					na + 1, na, 1.0f - ( nphase - nf ), oa + 1, na + 1 );
				pe->skelUpStart = cg.time;
			}
			pe->skelClipUp = sclip;
			pe->skelPhaseUp = nphase;
		}
		if ( pe->skelUpper > 0.01f ) {
			sf = (int)pe->skelPhaseUp;
			sb = 1.0f - ( pe->skelPhaseUp - sf );
			hold = skelClips[pe->skelClipUp].first + sf;
			ent.bonePose = trap_R_BoneLayer( model, ent.bonePose, "spine_01", hold + 1, hold, sb, pe->skelUpper );
			{
				float	ut = ( cg.time - pe->skelUpStart ) * 0.03f;

				ent.bonePose = trap_R_BoneInertia( model, ent.bonePose, s->clientNum + 64, ut < 8.0f ? ( 1.0f + ut ) * exp( -ut ) : 0, 0 );
			}
		}
	}

	// chest follows the view pitch so the body looks where the blade goes
	{
		orientation_t	tag;
		vec3_t			yaxis = { 0, 1, 0 };

		// the chest trails the hips through turns, then catches up
		float	twist;

		pe->skelYawF = AngleNormalize180( pe->skelYawF + AngleNormalize180( angles[YAW] - pe->skelYawF ) * ( 1.0f - exp( -dt * 10.0f ) ) );
		twist = Com_Clamp( -30, 30, AngleNormalize180( pe->skelYawF - angles[YAW] ) );
		if ( near && trap_R_LerpTag( &tag, model, ent.oldframe, ent.frame, 1.0f - ent.backlerp, "spine_02" ) ) {
			vec3_t	zaxis = { 0, 0, 1 };

			ent.bonePose = CG_BoneRotation( &ent, ent.bonePose, "spine_02", &tag, zaxis, DEG2RAD( twist * 0.6f ) );
		}
		// eased: one-shots own the chest, loops hand it back without a snap
		pe->skelAim += ( ( CG_SkelOneShot( clip ) ? 0.0f : 1.0f ) - pe->skelAim ) * ( 1.0f - exp( -dt * 12.0f ) );
		up = AngleNormalize180( cent->lerpAngles[PITCH] ) * 0.5f * pe->skelAim;
		if ( trap_R_LerpTag( &tag, model, ent.oldframe, ent.frame,
				1.0f - ent.backlerp, "spine_03" ) ) {
			ent.bonePose = CG_BoneRotation( &ent, ent.bonePose, "spine_03", &tag, yaxis, DEG2RAD( up ) );
		}
	}

	// directional cut: the clip keeps its swing and the hand is nudged along the
	// server's start->end compass line, peaking mid-swing. Additive, so it bends
	// the authored motion rather than replacing it; the chest turns into the cut
	if ( s->weapon == WP_SWORD && !( s->eFlags & EF_BLOCKING ) && cg_skelSwingArc.value > 0
			&& cg.time >= cent->swordSwingTime && cg.time - cent->swordSwingTime < 420 ) {
		static const float	qx[SQ_NUM_QUADS] = { 0, 0.7f, 1, 0.7f, 0, -0.7f, -1, -0.7f };
		static const float	qy[SQ_NUM_QUADS] = { 1, 0.7f, 0, -0.7f, -1, -0.7f, 0, 0.7f };
		float			f = ( cg.animTime - cent->swordSwingTime ) / 420.0f;
		float			g = sin( f * M_PI );
		float			rx, ry;
		int				a = cent->swordStartQuad & 7, b = cent->swordEndQuad & 7, i;
		vec3_t			h, t, zaxis = { 0, 0, 1 };
		orientation_t	tag;

		g *= g * cg_skelSwingArc.value;
		f = f * f * ( 3 - 2 * f );
		rx = qx[a] + ( qx[b] - qx[a] ) * f;
		ry = qy[a] + ( qy[b] - qy[a] ) * f;
		if ( trap_R_LerpTag( &tag, model, ent.oldframe, ent.frame, 1.0f - ent.backlerp, "spine_03" ) ) {
			ent.bonePose = CG_BoneRotation( &ent, ent.bonePose, "spine_03", &tag, zaxis, DEG2RAD( -10.0f * rx * g ) );
		}
		if ( trap_R_BoneTag( &tag, model, ent.frame, ent.oldframe, ent.backlerp, ent.bonePose, "hand_r" ) ) {
			VectorCopy( ent.origin, h );
			for ( i = 0; i < 3; i++ ) {
				VectorMA( h, tag.origin[i], ent.axis[i], h );
			}
			VectorMA( h, -9.0f * rx * g, ent.axis[1], t );
			VectorMA( t, 9.0f * ry * g, ent.axis[2], t );
			ent.bonePose = CG_LimbToWorld( &ent, ent.bonePose, "upperarm_r", "lowerarm_r", "hand_r", t );
		}
	}

	// grapple: the free arm pulls on the line
	if ( grapple ) {
		vec3_t	t, d;

		if ( s->clientNum == cg.snap->ps.clientNum ) {
			VectorSubtract( cg.predictedPlayerState.grapplePoint, cent->lerpOrigin, d );
		} else {
			VectorCopy( vel, d );
		}
		VectorNormalize( d );
		VectorCopy( cent->lerpOrigin, t );
		t[2] += 22;
		VectorMA( t, 40, d, t );
		ent.bonePose = CG_LimbToWorld( &ent, ent.bonePose, "upperarm_l", "lowerarm_l", "hand_l", t );
	}

	// kick: the right leg snaps out along the facing and retracts
	kicking = pe->kickTime && cg.time - pe->kickTime < 340;
	if ( kicking ) {
		float	kp = sin( ( cg.animTime - pe->kickTime ) / 340.0f * M_PI );
		vec3_t	hip, t;

		if ( CG_JointWorld( &ent, "thigh_r", hip ) ) {
			VectorMA( hip, 34.0f * kp, ent.axis[0], t );
			VectorMA( t, -30.0f + 34.0f * kp, ent.axis[2], t );
			ent.bonePose = CG_LimbToWorld( &ent, ent.bonePose, "thigh_r", "calf_r", "foot_r", t );
		}
	}

	// crouch and slide poses come at their own height: put the lowest part of the
	// body on the floor
	{
		static const char	*low[4] = { "foot_l", "foot_r", "pelvis", "hand_l" };
		static const float	rad[4] = { 3, 3, 8, 3 };
		float	want = 0;

		if ( ground && ( clip == SKEL_CROUCH_IDLE_LOOP || clip == SKEL_CROUCH_FWD_LOOP || clip == SKEL_SLIDE_LOOP
				|| clip == SKEL_SLIDE_START || clip == SKEL_SLIDE_EXIT ) ) {
			orientation_t	tag;
			int				n, i;

			want = 99;
			for ( n = 0; n < 4; n++ ) {
				if ( trap_R_BoneTag( &tag, model, ent.frame, ent.oldframe, ent.backlerp, ent.bonePose, low[n] ) ) {
					float	z = -rad[n];

					for ( i = 0; i < 3; i++ ) {
						z += tag.origin[i] * ent.axis[i][2];
					}
					if ( z < want ) {
						want = z;
					}
				}
			}
			if ( want == 99 ) {
				want = 0;
			}
		}
		pe->skelFloor += ( want - pe->skelFloor ) * ( 1.0f - exp( -dt * 16.0f ) );
		ent.origin[2] -= pe->skelFloor;
		VectorCopy( ent.origin, ent.oldorigin );
	}

	// vault: both hands reach the real lip, then push off it
	if ( clip == SKEL_CLIMBUP_1M && pe->skelVaultHit ) {
		float	p = pe->skelPhase / ( skelClips[clip].count - 1 );

		if ( p < 0.65f ) {
			float	w = sin( p / 0.65f * M_PI );
			vec3_t	h, t;
			int		side;

			for ( side = 0; side < 2; side++ ) {
				if ( CG_JointWorld( &ent, side ? "hand_r" : "hand_l", h ) ) {
					VectorMA( pe->skelVaultPt, side ? -11.0f : 11.0f, pe->skelVaultSide, t );
					t[2] += 2;
					VectorSubtract( t, h, t );
					VectorMA( h, w, t, t );
					ent.bonePose = CG_LimbToWorld( &ent, ent.bonePose, side ? "upperarm_r" : "upperarm_l",
						side ? "lowerarm_r" : "lowerarm_l", side ? "hand_r" : "hand_l", t );
				}
			}
		}
	}

	// landing: an underdamped spring. The hips sink with the impact, feet stay
	// put so the knees take it, then the body rebounds past standing and settles
	{
		const float	w = 10.0f, zeta = 0.42f;
		float		h = dt < 0.03f ? dt : 0.03f;

		if ( ground && !pe->skelGround && pe->skelVel[2] < -180 ) {
			pe->skelDropV = Com_Clamp( 0, 420, -pe->skelVel[2] * 0.55f );
		}
		pe->skelDropV += ( -w * w * pe->skelDrop - 2 * zeta * w * pe->skelDropV ) * h;
		pe->skelDrop += pe->skelDropV * h;
		pe->skelDrop = Com_Clamp( -4, 24, pe->skelDrop );
		if ( fabs( pe->skelDrop ) > 0.05f && ground ) {
			vec3_t	fl, fr;

			if ( CG_JointWorld( &ent, "foot_l", fl ) && CG_JointWorld( &ent, "foot_r", fr ) ) {
				ent.origin[2] -= pe->skelDrop;
				VectorCopy( ent.origin, ent.oldorigin );
				ent.bonePose = CG_LimbToWorld( &ent, ent.bonePose, "thigh_l", "calf_l", "foot_l", fl );
				ent.bonePose = CG_LimbToWorld( &ent, ent.bonePose, "thigh_r", "calf_r", "foot_r", fr );
			}
		}
	}

	// foot lock: a foot on the ground stays where it landed until the stride has
	// carried the body past it, which stops the skate. Eased in and out: a hard
	// release would throw the foot up to 26u in a frame
	{
		int		leg;

		for ( leg = 0; leg < 2; leg++ ) {
			vec3_t	p, d;
			const char	*foot = leg ? "foot_r" : "foot_l";
			qboolean	can = near && cg_skelFootLock.integer && ground && speed <= 520 && fabs( pe->skelDrop ) < 0.5f && !( kicking && leg )
				&& ( clip == SKEL_SPRINT_LOOP || clip == SKEL_WALK_LOOP );

			if ( !can && pe->skelFootW[leg] < 0.02f ) {
				pe->skelFootOn[leg] = qfalse;
				continue;
			}
			if ( !CG_JointWorld( &ent, foot, p ) ) {
				continue;
			}
			if ( !can || p[2] - ent.origin[2] > 7.0f ) {
				pe->skelFootOn[leg] = qfalse;
			} else if ( !pe->skelFootOn[leg] && pe->skelFootW[leg] < 0.05f ) {
				VectorCopy( p, pe->skelFootPt[leg] );
				pe->skelFootOn[leg] = qtrue;
			}
			VectorSubtract( pe->skelFootPt[leg], p, d );
			d[2] = 0;
			if ( VectorLength( d ) > 26.0f ) {
				pe->skelFootOn[leg] = qfalse;
			}
			pe->skelFootW[leg] += ( ( pe->skelFootOn[leg] ? 1.0f : 0.0f ) - pe->skelFootW[leg] )
				* ( 1.0f - exp( -dt * ( pe->skelFootOn[leg] ? 16.0f : 12.0f ) ) );
			if ( pe->skelFootW[leg] > 0.02f ) {
				VectorSubtract( pe->skelFootPt[leg], p, d );
				VectorMA( p, pe->skelFootW[leg], d, p );
				ent.bonePose = CG_LimbToWorld( &ent, ent.bonePose, leg ? "thigh_r" : "thigh_l",
					leg ? "calf_r" : "calf_l", foot, p );
			}
		}
	}

	// on a wall the body runs along it, so the chest and head turn toward the look
	if ( wall ) {
		orientation_t	tag;
		vec3_t			zaxis = { 0, 0, 1 };
		float			look = Com_Clamp( -45, 45, AngleNormalize180( cent->lerpAngles[YAW] - angles[YAW] ) );

		pe->skelLook += ( look - pe->skelLook ) * ( 1.0f - exp( -dt * 10.0f ) );
		if ( trap_R_LerpTag( &tag, model, ent.oldframe, ent.frame, 1.0f - ent.backlerp, "spine_03" ) ) {
			ent.bonePose = CG_BoneRotation( &ent, ent.bonePose, "spine_03", &tag, zaxis, DEG2RAD( pe->skelLook * 0.35f ) );
		}
		if ( trap_R_LerpTag( &tag, model, ent.oldframe, ent.frame, 1.0f - ent.backlerp, "Head" ) ) {
			ent.bonePose = CG_BoneRotation( &ent, ent.bonePose, "Head", &tag, zaxis, DEG2RAD( pe->skelLook * 0.5f ) );
		}
	} else {
		pe->skelLook = 0;
	}

	// the stride fades with the blend, so leaving a wall doesn't cut the legs off
	if ( wallW > 0.02f && !cg_skelWallClip.integer && pe->skelWallSide ) {
		// its own clock: clip changes reset skelPhase, which would teleport the legs
		float	cycle = 2.0f * CG_SkelWallStep( gait ) / CG_SkelWallStance( gait );

		pe->skelWallPhase = fmod( pe->skelWallPhase + dt * Com_Clamp( 1.5f, 3.9f, speed / cycle ), 1.0f );
		CG_SkelWallStride( &ent, pe->skelWallPt, moveLeft, pe->skelVelF, pe->skelWallSide,
			pe->skelWallPhase * 2.0f * M_PI, wallW );
	}

	// cg_skelTrace 1: one line a frame for tools/modelgen/animjerk.py
	if ( s->clientNum == cg.snap->ps.clientNum ) {
		char	tr[8];

		trap_Cvar_VariableStringBuffer( "cg_skelTrace", tr, sizeof( tr ) );
		if ( atoi( tr ) ) {
			static const char	*names[4] = { "Head", "hand_r", "foot_l", "foot_r" };
			orientation_t	tag;
			int				n, i;
			char			line[256];
			vec3_t			w;

			Com_sprintf( line, sizeof( line ), "SKT %d %d %.1f %.1f %.1f", cg.time, clip, speed, pe->skelLean[0], pe->skelLean[1] );
			for ( n = 0; n < 4; n++ ) {
				if ( trap_R_BoneTag( &tag, model, ent.frame, ent.oldframe, ent.backlerp, ent.bonePose, names[n] ) ) {
					VectorCopy( ent.origin, w );
					for ( i = 0; i < 3; i++ ) {
						VectorMA( w, tag.origin[i], ent.axis[i], w );
					}
					VectorSubtract( w, cent->lerpOrigin, w );
					Q_strcat( line, sizeof( line ), va( " %.2f %.2f %.2f", w[0], w[1], w[2] ) );
				}
			}
			Com_Printf( "%s\n", line );
			Com_Printf( "SKW t=%d o=%.0f,%.0f,%.0f v=%.0f,%.0f,%.0f wr=%d g=%d\n", cg.time, cent->lerpOrigin[0], cent->lerpOrigin[1], cent->lerpOrigin[2],
				vel[0], vel[1], vel[2], cg.predictedPlayerState.stats[STAT_WALLRUN], ground );
		}
	}

	pe->skelGround = ground;
	VectorCopy( vel, pe->skelVel );
	// the pose a ragdoll takes over from
	pe->skelLastTime = cg.time;
	pe->skelLastFrame = ent.frame;
	pe->skelLastOld = ent.oldframe;
	pe->skelLastBack = ent.backlerp;
	VectorCopy( ent.origin, pe->skelLastOrigin );
	AxisCopy( ent.axis, pe->skelLastAxis );
	if ( !Q_stricmp( cg_skelBody.string, "models/s64/robot.iqm" ) ) {
		char pbr[8];
		trap_Cvar_VariableStringBuffer( "r_pbr", pbr, sizeof( pbr ) );
		ent.customShader = trap_R_RegisterShader( atoi( pbr ) ? "models/s64/robot_pbr" : "models/s64/robot" );
	}
	trap_R_AddRefEntityToScene( &ent );
	if ( cg_skelScarf.integer ) {
		CG_SkelScarf( &ent, pe, renderfx );
	}
	CG_AddPlayerWeapon( &ent, NULL, cent, cgs.clientinfo[s->clientNum].team );
}

void CG_Player( centity_t *cent ) {
	clientInfo_t	*ci;
	refEntity_t		legs;
	refEntity_t		torso;
	refEntity_t		head;
	int				clientNum;
	int				renderfx;
	qboolean		shadow;
	float			shadowPlane;
#ifdef MISSIONPACK
	refEntity_t		skull;
	refEntity_t		powerup;
	int				t;
	float			c;
	float			angle;
	vec3_t			dir, angles;
#endif

	// the client number is stored in clientNum.  It can't be derived
	// from the entity number, because a single client may have
	// multiple corpses on the level using the same clientinfo
	clientNum = cent->currentState.clientNum;
	if ( clientNum < 0 || clientNum >= MAX_CLIENTS ) {
		CG_Error( "Bad clientNum on player entity");
	}
	ci = &cgs.clientinfo[ clientNum ];

	// it is possible to see corpses from disconnected players that may
	// not have valid clientinfo
	if ( !ci->infoValid ) {
		return;
	}

	// get the player model information
	renderfx = 0;
	if ( cent->currentState.number == cg.snap->ps.clientNum) {
		if (!cg.renderingThirdPerson) {
			renderfx = RF_THIRD_PERSON;			// only draw in mirrors
		} else {
			if (cg_cameraMode.integer) {
				return;
			}
		}
	}


	// STRAFE 64: dead bodies are simulated as a Verlet ragdoll that flops and
	// settles on the geometry, instead of the canned death animation. Returns
	// qtrue when it has rendered the corpse, so we skip the normal assembly.
	if ( CG_RagdollAdd( cent, renderfx | RF_LIGHTING_ORIGIN, 0 ) ) {
		return;
	}

	memset( &legs, 0, sizeof(legs) );
	memset( &torso, 0, sizeof(torso) );
	memset( &head, 0, sizeof(head) );

	// get the rotation information
	CG_PlayerAngles( cent, legs.axis, torso.axis, head.axis );

	{
		if ( cg_skeletal.integer && cgs.media.skelModel ) {
			CG_SkeletalPlayer( cent, legs.axis, renderfx );
			CG_PlayerSprites( cent );
			return;
		}
	}

	// STRAFE 64: procedural wall-grip lean, layered on the MD3 axes before the
	// tag chain composes torso/head onto the legs.
	CG_WallGripPose( cent, legs.axis, torso.axis, head.axis );

	// STRAFE 64: procedural crouch-slide recline, same axis-overlay approach.
	CG_SlidePose( cent, legs.axis, torso.axis, head.axis );

	// STRAFE 64: cosmetic acrobatic flip / roll spin (air-jump somersault, dash
	// roll). Rolls the legs axis so the whole body spins through the tag chain.
	CG_AcrobaticPose( cent, legs.axis );

	// STRAFE 64: whole-body sword swing — twist/lean/lunge through the swing arc
	// (and guard-stance lean while blocking), on top of the MD3 attack frame.
	CG_SwordSwingPose( cent, legs.axis, torso.axis, head.axis );

	// STRAFE 64: melee-kick roundhouse / flying-kick lean (the tornado spin
	// itself rides the acrobatic flip above).
	CG_KickPose( cent, legs.axis, torso.axis, head.axis );

	// get the animation state (after rotation, to allow feet shuffle)
	CG_PlayerAnimation( cent, &legs.oldframe, &legs.frame, &legs.backlerp,
		 &torso.oldframe, &torso.frame, &torso.backlerp );

	// add the talk baloon or disconnect icon
	CG_PlayerSprites( cent );

	// add the shadow
	shadow = CG_PlayerShadow( cent, &shadowPlane );

	// add a water splash if partially in and out of water
	CG_PlayerSplash( cent );

	if ( cg_shadows.integer == 3 && shadow ) {
		renderfx |= RF_SHADOW_PLANE;
	}
	renderfx |= RF_LIGHTING_ORIGIN;			// use the same origin for all
#ifdef MISSIONPACK
	if( cgs.gametype == GT_HARVESTER ) {
		CG_PlayerTokens( cent, renderfx );
	}
#endif
	//
	// add the legs
	//
	legs.hModel = ci->legsModel;
	legs.customSkin = ci->legsSkin;

	VectorCopy( cent->lerpOrigin, legs.origin );

	// STRAFE 64: drop the whole body onto the deck during a slide so the butt
	// sits on the ground while the legs (kicked forward in CG_SlidePose) skim it.
	// The torso/head tag chain hangs off legs.origin, so lowering it sinks all
	// three. Eased via cent->pe.slide so it tracks the same ramp as the pose.
	if ( cent->pe.slide > 0.01f ) {
		legs.origin[2] -= cent->pe.slide * SLIDE_DROP * cg_slidePoseScale.value;
	}

	VectorCopy( cent->lerpOrigin, legs.lightingOrigin );
	legs.shadowPlane = shadowPlane;
	legs.renderfx = renderfx;
	VectorCopy (legs.origin, legs.oldorigin);	// don't positionally lerp at all

	CG_AddRefEntityWithPowerups( &legs, &cent->currentState, ci->team );

	// if the model failed, allow the default nullmodel to be displayed
	if (!legs.hModel) {
		return;
	}

	//
	// add the torso
	//
	torso.hModel = ci->torsoModel;
	if (!torso.hModel) {
		return;
	}

	torso.customSkin = ci->torsoSkin;

	VectorCopy( cent->lerpOrigin, torso.lightingOrigin );

	CG_PositionRotatedEntityOnTag( &torso, &legs, ci->legsModel, "tag_torso");

	torso.shadowPlane = shadowPlane;
	torso.renderfx = renderfx;

	CG_AddRefEntityWithPowerups( &torso, &cent->currentState, ci->team );

#ifdef MISSIONPACK
	if ( cent->currentState.eFlags & EF_KAMIKAZE ) {

		memset( &skull, 0, sizeof(skull) );

		VectorCopy( cent->lerpOrigin, skull.lightingOrigin );
		skull.shadowPlane = shadowPlane;
		skull.renderfx = renderfx;

		if ( cent->currentState.eFlags & EF_DEAD ) {
			// one skull bobbing above the dead body
			angle = ((cg.time / 7) & 255) * (M_PI * 2) / 255;
			if (angle > M_PI * 2)
				angle -= (float)M_PI * 2;
			dir[0] = sin(angle) * 20;
			dir[1] = cos(angle) * 20;
			angle = ((cg.time / 4) & 255) * (M_PI * 2) / 255;
			dir[2] = 15 + sin(angle) * 8;
			VectorAdd(torso.origin, dir, skull.origin);
			
			dir[2] = 0;
			VectorCopy(dir, skull.axis[1]);
			VectorNormalize(skull.axis[1]);
			VectorSet(skull.axis[2], 0, 0, 1);
			CrossProduct(skull.axis[1], skull.axis[2], skull.axis[0]);

			skull.hModel = cgs.media.kamikazeHeadModel;
			trap_R_AddRefEntityToScene( &skull );
			skull.hModel = cgs.media.kamikazeHeadTrail;
			trap_R_AddRefEntityToScene( &skull );
		}
		else {
			// three skulls spinning around the player
			angle = ((cg.time / 4) & 255) * (M_PI * 2) / 255;
			dir[0] = cos(angle) * 20;
			dir[1] = sin(angle) * 20;
			dir[2] = cos(angle) * 20;
			VectorAdd(torso.origin, dir, skull.origin);

			angles[0] = sin(angle) * 30;
			angles[1] = (angle * 180 / M_PI) + 90;
			if (angles[1] > 360)
				angles[1] -= 360;
			angles[2] = 0;
			AnglesToAxis( angles, skull.axis );

			/*
			dir[2] = 0;
			VectorInverse(dir);
			VectorCopy(dir, skull.axis[1]);
			VectorNormalize(skull.axis[1]);
			VectorSet(skull.axis[2], 0, 0, 1);
			CrossProduct(skull.axis[1], skull.axis[2], skull.axis[0]);
			*/

			skull.hModel = cgs.media.kamikazeHeadModel;
			trap_R_AddRefEntityToScene( &skull );
			// flip the trail because this skull is spinning in the other direction
			VectorInverse(skull.axis[1]);
			skull.hModel = cgs.media.kamikazeHeadTrail;
			trap_R_AddRefEntityToScene( &skull );

			angle = ((cg.time / 4) & 255) * (M_PI * 2) / 255 + M_PI;
			if (angle > M_PI * 2)
				angle -= (float)M_PI * 2;
			dir[0] = sin(angle) * 20;
			dir[1] = cos(angle) * 20;
			dir[2] = cos(angle) * 20;
			VectorAdd(torso.origin, dir, skull.origin);

			angles[0] = cos(angle - 0.5 * M_PI) * 30;
			angles[1] = 360 - (angle * 180 / M_PI);
			if (angles[1] > 360)
				angles[1] -= 360;
			angles[2] = 0;
			AnglesToAxis( angles, skull.axis );

			/*
			dir[2] = 0;
			VectorCopy(dir, skull.axis[1]);
			VectorNormalize(skull.axis[1]);
			VectorSet(skull.axis[2], 0, 0, 1);
			CrossProduct(skull.axis[1], skull.axis[2], skull.axis[0]);
			*/

			skull.hModel = cgs.media.kamikazeHeadModel;
			trap_R_AddRefEntityToScene( &skull );
			skull.hModel = cgs.media.kamikazeHeadTrail;
			trap_R_AddRefEntityToScene( &skull );

			angle = ((cg.time / 3) & 255) * (M_PI * 2) / 255 + 0.5 * M_PI;
			if (angle > M_PI * 2)
				angle -= (float)M_PI * 2;
			dir[0] = sin(angle) * 20;
			dir[1] = cos(angle) * 20;
			dir[2] = 0;
			VectorAdd(torso.origin, dir, skull.origin);
			
			VectorCopy(dir, skull.axis[1]);
			VectorNormalize(skull.axis[1]);
			VectorSet(skull.axis[2], 0, 0, 1);
			CrossProduct(skull.axis[1], skull.axis[2], skull.axis[0]);

			skull.hModel = cgs.media.kamikazeHeadModel;
			trap_R_AddRefEntityToScene( &skull );
			skull.hModel = cgs.media.kamikazeHeadTrail;
			trap_R_AddRefEntityToScene( &skull );
		}
	}

	if ( cent->currentState.powerups & ( 1 << PW_GUARD ) ) {
		memcpy(&powerup, &torso, sizeof(torso));
		powerup.hModel = cgs.media.guardPowerupModel;
		powerup.frame = 0;
		powerup.oldframe = 0;
		powerup.customSkin = 0;
		trap_R_AddRefEntityToScene( &powerup );
	}
	if ( cent->currentState.powerups & ( 1 << PW_SCOUT ) ) {
		memcpy(&powerup, &torso, sizeof(torso));
		powerup.hModel = cgs.media.scoutPowerupModel;
		powerup.frame = 0;
		powerup.oldframe = 0;
		powerup.customSkin = 0;
		trap_R_AddRefEntityToScene( &powerup );
	}
	if ( cent->currentState.powerups & ( 1 << PW_DOUBLER ) ) {
		memcpy(&powerup, &torso, sizeof(torso));
		powerup.hModel = cgs.media.doublerPowerupModel;
		powerup.frame = 0;
		powerup.oldframe = 0;
		powerup.customSkin = 0;
		trap_R_AddRefEntityToScene( &powerup );
	}
	if ( cent->currentState.powerups & ( 1 << PW_AMMOREGEN ) ) {
		memcpy(&powerup, &torso, sizeof(torso));
		powerup.hModel = cgs.media.ammoRegenPowerupModel;
		powerup.frame = 0;
		powerup.oldframe = 0;
		powerup.customSkin = 0;
		trap_R_AddRefEntityToScene( &powerup );
	}
	if ( cent->currentState.powerups & ( 1 << PW_INVULNERABILITY ) ) {
		if ( !ci->invulnerabilityStartTime ) {
			ci->invulnerabilityStartTime = cg.time;
		}
		ci->invulnerabilityStopTime = cg.time;
	}
	else {
		ci->invulnerabilityStartTime = 0;
	}
	if ( (cent->currentState.powerups & ( 1 << PW_INVULNERABILITY ) ) ||
		cg.time - ci->invulnerabilityStopTime < 250 ) {

		memcpy(&powerup, &torso, sizeof(torso));
		powerup.hModel = cgs.media.invulnerabilityPowerupModel;
		powerup.frame = 0;
		powerup.oldframe = 0;
		powerup.customSkin = 0;
		// always draw
		powerup.renderfx &= ~RF_THIRD_PERSON;
		VectorCopy(cent->lerpOrigin, powerup.origin);

		if ( cg.time - ci->invulnerabilityStartTime < 250 ) {
			c = (float) (cg.time - ci->invulnerabilityStartTime) / 250;
		}
		else if (cg.time - ci->invulnerabilityStopTime < 250 ) {
			c = (float) (250 - (cg.time - ci->invulnerabilityStopTime)) / 250;
		}
		else {
			c = 1;
		}
		VectorSet( powerup.axis[0], c, 0, 0 );
		VectorSet( powerup.axis[1], 0, c, 0 );
		VectorSet( powerup.axis[2], 0, 0, c );
		trap_R_AddRefEntityToScene( &powerup );
	}

	t = cg.time - ci->medkitUsageTime;
	if ( ci->medkitUsageTime && t < 500 ) {
		memcpy(&powerup, &torso, sizeof(torso));
		powerup.hModel = cgs.media.medkitUsageModel;
		powerup.frame = 0;
		powerup.oldframe = 0;
		powerup.customSkin = 0;
		// always draw
		powerup.renderfx &= ~RF_THIRD_PERSON;
		VectorClear(angles);
		AnglesToAxis(angles, powerup.axis);
		VectorCopy(cent->lerpOrigin, powerup.origin);
		powerup.origin[2] += -24 + (float) t * 80 / 500;
		if ( t > 400 ) {
			c = (float) (t - 1000) * 0xff / 100;
			powerup.shaderRGBA[0] = 0xff - c;
			powerup.shaderRGBA[1] = 0xff - c;
			powerup.shaderRGBA[2] = 0xff - c;
			powerup.shaderRGBA[3] = 0xff - c;
		}
		else {
			powerup.shaderRGBA[0] = 0xff;
			powerup.shaderRGBA[1] = 0xff;
			powerup.shaderRGBA[2] = 0xff;
			powerup.shaderRGBA[3] = 0xff;
		}
		trap_R_AddRefEntityToScene( &powerup );
	}
#endif // MISSIONPACK

	//
	// add the head
	//
	head.hModel = ci->headModel;
	if (!head.hModel) {
		return;
	}
	head.customSkin = ci->headSkin;

	VectorCopy( cent->lerpOrigin, head.lightingOrigin );

	CG_PositionRotatedEntityOnTag( &head, &torso, ci->torsoModel, "tag_head");

	head.shadowPlane = shadowPlane;
	head.renderfx = renderfx;

	CG_AddRefEntityWithPowerups( &head, &cent->currentState, ci->team );

#ifdef MISSIONPACK
	CG_BreathPuffs(cent, &head);

	CG_DustTrail(cent);
#endif

	CG_SlideTrail(cent);

	// crouch-slide scrape loop — rides the same networked EF_SLIDING flag as
	// the dust, so every sliding pilot is audible, and stops the frame the
	// slide ends (looping sounds must be re-added every frame)
	if ( cg_slideSound.integer && ( cent->currentState.eFlags & EF_SLIDING )
		&& cgs.media.slideSound ) {
		trap_S_AddLoopingSound( cent->currentState.number, cent->lerpOrigin,
			vec3_origin, cgs.media.slideSound );
	}

	// STRAFE 64: each fighter glows faintly in their OWN colour, so they read as
	// neon-lit holographic figures rather than dark silhouettes against the world.
	// Skip the local pilot in first person (the light would wash the near view);
	// everyone seen in third person / spectate gets the halo.
	if ( cg_playerGlow.value > 0
			&& !( cent->currentState.number == cg.snap->ps.clientNum && !cg.renderingThirdPerson ) ) {
		vec3_t	glowOrg;
		float	r = ci->color1[0], g = ci->color1[1], b = ci->color1[2];
		float	mn, mx, f;
		// guarantee a visible neon even if the player picked a near-black colour
		if ( r + g + b < 0.4f ) { r = 0.4f; g = 0.7f; b = 1.0f; }

		// #0 DISTINCT HUE: bots nearly all share the same color1, so every glow
		// comes out the same colour and there's nothing to mix. Give each pilot a
		// hue from its clientNum, stepped around the wheel by the golden angle
		// (0.618 turns) so neighbours land maximally far apart. Blend toward it by
		// cg_playerGlowHue (0 = player's own colour, 1 = full rainbow spread).
		if ( cg_playerGlowHue.value > 0 ) {
			float	hgh = clientNum * 0.61803399f;
			float	h6, ff, q, tt, hr, hgg, hbb, hb;
			int		hi;
			hgh -= floor( hgh );                 // hue 0..1
			h6 = hgh * 6.0f; hi = (int)h6; ff = h6 - hi; q = 1.0f - ff; tt = ff;
			switch ( hi % 6 ) {                  // HSV(h,1,1) -> rgb
				case 0:  hr = 1;  hgg = tt; hbb = 0;  break;
				case 1:  hr = q;  hgg = 1;  hbb = 0;  break;
				case 2:  hr = 0;  hgg = 1;  hbb = tt; break;
				case 3:  hr = 0;  hgg = q;  hbb = 1;  break;
				case 4:  hr = tt; hgg = 0;  hbb = 1;  break;
				default: hr = 1;  hgg = 0;  hbb = q;  break;
			}
			hb = cg_playerGlowHue.value; if ( hb > 1.0f ) hb = 1.0f;
			r = r * ( 1.0f - hb ) + hr  * hb;
			g = g * ( 1.0f - hb ) + hgg * hb;
			b = b * ( 1.0f - hb ) + hbb * hb;
		}

		// #1 SATURATE the hue: pull out the white/grey component so OVERLAPPING
		// pilot lights MIX as colour instead of summing to white. cg_playerGlowSat
		// 0 = raw colour, 1 = pure hue. Renormalize so the brightest channel stays
		// vivid (a saturated colour, not a dim one).
		mn = r; if ( g < mn ) mn = g; if ( b < mn ) mn = b;
		r -= mn * cg_playerGlowSat.value;
		g -= mn * cg_playerGlowSat.value;
		b -= mn * cg_playerGlowSat.value;
		mx = r; if ( g > mx ) mx = g; if ( b > mx ) mx = b;
		if ( mx > 0.001f ) { r /= mx; g /= mx; b /= mx; }

		// #2 COUNT-AWARE soft clamp: with N glowing pilots, additive light piles
		// toward white. Scale each light by lerp(1, 1/sqrt(N), clamp) so a crowd
		// reads ~sqrt(N)x brighter, not Nx. clamp 0 = raw additive, 1 = full norm.
		f = cg_playerGlow.value;
		if ( cg_playerGlowClamp.value > 0 ) {
			int n = CG_GlowPilotCount();
			if ( n > 1 ) {
				f *= 1.0f - cg_playerGlowClamp.value * ( 1.0f - 1.0f / sqrt( (float)n ) );
			}
		}
		// #3 AUDIO INTERPLAY: each pilot reacts to a DIFFERENT slice of the
		// music spectrum — a per-clientNum blend of bass/mid/high, spread around
		// the circle by the golden angle so no two pilots favour the same band.
		// They flare OUT OF PHASE (one on the kick, one on the hats, one on the
		// swell), so a cluster's dominant colour shifts with the track and the
		// colours mix over TIME, not just where they overlap.
		// SILENT MODE: only modulate when music is actually playing — with no
		// track the band envelopes are ~0 and the pulse would just rest the glow
		// at a permanent dim. No music -> leave the glow at full base brightness.
		if ( cg_playerGlowAudio.value > 0
				&& ( au_bass.value + au_mid.value + au_high.value ) > 0.02f ) {
			float	ph = clientNum * 2.39996323f;          // golden angle (rad)
			float	wB = 0.5f + 0.5f * sin( ph );
			float	wM = 0.5f + 0.5f * sin( ph + 2.09439510f );
			float	wH = 0.5f + 0.5f * sin( ph + 4.18879020f );
			float	s  = wB + wM + wH;
			float	pulse, amt;
			if ( s < 0.001f ) s = 1.0f;
			pulse = ( wB * au_bass.value + wM * au_mid.value
					+ wH * au_high.value ) / s;            // 0..1, this pilot's band
			amt = cg_playerGlowAudio.value;
			// dim when this pilot's band is quiet, flare when it hits
			f *= 1.0f - 0.55f * amt + 1.55f * amt * pulse;
			if ( f < 0.02f ) f = 0.02f;
		}

		r *= f; g *= f; b *= f;

		VectorCopy( cent->lerpOrigin, glowOrg );
		glowOrg[2] += 28;		// chest height
		trap_R_AddLightToScene( glowOrg, 110.0f + 90.0f * cg_playerGlow.value, r, g, b );
	}

	// STRAFE 64: chromatic-ghost glitch trail on a fresh air-dash
	CG_DashGlitchGhost( cent, &legs, &torso, &head );

	//
	// add the gun / barrel / flash
	//
	CG_AddPlayerWeapon( &torso, NULL, cent, ci->team );

	// add powerups floating behind the player
	CG_PlayerPowerups( cent, &torso );
}


//=====================================================================

/*
===============
CG_ResetPlayerEntity

A player just came into view or teleported, so reset all animation info
===============
*/
void CG_ResetPlayerEntity( centity_t *cent ) {
	cent->errorTime = -99999;		// guarantee no error decay added
	cent->extrapolated = qfalse;	

	CG_ClearLerpFrame( &cgs.clientinfo[ cent->currentState.clientNum ], &cent->pe.legs, cent->currentState.legsAnim );
	CG_ClearLerpFrame( &cgs.clientinfo[ cent->currentState.clientNum ], &cent->pe.torso, cent->currentState.torsoAnim );

	BG_EvaluateTrajectory( &cent->currentState.pos, cg.time, cent->lerpOrigin );
	BG_EvaluateTrajectory( &cent->currentState.apos, cg.time, cent->lerpAngles );

	VectorCopy( cent->lerpOrigin, cent->rawOrigin );
	VectorCopy( cent->lerpAngles, cent->rawAngles );

	memset( &cent->pe.legs, 0, sizeof( cent->pe.legs ) );
	cent->pe.legs.yawAngle = cent->rawAngles[YAW];
	cent->pe.legs.yawing = qfalse;
	cent->pe.legs.pitchAngle = 0;
	cent->pe.legs.pitching = qfalse;

	memset( &cent->pe.torso, 0, sizeof( cent->pe.torso ) );
	cent->pe.torso.yawAngle = cent->rawAngles[YAW];
	cent->pe.torso.yawing = qfalse;
	cent->pe.torso.pitchAngle = cent->rawAngles[PITCH];
	cent->pe.torso.pitching = qfalse;

	if ( cg_debugPosition.integer ) {
		CG_Printf("%i ResetPlayerEntity yaw=%f\n", cent->currentState.number, cent->pe.torso.yawAngle );
	}
}

