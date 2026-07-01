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
// cg_playerstate.c -- this file acts on changes in a new playerState_t
// With normal play, this will be done after local prediction, but when
// following another player or playing back a demo, it will be checked
// when the snapshot transitions like all the other entities

#include "cg_local.h"

/*
==============
CG_CheckAmmo

If the ammo has gone low enough to generate the warning, play a sound
==============
*/
void CG_CheckAmmo( void ) {
	int		i;
	int		total;
	int		previous;
	int		weapons;

	// see about how many seconds of ammo we have remaining
	weapons = cg.snap->ps.stats[ STAT_WEAPONS ];
	total = 0;
	for ( i = WP_MACHINEGUN ; i < WP_NUM_WEAPONS ; i++ ) {
		if ( ! ( weapons & ( 1 << i ) ) ) {
			continue;
		}
		if ( cg.snap->ps.ammo[i] < 0 ) {
			continue;
		}
		switch ( i ) {
		case WP_ROCKET_LAUNCHER:
		case WP_GRENADE_LAUNCHER:
		case WP_RAILGUN:
		case WP_SHOTGUN:
#ifdef MISSIONPACK
		case WP_PROX_LAUNCHER:
#endif
			total += cg.snap->ps.ammo[i] * 1000;
			break;
		default:
			total += cg.snap->ps.ammo[i] * 200;
			break;
		}
		if ( total >= 5000 ) {
			cg.lowAmmoWarning = 0;
			return;
		}
	}

	previous = cg.lowAmmoWarning;

	if ( total == 0 ) {
		cg.lowAmmoWarning = 2;
	} else {
		cg.lowAmmoWarning = 1;
	}

	// play a sound on transitions
	if ( cg.lowAmmoWarning != previous ) {
		trap_S_StartLocalSound( cgs.media.noAmmoSound, CHAN_LOCAL_SOUND );
	}
}

/*
==============
CG_DamageFeedback
==============
*/
void CG_DamageFeedback( int yawByte, int pitchByte, int damage ) {
	float		left, front, up;
	float		kick;
	int			health;
	float		scale;
	vec3_t		dir;
	vec3_t		angles;
	float		dist;
	float		yaw, pitch;

	// show the attacking player's head and name in corner
	cg.attackerTime = cg.time;

	// the lower on health you are, the greater the view kick will be
	health = cg.snap->ps.stats[STAT_HEALTH];
	if ( health < 40 ) {
		scale = 1;
	} else {
		scale = 40.0 / health;
	}
	kick = damage * scale;

	if (kick < 5)
		kick = 5;
	if (kick > 10)
		kick = 10;

	// if yaw and pitch are both 255, make the damage always centered (falling, etc)
	if ( yawByte == 255 && pitchByte == 255 ) {
		cg.damageX = 0;
		cg.damageY = 0;
		cg.v_dmg_roll = 0;
		cg.v_dmg_pitch = -kick;
		cg.dmgDirTime = 0;		// non-directional (falling, etc) — no indicator
	} else {
		// positional
		pitch = pitchByte / 255.0 * 360;
		yaw = yawByte / 255.0 * 360;

		angles[PITCH] = pitch;
		angles[YAW] = yaw;
		angles[ROLL] = 0;

		AngleVectors( angles, dir, NULL, NULL );
		VectorSubtract( vec3_origin, dir, dir );

		front = DotProduct (dir, cg.refdef.viewaxis[0] );
		left = DotProduct (dir, cg.refdef.viewaxis[1] );
		up = DotProduct (dir, cg.refdef.viewaxis[2] );

		// bearing to the attacker for the directional indicator
		cg.dmgDirTime = cg.time;
		cg.dmgDirAngle = atan2( left, front );

		dir[0] = front;
		dir[1] = left;
		dir[2] = 0;
		dist = VectorLength( dir );
		if ( dist < 0.1 ) {
			dist = 0.1f;
		}

		cg.v_dmg_roll = kick * left;
		
		cg.v_dmg_pitch = -kick * front;

		if ( front <= 0.1 ) {
			front = 0.1f;
		}
		cg.damageX = -left / front;
		cg.damageY = up / dist;
	}

	// clamp the position
	if ( cg.damageX > 1.0 ) {
		cg.damageX = 1.0;
	}
	if ( cg.damageX < - 1.0 ) {
		cg.damageX = -1.0;
	}

	if ( cg.damageY > 1.0 ) {
		cg.damageY = 1.0;
	}
	if ( cg.damageY < - 1.0 ) {
		cg.damageY = -1.0;
	}

	// don't let the screen flashes vary as much
	if ( kick > 10 ) {
		kick = 10;
	}
	cg.damageValue = kick;
	cg.v_dmg_time = cg.time + DAMAGE_TIME;
	cg.damageTime = cg.snap->serverTime;
}




