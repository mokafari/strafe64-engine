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
// bg_pmove.c -- both games player movement code
// takes a playerstate and a usercmd as input and returns a modifed playerstate

#include "../qcommon/q_shared.h"
#include "bg_public.h"
#include "bg_local.h"

pmove_t		*pm;
pml_t		pml;

// movement parameters
float	pm_stopspeed = 100.0f;
float	pm_duckScale = 0.25f;
float	pm_swimScale = 0.50f;

float	pm_accelerate = 14.0f;		// snappier ground accel (was 10): reach top speed fast
float	pm_airaccelerate = 1.0f;
float	pm_wateraccelerate = 4.0f;
float	pm_flyaccelerate = 8.0f;

float	pm_friction = 6.0f;
float	pm_waterfriction = 1.0f;
float	pm_flightfriction = 3.0f;
float	pm_spectatorfriction = 5.0f;

// movement mod parameters
// air movement (CPM style: vanilla strafe jumping is untouched, these add
// W-turning and fast A/D-only strafing on top of it)
float	pm_airStopAccelerate = 3.0f;	// extra decel when pushing against velocity (snappier air commits)
float	pm_strafeAccelerate = 70.0f;	// accel when strafing with A/D only
float	pm_wishSpeedClamp = 30.0f;		// wishspeed cap for A/D-only strafing
float	pm_airControlAmount = 150.0f;	// W-only velocity turning strength

// bunny hop chain (jump can be held: rehop on the landing frame, no friction)
int		pm_bhopWindowMs = 100;			// grounded longer than this breaks the chain
float	pm_bhopBoostBase = 1.05f;		// speed multiplier on the first chained hop
float	pm_bhopBoostPerHop = 0.01f;		// extra per consecutive hop...
float	pm_bhopBoostMax = 1.10f;		// ...up to this cap

// double jump / air dash (fresh jump press in mid-air)
float	pm_airJumpVelocity = 300.0f;	// upward speed of the air jump (matches JUMP_VELOCITY)
int		pm_airJumpMax = 1;				// air jumps allowed between groundings
float	pm_airDashSpeed = 150.0f;		// horizontal burst added on the air jump -> a dash

// stair jump (rejumping within the window after landing goes higher)
int		pm_doubleJumpWindowMs = 400;
float	pm_doubleJumpBoost = 75.0f;		// added to JUMP_VELOCITY

// crouch slide (ducking at speed nearly removes ground friction)
float	pm_slideMinSpeed = 250.0f;		// need at least this much speed to slide
float	pm_slideFrictionScale = 0.12f;	// friction multiplier while sliding (slicker carry)
float	pm_slideJumpBoost = 1.15f;		// horizontal kick when jumping out of a slide

// wall jump (fresh jump press while airborne next to a wall)
float	pm_wallJumpKick = 200.0f;		// push away from the wall
float	pm_wallJumpVelocity = 250.0f;	// minimum upward speed after the kick
int		pm_wallJumpMax = 2;				// walljumps allowed between groundings

// grapple swing: jump reels in (build speed), crouch reels out
float	pm_grappleReel = 900.0f;		// rope shorten/lengthen rate, ups

// wall run (Titanfall-style: hold forward and ride a wall at speed with
// greatly reduced gravity; jump to kick off, refunded each ride)
int		pm_wallRunDurationMs = 1600;	// max ride time before the wall lets go
float	pm_wallRunGravityScale = 0.22f;	// gravity multiplier while running
float	pm_wallRunMinSpeed = 280.0f;	// horizontal speed needed to stick
float	pm_wallRunSlideSpeed = 60.0f;	// max downward speed while running (gentle slide)
float	pm_wallRunBoost = 80.0f;		// upward kick on the frame you attach while falling

// vault / mantle (Titanfall-style: clip a ledge with speed, pull up over it
// keeping momentum). No cooldown stat — it self-limits because after a vault
// you're rising, and the lip-height check stops re-firing.
float	pm_vaultMinSpeed = 150.0f;		// horizontal speed needed to vault
float	pm_vaultReach = 26.0f;			// how close to the wall face triggers it
float	pm_vaultMinHeight = 20.0f;		// ledges taller than a step...
float	pm_vaultMaxHeight = 72.0f;		// ...up to about head height
float	pm_vaultSpeedKeep = 0.85f;		// horizontal momentum preserved through it

int		c_pmove = 0;


/*
===============
PM_AddEvent

===============
*/
void PM_AddEvent( int newEvent ) {
	BG_AddPredictableEventToPlayerstate( newEvent, 0, pm->ps );
}

/*
===============
PM_AddTouchEnt
===============
*/
void PM_AddTouchEnt( int entityNum ) {
	int		i;

	if ( entityNum == ENTITYNUM_WORLD ) {
		return;
	}
	if ( pm->numtouch == MAXTOUCH ) {
		return;
	}

	// see if it is already added
	for ( i = 0 ; i < pm->numtouch ; i++ ) {
		if ( pm->touchents[ i ] == entityNum ) {
			return;
		}
	}

	// add it
	pm->touchents[pm->numtouch] = entityNum;
	pm->numtouch++;
}

/*
===================
PM_StartTorsoAnim
===================
*/
static void PM_StartTorsoAnim( int anim ) {
	if ( pm->ps->pm_type >= PM_DEAD ) {
		return;
	}
	pm->ps->torsoAnim = ( ( pm->ps->torsoAnim & ANIM_TOGGLEBIT ) ^ ANIM_TOGGLEBIT )
		| anim;
}
static void PM_StartLegsAnim( int anim ) {
	if ( pm->ps->pm_type >= PM_DEAD ) {
		return;
	}
	if ( pm->ps->legsTimer > 0 ) {
		return;		// a high priority animation is running
	}
	pm->ps->legsAnim = ( ( pm->ps->legsAnim & ANIM_TOGGLEBIT ) ^ ANIM_TOGGLEBIT )
		| anim;
}

static void PM_ContinueLegsAnim( int anim ) {
	if ( ( pm->ps->legsAnim & ~ANIM_TOGGLEBIT ) == anim ) {
		return;
	}
	if ( pm->ps->legsTimer > 0 ) {
		return;		// a high priority animation is running
	}
	PM_StartLegsAnim( anim );
}

static void PM_ContinueTorsoAnim( int anim ) {
	if ( ( pm->ps->torsoAnim & ~ANIM_TOGGLEBIT ) == anim ) {
		return;
	}
	if ( pm->ps->torsoTimer > 0 ) {
		return;		// a high priority animation is running
	}
	PM_StartTorsoAnim( anim );
}

static void PM_ForceLegsAnim( int anim ) {
	pm->ps->legsTimer = 0;
	PM_StartLegsAnim( anim );
}


/*
==================
PM_ClipVelocity

Slide off of the impacting surface
==================
*/
void PM_ClipVelocity( vec3_t in, vec3_t normal, vec3_t out, float overbounce ) {
	float	backoff;
	float	change;
	int		i;
	
	backoff = DotProduct (in, normal);
	
	if ( backoff < 0 ) {
		backoff *= overbounce;
	} else {
		backoff /= overbounce;
	}

	for ( i=0 ; i<3 ; i++ ) {
		change = normal[i]*backoff;
		out[i] = in[i] - change;
	}
}


/*
==================
PM_Friction

Handles both ground friction and water friction
==================
*/
static void PM_Friction( void ) {
	vec3_t	vec;
	float	*vel;
	float	speed, newspeed, control;
	float	drop;
	
	vel = pm->ps->velocity;
	
	VectorCopy( vel, vec );
	if ( pml.walking ) {
		vec[2] = 0;	// ignore slope movement
	}

	speed = VectorLength(vec);
	if (speed < 1) {
		vel[0] = 0;
		vel[1] = 0;		// allow sinking underwater
		// FIXME: still have z friction underwater?
		return;
	}

	drop = 0;

	// apply ground friction
	if ( pm->waterlevel <= 1 ) {
		if ( pml.walking && !(pml.groundTrace.surfaceFlags & SURF_SLICK) ) {
			// if getting knocked back, no friction
			if ( ! (pm->ps->pm_flags & PMF_TIME_KNOCKBACK) ) {
				// crouch slide: ducking at speed rides momentum
				if ( ( pm->ps->pm_flags & PMF_DUCKED ) && speed > pm_slideMinSpeed ) {
					drop += speed*pm_friction*pm_slideFrictionScale*pml.frametime;
				} else {
					control = speed < pm_stopspeed ? pm_stopspeed : speed;
					drop += control*pm_friction*pml.frametime;
				}
			}
		}
	}

	// apply water friction even if just wading
	if ( pm->waterlevel ) {
		drop += speed*pm_waterfriction*pm->waterlevel*pml.frametime;
	}

	// apply flying friction
	if ( pm->ps->powerups[PW_FLIGHT]) {
		drop += speed*pm_flightfriction*pml.frametime;
	}

	if ( pm->ps->pm_type == PM_SPECTATOR) {
		drop += speed*pm_spectatorfriction*pml.frametime;
	}

	// scale the velocity
	newspeed = speed - drop;
	if (newspeed < 0) {
		newspeed = 0;
	}
	newspeed /= speed;

	vel[0] = vel[0] * newspeed;
	vel[1] = vel[1] * newspeed;
	vel[2] = vel[2] * newspeed;
}


/*
==============
PM_Accelerate

Handles user intended acceleration
==============
*/
static void PM_Accelerate( vec3_t wishdir, float wishspeed, float accel ) {
#if 1
	// q2 style
	int			i;
	float		addspeed, accelspeed, currentspeed;

	currentspeed = DotProduct (pm->ps->velocity, wishdir);
	addspeed = wishspeed - currentspeed;
	if (addspeed <= 0) {
		return;
	}
	accelspeed = accel*pml.frametime*wishspeed;
	if (accelspeed > addspeed) {
		accelspeed = addspeed;
	}
	
	for (i=0 ; i<3 ; i++) {
		pm->ps->velocity[i] += accelspeed*wishdir[i];	
	}
#else
	// proper way (avoids strafe jump maxspeed bug), but feels bad
	vec3_t		wishVelocity;
	vec3_t		pushDir;
	float		pushLen;
	float		canPush;

	VectorScale( wishdir, wishspeed, wishVelocity );
	VectorSubtract( wishVelocity, pm->ps->velocity, pushDir );
	pushLen = VectorNormalize( pushDir );

	canPush = accel*pml.frametime*wishspeed;
	if (canPush > pushLen) {
		canPush = pushLen;
	}

	VectorMA( pm->ps->velocity, canPush, pushDir, pm->ps->velocity );
#endif
}



/*
============
PM_CmdScale

Returns the scale factor to apply to cmd movements
This allows the clients to use axial -127 to 127 values for all directions
without getting a sqrt(2) distortion in speed.
============
*/
static float PM_CmdScale( usercmd_t *cmd ) {
	int		max;
	float	total;
	float	scale;

	max = abs( cmd->forwardmove );
	if ( abs( cmd->rightmove ) > max ) {
		max = abs( cmd->rightmove );
	}
	if ( abs( cmd->upmove ) > max ) {
		max = abs( cmd->upmove );
	}
	if ( !max ) {
		return 0;
	}

	total = sqrt( cmd->forwardmove * cmd->forwardmove
		+ cmd->rightmove * cmd->rightmove + cmd->upmove * cmd->upmove );
	scale = (float)pm->ps->speed * max / ( 127.0 * total );

	return scale;
}


