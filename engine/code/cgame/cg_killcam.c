// Copyright (C) 2024 STRAFE 64
//
// cg_killcam.c -- cinematic "diagnostic dusk" killcam.
//
// On the local pilot's death a small PROCEDURAL SHOT DIRECTOR plays a sequence
// of short cinematic shots (hard cuts between them, action-editing style)
// chosen by the kill data -- impact freeze, orbit the killed, reveal both
// fighters, push onto the killer. Each shot runs on the REAL clock while the
// world plays in bullet-time slow-motion (per-shot timescale), so the camera
// moves at full speed through a slowed world -- the Matrix signature.
//
// The stats are baked into the scene DIEGETICALLY (tracked brackets + typographic
// reveals that shift with the kind of kill), not a separate mission-report panel.
//
// The look is layered on existing renderer systems via cvars snapshotted on entry
// and restored on exit: cool desaturated grade, vignette, cyan fresnel rim, a warm
// kill-pool light + bloom, DoF rack-focus. g_timeBind is seized so we own the
// world clock for the bullet-time ramp. cg_killcamStyle 1 adds the full-Matrix
// layer (green wash + bodycam chroma/scanline).

#include "cg_local.h"

#ifndef Vector4Set
#define Vector4Set(v,a,b,c,d)	((v)[0]=(a),(v)[1]=(b),(v)[2]=(c),(v)[3]=(d))
#endif

// ----- tuning --------------------------------------------------------------

#define KC_FADE_IN_MS		200
#define KC_MAX_SHOTS		5

// ----- shot vocabulary ------------------------------------------------------

typedef enum {
	SUBJ_VICTIM,		// the killed body (tracks the corpse)
	SUBJ_KILLER,		// the attacker
	SUBJ_MID			// midpoint between the two (two-shot)
} kcSubject_t;

// per-shot effect flags
#define KSF_FLASH	1		// white flash-in on the cut
#define KSF_SHAKE	2		// decaying impact shake at the head of the shot

// one camera shot: an orbit arc around a subject with a dolly (radius ramp),
// crane (elevation ramp), zoom (fov ramp), dutch (roll ramp), its own
// bullet-time depth and DoF character. Every classic move is a config of this:
// push-in = radius ramp; crash zoom = fov ramp; crane reveal = elev+radius up;
// low hero = negative elevation; dutch orbit = arc + roll.
typedef struct {
	kcSubject_t	subject;
	float		startAz;	// starting azimuth, degrees relative to baseYaw
	float		arcDeg;		// azimuth swept across the shot (0 = static angle)
	float		r0, r1;		// dolly: start/end radius (r1<r0 = push in)
	float		e0, e1;		// crane: start/end elevation, degrees (negative = below eye)
	float		fov0, fov1;	// zoom: start/end FOV
	float		roll0, roll1;	// dutch: start/end camera roll, degrees
	int			durMs;		// real-clock duration
	float		ts0, ts1;	// world timescale ramp across the shot (freeze->release)
	float		dofRange;	// DoF focal range character (smaller = meltier background)
	int			flags;		// KSF_*
} kcShot_t;

// ----- state ---------------------------------------------------------------

typedef struct {
	const char	*name;
	const char	*def;		// the engine's compiled default -- the restore target
} kcLookCvar_t;

static struct {
	qboolean	active;
	qboolean	lookApplied;
	int			startReal;		// trap_Milliseconds() at trigger

	vec3_t		killSpot;		// our origin at death (focal fallback)
	vec3_t		killerSpot;		// attacker origin at death
	qboolean	haveKiller;
	float		baseYaw;		// yaw from kill spot toward the killer

	int			attacker;		// client num, or -1
	int			mod;			// means of death
	int			speed;			// horizontal speed at death (ups)
	float		intensity;		// 0..1 score
	int			killerCombo;	// killer's kills in the last few seconds (>=2 = combo)

	// run stats snapshotted at death (so a respawn reset can't clear them)
	int			statPeak, statStyle, statStylePB, statScore, statRecord, statPar;

	// shot list
	kcShot_t	shots[KC_MAX_SHOTS];
	int			numShots;
	int			totalMs;

	// most-recent obituary naming the local pilot as the victim
	int			obitAttacker, obitMod, obitTime;
} kc;

// The look is applied over these cvars, and teardown restores the COMPILED
// DEFAULTS outright -- deliberately NOT a snapshot. These are CVAR_ARCHIVE
// cvars and the engine writes q3config.cfg the moment one changes, so a crash
// or hard kill mid-killcam bakes the look into the config on disk. A snapshot
// taken at the next death would then capture the polluted values and faithfully
// re-"restore" the filter forever ("killcam filter is on the whole time").
// Restoring defaults self-heals that within one killcam. Values must match the
// renderer/game registration defaults (tr_init.c / g_main.c).
static const kcLookCvar_t kc_lookCvars[] = {
	{ "r_gradeSaturation",	"1.08"	},
	{ "r_gradeTemp",		"0.04"	},
	{ "r_gradeContrast",	"1.06"	},
	{ "r_vignette",			"0.18"	},
	{ "r_rimScale",			"0.32"	},
	{ "r_rimColorR",		"0.42"	},
	{ "r_rimColorG",		"0.66"	},
	{ "r_rimColorB",		"1.0"	},
	{ "r_bloom",			"0.25"	},
	{ "r_dofAutoFocus",		"1"		},
	{ "r_dofFocalRange",	"768"	},
	{ "r_bodycam",			"0"		},
	{ "r_bodycamChroma",	"0.7"	},
	{ "r_bodycamScanline",	"0.008"	},
	{ "g_timeBind",			"1"		},
	{ "timescale",			"1"		},
};

// ----- look teardown ---------------------------------------------------------

static void CG_KillcamRestoreCvars( void ) {
	int i;
	for ( i = 0; i < (int)ARRAY_LEN( kc_lookCvars ); i++ ) {
		trap_Cvar_Set( kc_lookCvars[i].name, kc_lookCvars[i].def );
	}
}

static void CG_KillcamApplyLook( void ) {
	qboolean matrix = ( cg_killcamStyle.integer == 1 );

	trap_Cvar_Set( "r_gradeSaturation", "0.86" );
	trap_Cvar_Set( "r_gradeTemp", "-0.12" );
	trap_Cvar_Set( "r_gradeContrast", "1.22" );
	trap_Cvar_Set( "r_vignette", "0.40" );

	trap_Cvar_Set( "r_rimScale", "0.62" );
	trap_Cvar_Set( "r_rimColorR", "0.24" );
	trap_Cvar_Set( "r_rimColorG", "0.70" );
	trap_Cvar_Set( "r_rimColorB", "1.0" );

	trap_Cvar_Set( "r_dofAutoFocus", "0" );
	trap_Cvar_Set( "g_timeBind", "0" );		// seize the world clock

	if ( matrix ) {
		trap_Cvar_Set( "r_bodycam", "1" );
		trap_Cvar_Set( "r_bodycamChroma", "1.1" );
		trap_Cvar_Set( "r_bodycamScanline", "0.02" );
	}

	kc.lookApplied = qtrue;
}