/*
================
CG_Respawn

A respawn happened this snapshot
================
*/
void CG_Respawn( void ) {
	// no error decay on player movement
	cg.thisFrameTeleport = qtrue;

	// display weapons available
	cg.weaponSelectTime = cg.time;

	// select the weapon the server says we are using
	cg.weaponSelect = cg.snap->ps.weapon;

	// movement mod: each life is a fresh run — re-arm the top-speed
	// milestone announcer and the flow-entry surge so every respawn
	// celebrates its own peak instead of staying silent after the first,
	// and reset the peak-speed readout for the new run
	cg.speedTier = 0;
	cg.inFlow = qfalse;
	cg.peakSpeed = 0;
	cg.peakStreak = 0;
	cg.lifeStylePeak = 0;
}

extern char *eventnames[];

/*
==============
CG_CheckPlayerstateEvents
==============
*/
void CG_CheckPlayerstateEvents( playerState_t *ps, playerState_t *ops ) {
	int			i;
	int			event;
	centity_t	*cent;

	if ( ps->externalEvent && ps->externalEvent != ops->externalEvent ) {
		cent = &cg_entities[ ps->clientNum ];
		cent->currentState.event = ps->externalEvent;
		cent->currentState.eventParm = ps->externalEventParm;
		CG_EntityEvent( cent, cent->lerpOrigin );
	}

	cent = &cg.predictedPlayerEntity; // cg_entities[ ps->clientNum ];
	// go through the predictable events buffer
	for ( i = ps->eventSequence - MAX_PS_EVENTS ; i < ps->eventSequence ; i++ ) {
		// if we have a new predictable event
		if ( i >= ops->eventSequence
			// or the server told us to play another event instead of a predicted event we already issued
			// or something the server told us changed our prediction causing a different event
			|| (i > ops->eventSequence - MAX_PS_EVENTS && ps->events[i & (MAX_PS_EVENTS-1)] != ops->events[i & (MAX_PS_EVENTS-1)]) ) {

			event = ps->events[ i & (MAX_PS_EVENTS-1) ];
			cent->currentState.event = event;
			cent->currentState.eventParm = ps->eventParms[ i & (MAX_PS_EVENTS-1) ];
			CG_EntityEvent( cent, cent->lerpOrigin );

			cg.predictableEvents[ i & (MAX_PREDICTED_EVENTS-1) ] = event;

			cg.eventSequence++;
		}
	}
}

/*
==================
CG_CheckChangedPredictableEvents
==================
*/
void CG_CheckChangedPredictableEvents( playerState_t *ps ) {
	int i;
	int event;
	centity_t	*cent;

	cent = &cg.predictedPlayerEntity;
	for ( i = ps->eventSequence - MAX_PS_EVENTS ; i < ps->eventSequence ; i++ ) {
		//
		if (i >= cg.eventSequence) {
			continue;
		}
		// if this event is not further back in than the maximum predictable events we remember
		if (i > cg.eventSequence - MAX_PREDICTED_EVENTS) {
			// if the new playerstate event is different from a previously predicted one
			if ( ps->events[i & (MAX_PS_EVENTS-1)] != cg.predictableEvents[i & (MAX_PREDICTED_EVENTS-1) ] ) {

				event = ps->events[ i & (MAX_PS_EVENTS-1) ];
				cent->currentState.event = event;
				cent->currentState.eventParm = ps->eventParms[ i & (MAX_PS_EVENTS-1) ];
				CG_EntityEvent( cent, cent->lerpOrigin );

				cg.predictableEvents[ i & (MAX_PREDICTED_EVENTS-1) ] = event;

				if ( cg_showmiss.integer ) {
					CG_Printf("WARNING: changed predicted event\n");
				}
			}
		}
	}
}