/*
================
PM_SetMovementDir

Determine the rotation of the legs relative
to the facing dir
================
*/
static void PM_SetMovementDir( void ) {
	if ( pm->cmd.forwardmove || pm->cmd.rightmove ) {
		if ( pm->cmd.rightmove == 0 && pm->cmd.forwardmove > 0 ) {
			pm->ps->movementDir = 0;
		} else if ( pm->cmd.rightmove < 0 && pm->cmd.forwardmove > 0 ) {
			pm->ps->movementDir = 1;
		} else if ( pm->cmd.rightmove < 0 && pm->cmd.forwardmove == 0 ) {
			pm->ps->movementDir = 2;
		} else if ( pm->cmd.rightmove < 0 && pm->cmd.forwardmove < 0 ) {
			pm->ps->movementDir = 3;
		} else if ( pm->cmd.rightmove == 0 && pm->cmd.forwardmove < 0 ) {
			pm->ps->movementDir = 4;
		} else if ( pm->cmd.rightmove > 0 && pm->cmd.forwardmove < 0 ) {
			pm->ps->movementDir = 5;
		} else if ( pm->cmd.rightmove > 0 && pm->cmd.forwardmove == 0 ) {
			pm->ps->movementDir = 6;
		} else if ( pm->cmd.rightmove > 0 && pm->cmd.forwardmove > 0 ) {
			pm->ps->movementDir = 7;
		}
	} else {
		// if they aren't actively going directly sideways,
		// change the animation to the diagonal so they
		// don't stop too crooked
		if ( pm->ps->movementDir == 2 ) {
			pm->ps->movementDir = 1;
		} else if ( pm->ps->movementDir == 6 ) {
			pm->ps->movementDir = 7;
		} 
	}
}


/*
=============
PM_CheckJump
=============
*/
static qboolean PM_CheckJump( void ) {
	float		speed2d;
	float		boost;
	qboolean	chained;

	if ( pm->ps->pm_flags & PMF_RESPAWNED ) {
		return qfalse;		// don't allow jump until all buttons are up
	}

	if ( pm->cmd.upmove < 10 ) {
		// not holding jump
		return qfalse;
	}

	// jump can be held: rehopping on the landing frame keeps the chain alive
	// (the PMF_JUMP_HELD gate is gone on purpose)

	speed2d = sqrt( pm->ps->velocity[0] * pm->ps->velocity[0]
		+ pm->ps->velocity[1] * pm->ps->velocity[1] );

	// chained hop: back on the ground only briefly since the last jump
	chained = ( pm->ps->stats[STAT_BHOP_STREAK] > 0
		&& pm->ps->stats[STAT_GROUND_MS] <= pm_bhopWindowMs );

	pml.groundPlane = qfalse;		// jumping away
	pml.walking = qfalse;
	pm->ps->pm_flags |= PMF_JUMP_HELD;

	pm->ps->groundEntityNum = ENTITYNUM_NONE;
	pm->ps->velocity[2] = JUMP_VELOCITY;

	// double jump: a second jump inside the window goes higher
	if ( pm->ps->stats[STAT_JUMP_MS] > 0 ) {
		pm->ps->velocity[2] += pm_doubleJumpBoost;
	}
	pm->ps->stats[STAT_JUMP_MS] = pm_doubleJumpWindowMs;

	// chained hops build horizontal speed
	if ( chained ) {
		boost = pm_bhopBoostBase + pm_bhopBoostPerHop * ( pm->ps->stats[STAT_BHOP_STREAK] - 1 );
		if ( boost > pm_bhopBoostMax ) {
			boost = pm_bhopBoostMax;
		}
		pm->ps->velocity[0] *= boost;
		pm->ps->velocity[1] *= boost;
		if ( pm->ps->stats[STAT_BHOP_STREAK] < 99 ) {
			pm->ps->stats[STAT_BHOP_STREAK]++;
		}
	} else {
		pm->ps->stats[STAT_BHOP_STREAK] = 1;
	}

	// slide jump: jumping out of a crouch slide kicks forward
	if ( ( pm->ps->pm_flags & PMF_DUCKED ) && speed2d > pm_slideMinSpeed ) {
		pm->ps->velocity[0] *= pm_slideJumpBoost;
		pm->ps->velocity[1] *= pm_slideJumpBoost;
	}

	PM_AddEvent( EV_JUMP );

	if ( pm->cmd.forwardmove >= 0 ) {
		PM_ForceLegsAnim( LEGS_JUMP );
		pm->ps->pm_flags &= ~PMF_BACKWARDS_JUMP;
	} else {
		PM_ForceLegsAnim( LEGS_JUMPB );
		pm->ps->pm_flags |= PMF_BACKWARDS_JUMP;
	}

	return qtrue;
}

/*
=============
PM_CheckWaterJump
=============
*/
static qboolean	PM_CheckWaterJump( void ) {
	vec3_t	spot;
	int		cont;
	vec3_t	flatforward;

	if (pm->ps->pm_time) {
		return qfalse;
	}

	// check for water jump
	if ( pm->waterlevel != 2 ) {
		return qfalse;
	}

	flatforward[0] = pml.forward[0];
	flatforward[1] = pml.forward[1];
	flatforward[2] = 0;
	VectorNormalize (flatforward);

	VectorMA (pm->ps->origin, 30, flatforward, spot);
	spot[2] += 4;
	cont = pm->pointcontents (spot, pm->ps->clientNum );
	if ( !(cont & CONTENTS_SOLID) ) {
		return qfalse;
	}

	spot[2] += 16;
	cont = pm->pointcontents (spot, pm->ps->clientNum );
	if ( cont & (CONTENTS_SOLID|CONTENTS_PLAYERCLIP|CONTENTS_BODY) ) {
		return qfalse;
	}

	// jump out of water
	VectorScale (pml.forward, 200, pm->ps->velocity);
	pm->ps->velocity[2] = 350;

	pm->ps->pm_flags |= PMF_TIME_WATERJUMP;
	pm->ps->pm_time = 2000;

	return qtrue;
}

//============================================================================


/*
===================
PM_WaterJumpMove

Flying out of the water
===================
*/
static void PM_WaterJumpMove( void ) {
	// waterjump has no control, but falls

	PM_StepSlideMove( qtrue );

	pm->ps->velocity[2] -= pm->ps->gravity * pml.frametime;
	if (pm->ps->velocity[2] < 0) {
		// cancel as soon as we are falling down again
		pm->ps->pm_flags &= ~PMF_ALL_TIMES;
		pm->ps->pm_time = 0;
	}
}

/*
===================
PM_WaterMove

===================
*/
static void PM_WaterMove( void ) {
	int		i;
	vec3_t	wishvel;
	float	wishspeed;
	vec3_t	wishdir;
	float	scale;
	float	vel;

	if ( PM_CheckWaterJump() ) {
		PM_WaterJumpMove();
		return;
	}
#if 0
	// jump = head for surface
	if ( pm->cmd.upmove >= 10 ) {
		if (pm->ps->velocity[2] > -300) {
			if ( pm->watertype & CONTENTS_WATER ) {
				pm->ps->velocity[2] = 100;
			} else if ( pm->watertype & CONTENTS_SLIME ) {
				pm->ps->velocity[2] = 80;
			} else {
				pm->ps->velocity[2] = 50;
			}
		}
	}
#endif
	PM_Friction ();

	scale = PM_CmdScale( &pm->cmd );
	//
	// user intentions
	//
	if ( !scale ) {
		wishvel[0] = 0;
		wishvel[1] = 0;
		wishvel[2] = -60;		// sink towards bottom
	} else {
		for (i=0 ; i<3 ; i++)
			wishvel[i] = scale * pml.forward[i]*pm->cmd.forwardmove + scale * pml.right[i]*pm->cmd.rightmove;

		wishvel[2] += scale * pm->cmd.upmove;
	}

	VectorCopy (wishvel, wishdir);
	wishspeed = VectorNormalize(wishdir);

	if ( wishspeed > pm->ps->speed * pm_swimScale ) {
		wishspeed = pm->ps->speed * pm_swimScale;
	}

	PM_Accelerate (wishdir, wishspeed, pm_wateraccelerate);

	// make sure we can go up slopes easily under water
	if ( pml.groundPlane && DotProduct( pm->ps->velocity, pml.groundTrace.plane.normal ) < 0 ) {
		vel = VectorLength(pm->ps->velocity);
		// slide along the ground plane
		PM_ClipVelocity (pm->ps->velocity, pml.groundTrace.plane.normal, 
			pm->ps->velocity, OVERCLIP );

		VectorNormalize(pm->ps->velocity);
		VectorScale(pm->ps->velocity, vel, pm->ps->velocity);
	}

	PM_SlideMove( qfalse );
}

#ifdef MISSIONPACK
/*
===================
PM_InvulnerabilityMove

Only with the invulnerability powerup
===================
*/
static void PM_InvulnerabilityMove( void ) {
	pm->cmd.forwardmove = 0;
	pm->cmd.rightmove = 0;
	pm->cmd.upmove = 0;
	VectorClear(pm->ps->velocity);
}
#endif

/*
===================
PM_FlyMove

Only with the flight powerup
===================
*/
static void PM_FlyMove( void ) {
	int		i;
	vec3_t	wishvel;
	float	wishspeed;
	vec3_t	wishdir;
	float	scale;

	// normal slowdown
	PM_Friction ();

	scale = PM_CmdScale( &pm->cmd );
	//
	// user intentions
	//
	if ( !scale ) {
		wishvel[0] = 0;
		wishvel[1] = 0;
		wishvel[2] = 0;
	} else {
		for (i=0 ; i<3 ; i++) {
			wishvel[i] = scale * pml.forward[i]*pm->cmd.forwardmove + scale * pml.right[i]*pm->cmd.rightmove;
		}

		wishvel[2] += scale * pm->cmd.upmove;
	}

	VectorCopy (wishvel, wishdir);
	wishspeed = VectorNormalize(wishdir);

	PM_Accelerate (wishdir, wishspeed, pm_flyaccelerate);

	PM_StepSlideMove( qfalse );
}


