// Copyright (C) 2024 STRAFE 64
//
// cg_killcam.c -- cinematic "diagnostic dusk" killcam.
//
// When the local pilot dies, the world is already near-frozen (the movement
// time-bind drops to g_timeBindMin because a dead pilot stops moving), so we
// don't touch the global clock at all -- we just fly a director camera through
// the frozen moment on the REAL clock (trap_Milliseconds), which is the
// bullet-time signature: time stopped, viewpoint moving.
//
// The look is layered on top of existing renderer systems via cvars that we
// snapshot on entry and restore on exit:
//   - cool, desaturated grade            (r_grade*)
//   - tight vignette tunnel              (r_vignette)
//   - cyan fresnel rim on the actors     (r_rim*)
//   - a warm dynamic light at the kill   (trap_R_AddLightToScene) + bloom pop
//   - DoF rack-focus pulled onto the kill (r_dof* with autofocus off)
//   - NERV-themed 2D framing + telemetry (CG_DrawMatrixString)
//
// cg_killcamStyle 1 adds the loud "full Matrix" layer (green wash + bodycam
// chroma/scanline) on the same pipeline.

#include "cg_local.h"

#ifndef Vector4Set
#define Vector4Set(v,a,b,c,d)	((v)[0]=(a),(v)[1]=(b),(v)[2]=(c),(v)[3]=(d))
#endif

// ----- tuning --------------------------------------------------------------

#define KC_MIN_MS			4200		// shortest camera move (trivial death)
#define KC_FADE_IN_MS		220
#define KC_HOLD_FADE_MS		400			// info panel fade-in once the move settles

// phase boundaries as a fraction of the total duration -- the orbit is the long,
// languid heart of the shot; freeze is a beat, push/release are the payoff
#define KC_FREEZE_END		0.09f
#define KC_ORBIT_END		0.74f
#define KC_PUSH_END			0.90f
// remainder is RELEASE

// FOV framing: the kill is framed at a controlled aesthetic FOV; the roaming
// orbit opens up wide and cinematic; the push racks in to a tight telephoto
#define KC_FOV_KILL			66.0f		// opening beat, framed on the kill
#define KC_FOV_WIDE			92.0f		// dramatic roam through the orbit
#define KC_FOV_TELE			60.0f		// compressed telephoto landing on the kill

// ----- state ---------------------------------------------------------------

typedef struct {
	char	name[32];
	char	saved[64];
} kcCvarSave_t;

static struct {
	qboolean	active;
	qboolean	lookApplied;	// renderer cvars are currently overridden
	int			startReal;		// trap_Milliseconds() at trigger
	int			durationMs;

	vec3_t		killSpot;		// victim (our) origin at death -- the focal point
	vec3_t		killerSpot;		// attacker origin at death
	qboolean	haveKiller;
	float		baseYaw;		// yaw from kill spot toward the killer

	int			attacker;		// client num, or -1
	int			mod;			// means of death
	int			speed;			// our horizontal speed at death (ups)
	float		intensity;		// 0..1 score -> drives bloom/dof/duration

	// most-recent obituary naming the local pilot as the victim
	int			obitAttacker;
	int			obitMod;
	int			obitTime;		// cg.time it arrived
} kc;

#define KC_NUM_SAVED 15
static kcCvarSave_t kc_saved[KC_NUM_SAVED];

// ----- cvar snapshot / restore ---------------------------------------------

// cvars the killcam drives; snapshotted verbatim on entry, restored on exit.
// g_timeBind is included so we can seize the world clock for the bullet-time
// ramp and hand the movement time-bind back exactly as it was afterwards.
static const char *kc_driveCvars[KC_NUM_SAVED] = {
	"r_gradeSaturation",
	"r_gradeTemp",
	"r_gradeContrast",
	"r_vignette",
	"r_rimScale",
	"r_rimColorR",
	"r_rimColorG",
	"r_rimColorB",
	"r_bloom",
	"r_dofAutoFocus",
	"r_dofFocalRange",
	"r_bodycam",
	"r_bodycamChroma",
	"r_bodycamScanline",
	"g_timeBind"
};

static void CG_KillcamSnapshotCvars( void ) {
	int i;

	for ( i = 0; i < KC_NUM_SAVED; i++ ) {
		Q_strncpyz( kc_saved[i].name, kc_driveCvars[i], sizeof( kc_saved[i].name ) );
		trap_Cvar_VariableStringBuffer( kc_saved[i].name, kc_saved[i].saved,
			sizeof( kc_saved[i].saved ) );
	}
}