/*
==================
pushReward
==================
*/
static void pushReward(sfxHandle_t sfx, qhandle_t shader, int rewardCount) {
	if (cg.rewardStack < (MAX_REWARDSTACK-1)) {
		cg.rewardStack++;
		cg.rewardSound[cg.rewardStack] = sfx;
		cg.rewardShader[cg.rewardStack] = shader;
		cg.rewardCount[cg.rewardStack] = rewardCount;
	}
}

/*
==================
CG_CheckLocalSounds
==================
*/
void CG_CheckLocalSounds( playerState_t *ps, playerState_t *ops ) {
	int			highScore, reward;
#ifdef MISSIONPACK
	int			health, armor;
#endif
	sfxHandle_t sfx;

	// don't play the sounds if the player just changed teams
	if ( ps->persistant[PERS_TEAM] != ops->persistant[PERS_TEAM] ) {
		return;
	}

	// hit changes
	if ( ps->persistant[PERS_HITS] > ops->persistant[PERS_HITS] ) {
		// speed-tinted hit marker: red once we're moving fast enough
		// for the speed-damage multiplier to be lethal (>= 1.5x)
		float	hitSpeed;

		hitSpeed = sqrt( ps->velocity[0] * ps->velocity[0]
			+ ps->velocity[1] * ps->velocity[1] );
		cg.hitMarkerTime = cg.time;
		cg.hitMarkerFast = ( hitSpeed >= 640.0f );
#ifdef MISSIONPACK
		armor  = ps->persistant[PERS_ATTACKEE_ARMOR] & 0xff;
		health = ps->persistant[PERS_ATTACKEE_ARMOR] >> 8;
		if (armor > 50 ) {
			trap_S_StartLocalSound( cgs.media.hitSoundHighArmor, CHAN_LOCAL_SOUND );
		} else if (armor || health > 100) {
			trap_S_StartLocalSound( cgs.media.hitSoundLowArmor, CHAN_LOCAL_SOUND );
		} else {
			trap_S_StartLocalSound( cgs.media.hitSound, CHAN_LOCAL_SOUND );
		}
#else
		trap_S_StartLocalSound( cgs.media.hitSound, CHAN_LOCAL_SOUND );
#endif
	} else if ( ps->persistant[PERS_HITS] < ops->persistant[PERS_HITS] ) {
		trap_S_StartLocalSound( cgs.media.hitTeamSound, CHAN_LOCAL_SOUND );
	}

	// frag flash: scoring grants health and armor (g_killReward), so
	// pulse the screen amber to make the heal-on-frag read as a deliberate
	// "the kill empowered you" rather than a silent stat bump
	if ( ps->persistant[PERS_SCORE] > ops->persistant[PERS_SCORE] ) {
		cg.fragFlashTime = cg.time;
	}

	// health changes of more than -1 should make pain sounds
	if ( ps->stats[STAT_HEALTH] < ops->stats[STAT_HEALTH] - 1 ) {
		if ( ps->stats[STAT_HEALTH] > 0 ) {
			CG_PainEvent( &cg.predictedPlayerEntity, ps->stats[STAT_HEALTH] );
		}
	}


	// if we are going into the intermission, don't start any voices
	if ( cg.intermissionStarted ) {
		return;
	}

	// reward sounds
	reward = qfalse;
	if (ps->persistant[PERS_CAPTURES] != ops->persistant[PERS_CAPTURES]) {
		pushReward(cgs.media.captureAwardSound, cgs.media.medalCapture, ps->persistant[PERS_CAPTURES]);
		reward = qtrue;
		//Com_Printf("capture\n");
	}
	if (ps->persistant[PERS_IMPRESSIVE_COUNT] != ops->persistant[PERS_IMPRESSIVE_COUNT]) {
#ifdef MISSIONPACK
		if (ps->persistant[PERS_IMPRESSIVE_COUNT] == 1) {
			sfx = cgs.media.firstImpressiveSound;
		} else {
			sfx = cgs.media.impressiveSound;
		}
#else
		sfx = cgs.media.impressiveSound;
#endif
		pushReward(sfx, cgs.media.medalImpressive, ps->persistant[PERS_IMPRESSIVE_COUNT]);
		reward = qtrue;
		//Com_Printf("impressive\n");
	}
	if (ps->persistant[PERS_EXCELLENT_COUNT] != ops->persistant[PERS_EXCELLENT_COUNT]) {
#ifdef MISSIONPACK
		if (ps->persistant[PERS_EXCELLENT_COUNT] == 1) {
			sfx = cgs.media.firstExcellentSound;
		} else {
			sfx = cgs.media.excellentSound;
		}
#else
		sfx = cgs.media.excellentSound;
#endif
		pushReward(sfx, cgs.media.medalExcellent, ps->persistant[PERS_EXCELLENT_COUNT]);
		reward = qtrue;
		//Com_Printf("excellent\n");
	}
	if (ps->persistant[PERS_GAUNTLET_FRAG_COUNT] != ops->persistant[PERS_GAUNTLET_FRAG_COUNT]) {
#ifdef MISSIONPACK
		if (ps->persistant[PERS_GAUNTLET_FRAG_COUNT] == 1) {
			sfx = cgs.media.firstHumiliationSound;
		} else {
			sfx = cgs.media.humiliationSound;
		}
#else
		sfx = cgs.media.humiliationSound;
#endif
		pushReward(sfx, cgs.media.medalGauntlet, ps->persistant[PERS_GAUNTLET_FRAG_COUNT]);
		reward = qtrue;
		//Com_Printf("gauntlet frag\n");
	}
	if (ps->persistant[PERS_DEFEND_COUNT] != ops->persistant[PERS_DEFEND_COUNT]) {
		pushReward(cgs.media.defendSound, cgs.media.medalDefend, ps->persistant[PERS_DEFEND_COUNT]);
		reward = qtrue;
		//Com_Printf("defend\n");
	}
	if (ps->persistant[PERS_ASSIST_COUNT] != ops->persistant[PERS_ASSIST_COUNT]) {
		pushReward(cgs.media.assistSound, cgs.media.medalAssist, ps->persistant[PERS_ASSIST_COUNT]);
		reward = qtrue;
		//Com_Printf("assist\n");
	}
	// if any of the player event bits changed
	if (ps->persistant[PERS_PLAYEREVENTS] != ops->persistant[PERS_PLAYEREVENTS]) {
		if ((ps->persistant[PERS_PLAYEREVENTS] & PLAYEREVENT_DENIEDREWARD) !=
				(ops->persistant[PERS_PLAYEREVENTS] & PLAYEREVENT_DENIEDREWARD)) {
			trap_S_StartLocalSound( cgs.media.deniedSound, CHAN_ANNOUNCER );
		}
		else if ((ps->persistant[PERS_PLAYEREVENTS] & PLAYEREVENT_GAUNTLETREWARD) !=
				(ops->persistant[PERS_PLAYEREVENTS] & PLAYEREVENT_GAUNTLETREWARD)) {
			trap_S_StartLocalSound( cgs.media.humiliationSound, CHAN_ANNOUNCER );
		}
		else if ((ps->persistant[PERS_PLAYEREVENTS] & PLAYEREVENT_HOLYSHIT) !=
				(ops->persistant[PERS_PLAYEREVENTS] & PLAYEREVENT_HOLYSHIT)) {
			trap_S_StartLocalSound( cgs.media.holyShitSound, CHAN_ANNOUNCER );
		}
		reward = qtrue;
	}

	// check for flag pickup
	if ( cgs.gametype > GT_TEAM ) {
		if ((ps->powerups[PW_REDFLAG] != ops->powerups[PW_REDFLAG] && ps->powerups[PW_REDFLAG]) ||
			(ps->powerups[PW_BLUEFLAG] != ops->powerups[PW_BLUEFLAG] && ps->powerups[PW_BLUEFLAG]) ||
			(ps->powerups[PW_NEUTRALFLAG] != ops->powerups[PW_NEUTRALFLAG] && ps->powerups[PW_NEUTRALFLAG]) )
		{
			trap_S_StartLocalSound( cgs.media.youHaveFlagSound, CHAN_ANNOUNCER );
		}
	}

	// lead changes
	if (!reward) {
		//
		if ( !cg.warmup ) {
			// never play lead changes during warmup
			if ( ps->persistant[PERS_RANK] != ops->persistant[PERS_RANK] ) {
				if ( cgs.gametype < GT_TEAM) {
					if (  ps->persistant[PERS_RANK] == 0 ) {
						CG_AddBufferedSound(cgs.media.takenLeadSound);
					} else if ( ps->persistant[PERS_RANK] == RANK_TIED_FLAG ) {
						CG_AddBufferedSound(cgs.media.tiedLeadSound);
					} else if ( ( ops->persistant[PERS_RANK] & ~RANK_TIED_FLAG ) == 0 ) {
						CG_AddBufferedSound(cgs.media.lostLeadSound);
					}
				}
			}
		}
	}

	// timelimit warnings
	if ( cgs.timelimit > 0 ) {
		int		msec;

		msec = cg.time - cgs.levelStartTime;
		if ( !( cg.timelimitWarnings & 4 ) && msec > ( cgs.timelimit * 60 + 2 ) * 1000 ) {
			cg.timelimitWarnings |= 1 | 2 | 4;
			trap_S_StartLocalSound( cgs.media.suddenDeathSound, CHAN_ANNOUNCER );
		}
		else if ( !( cg.timelimitWarnings & 2 ) && msec > (cgs.timelimit - 1) * 60 * 1000 ) {
			cg.timelimitWarnings |= 1 | 2;
			trap_S_StartLocalSound( cgs.media.oneMinuteSound, CHAN_ANNOUNCER );
		}
		else if ( cgs.timelimit > 5 && !( cg.timelimitWarnings & 1 ) && msec > (cgs.timelimit - 5) * 60 * 1000 ) {
			cg.timelimitWarnings |= 1;
			trap_S_StartLocalSound( cgs.media.fiveMinuteSound, CHAN_ANNOUNCER );
		}
	}

	// fraglimit warnings
	if ( cgs.fraglimit > 0 && cgs.gametype < GT_CTF) {
		highScore = cgs.scores1;

		if (cgs.gametype == GT_TEAM && cgs.scores2 > highScore) {
			highScore = cgs.scores2;
		}

		if ( !( cg.fraglimitWarnings & 4 ) && highScore == (cgs.fraglimit - 1) ) {
			cg.fraglimitWarnings |= 1 | 2 | 4;
			CG_AddBufferedSound(cgs.media.oneFragSound);
		}
		else if ( cgs.fraglimit > 2 && !( cg.fraglimitWarnings & 2 ) && highScore == (cgs.fraglimit - 2) ) {
			cg.fraglimitWarnings |= 1 | 2;
			CG_AddBufferedSound(cgs.media.twoFragSound);
		}
		else if ( cgs.fraglimit > 3 && !( cg.fraglimitWarnings & 1 ) && highScore == (cgs.fraglimit - 3) ) {
			cg.fraglimitWarnings |= 1;
			CG_AddBufferedSound(cgs.media.threeFragSound);
		}
	}
}