/*
=============
PM_CheckWallJump

A fresh jump press while airborne next to a wall kicks away from it
=============
*/
static qboolean PM_CheckWallJump( void ) {
	vec3_t	dirs[4];
	vec3_t	spot;
	trace_t	trace;
	int		i;

	if ( pm->ps->pm_flags & PMF_RESPAWNED ) {
		return qfalse;
	}
	if ( pm->ps->pm_flags & PMF_GRAPPLE_PULL ) {
		return qfalse;		// jump reels the grapple, not wall-jumps
	}
	if ( pm->cmd.upmove < 10 ) {
		return qfalse;
	}
	// unlike the ground hop, walljumps need a fresh press
	if ( pm->ps->pm_flags & PMF_JUMP_HELD ) {
		return qfalse;
	}
	if ( pm->ps->groundEntityNum != ENTITYNUM_NONE ) {
		return qfalse;
	}
	if ( pm->waterlevel > 1 ) {
		return qfalse;
	}
	if ( pm->ps->stats[STAT_WALLJUMP_COUNT] >= pm_wallJumpMax ) {
		return qfalse;
	}

	// probe the four flat view-relative directions for a wall
	for ( i = 0 ; i < 2 ; i++ ) {
		dirs[0][i] = pml.forward[i];
		dirs[1][i] = pml.right[i];
		dirs[2][i] = -pml.right[i];
		dirs[3][i] = -pml.forward[i];
	}
	for ( i = 0 ; i < 4 ; i++ ) {
		dirs[i][2] = 0;
		VectorNormalize( dirs[i] );

		VectorMA( pm->ps->origin, 20, dirs[i], spot );
		pm->trace( &trace, pm->ps->origin, pm->mins, pm->maxs, spot,
			pm->ps->clientNum, pm->tracemask );

		if ( trace.fraction == 1.0 || trace.allsolid ) {
			continue;
		}
		// walls only: too flat is a floor, upside down is a ceiling
		if ( trace.plane.normal[2] >= MIN_WALK_NORMAL || trace.plane.normal[2] < -0.1f ) {
			continue;
		}
		if ( trace.surfaceFlags & SURF_NOIMPACT ) {
			continue;
		}

		// kick off the wall, keeping tangential speed
		PM_ClipVelocity( pm->ps->velocity, trace.plane.normal, pm->ps->velocity, OVERCLIP );
		VectorMA( pm->ps->velocity, pm_wallJumpKick, trace.plane.normal, pm->ps->velocity );
		if ( pm->ps->velocity[2] < pm_wallJumpVelocity ) {
			pm->ps->velocity[2] = pm_wallJumpVelocity;
		}

		pm->ps->pm_flags |= PMF_JUMP_HELD;
		pm->ps->stats[STAT_WALLJUMP_COUNT]++;

		PM_AddEvent( EV_JUMP );
		PM_ForceLegsAnim( LEGS_JUMP );
		pm->ps->pm_flags &= ~PMF_BACKWARDS_JUMP;

		return qtrue;
	}

	return qfalse;
}

/*
=============
PM_CheckAirJump

True double jump: a fresh jump press while airborne, away from walls
=============
*/
static qboolean PM_CheckAirJump( void ) {
	float	vz;

	if ( pm->ps->pm_flags & PMF_RESPAWNED ) {
		return qfalse;
	}
	if ( pm->ps->pm_flags & PMF_GRAPPLE_PULL ) {
		return qfalse;		// jump reels the grapple, not air-jumps
	}
	if ( pm->cmd.upmove < 10 ) {
		return qfalse;
	}
	// needs a fresh press, holding jump only rehops on landing
	if ( pm->ps->pm_flags & PMF_JUMP_HELD ) {
		return qfalse;
	}
	if ( pm->ps->groundEntityNum != ENTITYNUM_NONE ) {
		return qfalse;
	}
	if ( pm->waterlevel > 1 ) {
		return qfalse;
	}
	if ( pm->ps->stats[STAT_AIRJUMP_COUNT] >=
		pm_airJumpMax + ( ( pm->ps->pm_flags & PMF_AIRJUMP_BONUS ) ? 1 : 0 ) ) {
		return qfalse;		// shop-bought bonus grants one extra air jump
	}

	// falling speed is cancelled, half of any rising speed carries over
	vz = pm->ps->velocity[2];
	if ( vz < 0 ) {
		vz = 0;
	}
	pm->ps->velocity[2] = vz * 0.5f + pm_airJumpVelocity;

	// AIR DASH: a horizontal burst that follows current MOMENTUM, not the held
	// move keys — so a double jump carries you forward along your existing flight
	// instead of throwing you toward a strafe key. Only when you have little
	// momentum to preserve does it fall back to held-dir, then view forward.
	{
		vec3_t	dir;
		float	hsp;

		dir[0] = pm->ps->velocity[0];
		dir[1] = pm->ps->velocity[1];
		dir[2] = 0.0f;
		hsp = VectorNormalize( dir );
		if ( hsp < 50.0f ) {		// barely moving: use held dir, else view, softer
			dir[0] = pml.forward[0] * pm->cmd.forwardmove + pml.right[0] * pm->cmd.rightmove;
			dir[1] = pml.forward[1] * pm->cmd.forwardmove + pml.right[1] * pm->cmd.rightmove;
			dir[2] = 0.0f;
			if ( VectorNormalize( dir ) < 1.0f ) {
				dir[0] = pml.forward[0];
				dir[1] = pml.forward[1];
				dir[2] = 0.0f;
				VectorNormalize( dir );
			}
			pm->ps->velocity[0] += dir[0] * pm_airDashSpeed * 0.6f;
			pm->ps->velocity[1] += dir[1] * pm_airDashSpeed * 0.6f;
		} else {					// moving: dash straight down the momentum vector
			pm->ps->velocity[0] += dir[0] * pm_airDashSpeed;
			pm->ps->velocity[1] += dir[1] * pm_airDashSpeed;
		}
	}

	pm->ps->pm_flags |= PMF_JUMP_HELD;
	pm->ps->stats[STAT_AIRJUMP_COUNT]++;

	PM_AddEvent( EV_JUMP );
	PM_ForceLegsAnim( LEGS_JUMP );
	pm->ps->pm_flags &= ~PMF_BACKWARDS_JUMP;

	return qtrue;
}

/*
=============
PM_CheckWallRun

Titanfall-style sustained wall running. While airborne, holding forward and
moving fast roughly parallel to an adjacent wall, the player sticks to it
with greatly reduced gravity for a limited time. Returns qtrue on the frames
it is active so PM_AirMove can damp gravity around the slide move. The sign
of STAT_WALLRUN carries the wall side (+ right / - left) for the cgame tilt.
=============
*/
static qboolean PM_CheckWallRun( void ) {
	vec3_t	side, spot, vel2d;
	trace_t	trace;
	float	speed, into;
	int		s, sign, elapsed;

	if ( pm->ps->pm_flags & PMF_RESPAWNED ) {
		goto detach;
	}
	if ( pm->ps->groundEntityNum != ENTITYNUM_NONE ) {
		goto detach;		// grounded
	}
	if ( pm->waterlevel > 1 ) {
		goto detach;
	}
	if ( pm->cmd.forwardmove <= 0 ) {
		goto detach;		// must be driving forward into the run
	}

	vel2d[0] = pm->ps->velocity[0];
	vel2d[1] = pm->ps->velocity[1];
	vel2d[2] = 0;
	speed = VectorLength( vel2d );
	if ( speed < pm_wallRunMinSpeed ) {
		goto detach;
	}
	if ( abs( pm->ps->stats[STAT_WALLRUN] ) >= pm_wallRunDurationMs ) {
		goto detach;		// this airtime's ride is spent; needs to touch ground
	}

	VectorNormalize2( vel2d, vel2d );

	// probe right then left for a wall to ride
	for ( s = 0 ; s < 2 ; s++ ) {
		side[0] = ( s == 0 ) ? pml.right[0] : -pml.right[0];
		side[1] = ( s == 0 ) ? pml.right[1] : -pml.right[1];
		side[2] = 0;
		VectorNormalize( side );

		VectorMA( pm->ps->origin, 24, side, spot );
		pm->trace( &trace, pm->ps->origin, pm->mins, pm->maxs, spot,
			pm->ps->clientNum, pm->tracemask );

		if ( trace.fraction == 1.0 || trace.allsolid ) {
			continue;
		}
		// vertical walls only (floor is too flat, ceiling is upside down)
		if ( trace.plane.normal[2] >= MIN_WALK_NORMAL || trace.plane.normal[2] < -0.1f ) {
			continue;
		}
		if ( trace.surfaceFlags & SURF_NOIMPACT ) {
			continue;
		}
		// must be travelling along the wall, not crashing into it and not
		// already kicking away from it (e.g. just after a wall jump)
		into = DotProduct( vel2d, trace.plane.normal );
		if ( into < -0.55f || into > 0.2f ) {
			continue;
		}

		// engaged: hug the wall, kill the into-wall component
		PM_ClipVelocity( pm->ps->velocity, trace.plane.normal, pm->ps->velocity, OVERCLIP );

		// a small lift the moment you attach while falling, then ride with a
		// capped gentle downward slide
		if ( pm->ps->stats[STAT_WALLRUN] == 0 && pm->ps->velocity[2] < 0 ) {
			pm->ps->velocity[2] = pm_wallRunBoost;
		} else if ( pm->ps->velocity[2] < -pm_wallRunSlideSpeed ) {
			pm->ps->velocity[2] = -pm_wallRunSlideSpeed;
		}

		// keep a wall jump available so you can always kick off the run
		pm->ps->stats[STAT_WALLJUMP_COUNT] = 0;

		// accumulate ride time, sign carries the wall side for the camera
		sign = ( s == 0 ) ? 1 : -1;
		elapsed = abs( pm->ps->stats[STAT_WALLRUN] ) + pml.msec;
		pm->ps->stats[STAT_WALLRUN] = sign * elapsed;
		return qtrue;
	}

detach:
	pm->ps->stats[STAT_WALLRUN] = 0;
	return qfalse;
}

/*
=============
PM_CheckVault

Titanfall-style vault/mantle: coming at a ledge with speed, pull up and over
it while keeping momentum — instead of stalling on the wall or dropping. Traces
forward for a near-vertical face, finds the lip top, and if it sits within
vault range with clear space to stand, launches the player ballistically onto
it preserving most horizontal speed. Self-gating; safe to call each frame.
The "already rising" guard stops it re-firing during the vault's own ascent.
=============
*/
static qboolean PM_CheckVault( void ) {
	vec3_t	fwd, spot, top, end;
	trace_t	trace;
	float	speed, climb, up;

	if ( pm->ps->pm_flags & PMF_RESPAWNED ) {
		return qfalse;
	}
	if ( pm->waterlevel > 1 ) {
		return qfalse;
	}
	if ( pm->cmd.forwardmove <= 0 ) {
		return qfalse;				// must be driving into the ledge
	}
	if ( pm->ps->velocity[2] > 50.0f ) {
		return qfalse;				// already rising (just vaulted/jumped)
	}

	speed = sqrt( pm->ps->velocity[0] * pm->ps->velocity[0]
		+ pm->ps->velocity[1] * pm->ps->velocity[1] );
	if ( speed < pm_vaultMinSpeed ) {
		return qfalse;				// momentum vault only
	}

	fwd[0] = pml.forward[0];
	fwd[1] = pml.forward[1];
	fwd[2] = 0;
	if ( VectorNormalize( fwd ) < 0.1f ) {
		return qfalse;
	}

	// 1) a near-vertical face just ahead?
	VectorMA( pm->ps->origin, pm_vaultReach, fwd, spot );
	pm->trace( &trace, pm->ps->origin, pm->mins, pm->maxs, spot,
		pm->ps->clientNum, pm->tracemask );
	if ( trace.fraction == 1.0f || trace.allsolid ) {
		return qfalse;				// nothing to vault
	}
	if ( trace.plane.normal[2] >= MIN_WALK_NORMAL || trace.plane.normal[2] < -0.1f ) {
		return qfalse;				// floor/ceiling, not a wall
	}
	if ( trace.surfaceFlags & SURF_NOIMPACT ) {
		return qfalse;
	}

	// 2) find the lip top: from above & just past the face, trace straight down
	VectorMA( pm->ps->origin, pm_vaultReach + 8.0f, fwd, top );
	top[2] += pm_vaultMaxHeight + 8.0f;
	VectorCopy( top, end );
	end[2] -= pm_vaultMaxHeight + 16.0f;
	pm->trace( &trace, top, pm->mins, pm->maxs, end,
		pm->ps->clientNum, pm->tracemask );
	if ( trace.startsolid || trace.allsolid || trace.fraction == 1.0f ) {
		return qfalse;				// no clear standable lip
	}

	climb = trace.endpos[2] - pm->ps->origin[2];
	if ( climb < pm_vaultMinHeight || climb > pm_vaultMaxHeight ) {
		return qfalse;				// ledge not in vault range
	}

	// VAULT: ballistic lift to clear the lip, keep most horizontal speed
	up = sqrt( 2.0f * pm->ps->gravity * ( climb + 16.0f ) );
	pm->ps->velocity[2] = up;
	pm->ps->velocity[0] *= pm_vaultSpeedKeep;
	pm->ps->velocity[1] *= pm_vaultSpeedKeep;
	pm->ps->groundEntityNum = ENTITYNUM_NONE;
	pml.groundPlane = qfalse;
	pml.walking = qfalse;
	PM_AddEvent( EV_JUMP );
	return qtrue;
}

