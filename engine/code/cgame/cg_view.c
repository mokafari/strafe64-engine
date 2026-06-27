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
// cg_view.c -- setup all the parameters (position, angle, etc)
// for a 3D rendering
#include "cg_local.h"


/*
=============================================================================

  MODEL TESTING

The viewthing and gun positioning tools from Q2 have been integrated and
enhanced into a single model testing facility.

Model viewing can begin with either "testmodel <modelname>" or "testgun <modelname>".

The names must be the full pathname after the basedir, like 
"models/weapons/v_launch/tris.md3" or "players/male/tris.md3"

Testmodel will create a fake entity 100 units in front of the current view
position, directly facing the viewer.  It will remain immobile, so you can
move around it to view it from different angles.

Testgun will cause the model to follow the player around and suppress the real
view weapon model.  The default frame 0 of most guns is completely off screen,
so you will probably have to cycle a couple frames to see it.

"nextframe", "prevframe", "nextskin", and "prevskin" commands will change the
frame or skin of the testmodel.  These are bound to F5, F6, F7, and F8 in
q3default.cfg.

If a gun is being tested, the "gun_x", "gun_y", and "gun_z" variables will let
you adjust the positioning.

Note that none of the model testing features update while the game is paused, so
it may be convenient to test with deathmatch set to 1 so that bringing down the
console doesn't pause the game.

=============================================================================
*/

/*
=================
CG_TestModel_f

Creates an entity in front of the current position, which
can then be moved around
=================
*/
void CG_TestModel_f (void) {
	vec3_t		angles;

	cg.testGun = qfalse;
	memset( &cg.testModelEntity, 0, sizeof(cg.testModelEntity) );
	if ( trap_Argc() < 2 ) {
		return;
	}

	Q_strncpyz (cg.testModelName, CG_Argv( 1 ), MAX_QPATH );
	cg.testModelEntity.hModel = trap_R_RegisterModel( cg.testModelName );

	if ( trap_Argc() == 3 ) {
		cg.testModelEntity.backlerp = atof( CG_Argv( 2 ) );
		cg.testModelEntity.frame = 1;
		cg.testModelEntity.oldframe = 0;
	}
	if (! cg.testModelEntity.hModel ) {
		CG_Printf( "Can't register model\n" );
		return;
	}

	VectorMA( cg.refdef.vieworg, 100, cg.refdef.viewaxis[0], cg.testModelEntity.origin );

	angles[PITCH] = 0;
	angles[YAW] = 180 + cg.refdefViewAngles[1];
	angles[ROLL] = 0;

	AnglesToAxis( angles, cg.testModelEntity.axis );
}

/*
=================
CG_TestGun_f

Replaces the current view weapon with the given model
=================
*/
void CG_TestGun_f (void) {
	CG_TestModel_f();

	if ( !cg.testModelEntity.hModel ) {
		return;
	}

	cg.testGun = qtrue;
	cg.testModelEntity.renderfx = RF_MINLIGHT | RF_DEPTHHACK | RF_FIRST_PERSON;
}


void CG_TestModelNextFrame_f (void) {
	cg.testModelEntity.frame++;
	CG_Printf( "frame %i\n", cg.testModelEntity.frame );
}

void CG_TestModelPrevFrame_f (void) {
	cg.testModelEntity.frame--;
	if ( cg.testModelEntity.frame < 0 ) {
		cg.testModelEntity.frame = 0;
	}
	CG_Printf( "frame %i\n", cg.testModelEntity.frame );
}

void CG_TestModelNextSkin_f (void) {
	cg.testModelEntity.skinNum++;
	CG_Printf( "skin %i\n", cg.testModelEntity.skinNum );
}

void CG_TestModelPrevSkin_f (void) {
	cg.testModelEntity.skinNum--;
	if ( cg.testModelEntity.skinNum < 0 ) {
		cg.testModelEntity.skinNum = 0;
	}
	CG_Printf( "skin %i\n", cg.testModelEntity.skinNum );
}

static void CG_AddTestModel (void) {
	int		i;

	// re-register the model, because the level may have changed
	cg.testModelEntity.hModel = trap_R_RegisterModel( cg.testModelName );
	if (! cg.testModelEntity.hModel ) {
		CG_Printf ("Can't register model\n");
		return;
	}

	// if testing a gun, set the origin relative to the view origin
	if ( cg.testGun ) {
		VectorCopy( cg.refdef.vieworg, cg.testModelEntity.origin );
		VectorCopy( cg.refdef.viewaxis[0], cg.testModelEntity.axis[0] );
		VectorCopy( cg.refdef.viewaxis[1], cg.testModelEntity.axis[1] );
		VectorCopy( cg.refdef.viewaxis[2], cg.testModelEntity.axis[2] );

		// allow the position to be adjusted
		for (i=0 ; i<3 ; i++) {
			cg.testModelEntity.origin[i] += cg.refdef.viewaxis[0][i] * cg_gun_x.value;
			cg.testModelEntity.origin[i] += cg.refdef.viewaxis[1][i] * cg_gun_y.value;
			cg.testModelEntity.origin[i] += cg.refdef.viewaxis[2][i] * cg_gun_z.value;
		}
	}

	trap_R_AddRefEntityToScene( &cg.testModelEntity );
}



//============================================================================


/*
=================
CG_CalcVrect

Sets the coordinates of the rendered window
=================
*/
static void CG_CalcVrect (void) {
	int		size;

	// the intermission should allways be full screen
	if ( cg.snap->ps.pm_type == PM_INTERMISSION ) {
		size = 100;
	} else {
		// bound normal viewsize
		if (cg_viewsize.integer < 30) {
			trap_Cvar_Set ("cg_viewsize","30");
			size = 30;
		} else if (cg_viewsize.integer > 100) {
			trap_Cvar_Set ("cg_viewsize","100");
			size = 100;
		} else {
			size = cg_viewsize.integer;
		}

	}
	cg.refdef.width = cgs.glconfig.vidWidth*size/100;
	cg.refdef.width &= ~1;

	cg.refdef.height = cgs.glconfig.vidHeight*size/100;
	cg.refdef.height &= ~1;

	cg.refdef.x = (cgs.glconfig.vidWidth - cg.refdef.width)/2;
	cg.refdef.y = (cgs.glconfig.vidHeight - cg.refdef.height)/2;
}

//==============================================================================


/*
===============
CG_OffsetThirdPersonView

===============
*/
#define	FOCUS_DISTANCE	512
static void CG_OffsetThirdPersonView( void ) {
	vec3_t		forward, right, up;
	vec3_t		view;
	vec3_t		focusAngles;
	trace_t		trace;
	static vec3_t	mins = { -4, -4, -4 };
	static vec3_t	maxs = { 4, 4, 4 };
	vec3_t		focusPoint;
	float		focusDist;
	float		forwardScale, sideScale;

	cg.refdef.vieworg[2] += cg.predictedPlayerState.viewheight;

	VectorCopy( cg.refdefViewAngles, focusAngles );

	// if dead, look at killer
	if ( cg.predictedPlayerState.stats[STAT_HEALTH] <= 0 ) {
		focusAngles[YAW] = cg.predictedPlayerState.stats[STAT_DEAD_YAW];
		cg.refdefViewAngles[YAW] = cg.predictedPlayerState.stats[STAT_DEAD_YAW];
	}

	if ( focusAngles[PITCH] > 45 ) {
		focusAngles[PITCH] = 45;		// don't go too far overhead
	}
	AngleVectors( focusAngles, forward, NULL, NULL );

	VectorMA( cg.refdef.vieworg, FOCUS_DISTANCE, forward, focusPoint );

	VectorCopy( cg.refdef.vieworg, view );

	view[2] += 8;

	cg.refdefViewAngles[PITCH] *= 0.5;

	AngleVectors( cg.refdefViewAngles, forward, right, up );

	forwardScale = cos( cg_thirdPersonAngle.value / 180 * M_PI );
	sideScale = sin( cg_thirdPersonAngle.value / 180 * M_PI );
	VectorMA( view, -cg_thirdPersonRange.value * forwardScale, forward, view );
	VectorMA( view, -cg_thirdPersonRange.value * sideScale, right, view );

	// trace a ray from the origin to the viewpoint to make sure the view isn't
	// in a solid block.  Use an 8 by 8 block to prevent the view from near clipping anything

	if (!cg_cameraMode.integer) {
		CG_Trace( &trace, cg.refdef.vieworg, mins, maxs, view, cg.predictedPlayerState.clientNum, MASK_SOLID );

		if ( trace.fraction != 1.0 ) {
			VectorCopy( trace.endpos, view );
			view[2] += (1.0 - trace.fraction) * 32;
			// try another trace to this position, because a tunnel may have the ceiling
			// close enough that this is poking out

			CG_Trace( &trace, cg.refdef.vieworg, mins, maxs, view, cg.predictedPlayerState.clientNum, MASK_SOLID );
			VectorCopy( trace.endpos, view );
		}
	}


	VectorCopy( view, cg.refdef.vieworg );

	// select pitch to look at focus point from vieword
	VectorSubtract( focusPoint, cg.refdef.vieworg, focusPoint );
	focusDist = sqrt( focusPoint[0] * focusPoint[0] + focusPoint[1] * focusPoint[1] );
	if ( focusDist < 1 ) {
		focusDist = 1;	// should never happen
	}
	cg.refdefViewAngles[PITCH] = -180 / M_PI * atan2( focusPoint[2], focusDist );
	cg.refdefViewAngles[YAW] -= cg_thirdPersonAngle.value;
}