// ----- easing ---------------------------------------------------------------

static float CG_KC_Ease( float t ) {
	if ( t < 0.0f ) t = 0.0f;
	if ( t > 1.0f ) t = 1.0f;
	return t * t * ( 3.0f - 2.0f * t );
}

static float CG_KC_Lerp( float a, float b, float t ) {
	return a + ( b - a ) * t;
}

// ----- obituary + scoring ---------------------------------------------------

static void CG_KillcamResolveKiller( int attacker, int mod );

// rolling log of EVERY obituary (any victim), for combo detection: a killer who
// dropped several fighters in the last few seconds has "struck a combo" and
// earns the hero orbit
#define KC_OBITLOG		8
#define KC_COMBO_MS		5000
static struct { int attacker; int time; } kc_obitLog[KC_OBITLOG];
static int kc_obitLogHead;

static int CG_KillcamComboCount( int attacker ) {
	int i, n = 0;
	for ( i = 0; i < KC_OBITLOG; i++ ) {
		if ( kc_obitLog[i].attacker == attacker && kc_obitLog[i].time
			&& ( cg.time - kc_obitLog[i].time ) <= KC_COMBO_MS ) {
			n++;
		}
	}
	return n;
}

void CG_KillcamNoteObituary( int victim, int attacker, int mod ) {
	// log every kill for combo detection (world kills excluded)
	if ( attacker >= 0 && attacker < MAX_CLIENTS && attacker != victim ) {
		kc_obitLog[kc_obitLogHead].attacker = attacker;
		kc_obitLog[kc_obitLogHead].time = cg.time;
		kc_obitLogHead = ( kc_obitLogHead + 1 ) % KC_OBITLOG;
	}

	if ( victim != cg.clientNum ) {
		return;
	}
	kc.obitAttacker = attacker;
	kc.obitMod = mod;
	kc.obitTime = cg.time;

	// the local death is client-predicted BEFORE this server obituary arrives,
	// so the killcam usually starts WITHOUT knowing the killer. If it just began,
	// fold the killer in now and rebuild to the killer shot sequence.
	if ( kc.active && !kc.haveKiller
		&& ( trap_Milliseconds() - kc.startReal ) < 900 ) {
		CG_KillcamResolveKiller( attacker, mod );
	}
}

static float CG_KillcamScore( void ) {
	float s = (float)kc.speed / 1000.0f;
	if ( kc.haveKiller )         s += 0.30f;
	if ( kc.mod == MOD_SWORD )   s += 0.35f;
	if ( s < 0.0f ) s = 0.0f;
	if ( s > 1.0f ) s = 1.0f;
	return s;
}

// ----- shot-list builder ----------------------------------------------------

// tiny LCG so each death rolls a different edit (seeded at build time)
static int kc_seed;
static int KC_Rnd( int n ) {
	kc_seed = kc_seed * 1103515245 + 12345;
	return ( ( kc_seed >> 16 ) & 0x7fff ) % ( n > 0 ? n : 1 );
}
// random float in [a,b]
static float KC_RndF( float a, float b ) {
	return a + ( b - a ) * ( KC_Rnd( 1024 ) / 1023.0f );
}
// random sign
static float KC_RndSign( void ) {
	return KC_Rnd( 2 ) ? 1.0f : -1.0f;
}

// push a zeroed shot slot with sane defaults; composers then shape it
static kcShot_t *KC_Push( kcSubject_t subj, int durMs ) {
	kcShot_t *s;
	if ( kc.numShots >= KC_MAX_SHOTS ) {
		return NULL;
	}
	s = &kc.shots[kc.numShots++];
	memset( s, 0, sizeof( *s ) );
	s->subject = subj;
	s->durMs = durMs;
	s->r0 = s->r1 = 180.0f;
	s->e0 = s->e1 = 22.0f;
	s->fov0 = s->fov1 = 70.0f;
	s->ts0 = s->ts1 = 0.20f;
	s->dofRange = 300.0f;
	kc.totalMs += durMs;
	return s;
}

// ----- the camera MOVE LIBRARY -----------------------------------------------
// each composer appends one configured shot; KC_Rnd variation inside each move
// keeps repeated deaths from ever cutting the same film twice

// IMPACT: close, near-frozen beat on the blow. Slight dutch + snap-settle zoom.
static void KC_MoveImpact( kcSubject_t subj, int durMs ) {
	kcShot_t *s = KC_Push( subj, durMs );
	if ( !s ) return;
	s->startAz = KC_RndF( 10.0f, 55.0f ) * KC_RndSign();
	s->arcDeg  = KC_RndF( 4.0f, 10.0f ) * KC_RndSign();	// barely-alive drift
	s->r0 = KC_RndF( 140.0f, 165.0f );  s->r1 = s->r0 - 25.0f;
	s->e0 = s->e1 = KC_RndF( 14.0f, 30.0f );
	s->fov0 = 64.0f;  s->fov1 = 57.0f;					// settle onto the moment
	s->roll0 = s->roll1 = KC_RndF( 2.0f, 6.0f ) * KC_RndSign();
	s->ts0 = 0.04f;  s->ts1 = 0.06f;					// hard freeze
	s->dofRange = 220.0f;
	s->flags = KSF_SHAKE;
}

// ORBIT: the classic bullet-time arc. Direction, height and reach all vary.
static void KC_MoveOrbit( kcSubject_t subj, int durMs, float ts0, float ts1 ) {
	kcShot_t *s = KC_Push( subj, durMs );
	if ( !s ) return;
	s->startAz = KC_RndF( 100.0f, 170.0f );
	s->arcDeg  = KC_RndF( 90.0f, 160.0f ) * KC_RndSign();
	s->r0 = KC_RndF( 195.0f, 235.0f );  s->r1 = s->r0 - KC_RndF( 20.0f, 45.0f );
	s->e0 = KC_RndF( 26.0f, 44.0f );    s->e1 = s->e0 - KC_RndF( 4.0f, 14.0f );
	s->fov0 = KC_RndF( 78.0f, 90.0f );  s->fov1 = s->fov0 - 8.0f;
	s->ts0 = ts0;  s->ts1 = ts1;
	s->dofRange = 320.0f;
}