/*
=============
PM_AirControl

CPM-style: holding only forward lets the player curve velocity toward the
view direction WITHOUT changing speed. Deliberately forward-only: during
strafe (A/D) the vanilla strafe-accel model gains speed by projection, and
running this speed-preserving redirect on top of it cancels that gain — the
two air models must not both run. The turn rate is compensated for the
SUPERHOT world timescale so air steering feels identical at any slow-mo.
=============
*/
static void PM_AirControl( vec3_t wishdir, float wishspeed ) {
	float	zspeed, speed, dot, k;
	int		i;

	if ( pm->cmd.forwardmove == 0 || pm->cmd.rightmove != 0 || wishspeed == 0 ) {
		return;
	}

	zspeed = pm->ps->velocity[2];
	pm->ps->velocity[2] = 0;
	speed = VectorNormalize( pm->ps->velocity );

	dot = DotProduct( pm->ps->velocity, wishdir );
	// divide by timeScale so the player's turn-per-real-second is constant
	k = 32 * pm_airControlAmount * dot * dot * pml.frametime / pm->timeScale;

	// only turn, never gain or lose speed
	if ( dot > 0 ) {
		for ( i = 0 ; i < 2 ; i++ ) {
			pm->ps->velocity[i] = pm->ps->velocity[i] * speed + wishdir[i] * k;
		}
		VectorNormalize( pm->ps->velocity );
	}

	for ( i = 0 ; i < 2 ; i++ ) {
		pm->ps->velocity[i] *= speed;
	}
	pm->ps->velocity[2] = zspeed;
}

/*
===================
PM_AirMove

===================
*/
static void PM_AirMove( void ) {
	int			i;
	vec3_t		wishvel;
	float		fmove, smove;
	vec3_t		wishdir;
	float		wishspeed;
	float		scale;
	usercmd_t	cmd;
	float		accel;
	float		wishspeed2;

	PM_Friction();

	// walls take priority over the air jump on a fresh press
	if ( !PM_CheckWallJump() ) {
		PM_CheckAirJump();
	}

	fmove = pm->cmd.forwardmove;
	smove = pm->cmd.rightmove;

	cmd = pm->cmd;
	scale = PM_CmdScale( &cmd );

	// set the movementDir so clients can rotate the legs for strafing
	PM_SetMovementDir();

	// project moves down to flat plane
	pml.forward[2] = 0;
	pml.right[2] = 0;
	VectorNormalize (pml.forward);
	VectorNormalize (pml.right);

	for ( i = 0 ; i < 2 ; i++ ) {
		wishvel[i] = pml.forward[i]*fmove + pml.right[i]*smove;
	}
	wishvel[2] = 0;

	VectorCopy (wishvel, wishdir);
	wishspeed = VectorNormalize(wishdir);
	wishspeed *= scale;

	// CPM-style air movement on top of vanilla strafe jumping:
	// pushing against current velocity stops faster, and A/D-only
	// strafing uses a high accel against a tiny wishspeed cap
	wishspeed2 = wishspeed;
	if ( DotProduct( pm->ps->velocity, wishdir ) < 0 ) {
		accel = pm_airStopAccelerate;
	} else {
		accel = pm_airaccelerate;
	}
	if ( pm->cmd.forwardmove == 0 && pm->cmd.rightmove != 0 ) {
		if ( wishspeed > pm_wishSpeedClamp ) {
			wishspeed = pm_wishSpeedClamp;
		}
		accel = pm_strafeAccelerate;
	}

	// divide accel by the world timescale so air strafing builds speed at the
	// same real-time rate whether the world is slowed or not (PM_Accelerate
	// still clamps to wishspeed, so this only restores responsiveness)
	PM_Accelerate (wishdir, wishspeed, accel / pm->timeScale);

	// curve velocity toward view (now active for strafe holds too)
	PM_AirControl( wishdir, wishspeed2 );

	// we may have a ground plane that is very steep, even
	// though we don't have a groundentity
	// slide along the steep plane
	if ( pml.groundPlane ) {
		PM_ClipVelocity (pm->ps->velocity, pml.groundTrace.plane.normal, 
			pm->ps->velocity, OVERCLIP );
	}

#if 0
	//ZOID:  If we are on the grapple, try stair-stepping
	//this allows a player to use the grapple to pull himself
	//over a ledge
	if (pm->ps->pm_flags & PMF_GRAPPLE_PULL)
		PM_StepSlideMove ( qtrue );
	else
		PM_SlideMove ( qtrue );
#endif

	// Titanfall-style vault: clip a ledge with speed and pull up over it,
	// keeping momentum — runs before the slide move so the lift applies
	PM_CheckVault();

	// Titanfall-style wall run: while hugging a wall, fall under greatly
	// reduced gravity for the slide move, then restore it (networked
	// ps->gravity is unchanged at frame end, so prediction stays exact)
	if ( PM_CheckWallRun() ) {
		int savedGravity = pm->ps->gravity;
		pm->ps->gravity = (int)( pm->ps->gravity * pm_wallRunGravityScale );
		PM_StepSlideMove ( qtrue );
		pm->ps->gravity = savedGravity;
	} else {
		PM_StepSlideMove ( qtrue );
	}
}

/*
===================
PM_GrappleMove

===================
*/
static void PM_GrappleMove( void ) {
	vec3_t	pull;
	float	dist, ropeLen, radial, target;

	VectorSubtract( pm->ps->grapplePoint, pm->ps->origin, pull );
	dist = VectorNormalize( pull );		// pull now points toward the anchor

	// seize the rope length the first frame after attaching, then keep it
	// fixed unless the player reels — the rope is a constraint, not a winch
	ropeLen = pm->ps->stats[STAT_GRAPPLE_LEN];
	if ( ropeLen < 64 ) {
		ropeLen = dist;
		if ( ropeLen < 64 ) {
			ropeLen = 64;
		} else if ( ropeLen > 2000 ) {
			ropeLen = 2000;		// keep within the 16-bit stat range
		}
	}

	// reel: jump shortens the rope to pull yourself in and build speed,
	// crouch lengthens it to drop. shortening leaves the constraint below
	// overstretched, which it converts into smooth inward speed — so you
	// can wind up a swing or rip straight toward a ceiling anchor.
	if ( pm->cmd.upmove > 0 ) {
		ropeLen -= pm_grappleReel * pml.frametime;
		if ( ropeLen < 64 ) {
			ropeLen = 64;
		}
	} else if ( pm->cmd.upmove < 0 ) {
		ropeLen += pm_grappleReel * pml.frametime;
		if ( ropeLen > 2000 ) {
			ropeLen = 2000;		// keep within the 16-bit stat range
		}
	}
	pm->ps->stats[STAT_GRAPPLE_LEN] = (int)ropeLen;

	// pendulum: a rope only pulls, never pushes, so it bites only when taut.
	// at rope length it cancels just the velocity heading further out (and
	// nudges back any overstretch) while leaving every bit of tangential
	// momentum intact — and it never adds inward speed, so you're not reeled
	// in. gravity (applied by PM_AirMove right after) then swings you along
	// the arc, building speed at the bottom to fling out of. inside the
	// radius the rope is slack and you fall freely until it snaps taut.
	if ( dist >= ropeLen ) {
		radial = DotProduct( pm->ps->velocity, pull );	// + = toward the anchor
		target = ( dist - ropeLen ) / pml.frametime;	// inward speed to restore length
		if ( radial < target ) {
			VectorMA( pm->ps->velocity, target - radial, pull, pm->ps->velocity );
		}
	}

	pml.groundPlane = qfalse;
}

/*
===================
PM_WalkMove

===================
*/
static void PM_WalkMove( void ) {
	int			i;
	vec3_t		wishvel;
	float		fmove, smove;
	vec3_t		wishdir;
	float		wishspeed;
	float		scale;
	usercmd_t	cmd;
	float		accelerate;
	float		vel;

	if ( pm->waterlevel > 2 && DotProduct( pml.forward, pml.groundTrace.plane.normal ) > 0 ) {
		// begin swimming
		PM_WaterMove();
		return;
	}


	if ( PM_CheckJump () ) {
		// jumped away
		if ( pm->waterlevel > 1 ) {
			PM_WaterMove();
		} else {
			PM_AirMove();
		}
		return;
	}

	PM_Friction ();

	fmove = pm->cmd.forwardmove;
	smove = pm->cmd.rightmove;

	cmd = pm->cmd;
	scale = PM_CmdScale( &cmd );

	// set the movementDir so clients can rotate the legs for strafing
	PM_SetMovementDir();

	// project moves down to flat plane
	pml.forward[2] = 0;
	pml.right[2] = 0;

	// project the forward and right directions onto the ground plane
	PM_ClipVelocity (pml.forward, pml.groundTrace.plane.normal, pml.forward, OVERCLIP );
	PM_ClipVelocity (pml.right, pml.groundTrace.plane.normal, pml.right, OVERCLIP );
	//
	VectorNormalize (pml.forward);
	VectorNormalize (pml.right);

	for ( i = 0 ; i < 3 ; i++ ) {
		wishvel[i] = pml.forward[i]*fmove + pml.right[i]*smove;
	}
	// when going up or down slopes the wish velocity should Not be zero
//	wishvel[2] = 0;

	VectorCopy (wishvel, wishdir);
	wishspeed = VectorNormalize(wishdir);
	wishspeed *= scale;

	// clamp the speed lower if ducking
	if ( pm->ps->pm_flags & PMF_DUCKED ) {
		if ( wishspeed > pm->ps->speed * pm_duckScale ) {
			wishspeed = pm->ps->speed * pm_duckScale;
		}
	}

	// clamp the speed lower if wading or walking on the bottom
	if ( pm->waterlevel ) {
		float	waterScale;

		waterScale = pm->waterlevel / 3.0;
		waterScale = 1.0 - ( 1.0 - pm_swimScale ) * waterScale;
		if ( wishspeed > pm->ps->speed * waterScale ) {
			wishspeed = pm->ps->speed * waterScale;
		}
	}

	// when a player gets hit, they temporarily lose
	// full control, which allows them to be moved a bit
	if ( ( pml.groundTrace.surfaceFlags & SURF_SLICK ) || pm->ps->pm_flags & PMF_TIME_KNOCKBACK ) {
		accelerate = pm_airaccelerate;
	} else {
		accelerate = pm_accelerate;
	}

	PM_Accelerate (wishdir, wishspeed, accelerate);

	//Com_Printf("velocity = %1.1f %1.1f %1.1f\n", pm->ps->velocity[0], pm->ps->velocity[1], pm->ps->velocity[2]);
	//Com_Printf("velocity1 = %1.1f\n", VectorLength(pm->ps->velocity));

	if ( ( pml.groundTrace.surfaceFlags & SURF_SLICK ) || pm->ps->pm_flags & PMF_TIME_KNOCKBACK ) {
		pm->ps->velocity[2] -= pm->ps->gravity * pml.frametime;
	} else {
		// don't reset the z velocity for slopes
//		pm->ps->velocity[2] = 0;
	}

	vel = VectorLength(pm->ps->velocity);

	// slide along the ground plane
	PM_ClipVelocity (pm->ps->velocity, pml.groundTrace.plane.normal, 
		pm->ps->velocity, OVERCLIP );

	// don't decrease velocity when going up or down a slope
	VectorNormalize(pm->ps->velocity);
	VectorScale(pm->ps->velocity, vel, pm->ps->velocity);

	// don't do anything if standing still
	if (!pm->ps->velocity[0] && !pm->ps->velocity[1]) {
		return;
	}

	PM_StepSlideMove( qfalse );

	//Com_Printf("velocity2 = %1.1f\n", VectorLength(pm->ps->velocity));

}