// this causes a compiler bug on mac MrC compiler
static void CG_StepOffset( void ) {
	int		timeDelta;
	
	// smooth out stair climbing
	timeDelta = cg.time - cg.stepTime;
	if ( timeDelta < STEP_TIME ) {
		cg.refdef.vieworg[2] -= cg.stepChange 
			* (STEP_TIME - timeDelta) / STEP_TIME;
	}
}

/*
===============
CG_MoveKicks

Fire a short view PUNCH on moveset events so the kit has OOMPH: walljump and
air/double-jump jolt the view UP (walljump also rolls toward the wall side), a
hard landing slams it DOWN (scaled by fall depth), and a big speed burst (dash /
slide-jump / pad) shoves a forward pitch. One channel (latest event wins), decays
in CG_OffsetFirstPersonView. Scaled by cg_moveKick (0 = off). Run once/frame.
===============
*/
static void CG_MoveKick( float pitch, float roll ) {
	cg.moveKickTime  = cg.time;
	cg.moveKickPitch = pitch * cg_moveKick.value;
	cg.moveKickRoll  = roll  * cg_moveKick.value;
}

static void CG_MoveKicks( void ) {
	playerState_t	*ps = &cg.predictedPlayerState;
	int				wj  = ps->stats[STAT_WALLJUMP_COUNT];
	int				aj  = ps->stats[STAT_AIRJUMP_COUNT];
	float			spd = sqrt( ps->velocity[0] * ps->velocity[0]
							  + ps->velocity[1] * ps->velocity[1] );

	if ( cg_moveKick.value > 0.0f && ps->pm_type == PM_NORMAL ) {
		if ( wj > cg.mkPrevWalljump ) {				// walljump: up + roll off the wall
			int wr = ps->stats[STAT_WALLRUN];
			CG_MoveKick( -2.2f, wr > 0 ? 3.0f : ( wr < 0 ? -3.0f : 0.0f ) );
		} else if ( aj > cg.mkPrevAirjump ) {		// air / double jump: upward pop
			CG_MoveKick( -1.8f, 0.0f );
		} else if ( cg.landTime != cg.mkPrevLandTime && cg.landChange < -7.0f ) {
			float m = -cg.landChange / 24.0f;		// hard land slams down, by depth
			CG_MoveKick( 2.6f * m, 0.0f );
		} else if ( spd - cg.mkPrevSpeed > 220.0f ) {	// dash / slide-jump / pad burst
			CG_MoveKick( -1.6f, 0.0f );
		}
	}
	cg.mkPrevWalljump = wj;
	cg.mkPrevAirjump  = aj;
	cg.mkPrevLandTime = cg.landTime;
	cg.mkPrevSpeed    = spd;
}