static void CG_KillcamRestoreCvars( void ) {
	int i;

	for ( i = 0; i < KC_NUM_SAVED; i++ ) {
		if ( kc_saved[i].name[0] ) {
			trap_Cvar_Set( kc_saved[i].name, kc_saved[i].saved );
		}
	}
}

// apply the static "diagnostic dusk" base look; per-frame values (rack focus,
// bloom ramp) are layered on top in CG_KillcamUpdate
static void CG_KillcamApplyLook( void ) {
	qboolean matrix = ( cg_killcamStyle.integer == 1 );

	// cool, contrasty grade -- desaturate only a little so it reads cinematic,
	// not washed out; the warm kill pool still pops as the one warm thing
	trap_Cvar_Set( "r_gradeSaturation", "0.86" );
	trap_Cvar_Set( "r_gradeTemp", "-0.12" );
	trap_Cvar_Set( "r_gradeContrast", "1.22" );
	trap_Cvar_Set( "r_vignette", "0.40" );

	// cyan fresnel rim outlines the actors as "data objects"
	trap_Cvar_Set( "r_rimScale", "0.62" );
	trap_Cvar_Set( "r_rimColorR", "0.24" );
	trap_Cvar_Set( "r_rimColorG", "0.70" );
	trap_Cvar_Set( "r_rimColorB", "1.0" );

	// rack focus: take manual control of the focal plane
	trap_Cvar_Set( "r_dofAutoFocus", "0" );

	// seize the world clock: disable the movement time-bind so it stops driving
	// timescale, then CG_KillcamUpdate ramps our own bullet-time curve (the slow
	// motion is locked to the same real-clock progress as the pan + zoom)
	trap_Cvar_Set( "g_timeBind", "0" );

	if ( matrix ) {
		// the loud layer: found-footage chroma + rolling scanline on top
		trap_Cvar_Set( "r_bodycam", "1" );
		trap_Cvar_Set( "r_bodycamChroma", "1.1" );
		trap_Cvar_Set( "r_bodycamScanline", "0.02" );
	}

	kc.lookApplied = qtrue;
}

// ----- trigger / teardown ---------------------------------------------------

/*
=================
CG_KillcamNoteObituary

Called from CG_Obituary for every death. We only stash the one where the local
pilot is the victim, so the death-state transition can find out who killed us.
=================
*/
void CG_KillcamNoteObituary( int victim, int attacker, int mod ) {
	if ( !cg.snap ) {
		return;
	}
	if ( victim != cg.snap->ps.clientNum ) {
		return;
	}
	kc.obitAttacker = attacker;
	kc.obitMod = mod;
	kc.obitTime = cg.time;
}

// quick 0..1 "how cinematic should this be" score from speed + kill type
static float CG_KillcamScore( void ) {
	float s = 0.0f;

	s += (float)kc.speed / 1000.0f;			// fast death == more dramatic
	if ( kc.haveKiller ) {
		s += 0.30f;							// a real opponent, not the void
	}
	if ( kc.mod == MOD_SWORD ) {
		s += 0.35f;							// blade kills are the money shot
	}
	if ( s < 0.0f ) s = 0.0f;
	if ( s > 1.0f ) s = 1.0f;
	return s;
}