/*
===============
CG_TransitionPlayerState

===============
*/
void CG_TransitionPlayerState( playerState_t *ps, playerState_t *ops ) {
	// check for changing follow mode
	if ( ps->clientNum != ops->clientNum ) {
		cg.thisFrameTeleport = qtrue;
		// make sure we don't get any unwanted transition effects
		*ops = *ps;
	}

	// damage events (player is getting wounded)
	if ( ps->damageEvent != ops->damageEvent && ps->damageCount ) {
		CG_DamageFeedback( ps->damageYaw, ps->damagePitch, ps->damageCount );
	}

	// run-end scorecard: dying ends the run, so if this life's peak speed
	// beat this map's all-time record, celebrate and persist it. the record
	// lives in the archived per-map cvar (cgr_<map>), so a daily-speedrun PB
	// survives quitting and each map keeps its own. only fires on a real
	// improvement above flow speed, so it self-limits to rare genuine records
	// and reframes death as "run done, here's your result". peakSpeed is still
	// valid here; CG_Respawn (which clears it) runs on the spawn-count change.
	if ( ps->stats[STAT_HEALTH] <= 0 && ops->stats[STAT_HEALTH] > 0
		&& cg.peakSpeed >= 500 && cg.peakSpeed > cg.mapSpeedRecord.integer ) {
		trap_Cvar_Set( cg.mapRecordName, va( "%i", cg.peakSpeed ) );
		trap_Cvar_Update( &cg.mapSpeedRecord );
		CG_CenterPrint( va( "NEW RECORD  %i UPS", cg.peakSpeed ), 120, BIGCHAR_WIDTH );
		trap_S_StartLocalSound( cgs.media.excellentSound, CHAN_ANNOUNCER );
	}

	// roll the cinematic killcam on the moment of death
	if ( ps->stats[STAT_HEALTH] <= 0 && ops->stats[STAT_HEALTH] > 0 ) {
		CG_KillcamPlayerDied();
	}

	// respawning. NOTE: don't stop the killcam here -- this also fires when the
	// dead pilot's view switches to FOLLOW the killer (ps flips to their
	// clientNum + spawn count), which would cut the kill screen short. The
	// killcam ends itself on a genuine local respawn (see CG_KillcamUpdate).
	if ( ps->persistant[PERS_SPAWN_COUNT] != ops->persistant[PERS_SPAWN_COUNT] ) {
		CG_Respawn();
	}

	if ( cg.mapRestart ) {
		CG_Respawn();
		cg.mapRestart = qfalse;
	}

	if ( cg.snap->ps.pm_type != PM_INTERMISSION 
		&& ps->persistant[PERS_TEAM] != TEAM_SPECTATOR ) {
		CG_CheckLocalSounds( ps, ops );
	}

	// check for going low on ammo
	CG_CheckAmmo();

	// chained bhop pop: the streak is the pulse of the run,
	// and the pitch ladder climbs with it
	if ( ps->stats[STAT_BHOP_STREAK] > ops->stats[STAT_BHOP_STREAK]
		&& ps->stats[STAT_BHOP_STREAK] >= 2 ) {
		sfxHandle_t	hop;

		if ( ps->stats[STAT_BHOP_STREAK] >= 9 ) {
			hop = cgs.media.hopSoundMax;
		} else if ( ps->stats[STAT_BHOP_STREAK] >= 5 ) {
			hop = cgs.media.hopSoundHigh;
		} else {
			hop = cgs.media.hopSound;
		}
		trap_S_StartLocalSound( hop, CHAN_LOCAL_SOUND );
	}

	// flow-entry surge: one whoosh when crossing up into flow speed,
	// where regen, the vignette, and the first milestone all switch on.
	// hysteresis (latch up at 500, release below 440) keeps it from
	// retriggering while hovering around the threshold.
	{
		float	flowSpeed;

		flowSpeed = sqrt( ps->velocity[0] * ps->velocity[0]
			+ ps->velocity[1] * ps->velocity[1] );
		if ( !cg.inFlow && flowSpeed >= 500.0f ) {
			cg.inFlow = qtrue;
			cg.flowSurgeTime = cg.time;		// drives the FOV surge punch
			trap_S_StartLocalSound( cgs.media.flowSound, CHAN_LOCAL_SOUND );
		} else if ( cg.inFlow && flowSpeed < 440.0f ) {
			cg.inFlow = qfalse;
		}
	}

	// air-dash kick: when the double-jump dash fires (STAT_AIRJUMP_COUNT ticked
	// up) punch the FOV so the burst reads as a boost, not a plain hop. The dash
	// velocity itself is applied server-side in PM_CheckJump's air branch.
	if ( ps->stats[STAT_AIRJUMP_COUNT] > ops->stats[STAT_AIRJUMP_COUNT]
		&& cg.fovPunch < 7.0f ) {
		cg.fovPunch = 7.0f;
	}

	// landing dip: feed the fall-deflect machinery on every ground
	// contact, scaled by impact speed, so even bhop landings have a
	// subtle weighted footfall in sync with the streak pop. capped well
	// under the hard-fall range, and set before the events below so a
	// real EV_FALL still overrides with its bigger dip.
	if ( ops->groundEntityNum == ENTITYNUM_NONE
		&& ps->groundEntityNum != ENTITYNUM_NONE ) {
		float	impact;

		impact = -ops->velocity[2];
		if ( impact > 120.0f && impact < 400.0f ) {
			cg.landChange = -( impact * 0.009f );	// ~ -1.1 to -3.6
			cg.landTime = cg.time;
		}
	}

	// run events
	CG_CheckPlayerstateEvents( ps, ops );

	// smooth the ducking viewheight change
	if ( ps->viewheight != ops->viewheight ) {
		cg.duckChange = ps->viewheight - ops->viewheight;
		cg.duckTime = cg.time;
	}

	// slide ENTRY: a quick FOV punch the instant you commit to a slide, so the
	// drop-and-go snaps wider and reads as a kinetic ground slide.
	if ( ( ps->pm_flags & PMF_SLIDING ) && !( ops->pm_flags & PMF_SLIDING ) ) {
		cg.fovPunch = 7.0f;
	}
}