/*
===============
CG_OffsetFirstPersonView

===============
*/
static void CG_OffsetFirstPersonView( void ) {
	float			*origin;
	float			*angles;
	float			bob;
	float			ratio;
	float			delta;
	float			speed;
	float			f;
	vec3_t			predictedVelocity;
	int				timeDelta;
	
	if ( cg.snap->ps.pm_type == PM_INTERMISSION ) {
		return;
	}

	origin = cg.refdef.vieworg;
	angles = cg.refdefViewAngles;

	// if dead, fix the angle and don't add any kick
	if ( cg.snap->ps.stats[STAT_HEALTH] <= 0 ) {
		angles[ROLL] = 40;
		angles[PITCH] = -15;
		angles[YAW] = cg.snap->ps.stats[STAT_DEAD_YAW];
		origin[2] += cg.predictedPlayerState.viewheight;
		return;
	}

	// add angles based on damage kick
	if ( cg.damageTime ) {
		ratio = cg.time - cg.damageTime;
		if ( ratio < DAMAGE_DEFLECT_TIME ) {
			ratio /= DAMAGE_DEFLECT_TIME;
			angles[PITCH] += ratio * cg.v_dmg_pitch;
			angles[ROLL] += ratio * cg.v_dmg_roll;
		} else {
			ratio = 1.0 - ( ratio - DAMAGE_DEFLECT_TIME ) / DAMAGE_RETURN_TIME;
			if ( ratio > 0 ) {
				angles[PITCH] += ratio * cg.v_dmg_pitch;
				angles[ROLL] += ratio * cg.v_dmg_roll;
			}
		}
	}

	// STRAFE 64: short snappy view punch when a melee hit connects — the
	// impact "bite" that makes hacking feel like it lands. Decays over 150ms.
	if ( cg.weaponKickTime ) {
		ratio = ( cg.time - cg.weaponKickTime ) / 150.0f;
		if ( ratio >= 0 && ratio < 1.0f ) {
			float	k = 1.0f - ratio;		// linear settle
			angles[PITCH] += k * cg.weaponKickPitch;
			angles[ROLL]  += k * cg.weaponKickRoll;
		}
	}

	// STRAFE 64: MOVE-kick — the oomph punch on walljump / air-jump / hard land /
	// speed burst (set in CG_MoveKicks). Sharp hit, eased settle over ~180ms.
	CG_MoveKicks();
	if ( cg.moveKickTime ) {
		ratio = ( cg.time - cg.moveKickTime ) / 180.0f;
		if ( ratio >= 0 && ratio < 1.0f ) {
			float	k = 1.0f - ratio;
			k = k * k;						// ease-out: snaps, then settles soft
			angles[PITCH] += k * cg.moveKickPitch;
			angles[ROLL]  += k * cg.moveKickRoll;
		}
	}

	// add pitch based on fall kick
#if 0
	ratio = ( cg.time - cg.landTime) / FALL_TIME;
	if (ratio < 0)
		ratio = 0;
	angles[PITCH] += ratio * cg.fall_value;
#endif

	// add angles based on velocity
	VectorCopy( cg.predictedPlayerState.velocity, predictedVelocity );

	delta = DotProduct ( predictedVelocity, cg.refdef.viewaxis[0]);
	angles[PITCH] += delta * cg_runpitch.value;
	
	delta = DotProduct ( predictedVelocity, cg.refdef.viewaxis[1]);
	angles[ROLL] -= delta * cg_runroll.value;

	// add angles based on bob

	// make sure the bob is visible even at low speeds
	speed = cg.xyspeed > 200 ? cg.xyspeed : 200;

	// NO view bob during a slide — a slide is a smooth glide, not footfalls.
	// (Bob reads as "walking", which is exactly what a slide must not feel like.)
	// Outside a slide, crouching still accentuates the bob x3 as before.
	if ( !( cg.predictedPlayerState.pm_flags & PMF_SLIDING ) ) {
		float	bobScale = ( cg.predictedPlayerState.pm_flags & PMF_DUCKED ) ? 3.0f : 1.0f;

		delta = cg.bobfracsin * cg_bobpitch.value * speed * bobScale;
		angles[PITCH] += delta;
		delta = cg.bobfracsin * cg_bobroll.value * speed * bobScale;
		if (cg.bobcycle & 1)
			delta = -delta;
		angles[ROLL] += delta;
	}

	// wall run: bank the camera so it reads as running along the wall, eased so
	// attach/detach rolls rather than snaps (STAT_WALLRUN sign = wall side:
	// + right / - left). Rolls AWAY from the wall (wall on the right -> lean left)
	// — the opposite of the air-strafe "into the turn" lean.
	{
		static float	wallRoll = 0;
		float			target = 0;
		int				wr = cg.predictedPlayerState.stats[STAT_WALLRUN];

		if ( wr > 0 ) {
			target = -12.0f;	// wall on the right
		} else if ( wr < 0 ) {
			target = 12.0f;		// wall on the left
		}
		wallRoll += ( target - wallRoll ) * 0.18f;
		angles[ROLL] += wallRoll;
	}

	// air-strafe lean: carving through the air banks the camera into the
	// turn, proportional to lateral speed and eased so takeoff and landing
	// roll smoothly to and from level. amplifies the airborne case where
	// strafing lives, on top of the subtle always-on cg_runroll. follows
	// the cg_runroll sign so both lean the same way.
	{
		static float	airRoll = 0;
		float			target = 0;

		if ( cg.predictedPlayerState.groundEntityNum == ENTITYNUM_NONE
			&& cg.predictedPlayerState.pm_type == PM_NORMAL ) {
			float	side;

			side = DotProduct( predictedVelocity, cg.refdef.viewaxis[1] );
			target = -side * 0.02f;
			if ( target > 8.0f ) {
				target = 8.0f;
			} else if ( target < -8.0f ) {
				target = -8.0f;
			}
		}
		airRoll += ( target - airRoll ) * 0.12f;
		angles[ROLL] += airRoll;
	}

	// crouch-slide lean: bank low INTO the slide so it reads as a ground slide
	// rather than a crouch-walk. A straight slide stays level; carving banks the
	// camera toward the turn (lateral velocity), eased in/out via PMF_SLIDING.
	{
		static float	slideRoll = 0;
		float			target = 0;

		if ( cg.predictedPlayerState.pm_flags & PMF_SLIDING ) {
			float	side = DotProduct( predictedVelocity, cg.refdef.viewaxis[1] );
			target = -side * 0.045f;		// bank harder into the carve (Titanfall)
			if ( target > 16.0f ) {
				target = 16.0f;
			} else if ( target < -16.0f ) {
				target = -16.0f;
			}
		}
		slideRoll += ( target - slideRoll ) * 0.16f;
		angles[ROLL] += slideRoll;
	}

	// FLOW juice: screen shake from hard landings, hits and big tricks
	if ( cg.viewShake > 0.0f ) {
		angles[PITCH] += crandom() * cg.viewShake;
		angles[YAW]   += crandom() * cg.viewShake;
		angles[ROLL]  += crandom() * cg.viewShake * 0.5f;
	}

	// STRAFE 64 bodycam: handheld vest-cam motion -- what actually sells "held by
	// a person" on top of the post-process look. Two detuned sines per axis at
	// incommensurate frequencies (0.83/1.91, 0.71/1.63) so the drift never
	// visibly loops -> reads "human", not "machine". A footstep bounce gated by
	// horizontal speed is the biggest realism lever (stand still -> near-still
	// cam; sprint -> heavy bounce), plus a quick depth-scaled landing jolt.
	// Real-time clock (trap_Milliseconds), NOT cg.time, so the operator keeps
	// breathing at wall-clock rate in bullet-time -- same rationale as CG_MoveKick.
	if ( cg_bodycam.integer ) {
		float	t    = trap_Milliseconds() * 0.001f;
		float	amp  = cg_bodycamScale.value;
		float	spd  = sqrt( cg.predictedPlayerState.velocity[0] * cg.predictedPlayerState.velocity[0]
						   + cg.predictedPlayerState.velocity[1] * cg.predictedPlayerState.velocity[1] );
		float	sref = spd / 320.0f;
		float	bcPitch, bcYaw, bcRoll, stepPhase;

		// idle breathing + irregular drift
		bcPitch = sin( t * 0.55f * M_PI * 2.0f ) * 0.35f
				+ sin( t * 0.83f + 1.7f ) * 0.20f
				+ sin( t * 1.91f + 4.2f ) * 0.10f;
		bcYaw   = cos( t * 0.55f * M_PI * 1.7f ) * 0.245f
				+ sin( t * 0.71f + 3.3f ) * 0.20f
				+ sin( t * 1.63f + 0.9f ) * 0.10f;
		bcRoll  = sin( t * 0.47f + 2.1f ) * 0.24f;

		// footstep bounce: cadence and amplitude both scale with speed
		stepPhase = sin( t * ( spd * 0.02f ) );
		bcPitch  += fabs( stepPhase ) * sref * 1.6f * 0.6f;
		bcRoll   += stepPhase * sref * 0.8f;

		// landing impulse: brief downward jolt scaled by fall depth, eased out
		{
			float	ld = (float)( cg.time - cg.landTime );
			if ( ld >= 0 && ld < 220.0f && cg.landChange < 0 ) {
				float	k = 1.0f - ld / 220.0f;
				bcPitch += k * k * ( -cg.landChange / 24.0f ) * 2.5f;
			}
		}

		angles[PITCH] += bcPitch * amp;
		angles[YAW]   += bcYaw   * amp;
		angles[ROLL]  += bcRoll  * amp;
	}

//===================================

	// add view height
	origin[2] += cg.predictedPlayerState.viewheight;

	// smooth out duck height changes
	timeDelta = cg.time - cg.duckTime;
	if ( timeDelta < DUCK_TIME) {
		cg.refdef.vieworg[2] -= cg.duckChange 
			* (DUCK_TIME - timeDelta) / DUCK_TIME;
	}

	// add bob height — none while sliding, so the slide camera stays glued steady
	if ( !( cg.predictedPlayerState.pm_flags & PMF_SLIDING ) ) {
		bob = cg.bobfracsin * cg.xyspeed * cg_bobup.value;
		if (bob > 6) {
			bob = 6;
		}
		origin[2] += bob;
	}


	// add fall height
	delta = cg.time - cg.landTime;
	if ( delta < LAND_DEFLECT_TIME ) {
		f = delta / LAND_DEFLECT_TIME;
		cg.refdef.vieworg[2] += cg.landChange * f;
	} else if ( delta < LAND_DEFLECT_TIME + LAND_RETURN_TIME ) {
		delta -= LAND_DEFLECT_TIME;
		f = 1.0 - ( delta / LAND_RETURN_TIME );
		cg.refdef.vieworg[2] += cg.landChange * f;
	}

	// add step offset
	CG_StepOffset();

	// pivot the eye based on a neck length
#if 0
	{
#define	NECK_LENGTH		8
	vec3_t			forward, up;
 
	cg.refdef.vieworg[2] -= NECK_LENGTH;
	AngleVectors( cg.refdefViewAngles, forward, NULL, up );
	VectorMA( cg.refdef.vieworg, 3, forward, cg.refdef.vieworg );
	VectorMA( cg.refdef.vieworg, NECK_LENGTH, up, cg.refdef.vieworg );
	}
#endif
}

//======================================================================

void CG_ZoomDown_f( void ) { 
	if ( cg.zoomed ) {
		return;
	}
	cg.zoomed = qtrue;
	cg.zoomTime = cg.time;
}

void CG_ZoomUp_f( void ) { 
	if ( !cg.zoomed ) {
		return;
	}
	cg.zoomed = qfalse;
	cg.zoomTime = cg.time;
}


/*
====================
CG_CalcFov

Fixed fov at intermissions, otherwise account for fov variable and zooms.
====================
*/
#define	WAVE_AMPLITUDE	1
#define	WAVE_FREQUENCY	0.4