/*
=================
CG_KillcamPlayerDied

Fired from CG_TransitionPlayerState on the health>0 -> health<=0 crossing.
=================
*/
void CG_KillcamPlayerDied( void ) {
	playerState_t	*ps;
	vec3_t			delta;

	if ( !cg_killcam.integer || kc.active || cg.demoPlayback ) {
		return;
	}
	if ( !cg.snap || cg.snap->ps.persistant[PERS_TEAM] == TEAM_SPECTATOR ) {
		return;
	}
	// only OUR death (not a followed player's) rolls the killcam
	if ( cg.snap->ps.clientNum != cg.clientNum ) {
		return;
	}

	ps = &cg.predictedPlayerState;

	// the focal point is where we fell
	VectorCopy( cg.snap->ps.origin, kc.killSpot );

	// horizontal speed at the moment of death (telemetry + score)
	kc.speed = (int)sqrt( ps->velocity[0] * ps->velocity[0] +
		ps->velocity[1] * ps->velocity[1] );

	// resolve the killer from a recent obituary (same frame, usually)
	kc.attacker = -1;
	kc.haveKiller = qfalse;
	if ( kc.obitTime && ( cg.time - kc.obitTime ) <= 250 ) {
		kc.mod = kc.obitMod;
		if ( kc.obitAttacker >= 0 && kc.obitAttacker < MAX_CLIENTS
			&& kc.obitAttacker != cg.snap->ps.clientNum ) {
			kc.attacker = kc.obitAttacker;
			VectorCopy( cg_entities[kc.attacker].lerpOrigin, kc.killerSpot );
			kc.haveKiller = qtrue;
		}
	} else {
		kc.mod = MOD_UNKNOWN;
	}

	// base orbit yaw points from the kill spot toward the killer (so the sweep
	// resolves with the attacker in frame); fall back to our own view yaw
	if ( kc.haveKiller ) {
		VectorSubtract( kc.killerSpot, kc.killSpot, delta );
		kc.baseYaw = atan2( delta[1], delta[0] ) * ( 180.0f / M_PI );
	} else {
		kc.baseYaw = cg.refdefViewAngles[YAW];
	}

	kc.intensity = CG_KillcamScore();
	kc.durationMs = KC_MIN_MS + (int)( kc.intensity * ( cg_killcamTime.integer - KC_MIN_MS > 0 ?
		( cg_killcamTime.integer - KC_MIN_MS ) : 0 ) );

	kc.startReal = trap_Milliseconds();
	kc.active = qtrue;

	CG_KillcamSnapshotCvars();
	CG_KillcamApplyLook();
}

/*
=================
CG_KillcamStop

Tear down: restore every renderer cvar we touched. Safe to call when inactive.
=================
*/
void CG_KillcamStop( void ) {
	if ( kc.lookApplied ) {
		CG_KillcamRestoreCvars();		// also restores g_timeBind to its prior value
		trap_Cvar_Set( "timescale", "1" );	// belt-and-suspenders before time-bind re-engages
		kc.lookApplied = qfalse;
	}
	kc.active = qfalse;
}

qboolean CG_KillcamActive( void ) {
	return (qboolean)( kc.active && cg_killcam.integer );
}

// true once the camera move has finished and we're holding the kill screen
qboolean CG_KillcamHolding( void ) {
	if ( !kc.active || kc.durationMs <= 0 ) {
		return qfalse;
	}
	return (qboolean)( ( trap_Milliseconds() - kc.startReal ) >= kc.durationMs );
}

// seconds spent holding past the end of the camera move (0 during the move)
static float CG_KillcamHoldSecs( void ) {
	int over = ( trap_Milliseconds() - kc.startReal ) - kc.durationMs;
	return over > 0 ? (float)over / 1000.0f : 0.0f;
}

// 0..1 progress on the real clock
static float CG_KillcamFrac( void ) {
	float f;

	if ( kc.durationMs <= 0 ) {
		return 1.0f;
	}
	f = (float)( trap_Milliseconds() - kc.startReal ) / (float)kc.durationMs;
	if ( f < 0.0f ) f = 0.0f;
	if ( f > 1.0f ) f = 1.0f;
	return f;
}

// smoothstep ease
static float CG_KC_Ease( float t ) {
	if ( t < 0.0f ) t = 0.0f;
	if ( t > 1.0f ) t = 1.0f;
	return t * t * ( 3.0f - 2.0f * t );
}

// remap x in [a,b] to eased 0..1
static float CG_KC_Span( float x, float a, float b ) {
	if ( b <= a ) return 0.0f;
	return CG_KC_Ease( ( x - a ) / ( b - a ) );
}

// the bullet-time curve, keyed to the SAME real-clock progress as the camera:
// the impact hangs near-frozen, the world then creeps in slow-motion through
// the pan (ragdoll/blood settle visibly), slows again for the telephoto rack,
// then freezes on the held hero frame for the mission report
static float CG_KillcamTimescale( float f ) {
	if ( f < KC_FREEZE_END ) {
		return 0.05f;												// impact beat
	}
	if ( f < KC_ORBIT_END ) {
		return 0.05f + 0.20f * CG_KC_Span( f, KC_FREEZE_END, KC_ORBIT_END );	// creep 0.05->0.25
	}
	if ( f < KC_PUSH_END ) {
		return 0.25f - 0.16f * CG_KC_Span( f, KC_ORBIT_END, KC_PUSH_END );	// slow for the rack 0.25->0.09
	}
	return 0.04f;													// hold, near-frozen
}