/*
==============
PM_DeadMove
==============
*/
static void PM_DeadMove( void ) {
	float	forward;

	if ( !pml.walking ) {
		return;
	}

	// extra friction

	forward = VectorLength (pm->ps->velocity);
	forward -= 20;
	if ( forward <= 0 ) {
		VectorClear (pm->ps->velocity);
	} else {
		VectorNormalize (pm->ps->velocity);
		VectorScale (pm->ps->velocity, forward, pm->ps->velocity);
	}
}


/*
===============
PM_NoclipMove
===============
*/
static void PM_NoclipMove( void ) {
	float	speed, drop, friction, control, newspeed;
	int			i;
	vec3_t		wishvel;
	float		fmove, smove;
	vec3_t		wishdir;
	float		wishspeed;
	float		scale;

	pm->ps->viewheight = DEFAULT_VIEWHEIGHT;

	// friction

	speed = VectorLength (pm->ps->velocity);
	if (speed < 1)
	{
		VectorCopy (vec3_origin, pm->ps->velocity);
	}
	else
	{
		drop = 0;

		friction = pm_friction*1.5;	// extra friction
		control = speed < pm_stopspeed ? pm_stopspeed : speed;
		drop += control*friction*pml.frametime;

		// scale the velocity
		newspeed = speed - drop;
		if (newspeed < 0)
			newspeed = 0;
		newspeed /= speed;

		VectorScale (pm->ps->velocity, newspeed, pm->ps->velocity);
	}

	// accelerate
	scale = PM_CmdScale( &pm->cmd );

	fmove = pm->cmd.forwardmove;
	smove = pm->cmd.rightmove;
	
	for (i=0 ; i<3 ; i++)
		wishvel[i] = pml.forward[i]*fmove + pml.right[i]*smove;
	wishvel[2] += pm->cmd.upmove;

	VectorCopy (wishvel, wishdir);
	wishspeed = VectorNormalize(wishdir);
	wishspeed *= scale;

	PM_Accelerate( wishdir, wishspeed, pm_accelerate );

	// move
	VectorMA (pm->ps->origin, pml.frametime, pm->ps->velocity, pm->ps->origin);
}

//============================================================================

/*
================
PM_FootstepForSurface

Returns an event number appropriate for the groundsurface
================
*/
static int PM_FootstepForSurface( void ) {
	if ( pml.groundTrace.surfaceFlags & SURF_NOSTEPS ) {
		return 0;
	}
	if ( pml.groundTrace.surfaceFlags & SURF_METALSTEPS ) {
		return EV_FOOTSTEP_METAL;
	}
	return EV_FOOTSTEP;
}


/*
=================
PM_CrashLand

Check for hard landings that generate sound events
=================
*/
static void PM_CrashLand( void ) {
	float		delta;
	float		dist;
	float		vel, acc;
	float		t;
	float		a, b, c, den;

	// decide which landing animation to use
	if ( pm->ps->pm_flags & PMF_BACKWARDS_JUMP ) {
		PM_ForceLegsAnim( LEGS_LANDB );
	} else {
		PM_ForceLegsAnim( LEGS_LAND );
	}

	pm->ps->legsTimer = TIMER_LAND;

	// calculate the exact velocity on landing
	dist = pm->ps->origin[2] - pml.previous_origin[2];
	vel = pml.previous_velocity[2];
	acc = -pm->ps->gravity;

	a = acc / 2;
	b = vel;
	c = -dist;

	den =  b * b - 4 * a * c;
	if ( den < 0 ) {
		return;
	}
	t = (-b - sqrt( den ) ) / ( 2 * a );

	delta = vel + t * acc;
	delta = delta*delta * 0.0001;

	// ducking while falling no longer doubles damage:
	// landing in a crouch slide is the intended play

	// never take falling damage if completely underwater
	if ( pm->waterlevel == 3 ) {
		return;
	}

	// reduce falling damage if there is standing water
	if ( pm->waterlevel == 2 ) {
		delta *= 0.25;
	}
	if ( pm->waterlevel == 1 ) {
		delta *= 0.5;
	}

	if ( delta < 1 ) {
		return;
	}

	// create a local entity event to play the sound

	// SURF_NODAMAGE is used for bounce pads where you don't ever
	// want to take damage or play a crunch sound
	if ( !(pml.groundTrace.surfaceFlags & SURF_NODAMAGE) )  {
		if ( delta > 60 ) {
			PM_AddEvent( EV_FALL_FAR );
		} else if ( delta > 40 ) {
			// this is a pain grunt, so don't play it if dead
			if ( pm->ps->stats[STAT_HEALTH] > 0 ) {
				PM_AddEvent( EV_FALL_MEDIUM );
			}
		} else if ( delta > 7 ) {
			PM_AddEvent( EV_FALL_SHORT );
		} else {
			PM_AddEvent( PM_FootstepForSurface() );
		}
	}

	// start footstep cycle over
	pm->ps->bobCycle = 0;
}

/*
=============
PM_CheckStuck
=============
*/
/*
void PM_CheckStuck(void) {
	trace_t trace;

	pm->trace (&trace, pm->ps->origin, pm->mins, pm->maxs, pm->ps->origin, pm->ps->clientNum, pm->tracemask);
	if (trace.allsolid) {
		//int shit = qtrue;
	}
}
*/

/*
=============
PM_CorrectAllSolid
=============
*/
static int PM_CorrectAllSolid( trace_t *trace ) {
	int			i, j, k;
	vec3_t		point;

	if ( pm->debugLevel ) {
		Com_Printf("%i:allsolid\n", c_pmove);
	}

	// jitter around
	for (i = -1; i <= 1; i++) {
		for (j = -1; j <= 1; j++) {
			for (k = -1; k <= 1; k++) {
				VectorCopy(pm->ps->origin, point);
				point[0] += (float) i;
				point[1] += (float) j;
				point[2] += (float) k;
				pm->trace (trace, point, pm->mins, pm->maxs, point, pm->ps->clientNum, pm->tracemask);
				if ( !trace->allsolid ) {
					point[0] = pm->ps->origin[0];
					point[1] = pm->ps->origin[1];
					point[2] = pm->ps->origin[2] - 0.25;

					pm->trace (trace, pm->ps->origin, pm->mins, pm->maxs, point, pm->ps->clientNum, pm->tracemask);
					pml.groundTrace = *trace;
					return qtrue;
				}
			}
		}
	}

	pm->ps->groundEntityNum = ENTITYNUM_NONE;
	pml.groundPlane = qfalse;
	pml.walking = qfalse;

	return qfalse;
}


/*
=============
PM_GroundTraceMissed

The ground trace didn't hit a surface, so we are in freefall
=============
*/
static void PM_GroundTraceMissed( void ) {
	trace_t		trace;
	vec3_t		point;

	if ( pm->ps->groundEntityNum != ENTITYNUM_NONE ) {
		// we just transitioned into freefall
		if ( pm->debugLevel ) {
			Com_Printf("%i:lift\n", c_pmove);
		}

		// if they aren't in a jumping animation and the ground is a ways away, force into it
		// if we didn't do the trace, the player would be backflipping down staircases
		VectorCopy( pm->ps->origin, point );
		point[2] -= 64;

		pm->trace (&trace, pm->ps->origin, pm->mins, pm->maxs, point, pm->ps->clientNum, pm->tracemask);
		if ( trace.fraction == 1.0 ) {
			if ( pm->cmd.forwardmove >= 0 ) {
				PM_ForceLegsAnim( LEGS_JUMP );
				pm->ps->pm_flags &= ~PMF_BACKWARDS_JUMP;
			} else {
				PM_ForceLegsAnim( LEGS_JUMPB );
				pm->ps->pm_flags |= PMF_BACKWARDS_JUMP;
			}
		}
	}

	pm->ps->groundEntityNum = ENTITYNUM_NONE;
	pml.groundPlane = qfalse;
	pml.walking = qfalse;
}


/*
=============
PM_GroundTrace
=============
*/
static void PM_GroundTrace( void ) {
	vec3_t		point;
	trace_t		trace;

	point[0] = pm->ps->origin[0];
	point[1] = pm->ps->origin[1];
	point[2] = pm->ps->origin[2] - 0.25;

	pm->trace (&trace, pm->ps->origin, pm->mins, pm->maxs, point, pm->ps->clientNum, pm->tracemask);
	pml.groundTrace = trace;

	// do something corrective if the trace starts in a solid...
	if ( trace.allsolid ) {
		if ( !PM_CorrectAllSolid(&trace) )
			return;
	}

	// if the trace didn't hit anything, we are in free fall
	if ( trace.fraction == 1.0 ) {
		PM_GroundTraceMissed();
		pml.groundPlane = qfalse;
		pml.walking = qfalse;
		return;
	}

	// check if getting thrown off the ground
	if ( pm->ps->velocity[2] > 0 && DotProduct( pm->ps->velocity, trace.plane.normal ) > 10 ) {
		if ( pm->debugLevel ) {
			Com_Printf("%i:kickoff\n", c_pmove);
		}
		// go into jump animation
		if ( pm->cmd.forwardmove >= 0 ) {
			PM_ForceLegsAnim( LEGS_JUMP );
			pm->ps->pm_flags &= ~PMF_BACKWARDS_JUMP;
		} else {
			PM_ForceLegsAnim( LEGS_JUMPB );
			pm->ps->pm_flags |= PMF_BACKWARDS_JUMP;
		}

		pm->ps->groundEntityNum = ENTITYNUM_NONE;
		pml.groundPlane = qfalse;
		pml.walking = qfalse;
		return;
	}
	
	// slopes that are too steep will not be considered onground
	if ( trace.plane.normal[2] < MIN_WALK_NORMAL ) {
		if ( pm->debugLevel ) {
			Com_Printf("%i:steep\n", c_pmove);
		}
		// FIXME: if they can't slide down the slope, let them
		// walk (sharp crevices)
		pm->ps->groundEntityNum = ENTITYNUM_NONE;
		pml.groundPlane = qtrue;
		pml.walking = qfalse;
		return;
	}

	pml.groundPlane = qtrue;
	pml.walking = qtrue;

	// hitting solid ground will end a waterjump
	if (pm->ps->pm_flags & PMF_TIME_WATERJUMP)
	{
		pm->ps->pm_flags &= ~(PMF_TIME_WATERJUMP | PMF_TIME_LAND);
		pm->ps->pm_time = 0;
	}

	if ( pm->ps->groundEntityNum == ENTITYNUM_NONE ) {
		// just hit the ground
		if ( pm->debugLevel ) {
			Com_Printf("%i:Land\n", c_pmove);
		}
		
		PM_CrashLand();

		// don't do landing time if we were just going down a slope
		if ( pml.previous_velocity[2] < -200 ) {
			// don't allow another jump for a little while
			pm->ps->pm_flags |= PMF_TIME_LAND;
			pm->ps->pm_time = 250;
		}
	}

	pm->ps->groundEntityNum = trace.entityNum;

	// don't reset the z velocity for slopes
//	pm->ps->velocity[2] = 0;

	PM_AddTouchEnt( trace.entityNum );
}