// DUTCH ORBIT: tighter arc, rolled horizon -- kinetic, off-kilter energy
static void KC_MoveDutchOrbit( kcSubject_t subj, int durMs, float ts0, float ts1 ) {
	kcShot_t *s = KC_Push( subj, durMs );
	if ( !s ) return;
	s->startAz = KC_RndF( 120.0f, 200.0f );
	s->arcDeg  = KC_RndF( 70.0f, 120.0f ) * KC_RndSign();
	s->r0 = KC_RndF( 160.0f, 190.0f );  s->r1 = s->r0 - 20.0f;
	s->e0 = KC_RndF( 10.0f, 20.0f );    s->e1 = s->e0 + 6.0f;
	s->fov0 = 74.0f;  s->fov1 = 68.0f;
	s->roll0 = KC_RndF( 8.0f, 14.0f ) * KC_RndSign();
	s->roll1 = -s->roll0 * 0.5f;						// rolls through level
	s->ts0 = ts0;  s->ts1 = ts1;
	s->dofRange = 260.0f;
}

// OVERHEAD: the god shot -- crane high above, looking straight down the scene
static void KC_MoveOverhead( kcSubject_t subj, int durMs, float ts0, float ts1 ) {
	kcShot_t *s = KC_Push( subj, durMs );
	if ( !s ) return;
	s->startAz = KC_RndF( 0.0f, 360.0f );
	s->arcDeg  = KC_RndF( 25.0f, 55.0f ) * KC_RndSign();	// slow top spin
	s->r0 = KC_RndF( 240.0f, 300.0f );  s->r1 = s->r0 - 40.0f;
	s->e0 = KC_RndF( 62.0f, 76.0f );    s->e1 = s->e0;
	s->fov0 = 76.0f;  s->fov1 = 70.0f;
	s->ts0 = ts0;  s->ts1 = ts1;
	s->dofRange = 380.0f;
}

// LOW HERO: below eye-line looking up -- makes the subject monumental
static void KC_MoveLowHero( kcSubject_t subj, int durMs, float ts0, float ts1 ) {
	kcShot_t *s = KC_Push( subj, durMs );
	if ( !s ) return;
	s->startAz = KC_RndF( 150.0f, 230.0f );
	s->arcDeg  = KC_RndF( 25.0f, 60.0f ) * KC_RndSign();
	s->r0 = KC_RndF( 120.0f, 150.0f );  s->r1 = s->r0 - 15.0f;
	s->e0 = KC_RndF( -8.0f, -2.0f );    s->e1 = s->e0 + 4.0f;	// rises slightly
	s->fov0 = 62.0f;  s->fov1 = 58.0f;
	s->roll0 = s->roll1 = KC_RndF( 2.0f, 5.0f ) * KC_RndSign();
	s->ts0 = ts0;  s->ts1 = ts1;
	s->dofRange = 200.0f;
}

// CRANE PULL: rises and pulls away -- the aftermath reveal
static void KC_MoveCranePull( kcSubject_t subj, int durMs, float ts0, float ts1 ) {
	kcShot_t *s = KC_Push( subj, durMs );
	if ( !s ) return;
	s->startAz = KC_RndF( 0.0f, 360.0f );
	s->arcDeg  = KC_RndF( 15.0f, 40.0f ) * KC_RndSign();
	s->r0 = 150.0f;  s->r1 = KC_RndF( 260.0f, 330.0f );	// away
	s->e0 = 18.0f;   s->e1 = KC_RndF( 40.0f, 55.0f );	// and up
	s->fov0 = 66.0f; s->fov1 = 80.0f;
	s->ts0 = ts0;  s->ts1 = ts1;
	s->dofRange = 420.0f;
}

// PUSH-IN: dolly straight onto the subject, background melting
static void KC_MovePush( kcSubject_t subj, int durMs, float ts0, float ts1 ) {
	kcShot_t *s = KC_Push( subj, durMs );
	if ( !s ) return;
	s->startAz = KC_RndF( 190.0f, 240.0f );
	s->arcDeg  = KC_RndF( 15.0f, 40.0f ) * KC_RndSign();
	s->r0 = KC_RndF( 220.0f, 260.0f );  s->r1 = KC_RndF( 120.0f, 140.0f );
	s->e0 = KC_RndF( 18.0f, 28.0f );    s->e1 = s->e0 - 6.0f;
	s->fov0 = 68.0f;  s->fov1 = 56.0f;
	s->ts0 = ts0;  s->ts1 = ts1;
	s->dofRange = 180.0f;
	s->flags = KSF_FLASH;
}

// CRASH ZOOM: static tripod, violent zoom -- pure kung-fu punctuation
static void KC_MoveCrashZoom( kcSubject_t subj, int durMs, float ts0, float ts1 ) {
	kcShot_t *s = KC_Push( subj, durMs );
	if ( !s ) return;
	s->startAz = KC_RndF( 0.0f, 360.0f );
	s->arcDeg  = 0.0f;									// locked off
	s->r0 = s->r1 = KC_RndF( 260.0f, 330.0f );
	s->e0 = s->e1 = KC_RndF( 12.0f, 26.0f );
	s->fov0 = 92.0f;  s->fov1 = 44.0f;					// the crash
	s->ts0 = ts0;  s->ts1 = ts1;
	s->dofRange = 170.0f;
	s->flags = KSF_FLASH | KSF_SHAKE;
}