// the shot's subject: the victim's corpse where it actually lies now (so a
// telephoto push always lands ON the body), falling back to the fixed death
// spot if the body entity isn't valid or has slid unreasonably far
static void CG_KillcamFocal( vec3_t out ) {
	VectorCopy( kc.killSpot, out );
	{
		centity_t	*body = &cg_entities[ cg.clientNum ];	// OUR corpse, even while following
		if ( body->currentValid ) {
			vec3_t d;
			VectorSubtract( body->lerpOrigin, kc.killSpot, d );
			if ( VectorLength( d ) < 400.0f ) {
				VectorCopy( body->lerpOrigin, out );
			}
		}
	}
	out[2] += 32.0f;		// aim at chest height, not the feet
}

// ----- the director camera --------------------------------------------------

/*
=================
CG_KillcamCalcView

Owns cg.refdef.vieworg + cg.refdefViewAngles while the killcam runs. Called from
CG_CalcViewValues, which then builds the view axis and FOV as normal. Also drops
the warm kill-pool light into the (already-cleared) scene.
=================
*/
void CG_KillcamCalcView( void ) {
	float	f, e;
	float	az, radius, elev, fov, xproj;
	float	caz, sez, cel, sel;
	vec3_t	focal, campos, dir;
	float	now;

	f = CG_KillcamFrac();

	// subject = the body where it lies now (keeps the telephoto on the action)
	CG_KillcamFocal( focal );

	// orbit progression spans ORBIT+PUSH; FREEZE holds the start pose
	e = CG_KC_Span( f, KC_FREEZE_END, KC_PUSH_END );

	// azimuth sweeps a wide arc around the kill (slow, since the move is long),
	// ending closer to the killer line; on hold it keeps drifting so it breathes
	az = kc.baseYaw + ( 150.0f - 140.0f * e ) + CG_KillcamHoldSecs() * 4.0f;

	// radius pulls in (dolly push) but stays back enough for the telephoto to
	// frame the whole body; elevation stays up so we look DOWN at it, not graze
	radius = 230.0f - 74.0f * e;
	elev   = 30.0f - 8.0f * e;

	// a touch of real-clock handheld float so the frozen frame still breathes
	now = (float)trap_Milliseconds() * 0.001f;
	az   += sin( now * 0.6f ) * 1.1f;
	elev += sin( now * 0.9f ) * 0.6f;

	// FOV: framed on the kill at the opening beat, widen through the roaming
	// orbit, then rack in to a compressed telephoto that lands on the kill and
	// holds -- so only the roam is wide; the kill stays deliberately framed
	if ( f < KC_ORBIT_END ) {
		fov = KC_FOV_KILL + ( KC_FOV_WIDE - KC_FOV_KILL )
			* CG_KC_Span( f, KC_FREEZE_END, KC_ORBIT_END );
	} else {
		fov = KC_FOV_WIDE + ( KC_FOV_TELE - KC_FOV_WIDE )
			* CG_KC_Span( f, KC_ORBIT_END, KC_PUSH_END );
	}

	caz = cos( DEG2RAD( az ) );
	sez = sin( DEG2RAD( az ) );
	cel = cos( DEG2RAD( elev ) );
	sel = sin( DEG2RAD( elev ) );

	campos[0] = focal[0] + radius * caz * cel;
	campos[1] = focal[1] + radius * sez * cel;
	campos[2] = focal[2] + radius * sel;

	// keep the rig out of walls/floor: trace from the kill point to the camera
	// and pull in to the first solid (a small box so it never pokes through)
	{
		static const vec3_t	mins = { -8, -8, -8 };
		static const vec3_t	maxs = {  8,  8,  8 };
		trace_t				tr;

		CG_Trace( &tr, focal, mins, maxs, campos, cg.snap->ps.clientNum, MASK_SOLID );
		if ( tr.fraction < 1.0f ) {
			VectorCopy( tr.endpos, campos );
		}
	}

	VectorCopy( campos, cg.refdef.vieworg );

	VectorSubtract( focal, campos, dir );
	vectoangles( dir, cg.refdefViewAngles );
	cg.refdefViewAngles[ROLL] = 0;

	// set the framing FOV directly (the view branch skips CG_CalcFov for us).
	// fov_y from the viewport aspect, matching CG_CalcFov's projection.
	xproj = cg.refdef.width / tan( DEG2RAD( fov * 0.5f ) );
	cg.refdef.fov_x = fov;
	cg.refdef.fov_y = atan2( cg.refdef.height, xproj ) * ( 360.0f / M_PI );

	// warm kill-pool light: against the cool grade + boosted bloom this becomes
	// the single hottest thing in frame, pulling the eye to the kill
	{
		float pulse = 0.85f + 0.15f * sin( now * 4.0f );
		trap_R_AddLightToScene( focal, ( 160.0f + 120.0f * kc.intensity ) * pulse,
			1.0f, 0.62f, 0.28f );
	}
}