static int CG_CalcFov( void ) {
	float	x;
	float	phase;
	float	v;
	int		contents;
	float	fov_x, fov_y;
	float	zoomFov;
	float	f;
	int		inwater;

	if ( cg.predictedPlayerState.pm_type == PM_INTERMISSION ) {
		// if in intermission, use a fixed value
		fov_x = 90;
	} else {
		// user selectable
		if ( cgs.dmflags & DF_FIXED_FOV ) {
			// dmflag to prevent wide fov for all clients
			fov_x = 90;
		} else {
			fov_x = cg_fov.value;
			if ( fov_x < 1 ) {
				fov_x = 1;
			} else if ( fov_x > 160 ) {
				fov_x = 160;
			}
		}

		// account for zooms
		zoomFov = cg_zoomFov.value;
		if ( zoomFov < 1 ) {
			zoomFov = 1;
		} else if ( zoomFov > 160 ) {
			zoomFov = 160;
		}

		if ( cg.zoomed ) {
			f = ( cg.time - cg.zoomTime ) / (float)ZOOM_TIME;
			if ( f > 1.0 ) {
				fov_x = zoomFov;
			} else {
				fov_x = fov_x + f * ( zoomFov - fov_x );
			}
		} else {
			f = ( cg.time - cg.zoomTime ) / (float)ZOOM_TIME;
			if ( f <= 1.0 ) {
				fov_x = zoomFov + f * ( fov_x - zoomFov );
			}
		}
	}

	// speed widens the view: up to +15 degrees as flow builds past run
	// speed, so going fast feels fast. Shares CG_Flow with the world
	// color wash and the speed-damage HUD — one curve, not three.
	if ( cg_speedFov.integer && !cg.zoomed
		&& cg.predictedPlayerState.pm_type != PM_INTERMISSION
		&& !( cgs.dmflags & DF_FIXED_FOV ) ) {
		fov_x += 15.0f * CG_Flow();
		if ( fov_x > 175 ) {
			fov_x = 175;
		}
	}

	// flow-surge punch: a quick extra kick at the instant you break into
	// flow, decaying over 300 ms, so the surge sound lands with a physical
	// snap-wider on top of the continuous speed widen.
	if ( cg_speedFov.integer && cg.flowSurgeTime
		&& !cg.zoomed && !( cgs.dmflags & DF_FIXED_FOV ) ) {
		int		dt;

		dt = cg.time - cg.flowSurgeTime;
		if ( dt >= 0 && dt < 300 ) {
			fov_x += 6.0f * ( 1.0f - dt / 300.0f );
			if ( fov_x > 175 ) {
				fov_x = 175;
			}
		}
	}

	// FLOW juice: transient FOV punch from landings and frags
	if ( cg.fovPunch > 0.0f && !cg.zoomed
		&& !( cgs.dmflags & DF_FIXED_FOV ) ) {
		fov_x += cg.fovPunch;
		if ( fov_x > 175 ) {
			fov_x = 175;
		}
	}

	x = cg.refdef.width / tan( fov_x / 360 * M_PI );
	fov_y = atan2( cg.refdef.height, x );
	fov_y = fov_y * 360 / M_PI;

	// warp if underwater
	contents = CG_PointContents( cg.refdef.vieworg, -1 );
	if ( contents & ( CONTENTS_WATER | CONTENTS_SLIME | CONTENTS_LAVA ) ){
		phase = cg.time / 1000.0 * WAVE_FREQUENCY * M_PI * 2;
		v = WAVE_AMPLITUDE * sin( phase );
		fov_x += v;
		fov_y -= v;
		inwater = qtrue;
	}
	else {
		inwater = qfalse;
	}


	// set it
	cg.refdef.fov_x = fov_x;
	cg.refdef.fov_y = fov_y;

	if ( !cg.zoomed ) {
		cg.zoomSensitivity = 1;
	} else {
		cg.zoomSensitivity = cg.refdef.fov_y / 75.0;
	}

	return inwater;
}



/*
===============
CG_DamageBlendBlob

===============
*/
static void CG_DamageBlendBlob( void ) {
	int			t;
	int			maxTime;
	refEntity_t		ent;

	if (!cg_blood.integer) {
		return;
	}

	if ( !cg.damageValue ) {
		return;
	}

	//if (cg.cameraMode) {
	//	return;
	//}

	// ragePro systems can't fade blends, so don't obscure the screen
	if ( cgs.glconfig.hardwareType == GLHW_RAGEPRO ) {
		return;
	}

	maxTime = DAMAGE_TIME;
	t = cg.time - cg.damageTime;
	if ( t <= 0 || t >= maxTime ) {
		return;
	}


	memset( &ent, 0, sizeof( ent ) );
	ent.reType = RT_SPRITE;
	ent.renderfx = RF_FIRST_PERSON;

	VectorMA( cg.refdef.vieworg, 8, cg.refdef.viewaxis[0], ent.origin );
	VectorMA( ent.origin, cg.damageX * -8, cg.refdef.viewaxis[1], ent.origin );
	VectorMA( ent.origin, cg.damageY * 8, cg.refdef.viewaxis[2], ent.origin );

	ent.radius = cg.damageValue * 3;
	ent.customShader = cgs.media.viewBloodShader;
	ent.shaderRGBA[0] = 255;
	ent.shaderRGBA[1] = 255;
	ent.shaderRGBA[2] = 255;
	ent.shaderRGBA[3] = 200 * ( 1.0 - ((float)t / maxTime) );
	trap_R_AddRefEntityToScene( &ent );
}


/*
===============
CG_CalcViewValues

Sets cg.refdef view values
===============
*/
static int CG_CalcViewValues( void ) {
	playerState_t	*ps;

	memset( &cg.refdef, 0, sizeof( cg.refdef ) );

	// strings for in game rendering
	// Q_strncpyz( cg.refdef.text[0], "Park Ranger", sizeof(cg.refdef.text[0]) );
	// Q_strncpyz( cg.refdef.text[1], "19", sizeof(cg.refdef.text[1]) );

	// calculate size of 3D view
	CG_CalcVrect();

	ps = &cg.predictedPlayerState;
/*
	if (cg.cameraMode) {
		vec3_t origin, angles;
		if (trap_getCameraInfo(cg.time, &origin, &angles)) {
			VectorCopy(origin, cg.refdef.vieworg);
			angles[ROLL] = 0;
			VectorCopy(angles, cg.refdefViewAngles);
			AnglesToAxis( cg.refdefViewAngles, cg.refdef.viewaxis );
			return CG_CalcFov();
		} else {
			cg.cameraMode = qfalse;
		}
	}
*/
	// intermission view
	if ( ps->pm_type == PM_INTERMISSION ) {
		VectorCopy( ps->origin, cg.refdef.vieworg );
		VectorCopy( ps->viewangles, cg.refdefViewAngles );
		AnglesToAxis( cg.refdefViewAngles, cg.refdef.viewaxis );
		return CG_CalcFov();
	}

	cg.bobcycle = ( ps->bobCycle & 128 ) >> 7;
	cg.bobfracsin = fabs( sin( ( ps->bobCycle & 127 ) / 127.0 * M_PI ) );
	cg.xyspeed = sqrt( ps->velocity[0] * ps->velocity[0] +
		ps->velocity[1] * ps->velocity[1] );


	VectorCopy( ps->origin, cg.refdef.vieworg );
	VectorCopy( ps->viewangles, cg.refdefViewAngles );

	// SUPERHOT slow-mo smoothing: our own first-person camera follows the
	// PREDICTED player origin, which only advances when the integer-ms server
	// clock ticks (timescale*1000 Hz) -> it stair-steps in deep slow-mo, while
	// bots look smooth because they already get CG_SmoothEntityMotion (which
	// explicitly skips our predicted body). Give the view the same treatment: a
	// REAL-time low-pass that eases the camera toward the stepped predicted
	// origin every render frame. It is driven by trap_Milliseconds (real ms), so
	// it keeps converging even while cg.time is frozen between ticks -- that is
	// what turns the staircase into a glide. Faded by timescale exactly like the
	// bot smoother (full <=0.5x, off >=0.95x) so normal play is untouched, and
	// snapped on a big jump (teleport/respawn). Toggle cg_slowmoSmooth 0; the
	// real-time constant is cg_slowmoSmoothMs (default 55 -> smoothness vs lag).
	{
		char	enbuf[32], tsbuf[32], tcbuf[32];
		float	ts, blend;

		trap_Cvar_VariableStringBuffer( "cg_slowmoSmooth", enbuf, sizeof( enbuf ) );
		trap_Cvar_VariableStringBuffer( "timescale", tsbuf, sizeof( tsbuf ) );
		ts = atof( tsbuf );
		if ( ts <= 0.0f ) {
			ts = 1.0f;
		}
		blend = ( 0.95f - ts ) / 0.45f;		// fade with dilation depth

		if ( enbuf[0] != '0' && blend > 0.0f && ps->pm_type != PM_INTERMISSION ) {
			static vec3_t	smoothOrigin;
			static int		smoothRealMs = 0;
			static qboolean	seeded = qfalse;
			int		nowReal = trap_Milliseconds();
			float	tc, dtReal, k, dist;
			vec3_t	raw, delta;

			if ( blend > 1.0f ) {
				blend = 1.0f;
			}
			trap_Cvar_VariableStringBuffer( "cg_slowmoSmoothMs", tcbuf, sizeof( tcbuf ) );
			tc = atof( tcbuf );
			if ( tc < 1.0f ) {
				tc = 55.0f;		// default real-time constant (ms)
			}

			VectorCopy( cg.refdef.vieworg, raw );
			VectorSubtract( raw, smoothOrigin, delta );
			dist = VectorLength( delta );

			if ( !seeded || dist > 120.0f ) {
				VectorCopy( raw, smoothOrigin );	// seed / snap on teleport or respawn
				seeded = qtrue;
			} else {
				dtReal = (float)( nowReal - smoothRealMs );
				if ( dtReal < 0.0f ) {
					dtReal = 0.0f;
				}
				k = dtReal / tc;			// real-time exponential follow
				if ( k > 1.0f ) {
					k = 1.0f;
				}
				VectorMA( smoothOrigin, k, delta, smoothOrigin );
				cg.refdef.vieworg[0] = raw[0] + ( smoothOrigin[0] - raw[0] ) * blend;
				cg.refdef.vieworg[1] = raw[1] + ( smoothOrigin[1] - raw[1] ) * blend;
				cg.refdef.vieworg[2] = raw[2] + ( smoothOrigin[2] - raw[2] ) * blend;
			}
			smoothRealMs = nowReal;
		}
	}

	if (cg_cameraOrbit.integer) {
		if (cg.time > cg.nextOrbitTime) {
			cg.nextOrbitTime = cg.time + cg_cameraOrbitDelay.integer;
			cg_thirdPersonAngle.value += cg_cameraOrbit.value;
		}
	}
	// add error decay
	if ( cg_errorDecay.value > 0 ) {
		int		t;
		float	f;

		t = cg.time - cg.predictedErrorTime;
		f = ( cg_errorDecay.value - t ) / cg_errorDecay.value;
		if ( f > 0 && f < 1 ) {
			VectorMA( cg.refdef.vieworg, f, cg.predictedError, cg.refdef.vieworg );
		} else {
			cg.predictedErrorTime = 0;
		}
	}

	if ( cg.renderingThirdPerson ) {
		// back away from character
		CG_OffsetThirdPersonView();
	} else {
		// offset for local bobbing and kicks
		CG_OffsetFirstPersonView();
	}

	// position eye relative to origin
	AnglesToAxis( cg.refdefViewAngles, cg.refdef.viewaxis );

	if ( cg.hyperspace ) {
		cg.refdef.rdflags |= RDF_NOWORLDMODEL | RDF_HYPERSPACE;
	}

	// field of view
	return CG_CalcFov();
}