// Pick a shot sequence from the kill data. Hard cuts between shots keep it
// kinetic even though the world is in slow-mo. Timescales are chosen so the
// world visibly MOVES during orbits/reveals and hard-freezes on the blow.
static void CG_KillcamBuildShots( void ) {
	qboolean sword = ( kc.mod == MOD_SWORD );

	kc.numShots = 0;
	kc.totalMs = 0;
	kc.intensity = CG_KillcamScore();		// depends on haveKiller/mod, set by now

	// beat-based assembly from the move library, seeded per death so every
	// killcam cuts a different film. Structure: IMPACT beat (the blow, frozen)
	// -> BODY beat (the fallen, world releasing into slow-mo) -> STORY/PAYOFF
	// beat (the killer -- or the kill itself when there's no one to blame).
	// Sword kills linger longer in the freeze; combos hand the film to the killer.
	kc_seed = trap_Milliseconds();

	// --- IMPACT beat: always on the victim, always near-frozen
	KC_MoveImpact( SUBJ_VICTIM, sword ? KC_Rnd( 200 ) + 550 : KC_Rnd( 160 ) + 440 );

	if ( kc.haveKiller && kc.killerCombo >= 2 ) {
		// --- COMBO: the killer is the story. Short body beat, then a long hero
		// treatment on the killer while they keep fighting in slow-mo.
		KC_MoveOrbit( SUBJ_VICTIM, 700, 0.14f, 0.30f );
		switch ( KC_Rnd( 3 ) ) {
		case 0:  KC_MoveOrbit( SUBJ_KILLER, 1900, 0.30f, 0.16f ); break;
		case 1:  KC_MoveLowHero( SUBJ_KILLER, 1700, 0.28f, 0.15f ); break;
		default: KC_MoveDutchOrbit( SUBJ_KILLER, 1800, 0.30f, 0.16f ); break;
		}
	} else if ( kc.haveKiller ) {
		// --- BODY beat: one long look at the fallen, varied per death
		switch ( KC_Rnd( 4 ) ) {
		case 0:  KC_MoveOrbit( SUBJ_VICTIM, sword ? 1300 : 1100, 0.14f, 0.42f ); break;
		case 1:  KC_MoveDutchOrbit( SUBJ_VICTIM, 1150, 0.14f, 0.40f ); break;
		case 2:  KC_MoveOverhead( SUBJ_VICTIM, 1100, 0.16f, 0.38f ); break;
		default: KC_MoveCranePull( SUBJ_VICTIM, 1200, 0.14f, 0.42f ); break;
		}
		// --- STORY beat: cut to the killer, varied punctuation
		switch ( KC_Rnd( 3 ) ) {
		case 0:  KC_MovePush( SUBJ_KILLER, 1000, 0.25f, 0.10f ); break;
		case 1:  KC_MoveCrashZoom( SUBJ_KILLER, 850, 0.22f, 0.10f ); break;
		default: KC_MoveLowHero( SUBJ_KILLER, 1000, 0.24f, 0.12f ); break;
		}
	} else {
		// --- no killer (void/fall/suicide): the kill itself is the film
		switch ( KC_Rnd( 3 ) ) {
		case 0:  KC_MoveOrbit( SUBJ_VICTIM, 1400, 0.14f, 0.45f ); break;
		case 1:  KC_MoveOverhead( SUBJ_VICTIM, 1250, 0.16f, 0.40f ); break;
		default: KC_MoveCranePull( SUBJ_VICTIM, 1350, 0.14f, 0.45f ); break;
		}
		switch ( KC_Rnd( 2 ) ) {
		case 0:  KC_MovePush( SUBJ_VICTIM, 900, 0.30f, 0.12f ); break;
		default: KC_MoveCrashZoom( SUBJ_VICTIM, 800, 0.28f, 0.12f ); break;
		}
	}
}

// resolve the killer from an attacker client num, aim baseYaw at them, and
// (if the killcam is already running) rebuild the shot list to the killer
// sequence. Called both at death and when the obituary lands a beat later.
static void CG_KillcamResolveKiller( int attacker, int mod ) {
	vec3_t delta;

	if ( attacker < 0 || attacker >= MAX_CLIENTS
		|| ( cg.snap && attacker == cg.snap->ps.clientNum ) ) {
		return;
	}
	kc.attacker = attacker;
	kc.mod = mod;
	VectorCopy( cg_entities[attacker].lerpOrigin, kc.killerSpot );
	kc.haveKiller = qtrue;
	kc.killerCombo = CG_KillcamComboCount( attacker );

	VectorSubtract( kc.killerSpot, kc.killSpot, delta );
	kc.baseYaw = atan2( delta[1], delta[0] ) * ( 180.0f / M_PI );

	if ( kc.active ) {
		CG_KillcamBuildShots();		// switch to the killer template (startReal kept)
	}
}

// ----- trigger / teardown ---------------------------------------------------

void CG_KillcamPlayerDied( void ) {
	playerState_t	*ps;

	if ( !cg_killcam.integer || kc.active || cg.demoPlayback ) {
		return;
	}
	if ( !cg.snap || cg.snap->ps.persistant[PERS_TEAM] == TEAM_SPECTATOR ) {
		return;
	}
	if ( cg.snap->ps.clientNum != cg.clientNum ) {	// only OUR death
		return;
	}

	ps = &cg.predictedPlayerState;
	VectorCopy( ps->origin, kc.killSpot );			// predicted origin (snapshot lags)
	kc.speed = (int)sqrt( ps->velocity[0] * ps->velocity[0] +
		ps->velocity[1] * ps->velocity[1] );

	// defaults -- the killer is usually folded in a beat LATER by the obituary
	// (it arrives ~1 round-trip after the client-predicted death)
	kc.attacker = -1;
	kc.haveKiller = qfalse;
	kc.mod = MOD_UNKNOWN;
	kc.baseYaw = cg.refdefViewAngles[YAW];

	// snapshot the run stats now (respawn clears the live cg fields)
	kc.statPeak    = cg.peakSpeed;
	kc.statStyle   = cg.lifeStylePeak;
	kc.statStylePB = cg.styleBest;
	kc.statScore   = cg.snap->ps.persistant[PERS_SCORE];
	kc.statRecord  = cg.mapSpeedRecord.integer;
	kc.statPar     = cg.mapPar.integer;

	kc.startReal = trap_Milliseconds();
	kc.active = qtrue;

	CG_KillcamApplyLook();

	// if the obituary happened to already arrive, resolve the killer + build the
	// killer shots now; otherwise build the no-killer template (the obituary may
	// upgrade it via CG_KillcamNoteObituary a beat later)
	if ( kc.obitTime && ( cg.time - kc.obitTime ) <= 250 ) {
		CG_KillcamResolveKiller( kc.obitAttacker, kc.obitMod );
	}
	if ( !kc.haveKiller ) {
		CG_KillcamBuildShots();
	}
}

void CG_KillcamStop( void ) {
	if ( kc.lookApplied ) {
		CG_KillcamRestoreCvars();	// compiled defaults, incl. timescale + g_timeBind
		kc.lookApplied = qfalse;
	}
	kc.active = qfalse;
}

qboolean CG_KillcamActive( void ) {
	return (qboolean)( kc.active && cg_killcam.integer );
}

static int CG_KillcamElapsed( void ) {
	return trap_Milliseconds() - kc.startReal;
}

// past the end of the last shot -> we're holding the final frame for the stats
qboolean CG_KillcamHolding( void ) {
	return (qboolean)( kc.active && CG_KillcamElapsed() >= kc.totalMs );
}