/*
=================
CG_KillcamUpdate

Per-frame bookkeeping: advance/expire the killcam, drive the rack-focus and
bloom ramps, and on respawn (or timeout) restore the look. Called once per frame
from CG_DrawActiveFrame.
=================
*/
void CG_KillcamUpdate( void ) {
	float	f, push;
	float	dist;
	vec3_t	focal, delta;

	// turned off mid-run, or disabled entirely -> tidy up
	if ( kc.active && !cg_killcam.integer ) {
		CG_KillcamStop();
		return;
	}
	if ( !kc.active ) {
		return;
	}

	// end the kill screen only on a GENUINE local respawn -- alive again AND
	// viewing our own player. While dead (health<=0) OR while following the
	// killer (ps.clientNum != ours), the killcam holds.
	if ( cg.snap && cg.snap->ps.clientNum == cg.clientNum
		&& cg.snap->ps.stats[STAT_HEALTH] > 0 ) {
		CG_KillcamStop();
		return;
	}

	f = CG_KillcamFrac();

	// own the world clock: ramp the bullet-time curve every frame (we disabled
	// g_timeBind on entry, so nothing else is touching timescale). The camera
	// runs on the real clock, so slow-mo world + full-speed pan/zoom stay locked.
	trap_Cvar_Set( "timescale", va( "%.3f", CG_KillcamTimescale( f ) ) );

	// NOTE: we do NOT stop when the move finishes (f>=1). The killcam is the
	// single kill screen -- it holds the composed hero frame + mission report
	// until the pilot fires to respawn (CG_KillcamStop from the spawn-count
	// transition in cg_playerstate.c).

	// rack focus: keep the focal plane locked to the body, and tighten the
	// focal range during the push so the background melts (shallow DoF)
	CG_KillcamFocal( focal );
	VectorSubtract( focal, cg.refdef.vieworg, delta );
	dist = VectorLength( delta );

	push = CG_KC_Span( f, KC_FREEZE_END, KC_PUSH_END );

	// lock the focal plane to the kill, melt the range as we push in (shallow DoF)
	trap_Cvar_Set( "r_dofFocalDist", va( "%.0f", dist ) );
	trap_Cvar_Set( "r_dofFocalRange", va( "%.0f", 320.0f - 200.0f * push ) );
	trap_Cvar_Set( "r_bloom", va( "%.2f", 0.20f + 0.26f * kc.intensity * push ) );
}

// ----- 2D overlay (NERV framing + telemetry) --------------------------------

// project a world point to 640x480 virtual screen coords; qfalse if behind cam
static qboolean CG_KC_Project( const vec3_t world, float *sx, float *sy ) {
	vec3_t	d;
	float	xf, xr, xu;
	float	tanx, tany;

	VectorSubtract( world, cg.refdef.vieworg, d );
	xf = DotProduct( d, cg.refdef.viewaxis[0] );	// forward
	if ( xf < 1.0f ) {
		return qfalse;								// at or behind the lens
	}
	xr = DotProduct( d, cg.refdef.viewaxis[1] );	// left
	xu = DotProduct( d, cg.refdef.viewaxis[2] );	// up

	tanx = tan( DEG2RAD( cg.refdef.fov_x * 0.5f ) );
	tany = tan( DEG2RAD( cg.refdef.fov_y * 0.5f ) );
	if ( tanx <= 0.0f || tany <= 0.0f ) {
		return qfalse;
	}

	*sx = 320.0f - 320.0f * ( xr / ( xf * tanx ) );	// +left -> screen-left
	*sy = 240.0f - 240.0f * ( xu / ( xf * tany ) );
	return qtrue;
}