/*
=====================
CG_PowerupTimerSounds
=====================
*/
static void CG_PowerupTimerSounds( void ) {
	int		i;
	int		t;

	// powerup timers going away
	for ( i = 0 ; i < MAX_POWERUPS ; i++ ) {
		t = cg.snap->ps.powerups[i];
		if ( t <= cg.time ) {
			continue;
		}
		if ( t - cg.time >= POWERUP_BLINKS * POWERUP_BLINK_TIME ) {
			continue;
		}
		if ( ( t - cg.time ) / POWERUP_BLINK_TIME != ( t - cg.oldTime ) / POWERUP_BLINK_TIME ) {
			trap_S_StartSound( NULL, cg.snap->ps.clientNum, CHAN_ITEM, cgs.media.wearOffSound );
		}
	}
}

/*
=====================
CG_AddBufferedSound
=====================
*/
void CG_AddBufferedSound( sfxHandle_t sfx ) {
	if ( !sfx )
		return;
	cg.soundBuffer[cg.soundBufferIn] = sfx;
	cg.soundBufferIn = (cg.soundBufferIn + 1) % MAX_SOUNDBUFFER;
	if (cg.soundBufferIn == cg.soundBufferOut) {
		cg.soundBufferOut++;
	}
}

/*
=====================
CG_PlayBufferedSounds
=====================
*/
static void CG_PlayBufferedSounds( void ) {
	if ( cg.soundTime < cg.time ) {
		if (cg.soundBufferOut != cg.soundBufferIn && cg.soundBuffer[cg.soundBufferOut]) {
			trap_S_StartLocalSound(cg.soundBuffer[cg.soundBufferOut], CHAN_ANNOUNCER);
			cg.soundBuffer[cg.soundBufferOut] = 0;
			cg.soundBufferOut = (cg.soundBufferOut + 1) % MAX_SOUNDBUFFER;
			cg.soundTime = cg.time + 750;
		}
	}
}

//=========================================================================

/*
=========================================================================

STRAFE 64 race layer: ghost recording / replay and the rising void.

Ghosts are pure cgame — the local player's best run on this map is
sampled at 20 Hz while STAT_RACE_START is live, persisted under
ghosts/<map>.gho, and replayed as a translucent player model pacing
the current run. No protocol or server involvement.

=========================================================================
*/

#define	GHOST_HZ			20
#define	GHOST_INTERVAL		( 1000 / GHOST_HZ )
#define	GHOST_MAX_SAMPLES	( GHOST_HZ * 600 )		// ten minutes
#define	GHOST_MAGIC			0x36344753				// "SG64"
#define	GHOST_VERSION		1

typedef struct {
	int		magic;
	int		version;
	int		finishMs;
	int		numSamples;
} ghostHeader_t;

typedef struct {
	qboolean	valid;
	int			finishMs;
	int			numSamples;
	vec3_t		origins[GHOST_MAX_SAMPLES];
	float		yaws[GHOST_MAX_SAMPLES];
} ghostRun_t;

static ghostRun_t	ghostBest;		// best saved run for this map
static ghostRun_t	ghostRec;		// run being recorded
static qboolean		ghostRacing;
static int			ghostRaceStartMs;	// REAL (trap_Milliseconds) clock the run started — for the saved/scored finish time
static int			ghostRaceStartTime;	// SCALED game clock (cg.time) the run started — for record+replay so the ghost dilates with slow-mo
static int			ghostLastStat;

static void CG_GhostFilename( char *buf, int bufSize ) {
	char	map[MAX_QPATH];

	// cgs.mapname is "maps/<name>.bsp"
	COM_StripExtension( cgs.mapname, map, sizeof( map ) );
	Com_sprintf( buf, bufSize, "ghosts/%s.gho", strchr( map, '/' ) ? strchr( map, '/' ) + 1 : map );
}

static void CG_GhostSave( void ) {
	fileHandle_t	f;
	char			name[MAX_QPATH];
	ghostHeader_t	h;

	CG_GhostFilename( name, sizeof( name ) );
	if ( trap_FS_FOpenFile( name, &f, FS_WRITE ) < 0 || !f ) {
		CG_Printf( "couldn't write %s\n", name );
		return;
	}
	h.magic = GHOST_MAGIC;
	h.version = GHOST_VERSION;
	h.finishMs = ghostBest.finishMs;
	h.numSamples = ghostBest.numSamples;
	trap_FS_Write( &h, sizeof( h ), f );
	trap_FS_Write( ghostBest.origins, ghostBest.numSamples * sizeof( vec3_t ), f );
	trap_FS_Write( ghostBest.yaws, ghostBest.numSamples * sizeof( float ), f );
	trap_FS_FCloseFile( f );
}

void CG_GhostInit( void ) {
	fileHandle_t	f;
	char			name[MAX_QPATH];
	int				len;
	ghostHeader_t	h;

	memset( &ghostBest, 0, sizeof( ghostBest ) );
	memset( &ghostRec, 0, sizeof( ghostRec ) );
	ghostRacing = qfalse;
	ghostLastStat = 0;

	CG_GhostFilename( name, sizeof( name ) );
	len = trap_FS_FOpenFile( name, &f, FS_READ );
	if ( len < sizeof( h ) ) {
		if ( len >= 0 ) {
			trap_FS_FCloseFile( f );
		}
		return;
	}
	trap_FS_Read( &h, sizeof( h ), f );
	if ( h.magic != GHOST_MAGIC || h.version != GHOST_VERSION
		|| h.numSamples < 2 || h.numSamples > GHOST_MAX_SAMPLES ) {
		trap_FS_FCloseFile( f );
		return;
	}
	trap_FS_Read( ghostBest.origins, h.numSamples * sizeof( vec3_t ), f );
	trap_FS_Read( ghostBest.yaws, h.numSamples * sizeof( float ), f );
	trap_FS_FCloseFile( f );

	// reject a degenerate (non-moving) ghost: the spectator/intermission
	// recorder bug could save a stationary run, which would show a bogus
	// best-lap and a parked pace dot. If the path barely moves, it isn't a run.
	{
		vec3_t	mn, mx;
		int		i;
		VectorCopy( ghostBest.origins[0], mn );
		VectorCopy( ghostBest.origins[0], mx );
		for ( i = 1; i < h.numSamples; i++ ) {
			AddPointToBounds( ghostBest.origins[i], mn, mx );
		}
		if ( ( mx[0] - mn[0] ) + ( mx[1] - mn[1] ) + ( mx[2] - mn[2] ) < 64.0f ) {
			CG_Printf( "ghost: %s is degenerate (no movement) — ignored\n", name );
			memset( &ghostBest, 0, sizeof( ghostBest ) );
			return;
		}
	}

	ghostBest.finishMs = h.finishMs;
	ghostBest.numSamples = h.numSamples;
	ghostBest.valid = qtrue;
	CG_Printf( "ghost: %s loaded, %i:%02i.%03i\n", name,
		h.finishMs / 60000, ( h.finishMs / 1000 ) % 60, h.finishMs % 1000 );
}