// find the current shot and the eased 0..1 progress within it; kc_curShotIdx
// is exposed so the camera can detect a CUT and snap its smoothing cleanly
static int kc_curShotIdx;

static kcShot_t *CG_KillcamCurShot( float *progress ) {
	int	elapsed = CG_KillcamElapsed();
	int	i, acc = 0;

	for ( i = 0; i < kc.numShots; i++ ) {
		if ( elapsed < acc + kc.shots[i].durMs || i == kc.numShots - 1 ) {
			int local = elapsed - acc;
			if ( local < 0 ) local = 0;
			if ( progress ) {
				*progress = kc.shots[i].durMs > 0 ?
					(float)local / (float)kc.shots[i].durMs : 1.0f;
			}
			kc_curShotIdx = i;
			return &kc.shots[i];
		}
		acc += kc.shots[i].durMs;
	}
	if ( progress ) *progress = 1.0f;
	kc_curShotIdx = kc.numShots > 0 ? kc.numShots - 1 : 0;
	return &kc.shots[kc_curShotIdx];
}

// ----- subjects & camera ----------------------------------------------------

// the killed body where it lies now (tracks the corpse; falls back to killSpot)
static void CG_KillcamVictimPos( vec3_t out ) {
	centity_t *body = &cg_entities[ cg.clientNum ];
	VectorCopy( kc.killSpot, out );
	if ( body->currentValid ) {
		vec3_t d;
		VectorSubtract( body->lerpOrigin, kc.killSpot, d );
		if ( VectorLength( d ) < 400.0f ) {
			VectorCopy( body->lerpOrigin, out );
		}
	}
	out[2] += 20.0f;	// body CENTER, not chest -- aiming high cropped the legs
}

static void CG_KillcamKillerPos( vec3_t out ) {
	if ( kc.haveKiller ) {
		VectorCopy( cg_entities[kc.attacker].lerpOrigin, out );
	} else {
		VectorCopy( kc.killSpot, out );
	}
	out[2] += 24.0f;	// body center (standing model spans about -24..+56)
}

static void CG_KillcamSubject( kcSubject_t subj, vec3_t out ) {
	vec3_t v, k;
	switch ( subj ) {
	case SUBJ_KILLER:
		CG_KillcamKillerPos( out );
		break;
	case SUBJ_MID:
		CG_KillcamVictimPos( v );
		CG_KillcamKillerPos( k );
		VectorAdd( v, k, out );
		VectorScale( out, 0.5f, out );
		break;
	case SUBJ_VICTIM:
	default:
		CG_KillcamVictimPos( out );
		break;
	}
}

/*
=================
CG_KillcamCalcView

Owns cg.refdef.vieworg/viewangles/fov while the killcam runs. Picks the current
shot, orbits/dollies the subject on the real clock. Drops the warm kill light.
=================
*/
void CG_KillcamCalcView( void ) {
	kcShot_t	*shot;
	float		p, az, radius, elev, fov, xproj, now;
	float		caz, sez, cel, sel;
	vec3_t		subject, campos, dir;
	// damped rig: the desired camera/aim are LOW-PASSED on the real clock so a
	// moving subject (a killer sprinting mid-combo) is followed like a camera
	// operator would -- eased, heavy, never welded. Snapped at shot cuts.
	static vec3_t	smCam, smAim;
	static int		smShot = -1, smReal = 0, smStart = 0;

	float	roll, rawP, shotLocalMs;

	shot = CG_KillcamCurShot( &rawP );
	p = CG_KC_Ease( rawP );
	shotLocalMs = rawP * (float)shot->durMs;

	// the shot frames ONE subject cleanly
	CG_KillcamSubject( shot->subject, subject );

	az     = kc.baseYaw + shot->startAz + shot->arcDeg * p;
	radius = CG_KC_Lerp( shot->r0, shot->r1, p );
	elev   = CG_KC_Lerp( shot->e0, shot->e1, p );		// crane
	fov    = CG_KC_Lerp( shot->fov0, shot->fov1, p );	// zoom
	roll   = CG_KC_Lerp( shot->roll0, shot->roll1, p );	// dutch

	// while holding the final frame, keep a slow drift so it stays alive
	if ( CG_KillcamHolding() ) {
		az += ( CG_KillcamElapsed() - kc.totalMs ) * 0.004f;
		roll *= 0.4f;									// settle mostly level
	}

	// whisper of handheld float (kept tiny -- wobble reads as spazz)
	now = (float)trap_Milliseconds() * 0.001f;
	az   += sin( now * 0.5f ) * 0.5f;
	elev += sin( now * 0.8f ) * 0.3f;

	// decaying impact shake at the head of a flagged shot
	if ( ( shot->flags & KSF_SHAKE ) && shotLocalMs < 300.0f ) {
		float amp = ( 1.0f - shotLocalMs / 300.0f ) * 1.6f;
		az   += sin( now * 31.0f ) * amp;
		elev += cos( now * 37.0f ) * amp * 0.7f;
	}

	// FULL-BODY FRAMING GUARD: whatever the move wants, never frame so tight
	// that the whole character (plus margin) can't fit vertically -- and give a
	// fast-moving subject extra breathing room + an aim lead that cancels the
	// smoother's lag, so sprinters stay centered instead of dragging the frame
	{
		centity_t	*scent;
		float		spd, k, tanY, need;

		scent = ( shot->subject != SUBJ_VICTIM && kc.haveKiller ) ?
			&cg_entities[kc.attacker] : &cg_entities[cg.clientNum];
		spd = VectorLength( scent->currentState.pos.trDelta );
		k = spd / 600.0f;
		if ( k > 1.0f ) k = 1.0f;

		fov += 7.0f * k;						// fast subject -> wider shot
		if ( fov > 100.0f ) fov = 100.0f;

		// min distance so a ~90u body + margin fits the vertical FOV
		tanY = tan( DEG2RAD( fov * 0.5f ) )
			* ( (float)cg.refdef.height / (float)cg.refdef.width );
		if ( tanY > 0.05f ) {
			need = ( 58.0f + 26.0f * k ) / tanY;
			if ( radius < need ) {
				radius = need;
			}
		}

		// lead the aim by about the aim-smoother's time constant
		VectorMA( subject, 0.11f * cg_timescale.value,
			scent->currentState.pos.trDelta, subject );
	}

	caz = cos( DEG2RAD( az ) );  sez = sin( DEG2RAD( az ) );
	cel = cos( DEG2RAD( elev ) ); sel = sin( DEG2RAD( elev ) );

	campos[0] = subject[0] + radius * caz * cel;
	campos[1] = subject[1] + radius * sez * cel;
	campos[2] = subject[2] + radius * sel;

	// damped follow: hard-snap on a CUT (new shot / new killcam), otherwise ease
	// toward the desired rig on the real clock. The aim tracks a touch faster
	// than the body of the camera, like an operator leading with the head.
	{
		int		nowMs = trap_Milliseconds();
		float	dt = (float)( nowMs - smReal );

		if ( smShot != kc_curShotIdx || smStart != kc.startReal || dt > 500.0f ) {
			VectorCopy( campos, smCam );
			VectorCopy( subject, smAim );
			smShot = kc_curShotIdx;
			smStart = kc.startReal;
		} else {
			float	kCam = 1.0f - exp( -dt / 170.0f );
			float	kAim = 1.0f - exp( -dt / 110.0f );
			vec3_t	d;

			VectorSubtract( campos, smCam, d );
			VectorMA( smCam, kCam, d, smCam );
			VectorSubtract( subject, smAim, d );
			VectorMA( smAim, kAim, d, smAim );
		}
		smReal = nowMs;
	}

	VectorCopy( smCam, campos );

	// keep out of geometry (trace to the SMOOTHED position)
	{
		static const vec3_t	mins = { -8, -8, -8 };
		static const vec3_t	maxs = {  8,  8,  8 };
		trace_t				tr;
		CG_Trace( &tr, smAim, mins, maxs, campos, cg.snap->ps.clientNum, MASK_SOLID );
		if ( tr.fraction < 1.0f ) {
			VectorCopy( tr.endpos, campos );
		}
	}

	VectorCopy( campos, cg.refdef.vieworg );
	VectorSubtract( smAim, campos, dir );
	vectoangles( dir, cg.refdefViewAngles );
	cg.refdefViewAngles[ROLL] = roll;					// dutch angle

	xproj = cg.refdef.width / tan( DEG2RAD( fov * 0.5f ) );
	cg.refdef.fov_x = fov;
	cg.refdef.fov_y = atan2( cg.refdef.height, xproj ) * ( 360.0f / M_PI );

	// warm kill-pool light -- the one warm thing against the cool grade
	{
		vec3_t	vpos;
		float	pulse = 0.85f + 0.15f * sin( now * 4.0f );
		CG_KillcamVictimPos( vpos );
		trap_R_AddLightToScene( vpos, ( 160.0f + 120.0f * kc.intensity ) * pulse,
			1.0f, 0.62f, 0.28f );
	}
}