// one NERV corner bracket box centered at (cx,cy), half-size hx/hy
static void CG_KC_Bracket( float cx, float cy, float hx, float hy,
		float arm, float thick, const float *color ) {
	float l = cx - hx, r = cx + hx, t = cy - hy, b = cy + hy;

	trap_R_SetColor( color );
	// top-left
	CG_FillRect( l, t, arm, thick, color );
	CG_FillRect( l, t, thick, arm, color );
	// top-right
	CG_FillRect( r - arm, t, arm, thick, color );
	CG_FillRect( r - thick, t, thick, arm, color );
	// bottom-left
	CG_FillRect( l, b - thick, arm, thick, color );
	CG_FillRect( l, b - arm, thick, arm, color );
	// bottom-right
	CG_FillRect( r - arm, b - thick, arm, thick, color );
	CG_FillRect( r - thick, b - arm, thick, arm, color );
	trap_R_SetColor( NULL );
}

// dashed connector between two screen points (CG_FillRect is axis-aligned only,
// so we step square dashes along the line)
static void CG_KC_DashLine( float x0, float y0, float x1, float y1,
		const float *color ) {
	float	dx = x1 - x0, dy = y1 - y0;
	float	len = sqrt( dx * dx + dy * dy );
	int		i, n;

	if ( len < 1.0f ) {
		return;
	}
	n = (int)( len / 9.0f );
	for ( i = 0; i <= n; i++ ) {
		float a = (float)i / (float)( n > 0 ? n : 1 );
		float px = x0 + dx * a;
		float py = y0 + dy * a;
		CG_FillRect( px - 1.0f, py - 1.0f, 2.0f, 2.0f, color );
	}
}