int CG_GhostBestMs( void ) {
	return ghostBest.valid ? ghostBest.finishMs : 0;
}

/*
====================
CG_GhostColor

The ghost's cool hologram tint, modulated to a byte shaderRGBA. Alpha is
driven by cg_ghostAlpha so opacity is a live, archived cvar. The dedicated
strafe64/ghost shader (rgbGen/alphaGen entity) reads these; if a map lacks
the strafe64 pk3 the shader degrades but the tint is harmless.
====================
*/
#define GHOST_TINT_R	60
#define GHOST_TINT_G	220
#define GHOST_TINT_B	255

static void CG_GhostColor( byte *rgba ) {
	float a = cg_ghostAlpha.value;

	if ( a < 0.05f ) {
		a = 0.05f;
	} else if ( a > 1.0f ) {
		a = 1.0f;
	}
	rgba[0] = GHOST_TINT_R;
	rgba[1] = GHOST_TINT_G;
	rgba[2] = GHOST_TINT_B;
	rgba[3] = (byte)( a * 255 );
}

/*
====================
CG_AddGhostModel

The ghost is the local player model rendered through the dedicated
translucent ghost shader (a cool hologram tint at cg_ghostAlpha opacity),
gliding along the best run. Falls back to the invisibility warp if the
strafe64 ghost shader is unavailable.
====================
*/
static void CG_AddGhostModel( const vec3_t origin, float yaw ) {
	refEntity_t		legs, torso, head;
	vec3_t			angles;
	clientInfo_t	*ci;
	qhandle_t		shader;
	byte			rgba[4];

	ci = &cgs.clientinfo[cg.snap->ps.clientNum];
	if ( !ci->infoValid || !ci->legsModel || !ci->torsoModel || !ci->headModel ) {
		return;
	}

	shader = cgs.media.ghostShader ? cgs.media.ghostShader : cgs.media.invisShader;
	CG_GhostColor( rgba );

	memset( &legs, 0, sizeof( legs ) );
	memset( &torso, 0, sizeof( torso ) );
	memset( &head, 0, sizeof( head ) );

	VectorSet( angles, 0, yaw, 0 );
	AnglesToAxis( angles, legs.axis );
	AxisCopy( legs.axis, torso.axis );
	AxisCopy( legs.axis, head.axis );

	legs.hModel = ci->legsModel;
	legs.customShader = shader;
	legs.renderfx = RF_NOSHADOW;
	Com_Memcpy( legs.shaderRGBA, rgba, 4 );
	legs.frame = legs.oldframe = ci->animations[LEGS_RUN].firstFrame;
	VectorCopy( origin, legs.origin );
	VectorCopy( origin, legs.lightingOrigin );
	trap_R_AddRefEntityToScene( &legs );

	torso.hModel = ci->torsoModel;
	torso.customShader = shader;
	torso.renderfx = RF_NOSHADOW;
	Com_Memcpy( torso.shaderRGBA, rgba, 4 );
	torso.frame = torso.oldframe = ci->animations[TORSO_STAND].firstFrame;
	CG_PositionRotatedEntityOnTag( &torso, &legs, ci->legsModel, "tag_torso" );
	VectorCopy( origin, torso.lightingOrigin );
	trap_R_AddRefEntityToScene( &torso );

	head.hModel = ci->headModel;
	head.customShader = shader;
	head.renderfx = RF_NOSHADOW;
	Com_Memcpy( head.shaderRGBA, rgba, 4 );
	CG_PositionRotatedEntityOnTag( &head, &torso, ci->torsoModel, "tag_head" );
	VectorCopy( origin, head.lightingOrigin );
	trap_R_AddRefEntityToScene( &head );
}

/*
====================
CG_GhostTrail

A fading motion trail behind the ghost so it reads as speed: a billboarded
ribbon stitched through the recorded best-run samples just behind the
ghost's current replay position. Tinted to the ghost hologram colour,
width scaled by the ghost's speed, alpha fading the further back a segment
is. Drawn with the always-present white shader (additive), so it needs no
map asset and never clobbers the flow/glitch HUD layer.
====================
*/
#define GHOST_TRAIL_SEGS	12		// how many past samples to ribbon
#define GHOST_TRAIL_WIDTH	2.0f	// base half-width (u)

static void CG_GhostTrail( int idx, float frac ) {
	vec3_t	pts[GHOST_TRAIL_SEGS + 2];
	vec3_t	seg, cur;
	byte	col[3];
	int		i, count;

	if ( !cg_ghost.integer || !ghostBest.valid || ghostBest.numSamples < 3 ) {
		return;
	}

	// head of the trail = the ghost's interpolated position this frame
	VectorSubtract( ghostBest.origins[idx + 1], ghostBest.origins[idx], seg );
	VectorMA( ghostBest.origins[idx], frac, seg, cur );

	// build a NEWEST-FIRST list of the recent samples (head, then back along
	// the recorded path) for the lattice wall to stitch through
	count = 0;
	VectorCopy( cur, pts[count++] );
	for ( i = idx ; i >= 0 && count <= GHOST_TRAIL_SEGS ; i-- ) {
		VectorCopy( ghostBest.origins[i], pts[count++] );
	}

	col[0] = GHOST_TINT_R;
	col[1] = GHOST_TINT_G;
	col[2] = GHOST_TINT_B;

	// the cool lattice speed-trail: the same vertical light-wall the LATTICE
	// pilots leave, now stitched behind the racing ghost (clean overlay — no
	// audio glitch/chip layer)
	CG_LatticeTrailWall( (const vec3_t *)pts, count, col, cg_ghostAlpha.value );
}

/*
====================
CG_VoidZ

Where the void is right now; valid only when cgs.voidActive.
====================
*/
float CG_VoidZ( void ) {
	// REAL-time (trap_Milliseconds), not cg.time: the drawn plane must track the
	// server's real-time kill plane (G_RunVoid) so it doesn't desync under
	// g_timeBind slow-mo. cgs.voidStartTime is a real-ms rise stamp (the server
	// puts trap_Milliseconds() in CS_VOIDINFO); the two agree on a listen server.
	int now = trap_Milliseconds();

	if ( now <= cgs.voidStartTime ) {
		return cgs.voidBase;
	}
	return cgs.voidBase + cgs.voidRise * ( now - cgs.voidStartTime ) / 1000.0f;
}

static void CG_AddVoidPlane( void ) {
	polyVert_t	verts[4];
	float		z, x, y, half;
	int			i;

	if ( !cgs.voidActive || !cgs.media.voidShader ) {
		return;
	}
	z = CG_VoidZ();
	x = cg.predictedPlayerState.origin[0];
	y = cg.predictedPlayerState.origin[1];
	half = 16384;

	VectorSet( verts[0].xyz, x - half, y - half, z );
	VectorSet( verts[1].xyz, x + half, y - half, z );
	VectorSet( verts[2].xyz, x + half, y + half, z );
	VectorSet( verts[3].xyz, x - half, y + half, z );
	for ( i = 0 ; i < 4 ; i++ ) {
		verts[i].st[0] = verts[i].xyz[0] / 512.0f;
		verts[i].st[1] = verts[i].xyz[1] / 512.0f;
		verts[i].modulate[0] = 255;
		verts[i].modulate[1] = 255;
		verts[i].modulate[2] = 255;
		verts[i].modulate[3] = 255;
	}
	trap_R_AddPolyToScene( cgs.media.voidShader, 4, verts );
}