/*
=================
CG_KillcamUpdate

Per-frame: end on genuine respawn (PREDICTED health -- the snapshot lags a RTT),
drive the per-shot world timescale and the DoF rack.
=================
*/
void CG_KillcamUpdate( void ) {
	kcShot_t	*shot;
	float		p, ts, dist;
	vec3_t		subject, delta;

	if ( kc.active && !cg_killcam.integer ) {
		CG_KillcamStop();
		return;
	}
	if ( !kc.active ) {
		return;
	}

	// end only on a genuine local respawn. Use PREDICTED health: the local death
	// is client-predicted instantly (-999) while the snapshot still reads alive
	// for ~1 round-trip -- checking the snapshot stopped the killcam on frame 0.
	if ( cg.predictedPlayerState.stats[STAT_HEALTH] > 0 ) {
		CG_KillcamStop();
		return;
	}

	shot = CG_KillcamCurShot( &p );

	// world clock. Within a shot the timescale RAMPS ts0->ts1 (hard freeze on
	// the blow, then release into faster slow-mo where the world clearly moves).
	// On the hold it RESUMES toward near-normal so the kill screen isn't a frozen
	// super-slow crawl.
	if ( CG_KillcamHolding() ) {
		float h = ( (float)( CG_KillcamElapsed() - kc.totalMs ) ) / 900.0f;
		ts = CG_KC_Lerp( shot->ts1, 0.85f, CG_KC_Ease( h ) );
	} else {
		ts = CG_KC_Lerp( shot->ts0, shot->ts1, CG_KC_Ease( p ) );
	}
	trap_Cvar_Set( "timescale", va( "%.3f", ts ) );

	// DoF rack onto the current subject; each move carries its own focal
	// character (a push melts the background, an overhead keeps depth), melting
	// further as the shot progresses
	CG_KillcamSubject( shot->subject, subject );
	VectorSubtract( subject, cg.refdef.vieworg, delta );
	dist = VectorLength( delta );
	trap_Cvar_Set( "r_dofFocalDist", va( "%.0f", dist ) );
	trap_Cvar_Set( "r_dofFocalRange", va( "%.0f",
		shot->dofRange * ( 1.0f - 0.35f * CG_KC_Ease( p ) ) ) );
	trap_Cvar_Set( "r_bloom", va( "%.2f", 0.22f + 0.28f * kc.intensity ) );
}

// ----- 2D: projection + primitives ------------------------------------------

static qboolean CG_KC_Project( const vec3_t world, float *sx, float *sy ) {
	vec3_t	d;
	float	xf, xr, xu, tanx, tany;

	VectorSubtract( world, cg.refdef.vieworg, d );
	xf = DotProduct( d, cg.refdef.viewaxis[0] );
	// reject points at/near the lens plane: xf close to 0 explodes the projected
	// coords, and a dash line drawn to a coordinate like x=-80000 spams tens of
	// thousands of render commands in one frame -> "Failed to allocate render
	// command" -> the engine spins forever (the freeze). 24u is inside any model.
	if ( xf < 24.0f ) return qfalse;
	xr = DotProduct( d, cg.refdef.viewaxis[1] );
	xu = DotProduct( d, cg.refdef.viewaxis[2] );
	tanx = tan( DEG2RAD( cg.refdef.fov_x * 0.5f ) );
	tany = tan( DEG2RAD( cg.refdef.fov_y * 0.5f ) );
	if ( tanx <= 0.0f || tany <= 0.0f ) return qfalse;
	*sx = 320.0f - 320.0f * ( xr / ( xf * tanx ) );
	*sy = 240.0f - 240.0f * ( xu / ( xf * tany ) );
	// sanity-bound: anything this far off-frame is useless for drawing and only
	// risks huge primitive counts downstream
	if ( *sx < -640.0f || *sx > 1280.0f || *sy < -480.0f || *sy > 960.0f ) {
		return qfalse;
	}
	return qtrue;
}