/*
=============
PM_SetWaterLevel	FIXME: avoid this twice?  certainly if not moving
=============
*/
static void PM_SetWaterLevel( void ) {
	vec3_t		point;
	int			cont;
	int			sample1;
	int			sample2;

	//
	// get waterlevel, accounting for ducking
	//
	pm->waterlevel = 0;
	pm->watertype = 0;

	point[0] = pm->ps->origin[0];
	point[1] = pm->ps->origin[1];
	point[2] = pm->ps->origin[2] + MINS_Z + 1;	
	cont = pm->pointcontents( point, pm->ps->clientNum );

	if ( cont & MASK_WATER ) {
		sample2 = pm->ps->viewheight - MINS_Z;
		sample1 = sample2 / 2;

		pm->watertype = cont;
		pm->waterlevel = 1;
		point[2] = pm->ps->origin[2] + MINS_Z + sample1;
		cont = pm->pointcontents (point, pm->ps->clientNum );
		if ( cont & MASK_WATER ) {
			pm->waterlevel = 2;
			point[2] = pm->ps->origin[2] + MINS_Z + sample2;
			cont = pm->pointcontents (point, pm->ps->clientNum );
			if ( cont & MASK_WATER ){
				pm->waterlevel = 3;
			}
		}
	}

}

/*
==============
PM_CheckDuck

Sets mins, maxs, and pm->ps->viewheight
==============
*/
static void PM_CheckDuck (void)
{
	trace_t	trace;

	if ( pm->ps->powerups[PW_INVULNERABILITY] ) {
		if ( pm->ps->pm_flags & PMF_INVULEXPAND ) {
			// invulnerability sphere has a 42 units radius
			VectorSet( pm->mins, -INVUL_RADIUS, -INVUL_RADIUS, -INVUL_RADIUS );
			VectorSet( pm->maxs, INVUL_RADIUS, INVUL_RADIUS, INVUL_RADIUS );
		}
		else {
			VectorSet( pm->mins, -PLAYER_WIDTH, -PLAYER_WIDTH, MINS_Z );
			VectorSet( pm->maxs, PLAYER_WIDTH, PLAYER_WIDTH, 16 );
		}
		pm->ps->pm_flags |= PMF_DUCKED;
		pm->ps->viewheight = CROUCH_VIEWHEIGHT;
		return;
	}
	pm->ps->pm_flags &= ~PMF_INVULEXPAND;

	pm->mins[0] = -PLAYER_WIDTH;
	pm->mins[1] = -PLAYER_WIDTH;

	pm->maxs[0] = PLAYER_WIDTH;
	pm->maxs[1] = PLAYER_WIDTH;

	pm->mins[2] = MINS_Z;

	if (pm->ps->pm_type == PM_DEAD)
	{
		pm->maxs[2] = DEAD_HEIGHT;
		pm->ps->viewheight = DEAD_VIEWHEIGHT;
		return;
	}

	if (pm->cmd.upmove < 0)
	{	// duck
		pm->ps->pm_flags |= PMF_DUCKED;
	}
	else
	{	// stand up if possible
		if (pm->ps->pm_flags & PMF_DUCKED)
		{
			// try to stand up
			pm->maxs[2] = DEFAULT_HEIGHT;
			pm->trace (&trace, pm->ps->origin, pm->mins, pm->maxs, pm->ps->origin, pm->ps->clientNum, pm->tracemask );
			if (!trace.allsolid)
				pm->ps->pm_flags &= ~PMF_DUCKED;
		}
	}

	if (pm->ps->pm_flags & PMF_DUCKED)
	{
		pm->maxs[2] = CROUCH_HEIGHT;
		pm->ps->viewheight = CROUCH_VIEWHEIGHT;
	}
	else
	{
		pm->maxs[2] = DEFAULT_HEIGHT;
		pm->ps->viewheight = DEFAULT_VIEWHEIGHT;
	}
}



//===================================================================


/*
===============
PM_Footsteps
===============
*/
static void PM_Footsteps( void ) {
	float		bobmove;
	int			old;
	qboolean	footstep;

	//
	// calculate speed and cycle to be used for
	// all cyclic walking effects
	//
	pm->xyspeed = sqrt( pm->ps->velocity[0] * pm->ps->velocity[0]
		+  pm->ps->velocity[1] * pm->ps->velocity[1] );

	if ( pm->ps->groundEntityNum == ENTITYNUM_NONE ) {

		if ( pm->ps->powerups[PW_INVULNERABILITY] ) {
			PM_ContinueLegsAnim( LEGS_IDLECR );
		}
		// airborne leaves position in cycle intact, but doesn't advance
		if ( pm->waterlevel > 1 ) {
			PM_ContinueLegsAnim( LEGS_SWIM );
		}
		return;
	}

	// if not trying to move
	if ( !pm->cmd.forwardmove && !pm->cmd.rightmove ) {
		if (  pm->xyspeed < 5 ) {
			pm->ps->bobCycle = 0;	// start at beginning of cycle again
			if ( pm->ps->pm_flags & PMF_DUCKED ) {
				PM_ContinueLegsAnim( LEGS_IDLECR );
			} else {
				PM_ContinueLegsAnim( LEGS_IDLE );
			}
		}
		return;
	}
	

	footstep = qfalse;

	if ( pm->ps->pm_flags & PMF_DUCKED ) {
		bobmove = 0.5;	// ducked characters bob much faster
		if ( pm->ps->pm_flags & PMF_BACKWARDS_RUN ) {
			PM_ContinueLegsAnim( LEGS_BACKCR );
		}
		else {
			PM_ContinueLegsAnim( LEGS_WALKCR );
		}
		// ducked characters never play footsteps
	/*
	} else 	if ( pm->ps->pm_flags & PMF_BACKWARDS_RUN ) {
		if ( !( pm->cmd.buttons & BUTTON_WALKING ) ) {
			bobmove = 0.4;	// faster speeds bob faster
			footstep = qtrue;
		} else {
			bobmove = 0.3;
		}
		PM_ContinueLegsAnim( LEGS_BACK );
	*/
	} else {
		if ( !( pm->cmd.buttons & BUTTON_WALKING ) ) {
			bobmove = 0.4f;	// faster speeds bob faster
			if ( pm->ps->pm_flags & PMF_BACKWARDS_RUN ) {
				PM_ContinueLegsAnim( LEGS_BACK );
			}
			else {
				PM_ContinueLegsAnim( LEGS_RUN );
			}
			footstep = qtrue;
		} else {
			bobmove = 0.3f;	// walking bobs slow
			if ( pm->ps->pm_flags & PMF_BACKWARDS_RUN ) {
				PM_ContinueLegsAnim( LEGS_BACKWALK );
			}
			else {
				PM_ContinueLegsAnim( LEGS_WALK );
			}
		}
	}

	// check for footstep / splash sounds
	old = pm->ps->bobCycle;
	pm->ps->bobCycle = (int)( old + bobmove * pml.msec ) & 255;

	// if we just crossed a cycle boundary, play an appropriate footstep event
	if ( ( ( old + 64 ) ^ ( pm->ps->bobCycle + 64 ) ) & 128 ) {
		if ( pm->waterlevel == 0 ) {
			// on ground will only play sounds if running
			if ( footstep && !pm->noFootsteps ) {
				PM_AddEvent( PM_FootstepForSurface() );
			}
		} else if ( pm->waterlevel == 1 ) {
			// splashing
			PM_AddEvent( EV_FOOTSPLASH );
		} else if ( pm->waterlevel == 2 ) {
			// wading / swimming at surface
			PM_AddEvent( EV_SWIM );
		} else if ( pm->waterlevel == 3 ) {
			// no sound when completely underwater

		}
	}
}

/*
==============
PM_WaterEvents

Generate sound events for entering and leaving water
==============
*/
static void PM_WaterEvents( void ) {		// FIXME?
	//
	// if just entered a water volume, play a sound
	//
	if (!pml.previous_waterlevel && pm->waterlevel) {
		PM_AddEvent( EV_WATER_TOUCH );
	}

	//
	// if just completely exited a water volume, play a sound
	//
	if (pml.previous_waterlevel && !pm->waterlevel) {
		PM_AddEvent( EV_WATER_LEAVE );
	}

	//
	// check for head just going under water
	//
	if (pml.previous_waterlevel != 3 && pm->waterlevel == 3) {
		PM_AddEvent( EV_WATER_UNDER );
	}

	//
	// check for head just coming out of water
	//
	if (pml.previous_waterlevel == 3 && pm->waterlevel != 3) {
		PM_AddEvent( EV_WATER_CLEAR );
	}
}


/*
===============
PM_BeginWeaponChange
===============
*/
static void PM_BeginWeaponChange( int weapon ) {
	if ( weapon <= WP_NONE || weapon >= WP_NUM_WEAPONS ) {
		return;
	}

	if ( !( pm->ps->stats[STAT_WEAPONS] & ( 1 << weapon ) ) ) {
		return;
	}
	
	if ( pm->ps->weaponstate == WEAPON_DROPPING ) {
		return;
	}

	PM_AddEvent( EV_CHANGE_WEAPON );
	pm->ps->weaponstate = WEAPON_DROPPING;
	pm->ps->weaponTime += 100;		// fast switch: combat shouldn't slow the run
	PM_StartTorsoAnim( TORSO_DROP );
}


/*
===============
PM_FinishWeaponChange
===============
*/
static void PM_FinishWeaponChange( void ) {
	int		weapon;

	weapon = pm->cmd.weapon;
	if ( weapon < WP_NONE || weapon >= WP_NUM_WEAPONS ) {
		weapon = WP_NONE;
	}

	if ( !( pm->ps->stats[STAT_WEAPONS] & ( 1 << weapon ) ) ) {
		weapon = WP_NONE;
	}

	pm->ps->weapon = weapon;
	pm->ps->weaponstate = WEAPON_RAISING;
	pm->ps->weaponTime += 100;		// fast switch: combat shouldn't slow the run
	PM_StartTorsoAnim( TORSO_RAISE );
}


/*
==============
PM_TorsoAnimation

==============
*/
static void PM_TorsoAnimation( void ) {
	if ( pm->ps->weaponstate == WEAPON_READY ) {
		if ( pm->ps->weapon == WP_GAUNTLET ) {
			PM_ContinueTorsoAnim( TORSO_STAND2 );
		} else {
			PM_ContinueTorsoAnim( TORSO_STAND );
		}
		return;
	}
}