/*
====================
CG_RaceFrame

Run once per frame: drives the ghost recorder state machine off
STAT_RACE_START, replays the best ghost while racing, and draws the
void plane.
====================
*/
void CG_RaceFrame( void ) {
	int		stat;
	int		t, idx;
	float	frac, yaw;
	vec3_t	pos;

	CG_AddVoidPlane();

	// A ghost is the LOCAL player's own live run ONLY. Following a bot or sitting
	// at intermission/spectator copies that entity's race stat into our snap but
	// the recorder samples OUR predicted origin — so it logs the stationary
	// camera and saves a bogus zero-speed "run" + a fake best-lap (the surf_64
	// ghost bug). Bail out of all ghost logic unless we're driving our own racer.
	if ( cg.snap->ps.pm_type != PM_NORMAL
		|| cg.snap->ps.clientNum != cg.clientNum ) {
		ghostRacing = qfalse;
		ghostLastStat = 0;
		return;
	}

	stat = cg.snap->ps.stats[STAT_RACE_START];

	// dying mid-run kills the run: the stat is only cleared at respawn,
	// when health is back up, so without this a death would look like a
	// finish and save a bogus ghost
	if ( ghostRacing && cg.snap->ps.stats[STAT_HEALTH] <= 0 ) {
		ghostRacing = qfalse;
	}

	// Ghost record + replay run off the SCALED game clock (cg.time) so the ghost
	// DILATES with the world: dip into g_timeBind slow-mo and your past self slows
	// down right alongside you, staying in visual sync instead of speed-cheating
	// ahead. Record and replay share this one clock, so there's no record/replay
	// drift. The SAVED/SCORED finish time still runs off the REAL clock
	// (trap_Milliseconds, ghostRaceStartMs) so slow-mo can't shrink a lap time —
	// same "timing stays real" rule as the void and the server payout.
	// STAT_RACE_START remains just the edge/restamp signal.
	if ( stat && stat != ghostLastStat ) {
		// (re)stamped on the start pad: the run begins now
		ghostRacing = qtrue;
		ghostRaceStartMs = trap_Milliseconds();
		ghostRaceStartTime = cg.time;
		ghostRec.numSamples = 0;
		ghostRec.valid = qfalse;
	} else if ( !stat && ghostRacing ) {
		// run over: finish if alive, abandoned if dead
		ghostRacing = qfalse;
		if ( cg.snap->ps.stats[STAT_HEALTH] > 0 && ghostRec.numSamples >= 2 ) {
			int finishMs = trap_Milliseconds() - ghostRaceStartMs;

			if ( !ghostBest.valid || finishMs < ghostBest.finishMs ) {
				memcpy( ghostBest.origins, ghostRec.origins,
					ghostRec.numSamples * sizeof( vec3_t ) );
				memcpy( ghostBest.yaws, ghostRec.yaws,
					ghostRec.numSamples * sizeof( float ) );
				ghostBest.numSamples = ghostRec.numSamples;
				ghostBest.finishMs = finishMs;
				ghostBest.valid = qtrue;
				CG_GhostSave();
			}
		}
	}
	ghostLastStat = stat;

	if ( !ghostRacing ) {
		return;
	}

	t = cg.time - ghostRaceStartTime;	// SCALED game time: dilates with slow-mo
	if ( t < 0 ) {
		t = 0;
	}

	// record: one sample per GHOST_INTERVAL, backfilling any frames we skipped
	while ( ghostRec.numSamples < GHOST_MAX_SAMPLES
		&& ghostRec.numSamples * GHOST_INTERVAL <= t ) {
		VectorCopy( cg.predictedPlayerState.origin,
			ghostRec.origins[ghostRec.numSamples] );
		ghostRec.yaws[ghostRec.numSamples] =
			cg.predictedPlayerState.viewangles[YAW];
		ghostRec.numSamples++;
	}

	// replay the best run alongside. Bound by the recorded SAMPLE span (game-time
	// duration), not finishMs (real ms), since t is now the scaled game clock.
	if ( cg_ghost.integer && ghostBest.valid
		&& t < ( ghostBest.numSamples - 1 ) * GHOST_INTERVAL ) {
		idx = t / GHOST_INTERVAL;
		if ( idx >= ghostBest.numSamples - 1 ) {
			idx = ghostBest.numSamples - 2;
		}
		frac = ( t - idx * GHOST_INTERVAL ) / (float)GHOST_INTERVAL;
		if ( frac > 1 ) {
			frac = 1;
		}
		VectorSubtract( ghostBest.origins[idx + 1], ghostBest.origins[idx], pos );
		VectorMA( ghostBest.origins[idx], frac, pos, pos );
		yaw = LerpAngle( ghostBest.yaws[idx], ghostBest.yaws[idx + 1], frac );
		CG_GhostTrail( idx, frac );
		CG_AddGhostModel( pos, yaw );
	}
}

//=========================================================================

/*
=================
CG_UpdateDoF

Drives the renderer's depth of field from the game each frame:

  * blur AMOUNT racks with the live slow-mo factor (crisp at full speed, shallow
    and dreamy in deep bullet-time) -- estimates the timescale from the real
    clock vs the dilated game clock, same trick as the lattice wall, then lerps
    r_dofAmount from cg_dofBase to cg_dofMax. (cg_dofBulletTime)

  * focus DISTANCE tracks the surface/enemy under the reticle via a forward
    trace, EASED toward it with a real-time one-pole "focus pull" so it glides
    instead of snapping -- the camera-like feel real games use, and far steadier
    than the shader sampling a single centre pixel. Hands the eased distance to
    the shader and turns its own centre auto-focus off. (cg_dofFocusTrace)

Both are inert unless r_dof is on (the DoF pass no-ops without its depth texture)
and leave the cvars manually tunable when their toggle is off.
=================
*/
static void CG_UpdateDoF( void ) {
	static int   prevReal = 0, prevCg = 0;
	static float slow = 0.0f;
	static float smoothFocal = 512.0f;
	static float lastAmount = -1.0f;
	int    real, dr = 0;

	real = trap_Milliseconds();
	if ( prevReal > 0 ) {
		dr = real - prevReal;
	}

	// --- blur amount racked by the slow-mo clock ---
	if ( cg_dofBulletTime.integer ) {
		float amount, d;
		if ( dr > 0 ) {
			float ts = ( cg.time - prevCg ) / (float)dr;
			if ( ts < 0.0f ) ts = 0.0f; else if ( ts > 1.0f ) ts = 1.0f;
			slow += ( ( 1.0f - ts ) - slow ) * 0.15f;	// 0 = full speed, 1 = frozen
		}
		amount = cg_dofBase.value + ( cg_dofMax.value - cg_dofBase.value ) * slow;
		d = amount - lastAmount;
		if ( lastAmount < 0.0f || d > 0.05f || d < -0.05f ) {
			trap_Cvar_Set( "r_dofAmount", va( "%.2f", amount ) );
			lastAmount = amount;
		}
	}

	// --- focus distance: a small cone of forward traces under the reticle, taking
	// the NEAREST real surface, eased (focus pull). The cone + nearest-hit stops
	// focus from punching through a thin gap/doorway and latching onto the far
	// wall or the void (which would blur everything with nothing sharp to anchor),
	// and sky/void hits are ignored so looking at open air doesn't rack to infinity.
	if ( cg_dofFocusTrace.integer && cg.snap ) {
#define DOF_FOCUS_REACH		8192.0f
#define DOF_FOCUS_MAX		2200.0f		// cap: never focus past here (incl. void)
#define DOF_FOCUS_SPREAD	150.0f		// lateral cone radius at full reach (~1deg)
#define DOF_GAP_RATIO		3.0f		// centre this much past the near ring = gap
		// the 4 ring offsets sampled around the centre ray
		static const float offX[4] = {  1.0f, -1.0f,  0.0f,  0.0f };
		static const float offY[4] = {  0.0f,  0.0f,  1.0f, -1.0f };
		trace_t	tr;
		vec3_t	end;
		float	centerDist = DOF_FOCUS_MAX, nearRing = DOF_FOCUS_MAX, target;
		float	k;
		int		i;

		// centre ray: what you're actually aiming at (keeps ramps/corners sharp)
		VectorMA( cg.refdef.vieworg, DOF_FOCUS_REACH, cg.refdef.viewaxis[0], end );
		CG_Trace( &tr, cg.refdef.vieworg, NULL, NULL, end,
				  cg.snap->ps.clientNum, MASK_SHOT );
		if ( tr.fraction < 1.0f && !( tr.surfaceFlags & SURF_SKY ) ) {
			centerDist = tr.fraction * DOF_FOCUS_REACH;
		}

		// ring: nearest surrounding surface, used only to detect a gap punch-through
		for ( i = 0; i < 4; i++ ) {
			float dist;
			VectorMA( cg.refdef.vieworg, DOF_FOCUS_REACH, cg.refdef.viewaxis[0], end );
			VectorMA( end, offX[i] * DOF_FOCUS_SPREAD, cg.refdef.viewaxis[1], end );
			VectorMA( end, offY[i] * DOF_FOCUS_SPREAD, cg.refdef.viewaxis[2], end );
			CG_Trace( &tr, cg.refdef.vieworg, NULL, NULL, end,
					  cg.snap->ps.clientNum, MASK_SHOT );
			if ( tr.fraction >= 1.0f || ( tr.surfaceFlags & SURF_SKY ) ) {
				continue;
			}
			dist = tr.fraction * DOF_FOCUS_REACH;
			if ( dist < nearRing ) {
				nearRing = dist;
			}
		}

		// trust the centre (sharp ramps/corners); only fall to the near ring when
		// the centre clearly slipped THROUGH a gap to something far beyond it
		target = centerDist;
		if ( nearRing < DOF_FOCUS_MAX && centerDist > nearRing * DOF_GAP_RATIO ) {
			target = nearRing;
		}
		if ( target < 80.0f ) target = 80.0f;		// never focus right on the lens

		// real-time one-pole pull (~0.25s constant); clocked in REAL time so the
		// rack eases over real seconds regardless of how deep the slow-mo is
		k = ( dr > 0 ) ? ( dr / 250.0f ) : 1.0f;
		if ( k > 1.0f ) k = 1.0f; else if ( k < 0.0f ) k = 0.0f;
		smoothFocal += ( target - smoothFocal ) * k;

		trap_Cvar_Set( "r_dofFocalDist", va( "%.1f", smoothFocal ) );
		trap_Cvar_Set( "r_dofAutoFocus", "0" );		// use our eased CPU focal
	}

	prevReal = real;
	prevCg = cg.time;
}