static void CG_KC_Bracket( float cx, float cy, float hx, float hy,
		float arm, float thick, const float *color ) {
	float l = cx - hx, r = cx + hx, t = cy - hy, b = cy + hy;
	CG_FillRect( l, t, arm, thick, color );
	CG_FillRect( l, t, thick, arm, color );
	CG_FillRect( r - arm, t, arm, thick, color );
	CG_FillRect( r - thick, t, thick, arm, color );
	CG_FillRect( l, b - thick, arm, thick, color );
	CG_FillRect( l, b - arm, thick, arm, color );
	CG_FillRect( r - arm, b - thick, arm, thick, color );
	CG_FillRect( r - thick, b - arm, thick, arm, color );
}

static void CG_KC_DashLine( float x0, float y0, float x1, float y1,
		const float *color ) {
	float	dx = x1 - x0, dy = y1 - y0;
	float	len = sqrt( dx * dx + dy * dy );
	int		i, n;
	if ( len < 1.0f ) return;
	n = (int)( len / 9.0f );
	if ( n > 90 ) n = 90;		// hard cap: never let one line eat the render command buffer
	for ( i = 0; i <= n; i++ ) {
		float a = (float)i / (float)( n > 0 ? n : 1 );
		CG_FillRect( x0 + dx * a - 1.0f, y0 + dy * a - 1.0f, 2.0f, 2.0f, color );
	}
}

// project a character's body (chest = the +z-adjusted anchor the camera uses)
// and fit a bracket to its screen-space extent, so the marker wraps the figure
// and scales with distance instead of floating at a fixed size above it
static qboolean CG_KC_ProjectBody( const vec3_t chest, float *cx, float *cy,
		float *hx, float *hy ) {
	vec3_t	top, bot;
	float	x1, y1, x2, y2;

	VectorCopy( chest, top );  top[2] += 40.0f;		// head (+ margin)
	VectorCopy( chest, bot );  bot[2] -= 48.0f;		// feet (+ margin)
	if ( !CG_KC_Project( top, &x1, &y1 ) || !CG_KC_Project( bot, &x2, &y2 ) ) {
		return qfalse;
	}
	*cx = ( x1 + x2 ) * 0.5f;
	*cy = ( y1 + y2 ) * 0.5f;
	*hy = fabs( y2 - y1 ) * 0.60f + 6.0f;
	if ( *hy < 16.0f )  *hy = 16.0f;
	if ( *hy > 130.0f ) *hy = 130.0f;
	*hx = *hy * 0.55f;
	return qtrue;
}

// ----- 2D: diegetic stats ---------------------------------------------------

// reveal gate: 1 once the sequence real-time passes fraction `at` of total
static float CG_KC_Reveal( float at, float overMs ) {
	float t = ( (float)CG_KillcamElapsed() - at * (float)kc.totalMs ) / overMs;
	return CG_KC_Ease( t );
}