/*
==============
PM_Weapon

Generates weapon events and modifes the weapon counter
==============
*/
static void PM_Weapon( void ) {
	int		addTime;

	// don't allow attack until all buttons are up
	if ( pm->ps->pm_flags & PMF_RESPAWNED ) {
		return;
	}

	// ignore if spectator
	if ( pm->ps->persistant[PERS_TEAM] == TEAM_SPECTATOR ) {
		return;
	}

	// check for dead player
	if ( pm->ps->stats[STAT_HEALTH] <= 0 ) {
		pm->ps->weapon = WP_NONE;
		pm->ps->eFlags &= ~EF_BLOCKING;
		return;
	}

	// STRAFE 64: katana guard. Holding block (right-click) raises the blade.
	// While up it deflects frontal projectiles and soaks frontal melee/blast
	// (resolved server-side); here it just sets the networked state and
	// suppresses your own swing. Sword-only, and not while a swing is mid-flight.
	pm->ps->eFlags &= ~EF_BLOCKING;
	if ( ( pm->cmd.buttons & BUTTON_BLOCK ) && pm->ps->weapon == WP_SWORD
			&& pm->ps->weaponstate != WEAPON_FIRING && pm->ps->weaponTime <= 0 ) {
		pm->ps->eFlags |= EF_BLOCKING;
	}

	// check for item using
	if ( pm->cmd.buttons & BUTTON_USE_HOLDABLE ) {
		if ( ! ( pm->ps->pm_flags & PMF_USE_ITEM_HELD ) ) {
			if ( bg_itemlist[pm->ps->stats[STAT_HOLDABLE_ITEM]].giTag == HI_MEDKIT
				&& pm->ps->stats[STAT_HEALTH] >= (pm->ps->stats[STAT_MAX_HEALTH] + 25) ) {
				// don't use medkit if at max health
			} else {
				pm->ps->pm_flags |= PMF_USE_ITEM_HELD;
				PM_AddEvent( EV_USE_ITEM0 + bg_itemlist[pm->ps->stats[STAT_HOLDABLE_ITEM]].giTag );
				pm->ps->stats[STAT_HOLDABLE_ITEM] = 0;
			}
			return;
		}
	} else {
		pm->ps->pm_flags &= ~PMF_USE_ITEM_HELD;
	}


	// make weapon function
	if ( pm->ps->weaponTime > 0 ) {
		pm->ps->weaponTime -= pml.msec;
	}

	// check for weapon change
	// can't change if weapon is firing, but can change
	// again if lowering or raising
	if ( pm->ps->weaponTime <= 0 || pm->ps->weaponstate != WEAPON_FIRING ) {
		if ( pm->ps->weapon != pm->cmd.weapon ) {
			PM_BeginWeaponChange( pm->cmd.weapon );
		}
	}

	if ( pm->ps->weaponTime > 0 ) {
		return;
	}

	// change weapon if time
	if ( pm->ps->weaponstate == WEAPON_DROPPING ) {
		PM_FinishWeaponChange();
		return;
	}

	if ( pm->ps->weaponstate == WEAPON_RAISING ) {
		pm->ps->weaponstate = WEAPON_READY;
		if ( pm->ps->weapon == WP_GAUNTLET ) {
			PM_StartTorsoAnim( TORSO_STAND2 );
		} else {
			PM_StartTorsoAnim( TORSO_STAND );
		}
		return;
	}

	// guard up: hold the blade, don't swing
	if ( pm->ps->eFlags & EF_BLOCKING ) {
		pm->ps->weaponTime = 0;
		pm->ps->weaponstate = WEAPON_READY;
		return;
	}

	// check for fire
	if ( ! (pm->cmd.buttons & BUTTON_ATTACK) ) {
		pm->ps->weaponTime = 0;
		pm->ps->weaponstate = WEAPON_READY;
		return;
	}

	// start the animation even if out of ammo
	if ( pm->ps->weapon == WP_GAUNTLET ) {
		// the guantlet only "fires" when it actually hits something
		if ( !pm->gauntletHit ) {
			pm->ps->weaponTime = 0;
			pm->ps->weaponstate = WEAPON_READY;
			return;
		}
		PM_StartTorsoAnim( TORSO_ATTACK2 );
	} else if ( pm->ps->weapon == WP_SWORD ) {
		// alternate the two slash anims so a held attack reads as a fluid combo
		if ( ( pm->ps->torsoAnim & ~ANIM_TOGGLEBIT ) == TORSO_ATTACK ) {
			PM_StartTorsoAnim( TORSO_ATTACK2 );
		} else {
			PM_StartTorsoAnim( TORSO_ATTACK );
		}
	} else {
		PM_StartTorsoAnim( TORSO_ATTACK );
	}

	pm->ps->weaponstate = WEAPON_FIRING;

	// check for out of ammo
	if ( ! pm->ps->ammo[ pm->ps->weapon ] ) {
		PM_AddEvent( EV_NOAMMO );
		pm->ps->weaponTime += 500;
		return;
	}

	// take an ammo away if not infinite
	if ( pm->ps->ammo[ pm->ps->weapon ] != -1 ) {
		pm->ps->ammo[ pm->ps->weapon ]--;
	}

	// fire weapon
	PM_AddEvent( EV_FIRE_WEAPON );

	// STRAFE 64: the sword lunges forward on each swing so melee feeds the
	// movement chain instead of stalling it. Capped so it engages but can't
	// be milked as a speed exploit.
	if ( pm->ps->weapon == WP_SWORD ) {
		vec3_t	flatforward;
		float	speed;

		flatforward[0] = pml.forward[0];
		flatforward[1] = pml.forward[1];
		flatforward[2] = 0;
		VectorNormalize( flatforward );

		speed = sqrt( pm->ps->velocity[0] * pm->ps->velocity[0]
			+ pm->ps->velocity[1] * pm->ps->velocity[1] );
		if ( speed < 1000 ) {
			// step into each cut — flow between targets instead of rooting
			VectorMA( pm->ps->velocity, 120, flatforward, pm->ps->velocity );
		}
	}

	switch( pm->ps->weapon ) {
	default:
	case WP_GAUNTLET:
		addTime = 400;
		break;
	case WP_SWORD:
		addTime = 230;		// brisk hack-and-slash cadence (not machine-gun fast)
		break;
	case WP_LIGHTNING:
		addTime = 50;
		break;
	case WP_SHOTGUN:
		addTime = 1000;
		break;
	case WP_MACHINEGUN:
		addTime = 100;
		break;
	case WP_GRENADE_LAUNCHER:
		addTime = 800;
		break;
	case WP_ROCKET_LAUNCHER:
		addTime = 800;
		break;
	case WP_PLASMAGUN:
		addTime = 100;
		break;
	case WP_RAILGUN:
		addTime = 1500;
		// vectorgun: rate of fire is a function of speed — 1500 ms at a
		// standstill tapering to 450 ms at 960+ ups. damage and spread
		// already scale server side; this closes the loop in prediction
		if ( pm->vectorgun ) {
			float	vgSpeed;

			vgSpeed = sqrt( pm->ps->velocity[0] * pm->ps->velocity[0]
				+ pm->ps->velocity[1] * pm->ps->velocity[1] );
			if ( vgSpeed > 960 ) {
				vgSpeed = 960;
			}
			addTime = 1500 - (int)( 1050 * vgSpeed / 960 );
		}
		break;
	case WP_BFG:
		addTime = 200;
		break;
	case WP_GRAPPLING_HOOK:
		addTime = 400;
		break;
#ifdef MISSIONPACK
	case WP_NAILGUN:
		addTime = 1000;
		break;
	case WP_PROX_LAUNCHER:
		addTime = 800;
		break;
	case WP_CHAINGUN:
		addTime = 30;
		break;
#endif
	}

#ifdef MISSIONPACK
	if( bg_itemlist[pm->ps->stats[STAT_PERSISTANT_POWERUP]].giTag == PW_SCOUT ) {
		addTime /= 1.5;
	}
	else
	if( bg_itemlist[pm->ps->stats[STAT_PERSISTANT_POWERUP]].giTag == PW_AMMOREGEN ) {
		addTime /= 1.3;
	}
	else
#endif
	if ( pm->ps->powerups[PW_HASTE] ) {
		addTime /= 1.3;
	}

	pm->ps->weaponTime += addTime;
}

/*
================
PM_Animate
================
*/

static void PM_Animate( void ) {
	if ( pm->cmd.buttons & BUTTON_GESTURE ) {
		if ( pm->ps->torsoTimer == 0 ) {
			PM_StartTorsoAnim( TORSO_GESTURE );
			pm->ps->torsoTimer = TIMER_GESTURE;
			PM_AddEvent( EV_TAUNT );
		}
#ifdef MISSIONPACK
	} else if ( pm->cmd.buttons & BUTTON_GETFLAG ) {
		if ( pm->ps->torsoTimer == 0 ) {
			PM_StartTorsoAnim( TORSO_GETFLAG );
			pm->ps->torsoTimer = 600;	//TIMER_GESTURE;
		}
	} else if ( pm->cmd.buttons & BUTTON_GUARDBASE ) {
		if ( pm->ps->torsoTimer == 0 ) {
			PM_StartTorsoAnim( TORSO_GUARDBASE );
			pm->ps->torsoTimer = 600;	//TIMER_GESTURE;
		}
	} else if ( pm->cmd.buttons & BUTTON_PATROL ) {
		if ( pm->ps->torsoTimer == 0 ) {
			PM_StartTorsoAnim( TORSO_PATROL );
			pm->ps->torsoTimer = 600;	//TIMER_GESTURE;
		}
	} else if ( pm->cmd.buttons & BUTTON_FOLLOWME ) {
		if ( pm->ps->torsoTimer == 0 ) {
			PM_StartTorsoAnim( TORSO_FOLLOWME );
			pm->ps->torsoTimer = 600;	//TIMER_GESTURE;
		}
	} else if ( pm->cmd.buttons & BUTTON_AFFIRMATIVE ) {
		if ( pm->ps->torsoTimer == 0 ) {
			PM_StartTorsoAnim( TORSO_AFFIRMATIVE);
			pm->ps->torsoTimer = 600;	//TIMER_GESTURE;
		}
	} else if ( pm->cmd.buttons & BUTTON_NEGATIVE ) {
		if ( pm->ps->torsoTimer == 0 ) {
			PM_StartTorsoAnim( TORSO_NEGATIVE );
			pm->ps->torsoTimer = 600;	//TIMER_GESTURE;
		}
#endif
	}
}


/*
================
PM_DropTimers
================
*/
static void PM_DropTimers( void ) {
	// drop misc timing counter
	if ( pm->ps->pm_time ) {
		if ( pml.msec >= pm->ps->pm_time ) {
			pm->ps->pm_flags &= ~PMF_ALL_TIMES;
			pm->ps->pm_time = 0;
		} else {
			pm->ps->pm_time -= pml.msec;
		}
	}

	// drop animation counter
	if ( pm->ps->legsTimer > 0 ) {
		pm->ps->legsTimer -= pml.msec;
		if ( pm->ps->legsTimer < 0 ) {
			pm->ps->legsTimer = 0;
		}
	}

	if ( pm->ps->torsoTimer > 0 ) {
		pm->ps->torsoTimer -= pml.msec;
		if ( pm->ps->torsoTimer < 0 ) {
			pm->ps->torsoTimer = 0;
		}
	}
}