/*
=================
CG_DrawActiveFrame

Generates and draws a game scene and status information at the given time.
=================
*/
void CG_DrawActiveFrame( int serverTime, stereoFrame_t stereoView, qboolean demoPlayback ) {
	int		inwater;

	cg.time = serverTime;
	cg.demoPlayback = demoPlayback;

	// update cvars
	CG_UpdateCvars();

	// if we are only updating the screen as a loading
	// pacifier, don't even try to read snapshots
	if ( cg.infoScreenText[0] != 0 ) {
		CG_DrawInformation();
		return;
	}

	// any looped sounds will be respecified as entities
	// are added to the render list
	trap_S_ClearLoopingSounds(qfalse);

	// clear all the render lists
	trap_R_ClearScene();

	// set up cg.snap and possibly cg.nextSnap
	CG_ProcessSnapshots();

	// if we haven't received any snapshots yet, all
	// we can draw is the information screen
	if ( !cg.snap || ( cg.snap->snapFlags & SNAPFLAG_NOT_ACTIVE ) ) {
		CG_DrawInformation();
		return;
	}

	// let the client system know what our weapon and zoom settings are
	trap_SetUserCmdValue( cg.weaponSelect, cg.zoomSensitivity );

	// this counter will be bumped for every valid scene we generate
	cg.clientFrame++;

	// update cg.predictedPlayerState
	CG_PredictPlayerState();

	// decide on third person view
	cg.renderingThirdPerson = cg.snap->ps.persistant[PERS_TEAM] != TEAM_SPECTATOR
							&& (cg_thirdPerson.integer || (cg.snap->ps.stats[STAT_HEALTH] <= 0));

	// build cg.refdef
	inwater = CG_CalcViewValues();

	// first person blend blobs, done after AnglesToAxis
	if ( !cg.renderingThirdPerson ) {
		CG_DamageBlendBlob();
	}

	// build the render lists
	if ( !cg.hyperspace ) {
		CG_AddPacketEntities();			// adter calcViewValues, so predicted player state is correct
		CG_AddMarks();
		CG_AddParticles ();
		CG_AddLocalEntities();
	}
	CG_AddViewWeapon( &cg.predictedPlayerState );

	// ghost recording / replay and the rising void
	CG_RaceFrame();

	// add buffered sounds
	CG_PlayBufferedSounds();

#ifdef MISSIONPACK
	// play buffered voice chats
	CG_PlayBufferedVoiceChats();
#endif

	// finish up the rest of the refdef
	if ( cg.testModelEntity.hModel ) {
		CG_AddTestModel();
	}

	// STRAFE 64 DEBUG: skeletal (IQM) render-proof harness. `cg_iqmTest 1`
	// registers a known-good IQM and drops it as a world entity in front of the
	// player, cycling its animation frames so it can be orbited and confirmed.
	// Scaffolding for the skeletal-animation work — implicit cvars, one file.
	{
		char	ibuf[32];

		trap_Cvar_VariableStringBuffer( "cg_iqmTest", ibuf, sizeof( ibuf ) );
		if ( atof( ibuf ) ) {
		static qhandle_t	iqmTest = -1;
		refEntity_t			ent;
		vec3_t				fwd;
		float				scale, yaw;
		int					nf;

		if ( iqmTest == -1 ) {
			iqmTest = trap_R_RegisterModel( "models/test/mrfixit.iqm" );
		}
		if ( iqmTest ) {
			memset( &ent, 0, sizeof( ent ) );

			trap_Cvar_VariableStringBuffer( "cg_iqmTestScale", ibuf, sizeof( ibuf ) );
			scale = atof( ibuf );
			if ( scale <= 0 ) {
				scale = 24.0f;
			}
			// slow turntable spin (deg) so every side can be inspected
			yaw = ( cg.time / 30 ) % 360;
			VectorSet( ent.axis[0], cos( DEG2RAD( yaw ) ) * scale, sin( DEG2RAD( yaw ) ) * scale, 0 );
			VectorSet( ent.axis[1], -sin( DEG2RAD( yaw ) ) * scale, cos( DEG2RAD( yaw ) ) * scale, 0 );
			VectorSet( ent.axis[2], 0, 0, scale );
			ent.nonNormalizedAxes = qtrue;

			// fixed world point on the arena floor (independent of the player)
			VectorSet( ent.origin, 0, 0, 26 );
			(void)fwd;
			VectorCopy( ent.origin, ent.lightingOrigin );

			ent.hModel = iqmTest;
			ent.renderfx = RF_MINLIGHT;

			// cycle frames (~25 fps); the IQM loader wraps frame % num_frames
			trap_Cvar_VariableStringBuffer( "cg_iqmTestFrames", ibuf, sizeof( ibuf ) );
			nf = atoi( ibuf );
			if ( nf <= 0 ) {
				nf = 30;
			}
			ent.frame = ( cg.time / 40 ) % nf;
			ent.oldframe = ( ent.frame + nf - 1 ) % nf;
			ent.backlerp = 0;

			trap_R_AddRefEntityToScene( &ent );
		}
		}
	}
	cg.refdef.time = cg.time;
	memcpy( cg.refdef.areamask, cg.snap->areamask, sizeof( cg.refdef.areamask ) );

	// warning sounds when powerup is wearing off
	CG_PowerupTimerSounds();

	// update audio positions
	trap_S_Respatialize( cg.snap->ps.clientNum, cg.refdef.vieworg, cg.refdef.viewaxis, inwater );

	// make sure the lagometerSample and frame timing isn't done twice when in stereo
	if ( stereoView != STEREO_RIGHT ) {
		cg.frametime = cg.time - cg.oldTime;
		if ( cg.frametime < 0 ) {
			cg.frametime = 0;
		}
		cg.oldTime = cg.time;
		CG_AddLagometerFrameInfo();
	}
	if (cg_timescale.value != cg_timescaleFadeEnd.value) {
		if (cg_timescale.value < cg_timescaleFadeEnd.value) {
			cg_timescale.value += cg_timescaleFadeSpeed.value * ((float)cg.frametime) / 1000;
			if (cg_timescale.value > cg_timescaleFadeEnd.value)
				cg_timescale.value = cg_timescaleFadeEnd.value;
		}
		else {
			cg_timescale.value -= cg_timescaleFadeSpeed.value * ((float)cg.frametime) / 1000;
			if (cg_timescale.value < cg_timescaleFadeEnd.value)
				cg_timescale.value = cg_timescaleFadeEnd.value;
		}
		if (cg_timescaleFadeSpeed.value) {
			trap_Cvar_Set("timescale", va("%f", cg_timescale.value));
		}
	}

	// drive depth-of-field (amount from slow-mo, focus from a forward trace)
	CG_UpdateDoF();

	// actually issue the rendering calls
	CG_DrawActive( stereoView );

	if ( cg_stats.integer ) {
		CG_Printf( "cg.clientFrame:%i\n", cg.clientFrame );
	}


}