/*
=================
CG_DrawKillcam

The whole 2D layer: letterbox, tracked brackets on the fighters, and the run
stats revealed progressively and anchored to the scene (no mission-report panel).
Which stats headline shifts with the kill (blade kill vs a fast death).
=================
*/
void CG_DrawKillcam( void ) {
	static vec4_t	cyan    = { 0.44f, 0.86f, 1.0f, 1.0f };
	static vec4_t	cyanDim = { 0.30f, 0.62f, 0.78f, 1.0f };
	static vec4_t	amber   = { 1.0f, 0.66f, 0.24f, 1.0f };
	static vec4_t	green   = { 0.40f, 0.90f, 0.45f, 1.0f };
	static vec4_t	black   = { 0.0f, 0.0f, 0.0f, 1.0f };
	float		alpha, bar;
	float		vx, vy, kx, ky;
	qboolean	vvis, kvis;
	vec3_t		vpos, kpos;
	vec4_t		col, tc;
	char		buf[64];
	int			elapsed;

	elapsed = CG_KillcamElapsed();
	alpha = 1.0f;
	if ( elapsed < KC_FADE_IN_MS ) {
		alpha = (float)elapsed / (float)KC_FADE_IN_MS;
	}

	// full-Matrix green wash (style 1)
	if ( cg_killcamStyle.integer == 1 ) {
		Vector4Set( col, 0.0f, 0.32f, 0.06f, 0.12f * alpha );
		CG_FillRect( 0, 0, 640, 480, col );
	}

	// flash-in on a flagged cut: one bright pop that dies in ~120ms
	{
		float	rawP;
		kcShot_t *shot = CG_KillcamCurShot( &rawP );
		float	localMs = rawP * (float)shot->durMs;
		if ( ( shot->flags & KSF_FLASH ) && localMs < 120.0f && !CG_KillcamHolding() ) {
			Vector4Set( col, 0.85f, 0.95f, 1.0f, ( 1.0f - localMs / 120.0f ) * 0.5f * alpha );
			CG_FillRect( 0, 0, 640, 480, col );
		}
	}

	// cinematic letterbox, snaps in fast on the impact
	bar = 46.0f * CG_KC_Ease( elapsed / 220.0f ) * alpha;
	if ( bar > 1.0f ) {
		black[3] = alpha;
		CG_FillRect( 0, 0, 640, bar, black );
		CG_FillRect( 0, 480 - bar, 640, bar, black );
	}

	// project the fighters. Brackets FIT the character's screen extent (scale
	// with distance); an off-screen killer clamps to the frame edge so the
	// dotted link still points at them.
	{
		float	vhx = 34, vhy = 44, khx = 22, khy = 26;

		CG_KillcamVictimPos( vpos );
		vvis = CG_KC_ProjectBody( vpos, &vx, &vy, &vhx, &vhy );
		if ( !vvis && CG_KC_Project( vpos, &vx, &vy ) ) {
			vvis = qtrue;	// partial view: fall back to a fixed-size bracket
		}

		kvis = qfalse;
		if ( kc.haveKiller ) {
			CG_KillcamKillerPos( kpos );
			kvis = CG_KC_ProjectBody( kpos, &kx, &ky, &khx, &khy );
			if ( !kvis && CG_KC_Project( kpos, &kx, &ky ) ) {
				kvis = qtrue;
			}
			if ( kvis ) {	// keep the marker on-frame
				if ( kx < 30.0f ) kx = 30.0f; else if ( kx > 610.0f ) kx = 610.0f;
				if ( ky < 70.0f ) ky = 70.0f; else if ( ky > 410.0f ) ky = 410.0f;
			}
		}

		// dotted diagnostic link between the fighters, DIST riding the midpoint
		if ( vvis && kvis ) {
			Vector4Set( col, cyan[0], cyan[1], cyan[2], 0.9f * alpha );
			CG_KC_DashLine( vx, vy, kx, ky, col );
			{
				vec3_t d; float dist;
				VectorSubtract( kc.killerSpot, kc.killSpot, d );
				dist = VectorLength( d );
				Vector4Set( tc, cyanDim[0], cyanDim[1], cyanDim[2], 0.85f * alpha );
				Com_sprintf( buf, sizeof( buf ), "%iU", (int)dist );
				CG_DrawMatrixString( ( vx + kx ) * 0.5f - 12, ( vy + ky ) * 0.5f - 6, buf, 1.5f, tc );
			}
		}
		if ( vvis ) {
			Vector4Set( col, cyan[0], cyan[1], cyan[2], alpha );
			CG_KC_Bracket( vx, vy, vhx, vhy, vhy * 0.28f, 2, col );
			// dotted targeting cross flanking the frame
			Vector4Set( col, cyanDim[0], cyanDim[1], cyanDim[2], 0.5f * alpha );
			CG_KC_DashLine( vx - vhx - 52, vy, vx - vhx - 8, vy, col );
			CG_KC_DashLine( vx + vhx + 8, vy, vx + vhx + 52, vy, col );
			// method tag BESIDE the frame, top-right corner of the bracket
			Vector4Set( tc, cyan[0], cyan[1], cyan[2], alpha );
			CG_DrawMatrixString( vx + vhx + 6, vy - vhy + 2,
				kc.mod == MOD_SWORD ? "BLADE" : "K.I.A", 1.4f, tc );
		}
		if ( kvis ) {
			Vector4Set( col, cyanDim[0], cyanDim[1], cyanDim[2], 0.85f * alpha );
			CG_KC_Bracket( kx, ky, khx, khy, khy * 0.28f, 2, col );
			if ( kc.attacker >= 0 && kc.attacker < MAX_CLIENTS ) {
				Vector4Set( tc, amber[0], amber[1], amber[2], alpha );
				if ( kc.killerCombo >= 2 ) {
					Com_sprintf( buf, sizeof( buf ), "%s X%i",
						cgs.clientinfo[kc.attacker].name, kc.killerCombo );
					CG_DrawMatrixString( kx + khx + 6, ky - khy + 2, buf, 1.4f, tc );
				} else {
					CG_DrawMatrixString( kx + khx + 6, ky - khy + 2,
						cgs.clientinfo[kc.attacker].name, 1.4f, tc );
				}
			}
		}
	}

	// --- run stats: one composed readout in the letterbox bars, not scattered ---
	{
		float		rowY = 448.0f;		// baseline inside the bottom letterbox bar
		float		x, rv, cell, w;
		vec4_t		rankCol;
		const char	*rank;

		// TOP BAR headline: who eliminated you (the one warm accent)
		Vector4Set( tc, amber[0], amber[1], amber[2], alpha );
		if ( kc.haveKiller && kc.attacker >= 0 && kc.attacker < MAX_CLIENTS ) {
			Com_sprintf( buf, sizeof( buf ), "ELIMINATED BY %s", cgs.clientinfo[kc.attacker].name );
		} else {
			Q_strncpyz( buf, "ELIMINATED", sizeof( buf ) );
		}
		CG_DrawMatrixString( 28, 18, buf, 2.0f, tc );

		// BOTTOM BAR readout, left-aligned: SPEED (bigger on a fast death), then
		// STYLE and SCORE slide up into the row after the orbit beat
		cell = ( kc.speed >= 700 ) ? 2.4f : 2.0f;
		Vector4Set( tc, cyan[0], cyan[1], cyan[2], 0.95f * alpha );
		Com_sprintf( buf, sizeof( buf ), "SPEED %i", kc.speed );
		CG_DrawMatrixString( 28, rowY - ( cell - 2.0f ) * 4.0f, buf, cell, tc );
		x = 28.0f + CG_MatrixStringWidth( buf, cell ) + 26.0f;

		rv = CG_KC_Reveal( 0.34f, 320.0f ) * alpha;
		if ( rv > 0.01f ) {
			float yy = CG_KC_Lerp( rowY + 16.0f, rowY, rv );	// slide up into place
			Vector4Set( tc, cyanDim[0], cyanDim[1], cyanDim[2], rv );
			Com_sprintf( buf, sizeof( buf ), "STYLE %i", kc.statStyle );
			CG_DrawMatrixString( x, yy, buf, 2.0f, tc );
			x += CG_MatrixStringWidth( buf, 2.0f ) + 22.0f;
			Com_sprintf( buf, sizeof( buf ), "SCORE %i", kc.statScore );
			CG_DrawMatrixString( x, yy, buf, 2.0f, tc );
		}

		// RANK: the payoff. Stamps in bottom-RIGHT, punching down in size, paired
		// with the run's top speed. Colour by rank.
		{
			int peak = kc.statPeak, par = kc.statPar;
			int sThr = par > 0 ? par              : 900;
			int aThr = par > 0 ? (int)(par*0.85f) : 700;
			int bThr = par > 0 ? (int)(par*0.70f) : 500;

			if      ( peak >= sThr ) { rank = "S RANK"; Vector4Copy( green,   rankCol ); }
			else if ( peak >= aThr ) { rank = "A RANK"; Vector4Copy( amber,   rankCol ); }
			else if ( peak >= bThr ) { rank = "B RANK"; Vector4Copy( amber,   rankCol ); }
			else                     { rank = "C RANK"; Vector4Copy( cyanDim, rankCol ); }

			rv = CG_KC_Reveal( 0.60f, 300.0f ) * alpha;
			if ( rv > 0.01f ) {
				cell = CG_KC_Lerp( 3.6f, 2.6f, rv );			// punches down as it lands
				Com_sprintf( buf, sizeof( buf ), "%s  %iUPS", rank, peak );
				w = CG_MatrixStringWidth( buf, cell );
				rankCol[3] = rv;
				CG_DrawMatrixString( 612.0f - w, rowY - ( cell - 2.0f ) * 4.0f, buf, cell, rankCol );
			}
		}

		// respawn prompt, centered just above the bottom bar, once holding
		if ( CG_KillcamHolding() ) {
			float pr = 0.5f + 0.5f * sin( trap_Milliseconds() * 0.006f );
			Vector4Set( tc, cyanDim[0], cyanDim[1], cyanDim[2], ( 0.35f + 0.4f * pr ) * alpha );
			CG_DrawMatrixString( 320 - CG_MatrixStringWidth( "FIRE TO RUN AGAIN", 1.4f ) / 2, 414,
				"FIRE TO RUN AGAIN", 1.4f, tc );
		}
	}
}