/*
================
PM_UpdateMovementTimers

Bookkeeping for the movement mod: ground time, hop chain, jump windows
================
*/
static void PM_UpdateMovementTimers( void ) {
	// real-time msec: undo the SUPERHOT world timescale so movement windows
	// (double jump, bhop chain) stay the same real-time length at any slow-mo
	int realMsec = (int)( pml.msec / pm->timeScale + 0.5f );
	if ( realMsec < 1 ) {
		realMsec = 1;
	}

	// count down the double jump window
	if ( pm->ps->stats[STAT_JUMP_MS] > 0 ) {
		pm->ps->stats[STAT_JUMP_MS] -= realMsec;
		if ( pm->ps->stats[STAT_JUMP_MS] < 0 ) {
			pm->ps->stats[STAT_JUMP_MS] = 0;
		}
	}

	if ( pml.walking ) {
		// accumulate grounded time; too long on the ground breaks the chain
		pm->ps->stats[STAT_GROUND_MS] += realMsec;
		if ( pm->ps->stats[STAT_GROUND_MS] > 999 ) {
			pm->ps->stats[STAT_GROUND_MS] = 999;
		}
		if ( pm->ps->stats[STAT_GROUND_MS] > pm_bhopWindowMs ) {
			pm->ps->stats[STAT_BHOP_STREAK] = 0;
		}
		// grounding refunds walljumps, the air jump and the wall run
		pm->ps->stats[STAT_WALLJUMP_COUNT] = 0;
		pm->ps->stats[STAT_AIRJUMP_COUNT] = 0;
		pm->ps->stats[STAT_WALLRUN] = 0;
	} else {
		pm->ps->stats[STAT_GROUND_MS] = 0;
		// SURF pop-off: a steep slope you're touching but can't stand on
		// (groundPlane && !walking) refunds the air jump every frame, so a jump
		// press always launches you off the surface. Without this the surf
		// surface holds you the instant your single air jump is spent — the
		// "hard to pop off" feel. Only on contact; free-fall keeps the 1 cap.
		if ( pml.groundPlane ) {
			pm->ps->stats[STAT_AIRJUMP_COUNT] = 0;
		}
	}
}

/*
================
PM_UpdateViewAngles

This can be used as another entry point when only the viewangles
are being updated instead of a full move
================
*/
void PM_UpdateViewAngles( playerState_t *ps, const usercmd_t *cmd ) {
	short		temp;
	int		i;

	if ( ps->pm_type == PM_INTERMISSION || ps->pm_type == PM_SPINTERMISSION) {
		return;		// no view changes at all
	}

	if ( ps->pm_type != PM_SPECTATOR && ps->stats[STAT_HEALTH] <= 0 ) {
		return;		// no view changes at all
	}

	// circularly clamp the angles with deltas
	for (i=0 ; i<3 ; i++) {
		temp = cmd->angles[i] + ps->delta_angles[i];
		if ( i == PITCH ) {
			// don't let the player look up or down more than 90 degrees
			if ( temp > 16000 ) {
				ps->delta_angles[i] = 16000 - cmd->angles[i];
				temp = 16000;
			} else if ( temp < -16000 ) {
				ps->delta_angles[i] = -16000 - cmd->angles[i];
				temp = -16000;
			}
		}
		ps->viewangles[i] = SHORT2ANGLE(temp);
	}

}


/*
================
PmoveSingle

================
*/
void trap_SnapVector( float *v );

void PmoveSingle (pmove_t *pmove) {
	pm = pmove;

	// this counter lets us debug movement problems with a journal
	// by setting a conditional breakpoint fot the previous frame
	c_pmove++;

	// clear results
	pm->numtouch = 0;
	pm->watertype = 0;
	pm->waterlevel = 0;

	if ( pm->ps->stats[STAT_HEALTH] <= 0 ) {
		pm->tracemask &= ~CONTENTS_BODY;	// corpses can fly through bodies
	}

	// make sure walking button is clear if they are running, to avoid
	// proxy no-footsteps cheats
	if ( abs( pm->cmd.forwardmove ) > 64 || abs( pm->cmd.rightmove ) > 64 ) {
		pm->cmd.buttons &= ~BUTTON_WALKING;
	}

	// set the talk balloon flag
	if ( pm->cmd.buttons & BUTTON_TALK ) {
		pm->ps->eFlags |= EF_TALK;
	} else {
		pm->ps->eFlags &= ~EF_TALK;
	}

	// set the firing flag for continuous beam weapons
	if ( !(pm->ps->pm_flags & PMF_RESPAWNED) && pm->ps->pm_type != PM_INTERMISSION && pm->ps->pm_type != PM_NOCLIP
		&& ( pm->cmd.buttons & BUTTON_ATTACK ) && pm->ps->ammo[ pm->ps->weapon ] ) {
		pm->ps->eFlags |= EF_FIRING;
	} else {
		pm->ps->eFlags &= ~EF_FIRING;
	}

	// clear the respawned flag if attack and use are cleared
	if ( pm->ps->stats[STAT_HEALTH] > 0 && 
		!( pm->cmd.buttons & (BUTTON_ATTACK | BUTTON_USE_HOLDABLE) ) ) {
		pm->ps->pm_flags &= ~PMF_RESPAWNED;
	}

	// if talk button is down, dissallow all other input
	// this is to prevent any possible intercept proxy from
	// adding fake talk balloons
	if ( pmove->cmd.buttons & BUTTON_TALK ) {
		// keep the talk button set tho for when the cmd.serverTime > 66 msec
		// and the same cmd is used multiple times in Pmove
		pmove->cmd.buttons = BUTTON_TALK;
		pmove->cmd.forwardmove = 0;
		pmove->cmd.rightmove = 0;
		pmove->cmd.upmove = 0;
	}

	// clear all pmove local vars
	memset (&pml, 0, sizeof(pml));

	// determine the time
	pml.msec = pmove->cmd.serverTime - pm->ps->commandTime;
	if ( pml.msec < 1 ) {
		pml.msec = 1;
	} else if ( pml.msec > 200 ) {
		pml.msec = 200;
	}
	pm->ps->commandTime = pmove->cmd.serverTime;

	// save old org in case we get stuck
	VectorCopy (pm->ps->origin, pml.previous_origin);

	// save old velocity for crashlanding
	VectorCopy (pm->ps->velocity, pml.previous_velocity);

	pml.frametime = pml.msec * 0.001;

	// update the viewangles
	PM_UpdateViewAngles( pm->ps, &pm->cmd );

	AngleVectors (pm->ps->viewangles, pml.forward, pml.right, pml.up);

	if ( pm->cmd.upmove < 10 ) {
		// not holding jump
		pm->ps->pm_flags &= ~PMF_JUMP_HELD;
	}

	// decide if backpedaling animations should be used
	if ( pm->cmd.forwardmove < 0 ) {
		pm->ps->pm_flags |= PMF_BACKWARDS_RUN;
	} else if ( pm->cmd.forwardmove > 0 || ( pm->cmd.forwardmove == 0 && pm->cmd.rightmove ) ) {
		pm->ps->pm_flags &= ~PMF_BACKWARDS_RUN;
	}

	if ( pm->ps->pm_type >= PM_DEAD ) {
		pm->cmd.forwardmove = 0;
		pm->cmd.rightmove = 0;
		pm->cmd.upmove = 0;
	}

	if ( pm->ps->pm_type == PM_SPECTATOR ) {
		PM_CheckDuck ();
		PM_FlyMove ();
		PM_DropTimers ();
		return;
	}

	if ( pm->ps->pm_type == PM_NOCLIP ) {
		PM_NoclipMove ();
		PM_DropTimers ();
		return;
	}

	if (pm->ps->pm_type == PM_FREEZE) {
		return;		// no movement at all
	}

	if ( pm->ps->pm_type == PM_INTERMISSION || pm->ps->pm_type == PM_SPINTERMISSION) {
		return;		// no movement at all
	}

	// set watertype, and waterlevel
	PM_SetWaterLevel();
	pml.previous_waterlevel = pmove->waterlevel;

	// set mins, maxs, and viewheight
	PM_CheckDuck ();

	// set groundentity
	PM_GroundTrace();

	if ( pm->ps->pm_type == PM_DEAD ) {
		PM_DeadMove ();
	}

	PM_DropTimers();

	PM_UpdateMovementTimers();

#ifdef MISSIONPACK
	if ( pm->ps->powerups[PW_INVULNERABILITY] ) {
		PM_InvulnerabilityMove();
	} else
#endif
	if ( pm->ps->powerups[PW_FLIGHT] ) {
		// flight powerup doesn't allow jump and has different friction
		PM_FlyMove();
	} else if (pm->ps->pm_flags & PMF_GRAPPLE_PULL) {
		PM_GrappleMove();
		// We can wiggle a bit
		PM_AirMove();
	} else if (pm->ps->pm_flags & PMF_TIME_WATERJUMP) {
		PM_WaterJumpMove();
	} else if ( pm->waterlevel > 1 ) {
		// swimming
		PM_WaterMove();
	} else if ( pml.walking ) {
		// walking on ground
		PM_WalkMove();
	} else {
		// airborne
		PM_AirMove();
	}

	PM_Animate();

	// set groundentity, watertype, and waterlevel
	PM_GroundTrace();
	PM_SetWaterLevel();

	// weapons
	PM_Weapon();

	// torso animation
	PM_TorsoAnimation();

	// footstep events / legs animations
	PM_Footsteps();

	// entering / leaving water splashes
	PM_WaterEvents();

	// snap some parts of playerstate to save network bandwidth
	trap_SnapVector( pm->ps->velocity );
}


/*
================
Pmove

Can be called by either the server or the client
================
*/
void Pmove (pmove_t *pmove) {
	int			finalTime;

	// SUPERHOT time-bind divides by timeScale in air/ timing code; callers that
	// don't set it (spectators, tools, zeroed structs) must read as normal time
	if ( pmove->timeScale <= 0.01f ) {
		pmove->timeScale = 1.0f;
	}

	finalTime = pmove->cmd.serverTime;

	if ( finalTime < pmove->ps->commandTime ) {
		return;	// should not happen
	}

	if ( finalTime > pmove->ps->commandTime + 1000 ) {
		pmove->ps->commandTime = finalTime - 1000;
	}

	pmove->ps->pmove_framecount = (pmove->ps->pmove_framecount+1) & ((1<<PS_PMOVEFRAMECOUNTBITS)-1);

	// chop the move up if it is too long, to prevent framerate
	// dependent behavior
	while ( pmove->ps->commandTime != finalTime ) {
		int		msec;

		msec = finalTime - pmove->ps->commandTime;

		if ( pmove->pmove_fixed ) {
			if ( msec > pmove->pmove_msec ) {
				msec = pmove->pmove_msec;
			}
		}
		else {
			if ( msec > 66 ) {
				msec = 66;
			}
		}
		pmove->cmd.serverTime = pmove->ps->commandTime + msec;
		PmoveSingle( pmove );

		if ( pmove->ps->pm_flags & PMF_JUMP_HELD ) {
			pmove->cmd.upmove = 20;
		}
	}

	//PM_CheckStuck();

}