/*
=================
CG_DrawKillcam

The whole 2D layer. Drawn instead of the normal HUD while the killcam runs.
=================
*/
void CG_DrawKillcam( void ) {
	static vec4_t	cyan   = { 0.44f, 0.86f, 1.0f, 1.0f };
	static vec4_t	cyanDim= { 0.30f, 0.62f, 0.78f, 1.0f };
	static vec4_t	amber  = { 1.0f, 0.66f, 0.24f, 1.0f };
	static vec4_t	black  = { 0.0f, 0.0f, 0.0f, 1.0f };
	float	f, alpha, bar, remain;
	float	vx, vy, kx, ky;
	qboolean vvis, kvis;
	vec3_t	focal;
	vec4_t	col;
	char	buf[64];
	int		elapsedReal;

	f = CG_KillcamFrac();
	elapsedReal = trap_Milliseconds() - kc.startReal;

	// fade in on entry; then hold at full until respawn hard-cuts it (no
	// fade-out -- the killcam is the whole kill screen and stays up while dead)
	alpha = 1.0f;
	if ( elapsedReal < KC_FADE_IN_MS ) {
		alpha = (float)elapsedReal / (float)KC_FADE_IN_MS;
	}
	remain = (float)kc.durationMs - (float)elapsedReal;
	if ( remain < 0.0f ) remain = 0.0f;

	// full-Matrix green wash (style 1)
	if ( cg_killcamStyle.integer == 1 ) {
		Vector4Set( col, 0.0f, 0.32f, 0.06f, 0.12f * alpha );
		CG_FillRect( 0, 0, 640, 480, col );
	}

	// cinematic letterbox, eased in over the freeze
	bar = 44.0f * CG_KC_Span( f, 0.0f, KC_FREEZE_END ) * alpha;
	if ( bar > 1.0f ) {
		black[3] = alpha;
		CG_FillRect( 0, 0, 640, bar, black );
		CG_FillRect( 0, 480 - bar, 640, bar, black );
	}

	// scan-grid sweep: a single cyan line drops down the frame during the freeze
	{
		float sweep = CG_KC_Span( f, 0.0f, KC_ORBIT_END );
		float sy = sweep * 480.0f;
		float sa = ( 1.0f - sweep ) * 0.5f * alpha;
		if ( sa > 0.01f ) {
			Vector4Set( col, cyan[0], cyan[1], cyan[2], sa );
			CG_FillRect( 0, sy, 640, 2, col );
		}
	}

	// project the two actors
	CG_KillcamFocal( focal );
	vvis = CG_KC_Project( focal, &vx, &vy );

	// the killer: project it, and if it's in front of the lens but off-frame,
	// CLAMP it to the screen edge so the dotted connector still reads and points
	// toward it (the wide orbit puts the killer off-screen most of the time)
	kvis = qfalse;
	if ( kc.haveKiller ) {
		vec3_t kf;
		VectorCopy( cg_entities[kc.attacker].lerpOrigin, kf );
		kf[2] += 40.0f;
		kvis = CG_KC_Project( kf, &kx, &ky );
		if ( kvis ) {
			if ( kx < 22.0f ) kx = 22.0f; else if ( kx > 618.0f ) kx = 618.0f;
			if ( ky < 22.0f ) ky = 22.0f; else if ( ky > 458.0f ) ky = 458.0f;
		}
	}

	// NERV dotted connector + brackets. The dashed line victim->killer is the
	// signature "diagnostic link" -- draw it whenever both read.
	if ( vvis && kvis ) {
		vec4_t lc; Vector4Set( lc, cyan[0], cyan[1], cyan[2], 0.9f * alpha );
		CG_KC_DashLine( vx, vy, kx, ky, lc );
	}
	if ( vvis ) {
		vec4_t bc; Vector4Set( bc, cyan[0], cyan[1], cyan[2], alpha );
		CG_KC_Bracket( vx, vy, 36, 46, 12, 2, bc );
		// dotted targeting cross through the victim bracket -- always some dotted
		// line-work on frame even on a killer-less death (suicide/void)
		{
			vec4_t dc; Vector4Set( dc, cyanDim[0], cyanDim[1], cyanDim[2], 0.5f * alpha );
			CG_KC_DashLine( vx - 90, vy, vx - 40, vy, dc );
			CG_KC_DashLine( vx + 40, vy, vx + 90, vy, dc );
		}
	}
	if ( kvis ) {
		vec4_t bc; Vector4Set( bc, cyanDim[0], cyanDim[1], cyanDim[2], 0.85f * alpha );
		CG_KC_Bracket( kx, ky, 22, 26, 8, 2, bc );
	}

	// --- telemetry, in the matrix/LED HUD font -----------------------------
	{
		vec4_t tc;

		// top-left scrub stamp, counting down through the move (hidden on hold)
		if ( !CG_KillcamHolding() ) {
			Vector4Set( tc, cyan[0], cyan[1], cyan[2], alpha );
			Com_sprintf( buf, sizeof( buf ), "REPLAY -%.2fS", remain / 1000.0f );
			CG_DrawMatrixString( 24, 26, buf, 2.0f, tc );
		}

		// bottom-left: the diagnostic stats attached to the moment
		Vector4Set( tc, cyan[0], cyan[1], cyan[2], 0.9f * alpha );
		Com_sprintf( buf, sizeof( buf ), "SPEED %i", kc.speed );
		CG_DrawMatrixString( 24, 430, buf, 2.0f, tc );

		if ( vvis && kvis ) {
			vec3_t d; float dist;
			VectorSubtract( kc.killerSpot, kc.killSpot, d );
			dist = VectorLength( d );
			Vector4Set( tc, cyanDim[0], cyanDim[1], cyanDim[2], 0.8f * alpha );
			Com_sprintf( buf, sizeof( buf ), "DIST %iU", (int)dist );
			// label rides the connector midpoint
			CG_DrawMatrixString( ( vx + kx ) * 0.5f - 20, ( vy + ky ) * 0.5f - 6, buf, 1.6f, tc );
		}

		// the one warm UI accent: the elimination line
		Vector4Set( tc, amber[0], amber[1], amber[2], alpha );
		if ( kc.haveKiller && kc.attacker >= 0 && kc.attacker < MAX_CLIENTS ) {
			Com_sprintf( buf, sizeof( buf ), "ELIMINATED BY %s",
				cgs.clientinfo[kc.attacker].name );
		} else {
			Q_strncpyz( buf, "ELIMINATED", sizeof( buf ) );
		}
		CG_DrawMatrixString( 636 - CG_MatrixStringWidth( buf, 2.0f ), 430, buf, 2.0f, tc );
	}

	// once the cinematic move settles, the held frame becomes the kill screen:
	// the run's mission-report payoff (top speed / style / rank / best lap /
	// score) over the frozen hero shot, with the respawn prompt. This replaces
	// the old separate post-killcam buy menu -- one continuous death screen.
	if ( CG_KillcamHolding() ) {
		CG_DrawMissionReport();
	}
}
