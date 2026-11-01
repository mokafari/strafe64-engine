// Copyright (C) STRAFE 64
//
// cg_ragdoll.c -- client-side Verlet ragdoll for dead bodies.
//
// Q3 player models are MD3: three rigid segments (legs/torso/head) stitched by
// tags, with death handled by a canned BOTH_DEATH vertex animation. That reads
// as a statue tipping over, not a body. This replaces it with a small
// position-based-dynamics ragdoll: four point masses down the spine (feet,
// pelvis, chest, head) linked by distance constraints, integrated with Verlet
// and collided against the world with the same CG_Trace the gib fragments use.
// Each frame we read the particle chain back out and orient the three MD3
// segments directly -- no tags, no animation -- so the corpse flops, tumbles
// and settles on the geometry it died on.
//
// Purely cosmetic and entirely client-side: no netcode, no prediction, no new
// assets. It keys off EF_DEAD, which the server already sets on both the dying
// player and the body-queue corpses, so every death ragdolls. Set cg_ragdoll 0
// to fall straight back to the stock death animation.

#include "cg_local.h"

// spine particles, bottom to top
typedef enum {
	RP_FEET,
	RP_PELVIS,
	RP_CHEST,
	RP_HEAD,
	RP_COUNT
} ragPart_t;

// the mannequin's particles: the spine's four first, so the wounds and the
// shared step work on either body. RS_LIMB is the severed end of a cut limb; the
// joint it came off stays behind to hold the body's frame
typedef enum {
	RS_NECK = RP_COUNT,
	RS_ARM_L, RS_ELBOW_L, RS_HAND_L,
	RS_ARM_R, RS_ELBOW_R, RS_HAND_R,
	RS_THIGH_L, RS_KNEE_L,
	RS_THIGH_R, RS_KNEE_R, RS_FOOT_R,
	RS_LIMB,
	RS_COUNT
} ragSkelPart_t;
#define RS_FOOT_L	RP_FEET

// bones the particles turn. The pelvis and chest take a whole frame from a
// triangle of particles, the rest follow one particle pair
typedef enum {
	RB_PELVIS, RB_CHEST, RB_NECK,
	RB_UPPER_L, RB_LOWER_L, RB_UPPER_R, RB_LOWER_R,
	RB_THIGH_L, RB_CALF_L, RB_THIGH_R, RB_CALF_R,
	RB_COUNT
} ragBone_t;

typedef struct {
	int		a, b;			// particle indices
	float	rest;			// rest length
	float	stiff;			// 0..1 fraction of the correction applied per pass
} ragConstraint_t;

typedef struct {
	qboolean	active;
	int			startTime;
	int			lastTime;		// last sim time, to derive a stable dt
	vec3_t		pos[RS_COUNT];
	vec3_t		prev[RS_COUNT];
	float		rest[RP_COUNT];	// seed height above the feet, for re-seed sanity checks
	float		yaw;			// facing captured at death, for the forward axis
	qboolean	settled;		// all particles at rest -> stop simulating
	int			cut;			// blade cut tag (entityState.time2): 0 none,
								// 1 limb, 2 decap, 3 bisect
	int			airQuietTime;	// when the body first went quiet without ground
								// support (0 = moving/grounded); drives the
								// stuck-in-air -> ground-snap recovery
	int			limb;			// severed limb on a RAG_CUT_LIMB: 1 arm l, 2 arm r, 3 leg l, 4 leg r
	float		lastDt;			// the step before this one: Verlet velocity is per step
	float		spin;			// accumulated tumble phase (rad) for severed
								// pieces; frozen once the body settles
	// the mannequin: its death pose, and how far each bone has turned from it
	qboolean	skel;
	int			frame, oldframe;
	float		backlerp;
	vec3_t		seedOrigin;
	vec3_t		seedAxis[3];
	vec3_t		seed[RS_COUNT];
	byte		group[RS_COUNT];	// a cut splits the particles into two groups
	float		linkRest[32];
	float		limbRest;			// RS_LIMB to the next joint down
	quat_t		boneRot[RB_COUNT];
	quat_t		frameSeed[2];		// pelvis and chest frames at death
	vec3_t		boneAxis[RB_COUNT][3];
	vec3_t		spineAxis[3][3];
} cgRagdoll_t;

// indexed by entity number so it covers both live corpses and body-queue bodies
static cgRagdoll_t	cg_ragdolls[MAX_GENTITIES];

// spine constraints: neighbours hold length, skip-links keep the body from
// folding flat into a point (cheap bending stiffness without real joints).
// REST LENGTHS = the seed-distance between particles (ragSeedZ deltas — all
// particles seed at the same x,y so the distance is purely the z gap). v1 left
// these at 0, so the solver pulled every particle toward distance ZERO and the
// corpse collapsed into a flat puddle; with the real lengths the chain holds a
// ~52u body that topples and lies OUT like a slumped body instead. Keep in sync
// with ragSeedZ {2,24,40,52} below.
static const ragConstraint_t ragConstraints[] = {
	{ RP_FEET,   RP_PELVIS, 22.0f, 1.0f },	// 24-2
	{ RP_PELVIS, RP_CHEST,  16.0f, 1.0f },	// 40-24
	{ RP_CHEST,  RP_HEAD,   12.0f, 1.0f },	// 52-40
	{ RP_FEET,   RP_CHEST,  38.0f, 0.4f },	// 40-2  stiffener
	{ RP_PELVIS, RP_HEAD,   28.0f, 0.4f },	// 52-24 stiffener
};
#define RAG_NUM_CONSTRAINTS ( (int)( sizeof( ragConstraints ) / sizeof( ragConstraints[0] ) ) )

// seed heights above the entity origin (which sits at the feet)
static const float ragSeedZ[RP_COUNT] = { 2.0f, 24.0f, 40.0f, 52.0f };

#define RAG_HALFBOX		3.0f		// collision box half-extent per particle
#define RAG_SKEL_MAXMS	7000		// a jointed body that still twitches stops here
#define RAG_GRAVITY		800.0f		// matches g_gravity feel
#define RAG_SETTLE_VEL2	1.0f		// squared speed below which a particle is "stopped"
#define RAG_MAXSTEP		20.0f		// max units a particle may move per step (anti-explode)
#define RAG_AIRHANG_MS	1500		// quiet + unsupported this long -> a wedged/hung
									// piece is snapped down onto the floor below it
#define RAG_SPIN_RATE	5.0f		// rad/s tumble applied to severed pieces

// blade-cut tags (mirrors entityState.time2 = cutType + 1)
#define RAG_CUT_NONE	0
#define RAG_CUT_LIMB	1			// single limb sever -- spray only, body stays whole
#define RAG_CUT_DECAP	2			// head pops off
#define RAG_CUT_BISECT	3			// body splits at the waist

/*
===============
CG_RagdollCutBoundary

Spine index that the cut separates: particles below the boundary form the lower
body, particles at/above it form the upper body. A constraint is severed when
its two endpoints straddle the boundary.
===============
*/
static int CG_RagdollCutBoundary( int cut ) {
	if ( cut == RAG_CUT_BISECT ) {
		return RP_CHEST;			// feet+pelvis below, chest+head above
	}
	if ( cut == RAG_CUT_DECAP ) {
		return RP_HEAD;				// only the head separates
	}
	return RP_COUNT + 1;			// nothing severed
}

static qboolean CG_RagdollConstraintSevered( int cut, int a, int b ) {
	int bnd = CG_RagdollCutBoundary( cut );
	return ( ( a < bnd ) != ( b < bnd ) );
}

// ---- sword cut flash --------------------------------------------------------
// A short-lived bright plane swept through a sliced body, lying in the blade's
// cut plane (normal = blade up), so the slice reads at whatever angle you swung.

#define MAX_SWORD_CUTS	16
#define SWORD_CUT_MS	200

typedef struct {
	qboolean	active;
	int			startTime;
	vec3_t		origin;
	vec3_t		fwd;		// slash sweep axis (in the cut plane)
	vec3_t		side;		// across the slash (in the cut plane)
} swordCut_t;

static swordCut_t	cg_swordCuts[MAX_SWORD_CUTS];

// ---- kill confirm burst -----------------------------------------------------
// STRAFE 64: a radial pop on a blade kill so a clean 1-shot / finisher READS as
// the kill it is — a bright central flash + an expanding shockwave ring, scaled
// by the cut depth (cutType): a glancing sever barely pops, a bisection novas.
// Drawn additively with whiteShader (always present; no asset dependency), the
// same procedural-poly path as the bolt trails and the sword-cut flashes.

#define MAX_KILL_BURSTS	8
#define KILL_BURST_MS	380
#define KILL_RING_SEGS	24

typedef struct {
	qboolean	active;
	int			startTime;
	vec3_t		origin;
	float		intensity;	// scales radius + brightness (by cut depth)
} killBurst_t;

static killBurst_t	cg_killBursts[MAX_KILL_BURSTS];

/*
===============
CG_RagdollReset

Drop all corpses + pending cut flashes (level change / vid_restart).
===============
*/
void CG_RagdollReset( void ) {
	memset( cg_ragdolls, 0, sizeof( cg_ragdolls ) );
	memset( cg_swordCuts, 0, sizeof( cg_swordCuts ) );
	memset( cg_killBursts, 0, sizeof( cg_killBursts ) );
}

/*
===============
CG_SpawnSwordCut

Spawn a slice flash at a wound, oriented to the blade. normal is the cut-plane
normal (blade up); fwd is the blade forward (slash travel). Called from
CG_DismemberPlayer when a blade kill comes apart.
===============
*/
void CG_SpawnSwordCut( vec3_t origin, vec3_t normal, vec3_t fwd ) {
	swordCut_t	*sc;
	vec3_t		f, side;
	int			i, slot, oldestTime;

	// take a free slot, else recycle the oldest
	slot = 0;
	oldestTime = cg_swordCuts[0].startTime;
	for ( i = 0 ; i < MAX_SWORD_CUTS ; i++ ) {
		if ( !cg_swordCuts[i].active ) {
			slot = i;
			break;
		}
		if ( cg_swordCuts[i].startTime < oldestTime ) {
			oldestTime = cg_swordCuts[i].startTime;
			slot = i;
		}
	}
	sc = &cg_swordCuts[slot];

	// in-plane axes: fwd flattened into the cut plane, side = normal x fwd
	VectorCopy( fwd, f );
	VectorMA( f, -DotProduct( f, normal ), normal, f );
	if ( VectorNormalize( f ) == 0 ) {
		MakeNormalVectors( normal, f, side );
	}
	CrossProduct( normal, f, side );
	VectorNormalize( side );

	sc->active = qtrue;
	sc->startTime = cg.time;
	VectorCopy( origin, sc->origin );
	VectorCopy( f, sc->fwd );
	VectorCopy( side, sc->side );
}

/*
===============
CG_AddSwordCuts

Render the live slice flashes: a thin bright quad that extends along the slash
and fades. Called once per frame.
===============
*/
void CG_AddSwordCuts( void ) {
	int			i, j;
	swordCut_t	*sc;
	float		frac, alpha, halfLen, halfWid;
	polyVert_t	verts[4];
	vec3_t		a, b;
	byte		ca;

	for ( i = 0 ; i < MAX_SWORD_CUTS ; i++ ) {
		sc = &cg_swordCuts[i];
		if ( !sc->active ) {
			continue;
		}
		frac = ( cg.time - sc->startTime ) / (float)SWORD_CUT_MS;
		if ( frac >= 1.0f ) {
			sc->active = qfalse;
			continue;
		}

		// the slash extends outward as it completes, then fades and thins
		halfLen = 18.0f + frac * 42.0f;
		halfWid = 3.5f * ( 1.0f - frac );
		alpha = 1.0f - frac;
		ca = (byte)( alpha * 255 );

		for ( j = 0 ; j < 4 ; j++ ) {
			float ls = ( j == 0 || j == 3 ) ? -halfLen : halfLen;
			float ws = ( j < 2 ) ? -halfWid : halfWid;
			VectorMA( sc->origin, ls, sc->fwd, a );
			VectorMA( a, ws, sc->side, b );
			VectorCopy( b, verts[j].xyz );
			verts[j].st[0] = ( j == 1 || j == 2 ) ? 1.0f : 0.0f;
			verts[j].st[1] = ( j < 2 ) ? 0.0f : 1.0f;
			verts[j].modulate[0] = 255;
			verts[j].modulate[1] = 210;
			verts[j].modulate[2] = 190;
			verts[j].modulate[3] = ca;
		}
		trap_R_AddPolyToScene( cgs.media.swordCutShader, 4, verts );
	}
}

/*
===============
CG_SpawnKillConfirm

Pop a kill-confirm burst at a wound. cutType (0 single limb / 1 decap /
2 bisection) scales the spectacle so a hard 1-shot/finisher novas while a
glancing sever just glints. Called from CG_DismemberPlayer on a blade kill.
===============
*/
void CG_SpawnKillConfirm( vec3_t origin, int cutType ) {
	killBurst_t	*kb;
	int			i, slot, oldestTime;

	slot = 0;
	oldestTime = cg_killBursts[0].startTime;
	for ( i = 0 ; i < MAX_KILL_BURSTS ; i++ ) {
		if ( !cg_killBursts[i].active ) {
			slot = i;
			break;
		}
		if ( cg_killBursts[i].startTime < oldestTime ) {
			oldestTime = cg_killBursts[i].startTime;
			slot = i;
		}
	}
	kb = &cg_killBursts[slot];

	kb->active = qtrue;
	kb->startTime = cg.time;
	VectorCopy( origin, kb->origin );
	// 0 -> 0.75, 1 -> 1.05, 2 -> 1.5 : the harder the cut, the bigger the pop
	kb->intensity = 0.75f + 0.375f * (float)cutType;
}

/*
===============
CG_AddKillBursts

Render the live kill-confirm pops: a bright central flash that snaps in and
fades, plus a flat shockwave ring expanding outward across the kill. Called
once per frame beside CG_AddSwordCuts.
===============
*/
void CG_AddKillBursts( void ) {
	int			i, s;
	killBurst_t	*kb;
	float		frac, alpha;

	for ( i = 0 ; i < MAX_KILL_BURSTS ; i++ ) {
		kb = &cg_killBursts[i];
		if ( !kb->active ) {
			continue;
		}
		frac = ( cg.time - kb->startTime ) / (float)KILL_BURST_MS;
		if ( frac >= 1.0f ) {
			kb->active = qfalse;
			continue;
		}

		// --- central flash: a sprite that punches bright early then fades ---
		{
			refEntity_t	re;
			float		fa = ( frac < 0.30f ) ? ( frac / 0.30f ) : ( 1.0f - ( frac - 0.30f ) / 0.70f );

			if ( fa > 0.0f ) {
				memset( &re, 0, sizeof( re ) );
				re.reType = RT_SPRITE;
				// trailglow is a SOFT radial glow (bright core fading to black at the
				// edges). whiteShader is a solid white texture, so a sprite drawn with
				// it is a hard-edged colored SQUARE — the "blocky" flash. trailglow is
				// additive (GL_SRC_ALPHA GL_ONE, rgbGen/alphaGen vertex), so the sprite
				// fades by shaderRGBA alpha and tints by shaderRGBA — and additive means
				// it can never render black.
				re.customShader = cgs.media.trailGlowShader;
				VectorCopy( kb->origin, re.origin );
				re.radius = ( 12.0f + 26.0f * kb->intensity ) * ( 0.6f + 0.6f * frac );
				re.shaderRGBA[0] = 255;
				re.shaderRGBA[1] = 232;
				re.shaderRGBA[2] = 190;
				re.shaderRGBA[3] = (byte)( fa * 255.0f );
				trap_R_AddRefEntityToScene( &re );
			}
		}

		// --- shockwave ring: a flat annulus around Z, expanding + fading ---
		alpha = ( 1.0f - frac );
		alpha = alpha * alpha;					// ease-out fade
		{
			float	maxR  = 56.0f * kb->intensity;
			float	ringR = frac * maxR;
			float	band  = 6.0f + ( 1.0f - frac ) * 16.0f;
			float	inner = ringR;
			float	outer = ringR + band;
			// Draw the ring on the ADDITIVE datamosh shader (GL_SRC_ALPHA GL_ONE), not
			// the alpha-blend whiteShader: under the GL2 HDR/filmic-tonemap pipeline the
			// alpha-blend ring gets crushed to BLACK (same failure as the arena datamosh
			// blocks). Additive blooms into a neon shockwave instead and can never go
			// black. Still fades by alpha (alphaGen vertex scales the additive add).
			byte	ca    = (byte)( alpha * 200.0f );
			polyVert_t	verts[4];

			for ( s = 0 ; s < KILL_RING_SEGS ; s++ ) {
				float	a0 = ( 2.0f * M_PI * s ) / KILL_RING_SEGS;
				float	a1 = ( 2.0f * M_PI * ( s + 1 ) ) / KILL_RING_SEGS;
				float	c0 = cos( a0 ), s0 = sin( a0 );
				float	c1 = cos( a1 ), s1 = sin( a1 );
				int		j;

				verts[0].xyz[0] = kb->origin[0] + c0 * inner;
				verts[0].xyz[1] = kb->origin[1] + s0 * inner;
				verts[0].xyz[2] = kb->origin[2];
				verts[1].xyz[0] = kb->origin[0] + c0 * outer;
				verts[1].xyz[1] = kb->origin[1] + s0 * outer;
				verts[1].xyz[2] = kb->origin[2];
				verts[2].xyz[0] = kb->origin[0] + c1 * outer;
				verts[2].xyz[1] = kb->origin[1] + s1 * outer;
				verts[2].xyz[2] = kb->origin[2];
				verts[3].xyz[0] = kb->origin[0] + c1 * inner;
				verts[3].xyz[1] = kb->origin[1] + s1 * inner;
				verts[3].xyz[2] = kb->origin[2];

				verts[0].st[0] = 0; verts[0].st[1] = 0;
				verts[1].st[0] = 1; verts[1].st[1] = 0;
				verts[2].st[0] = 1; verts[2].st[1] = 1;
				verts[3].st[0] = 0; verts[3].st[1] = 1;

				for ( j = 0 ; j < 4 ; j++ ) {
					verts[j].modulate[0] = 255;
					verts[j].modulate[1] = 220;
					verts[j].modulate[2] = 180;
					verts[j].modulate[3] = ca;
				}
				trap_R_AddPolyToScene( cgs.media.datamoshShader, 4, verts );
			}
		}
	}
}

/*
===============
CG_RagdollSeed

Stand the spine up at the entity's feet and inherit its death velocity so the
corpse carries on from however it was moving when it died.
===============
*/
static int CG_RagdollPickLimb( centity_t *cent ) {
	char	force[8];

	trap_Cvar_VariableStringBuffer( "cg_testCutLimb", force, sizeof( force ) );
	return atoi( force ) ? atoi( force ) : 1 + ( cent->currentState.number + cg.time / 13 ) % 4;
}

static void CG_RagdollSeed( cgRagdoll_t *rag, centity_t *cent, int cut ) {
	int		i, j;
	vec3_t	vel, fwd;
	float	dt;

	rag->active = qtrue;
	rag->skel = qfalse;
	rag->settled = qfalse;
	rag->startTime = cg.time;
	rag->lastTime = cg.time;
	rag->yaw = cent->lerpAngles[YAW];
	rag->cut = cut;
	rag->airQuietTime = 0;
	rag->spin = 0;
	rag->lastDt = 0.015f;

	fwd[0] = cos( DEG2RAD( rag->yaw ) );
	fwd[1] = sin( DEG2RAD( rag->yaw ) );
	fwd[2] = 0;

	// death velocity (knockback + momentum), nudged so the body always topples
	// rather than balancing on its feet
	BG_EvaluateTrajectoryDelta( &cent->currentState.pos, cg.time, vel );
	dt = 0.015f;

	for ( i = 0 ; i < RP_COUNT ; i++ ) {
		rag->rest[i] = ragSeedZ[i];
		VectorCopy( cent->lerpOrigin, rag->pos[i] );
		rag->pos[i][2] += ragSeedZ[i];

		// encode velocity as a back-step; bias the upper body so it tips over
		for ( j = 0 ; j < 3 ; j++ ) {
			rag->prev[i][j] = rag->pos[i][j] - vel[j] * dt;
		}
		rag->prev[i][0] -= fwd[0] * ragSeedZ[i] * 0.05f;
		rag->prev[i][1] -= fwd[1] * ragSeedZ[i] * 0.05f;
	}

	// a blade cut kicks the severed section off the body so the halves
	// visibly separate instead of just dropping (velocity encoded as a
	// back-step, same dt as above)
	if ( cut == RAG_CUT_BISECT ) {
		// upper body (chest + head) slides off the waist and lifts
		for ( i = RP_CHEST ; i <= RP_HEAD ; i++ ) {
			rag->prev[i][0] -= fwd[0] * 70.0f * dt;
			rag->prev[i][1] -= fwd[1] * 70.0f * dt;
			rag->prev[i][2] -= 90.0f * dt;
		}
		// lower body settles back the other way a touch
		rag->prev[RP_PELVIS][0] += fwd[0] * 40.0f * dt;
		rag->prev[RP_PELVIS][1] += fwd[1] * 40.0f * dt;
	} else if ( cut == RAG_CUT_DECAP ) {
		// head pops up and tumbles off
		rag->prev[RP_HEAD][0] -= fwd[0] * 90.0f * dt;
		rag->prev[RP_HEAD][1] -= fwd[1] * 90.0f * dt;
		rag->prev[RP_HEAD][2] -= 200.0f * dt;
	} else if ( cut == RAG_CUT_LIMB ) {
		rag->limb = CG_RagdollPickLimb( cent );
	} else {
		rag->limb = 0;
	}
}

/*
===============
CG_RagdollCollide

Trace a small box along a particle's step and stop it at the first solid,
killing most of the velocity (heavy friction) so corpses don't skate. Returns
qtrue when the particle is resting on an upward-facing surface (i.e. actually
supported against gravity) -- the step uses this to decide whether the corpse
may settle, so a body can't latch "settled" at a toss apex and freeze midair.
===============
*/
static qboolean CG_RagdollCollide( vec3_t pos, vec3_t prev, float r, float friction ) {
	trace_t	tr;
	vec3_t	mins, maxs, vel;
	float	into;

	VectorSet( mins, -r, -r, -r );
	VectorSet( maxs,  r,  r,  r );

	CG_Trace( &tr, prev, mins, maxs, pos, -1, CONTENTS_SOLID );

	if ( tr.startsolid || tr.allsolid ) {
		// wedged in geometry -- freeze in place rather than letting the solver
		// pop it out explosively (the old behaviour's "spasm"). NOT counted as
		// ground support: a particle flung into a wall must not let the body
		// settle in the air.
		VectorCopy( prev, pos );
		return qfalse;
	}
	if ( tr.fraction >= 1.0f ) {
		return qfalse;						// free flight, no contact
	}

	// rest at the contact point and rebuild the implied velocity (pos - prev):
	// drop the component driving into the surface (no bounce) and keep the
	// tangent with friction. Single, stable response -- no second trace.
	VectorCopy( tr.endpos, pos );
	VectorSubtract( pos, prev, vel );
	into = DotProduct( vel, tr.plane.normal );
	if ( into < 0 ) {
		VectorMA( vel, -into, tr.plane.normal, vel );	// keep tangent only
	}
	VectorScale( vel, friction, vel );					// surface friction
	VectorSubtract( pos, vel, prev );

	// supported only if the surface can hold the body up (a floor, not a wall)
	return ( tr.plane.normal[2] > 0.3f );
}

/*
===============
CG_RagdollGroundSnap

Recovery for a corpse hung in the air: a severed half flung into a wall can
wedge a particle solid (CG_RagdollCollide freezes it), and since that particle
never counts as ground support the body would float there forever. Trace
straight down from the lowest particle and drop the whole spine so it rests on
the floor below, killing its velocity. If there's no floor (over a pit) just
stop it where it is rather than fall through the world.
===============
*/
static void CG_RagdollGroundSnap( cgRagdoll_t *rag ) {
	int		i, low, count = rag->skel ? RS_COUNT : RP_COUNT;
	float	drop;
	trace_t	tr;
	vec3_t	start, end, mins, maxs;

	low = 0;
	for ( i = 1 ; i < count ; i++ ) {
		if ( rag->pos[i][2] < rag->pos[low][2] ) {
			low = i;
		}
	}

	VectorSet( mins, -RAG_HALFBOX, -RAG_HALFBOX, -RAG_HALFBOX );
	VectorSet( maxs,  RAG_HALFBOX,  RAG_HALFBOX,  RAG_HALFBOX );

	VectorCopy( rag->pos[low], start );
	VectorCopy( rag->pos[low], end );
	end[2] -= 4096.0f;
	CG_Trace( &tr, start, mins, maxs, end, -1, CONTENTS_SOLID );

	if ( tr.fraction >= 1.0f || tr.startsolid || tr.allsolid ) {
		// no floor / buried: freeze in place (kill velocity) instead of floating
		drop = 0;
	} else {
		drop = rag->pos[low][2] - tr.endpos[2];
	}

	for ( i = 0 ; i < count ; i++ ) {
		rag->pos[i][2] -= drop;
		VectorCopy( rag->pos[i], rag->prev[i] );		// zero velocity -> rests
	}
}

/*
===============
The mannequin's jointed body

Particles start on the real joints of the pose the player died in. Each step
turns the bones to follow them, measured from that pose, so a body keeps its last
motion and goes limp from there.
===============
*/

typedef struct {
	const char	*joint;
	int			part;
} ragJoint_t;

static const ragJoint_t ragJoints[] = {
	{ "pelvis", RP_PELVIS }, { "spine_03", RP_CHEST }, { "neck_01", RS_NECK }, { "Head", RP_HEAD },
	{ "upperarm_l", RS_ARM_L }, { "lowerarm_l", RS_ELBOW_L }, { "hand_l", RS_HAND_L },
	{ "upperarm_r", RS_ARM_R }, { "lowerarm_r", RS_ELBOW_R }, { "hand_r", RS_HAND_R },
	{ "thigh_l", RS_THIGH_L }, { "calf_l", RS_KNEE_L }, { "foot_l", RS_FOOT_L },
	{ "thigh_r", RS_THIGH_R }, { "calf_r", RS_KNEE_R }, { "foot_r", RS_FOOT_R },
};
#define RAG_NUM_JOINTS ( (int)( sizeof( ragJoints ) / sizeof( ragJoints[0] ) ) )

static const float ragSkelRadius[RS_COUNT] = {
	3, 6, 6, 4,		// foot, pelvis, chest, crown
	4,				// neck
	3, 2, 2,  3, 2, 2,
	3, 3,  3, 3, 3,
	3,
};

// bones: the joint, the particle pair it follows, and the bone it hangs from
typedef struct {
	const char	*joint;
	int			from, to;
	int			parent;
} ragBoneDef_t;

static const ragBoneDef_t ragBones[RB_COUNT] = {
	{ "pelvis",     RP_PELVIS,  RP_PELVIS,  -1 },
	{ "spine_03",   RP_CHEST,   RP_CHEST,   RB_PELVIS },
	{ "neck_01",    RS_NECK,    RP_HEAD,    RB_CHEST },
	{ "upperarm_l", RS_ARM_L,   RS_ELBOW_L, RB_CHEST },
	{ "lowerarm_l", RS_ELBOW_L, RS_HAND_L,  RB_UPPER_L },
	{ "upperarm_r", RS_ARM_R,   RS_ELBOW_R, RB_CHEST },
	{ "lowerarm_r", RS_ELBOW_R, RS_HAND_R,  RB_UPPER_R },
	{ "thigh_l",    RS_THIGH_L, RS_KNEE_L,  RB_PELVIS },
	{ "calf_l",     RS_KNEE_L,  RS_FOOT_L,  RB_THIGH_L },
	{ "thigh_r",    RS_THIGH_R, RS_KNEE_R,  RB_PELVIS },
	{ "calf_r",     RS_KNEE_R,  RS_FOOT_R,  RB_THIGH_R },
};

static const char *ragSpineJoints[3] = { "spine_01", "spine_02", "spine_03" };

// rigid links hold their length from the death pose. A folding link only stops
// two particles closing past fold * the length of the chain through via
typedef struct {
	int		a, b, via;
	float	fold;
} ragLink_t;

static const ragLink_t ragLinks[] = {
	{ RP_PELVIS, RS_THIGH_L, -1, 0 }, { RP_PELVIS, RS_THIGH_R, -1, 0 }, { RS_THIGH_L, RS_THIGH_R, -1, 0 },
	{ RP_CHEST, RS_ARM_L, -1, 0 }, { RP_CHEST, RS_ARM_R, -1, 0 }, { RS_ARM_L, RS_ARM_R, -1, 0 },
	{ RS_NECK, RS_ARM_L, -1, 0 }, { RS_NECK, RS_ARM_R, -1, 0 }, { RP_CHEST, RS_NECK, -1, 0 },
	{ RP_PELVIS, RP_CHEST, -1, 0 }, { RS_NECK, RP_HEAD, -1, 0 },
	{ RS_ARM_L, RS_ELBOW_L, -1, 0 }, { RS_ELBOW_L, RS_HAND_L, -1, 0 },
	{ RS_ARM_R, RS_ELBOW_R, -1, 0 }, { RS_ELBOW_R, RS_HAND_R, -1, 0 },
	{ RS_THIGH_L, RS_KNEE_L, -1, 0 }, { RS_KNEE_L, RS_FOOT_L, -1, 0 },
	{ RS_THIGH_R, RS_KNEE_R, -1, 0 }, { RS_KNEE_R, RS_FOOT_R, -1, 0 },
	{ RS_ARM_L, RS_HAND_L, RS_ELBOW_L, 0.3f }, { RS_ARM_R, RS_HAND_R, RS_ELBOW_R, 0.3f },
	{ RS_THIGH_L, RS_FOOT_L, RS_KNEE_L, 0.35f }, { RS_THIGH_R, RS_FOOT_R, RS_KNEE_R, 0.35f },
	{ RP_CHEST, RS_KNEE_L, RS_THIGH_L, 0.6f }, { RP_CHEST, RS_KNEE_R, RS_THIGH_R, 0.6f },
	{ RP_PELVIS, RS_NECK, RP_CHEST, 0.8f }, { RP_CHEST, RP_HEAD, RS_NECK, 0.8f },
};
#define RAG_NUM_LINKS ( (int)( sizeof( ragLinks ) / sizeof( ragLinks[0] ) ) )

// by rag->limb: the joint it comes off, its bone, and the joints it carries
static const int ragLimbRoot[5] = { RP_CHEST, RS_ARM_L, RS_ARM_R, RS_THIGH_L, RS_THIGH_R };
static const int ragLimbBone[5] = { RB_CHEST, RB_UPPER_L, RB_UPPER_R, RB_THIGH_L, RB_THIGH_R };
static const int ragLimbChain[5][2] = { { 0, 0 },
	{ RS_ELBOW_L, RS_HAND_L }, { RS_ELBOW_R, RS_HAND_R }, { RS_KNEE_L, RS_FOOT_L }, { RS_KNEE_R, RS_FOOT_R } };

// a cut limb's upper bone hangs from RS_LIMB, not the joint left on the body
static int RagBoneFrom( const cgRagdoll_t *rag, int bone ) {
	return rag->limb && bone == ragLimbBone[rag->limb] ? RS_LIMB : ragBones[bone].from;
}

static void RagQuatMul( const quat_t a, const quat_t b, quat_t out ) {
	out[0] = a[3]*b[0] + a[0]*b[3] + a[1]*b[2] - a[2]*b[1];
	out[1] = a[3]*b[1] - a[0]*b[2] + a[1]*b[3] + a[2]*b[0];
	out[2] = a[3]*b[2] + a[0]*b[1] - a[1]*b[0] + a[2]*b[3];
	out[3] = a[3]*b[3] - a[0]*b[0] - a[1]*b[1] - a[2]*b[2];
}

// a's inverse times b
static void RagQuatRelative( const quat_t a, const quat_t b, quat_t out ) {
	quat_t	conj;

	conj[0] = -a[0];
	conj[1] = -a[1];
	conj[2] = -a[2];
	conj[3] = a[3];
	RagQuatMul( conj, b, out );
}

// the world-space turn that takes frame a to frame b
static void RagQuatTurn( const quat_t a, const quat_t b, quat_t out ) {
	quat_t	conj;

	conj[0] = -a[0];
	conj[1] = -a[1];
	conj[2] = -a[2];
	conj[3] = a[3];
	RagQuatMul( b, conj, out );
}

static void RagQuatRotate( const quat_t q, const vec3_t v, vec3_t out ) {
	vec3_t	t, c;

	CrossProduct( q, v, t );
	VectorScale( t, 2.0f, t );
	CrossProduct( q, t, c );
	VectorMA( v, q[3], t, out );
	VectorAdd( out, c, out );
}

static void RagQuatNormalize( quat_t q ) {
	float	l = sqrt( q[0]*q[0] + q[1]*q[1] + q[2]*q[2] + q[3]*q[3] );

	if ( l > 0.0001f ) {
		q[0] /= l;
		q[1] /= l;
		q[2] /= l;
		q[3] /= l;
	} else {
		q[0] = q[1] = q[2] = 0;
		q[3] = 1;
	}
}

// the rotation of a frame with these columns
static void RagQuatFromAxes( const vec3_t x, const vec3_t y, const vec3_t z, quat_t q ) {
	float	t = x[0] + y[1] + z[2];

	if ( t > 0 ) {
		q[0] = y[2] - z[1];
		q[1] = z[0] - x[2];
		q[2] = x[1] - y[0];
		q[3] = 1.0f + t;
	} else if ( x[0] > y[1] && x[0] > z[2] ) {
		q[0] = 1.0f + x[0] - y[1] - z[2];
		q[1] = x[1] + y[0];
		q[2] = z[0] + x[2];
		q[3] = y[2] - z[1];
	} else if ( y[1] > z[2] ) {
		q[0] = x[1] + y[0];
		q[1] = 1.0f + y[1] - x[0] - z[2];
		q[2] = y[2] + z[1];
		q[3] = z[0] - x[2];
	} else {
		q[0] = z[0] + x[2];
		q[1] = y[2] + z[1];
		q[2] = 1.0f + z[2] - x[0] - y[1];
		q[3] = x[1] - y[0];
	}
	RagQuatNormalize( q );
}

static void RagFrame( vec3_t pos[RS_COUNT], int left, int right, int top, quat_t q ) {
	vec3_t	x, y, z, mid;

	VectorSubtract( pos[right], pos[left], x );
	VectorNormalize( x );
	VectorAdd( pos[left], pos[right], mid );
	VectorScale( mid, 0.5f, mid );
	if ( top == RP_CHEST ) {
		VectorSubtract( mid, pos[top], z );		// shoulders sit above the chest joint
	} else {
		VectorSubtract( pos[top], mid, z );		// the pelvis above the hips
	}
	VectorMA( z, -DotProduct( z, x ), x, z );
	VectorNormalize( z );
	CrossProduct( z, x, y );
	RagQuatFromAxes( x, y, z, q );
}

/*
===============
CG_RagdollSkelTurn

Reads the bone rotations back from the particles. Limb bones are carried along
by the turn since last step instead of measured from the parent: that has no
angle where the answer flips, and a limb can point anywhere on a dead body.
===============
*/
static void CG_RagdollSkelTurn( cgRagdoll_t *rag ) {
	quat_t	cur, arc, q;
	vec3_t	was, now, d;
	int		i, from;

	RagFrame( rag->pos, RS_THIGH_L, RS_THIGH_R, RP_PELVIS, cur );
	RagQuatTurn( rag->frameSeed[0], cur, rag->boneRot[RB_PELVIS] );
	RagFrame( rag->pos, RS_ARM_L, RS_ARM_R, RP_CHEST, cur );
	RagQuatTurn( rag->frameSeed[1], cur, rag->boneRot[RB_CHEST] );

	for ( i = RB_NECK; i < RB_COUNT; i++ ) {
		from = RagBoneFrom( rag, i );
		VectorSubtract( rag->seed[ragBones[i].to], rag->seed[ragBones[i].from], d );
		VectorNormalize( d );
		RagQuatRotate( rag->boneRot[i], d, was );
		VectorSubtract( rag->pos[ragBones[i].to], rag->pos[from], now );
		if ( VectorNormalize( now ) < 0.001f ) {
			continue;
		}
		CrossProduct( was, now, arc );
		arc[3] = 1.0f + DotProduct( was, now );
		if ( arc[3] < 0.001f ) {
			continue;
		}
		RagQuatNormalize( arc );
		RagQuatMul( arc, rag->boneRot[i], q );
		RagQuatNormalize( q );
		QuatCopy( q, rag->boneRot[i] );
	}
}

/*
===============
CG_RagdollSkelLinks

One relaxation pass. Links across a cut no longer hold. Knees only bend forward,
which the links alone can't say: they'd fold either way.
===============
*/
static void CG_RagdollSkelLink( vec3_t a, vec3_t b, float rest, qboolean fold ) {
	vec3_t	delta;
	float	dist, diff;

	VectorSubtract( b, a, delta );
	dist = VectorLength( delta );
	if ( dist < 0.0001f || ( fold && dist >= rest ) ) {
		return;
	}
	diff = ( dist - rest ) / dist * 0.5f;
	VectorMA( a, diff, delta, a );
	VectorMA( b, -diff, delta, b );
}

static void CG_RagdollSkelLinks( cgRagdoll_t *rag ) {
	static const int	legs[2][3] = { { RS_THIGH_L, RS_KNEE_L, RS_FOOT_L }, { RS_THIGH_R, RS_KNEE_R, RS_FOOT_R } };
	vec3_t	fwd, side;
	float	diff;
	int		i;

	for ( i = 0; i < RAG_NUM_LINKS; i++ ) {
		const ragLink_t	*l = &ragLinks[i];

		if ( rag->group[l->a] == rag->group[l->b] ) {
			CG_RagdollSkelLink( rag->pos[l->a], rag->pos[l->b], rag->linkRest[i], l->fold != 0 );
		}
	}
	if ( rag->limb ) {
		CG_RagdollSkelLink( rag->pos[RS_LIMB], rag->pos[ragLimbChain[rag->limb][0]], rag->limbRest, qfalse );
	}

	RagQuatRotate( rag->boneRot[RB_PELVIS], rag->seedAxis[0], fwd );
	for ( i = 0; i < 2; i++ ) {
		const int	*n = legs[i];
		vec3_t		line, off;

		if ( rag->group[n[1]] != rag->group[RP_PELVIS] ) {
			continue;
		}
		VectorSubtract( rag->pos[n[2]], rag->pos[n[0]], line );
		if ( VectorNormalize( line ) < 0.001f ) {
			continue;
		}
		VectorSubtract( rag->pos[n[1]], rag->pos[n[0]], off );
		CrossProduct( line, fwd, side );
		CrossProduct( side, line, side );		// fwd, square to the hip-foot line
		diff = DotProduct( off, side );
		if ( diff < 0 ) {
			VectorMA( rag->pos[n[1]], -0.5f * diff, side, rag->pos[n[1]] );
		}
	}
}

static void RagSeedAxes( const cgRagdoll_t *rag, const char *joint, vec3_t out[3] ) {
	orientation_t	tag;
	int				j, k;

	trap_R_BoneTag( &tag, cgs.media.skelModel, rag->frame, rag->oldframe, rag->backlerp, 0, joint );
	for ( k = 0; k < 3; k++ ) {
		VectorClear( out[k] );
		for ( j = 0; j < 3; j++ ) {
			VectorMA( out[k], tag.axis[k][j], rag->seedAxis[j], out[k] );
		}
	}
}

/*
===============
CG_RagdollSeedSkel
===============
*/
static void CG_RagdollSeedSkel( cgRagdoll_t *rag, centity_t *cent, int cut ) {
	playerEntity_t	*pe = &cent->pe;
	orientation_t	tag;
	vec3_t			vel, fwd, d, centre, mins, maxs;
	trace_t			tr;
	float			dt = 0.015f, len;
	int				i, k;

	CG_RagdollSeed( rag, cent, RAG_CUT_NONE );
	rag->skel = qtrue;
	rag->cut = cut;
	rag->limb = 0;

	// the frame it was last drawn in. Nothing recent (a corpse joined late): stand
	VectorSubtract( pe->skelLastOrigin, cent->lerpOrigin, d );
	if ( pe->skelLastTime && cg.time - pe->skelLastTime < 500 && VectorLengthSquared( d ) < 128.0f * 128.0f ) {
		rag->frame = pe->skelLastFrame;
		rag->oldframe = pe->skelLastOld;
		rag->backlerp = pe->skelLastBack;
		VectorCopy( pe->skelLastOrigin, rag->seedOrigin );
		AxisCopy( pe->skelLastAxis, rag->seedAxis );
	} else {
		vec3_t	angles;

		rag->frame = rag->oldframe = 0;
		rag->backlerp = 0;
		VectorCopy( cent->lerpOrigin, rag->seedOrigin );
		rag->seedOrigin[2] -= 24;
		VectorSet( angles, 0, rag->yaw, 0 );
		AnglesToAxis( angles, rag->seedAxis );
	}
	VectorCopy( rag->seedAxis[0], fwd );

	for ( i = 0; i < RAG_NUM_JOINTS; i++ ) {
		trap_R_BoneTag( &tag, cgs.media.skelModel, rag->frame, rag->oldframe, rag->backlerp, 0, ragJoints[i].joint );
		VectorCopy( rag->seedOrigin, rag->seed[ragJoints[i].part] );
		for ( k = 0; k < 3; k++ ) {
			VectorMA( rag->seed[ragJoints[i].part], tag.origin[k], rag->seedAxis[k], rag->seed[ragJoints[i].part] );
		}
	}
	// the crown of the head, not its joint, so the skull has a bone to swing on
	VectorSubtract( rag->seed[RP_HEAD], rag->seed[RS_NECK], d );
	VectorNormalize( d );
	VectorMA( rag->seed[RP_HEAD], 9.0f, d, rag->seed[RP_HEAD] );

	for ( i = 0; i < RB_COUNT; i++ ) {
		RagSeedAxes( rag, ragBones[i].joint, rag->boneAxis[i] );
		rag->boneRot[i][0] = rag->boneRot[i][1] = rag->boneRot[i][2] = 0;
		rag->boneRot[i][3] = 1;
	}
	for ( i = 0; i < 3; i++ ) {
		RagSeedAxes( rag, ragSpineJoints[i], rag->spineAxis[i] );
	}

	memset( rag->group, 0, sizeof( rag->group ) );
	if ( cut == RAG_CUT_LIMB ) {
		rag->limb = CG_RagdollPickLimb( cent );
		VectorCopy( rag->seed[ragLimbRoot[rag->limb]], rag->seed[RS_LIMB] );
		VectorSubtract( rag->seed[RS_LIMB], rag->seed[ragLimbChain[rag->limb][0]], d );
		rag->limbRest = VectorLength( d );
	} else {
		VectorCopy( rag->seed[RP_PELVIS], rag->seed[RS_LIMB] );
	}

	// frames come from the true pose, so a joint clipped out of a wall below only
	// bends its bone
	RagFrame( rag->seed, RS_THIGH_L, RS_THIGH_R, RP_PELVIS, rag->frameSeed[0] );
	RagFrame( rag->seed, RS_ARM_L, RS_ARM_R, RP_CHEST, rag->frameSeed[1] );

	for ( i = 0; i < RAG_NUM_LINKS; i++ ) {
		const ragLink_t	*l = &ragLinks[i];

		if ( l->fold ) {
			VectorSubtract( rag->seed[l->a], rag->seed[l->via], d );
			len = VectorLength( d );
			VectorSubtract( rag->seed[l->via], rag->seed[l->b], d );
			rag->linkRest[i] = ( len + VectorLength( d ) ) * l->fold;
		} else {
			VectorSubtract( rag->seed[l->a], rag->seed[l->b], d );
			rag->linkRest[i] = VectorLength( d );
		}
	}

	// pull anything poking through a wall back to open air, along from the body
	VectorCopy( cent->lerpOrigin, centre );
	VectorSet( mins, -6, -6, -6 );
	VectorSet( maxs, 6, 6, 6 );
	CG_Trace( &tr, centre, mins, maxs, rag->seed[RP_PELVIS], -1, CONTENTS_SOLID );
	if ( !tr.startsolid ) {
		VectorCopy( tr.endpos, centre );
	}
	for ( i = 0; i < RS_COUNT; i++ ) {
		VectorSet( mins, -ragSkelRadius[i], -ragSkelRadius[i], -ragSkelRadius[i] );
		VectorSet( maxs, ragSkelRadius[i], ragSkelRadius[i], ragSkelRadius[i] );
		CG_Trace( &tr, centre, mins, maxs, rag->seed[i], -1, CONTENTS_SOLID );
		VectorCopy( tr.startsolid ? centre : tr.endpos, rag->pos[i] );
	}

	// the speed it died at: a player's trajectory is interpolated, so it's read
	// directly. It always topples, and the knees give first
	VectorCopy( cent->currentState.pos.trDelta, vel );
	for ( i = 0; i < RS_COUNT; i++ ) {
		float	h = rag->pos[i][2] - rag->seedOrigin[2];

		VectorMA( rag->pos[i], -dt, vel, rag->prev[i] );
		VectorMA( rag->prev[i], -h * 0.012f, fwd, rag->prev[i] );
	}
	VectorMA( rag->prev[RS_KNEE_L], -40.0f * dt, fwd, rag->prev[RS_KNEE_L] );
	VectorMA( rag->prev[RS_KNEE_R], -40.0f * dt, fwd, rag->prev[RS_KNEE_R] );

	if ( cut == RAG_CUT_BISECT ) {
		static const int	upper[] = { RP_CHEST, RP_HEAD, RS_NECK, RS_ARM_L, RS_ELBOW_L, RS_HAND_L, RS_ARM_R, RS_ELBOW_R, RS_HAND_R };

		for ( i = 0; i < 9; i++ ) {
			rag->group[upper[i]] = 1;
			VectorMA( rag->prev[upper[i]], -70.0f * dt, fwd, rag->prev[upper[i]] );
			rag->prev[upper[i]][2] -= 90.0f * dt;
		}
		VectorMA( rag->prev[RP_PELVIS], 40.0f * dt, fwd, rag->prev[RP_PELVIS] );
	} else if ( cut == RAG_CUT_DECAP ) {
		rag->group[RS_NECK] = rag->group[RP_HEAD] = 1;
		for ( i = 0; i < 2; i++ ) {
			int		p = i ? RP_HEAD : RS_NECK;

			VectorMA( rag->prev[p], -90.0f * dt, fwd, rag->prev[p] );
			rag->prev[p][2] -= ( i ? 240.0f : 170.0f ) * dt;
		}
	} else if ( cut == RAG_CUT_LIMB ) {
		float	kx = crandom() * 60.0f, ky = crandom() * 60.0f;

		for ( i = 0; i < 3; i++ ) {
			int		p = i ? ragLimbChain[rag->limb][i - 1] : RS_LIMB;
			float	whip = 0.7f + i * 0.35f;	// the far end leads, so it tumbles

			rag->group[p] = 1;
			VectorMA( rag->prev[p], -170.0f * whip * dt, fwd, rag->prev[p] );
			rag->prev[p][0] -= kx * whip * dt;
			rag->prev[p][1] -= ky * whip * dt;
			rag->prev[p][2] -= 260.0f * whip * dt;
		}
	}
}

/*
===============
CG_RagdollStep

One Verlet integration + constraint relaxation + collision pass. Uses a clamped
dt so frame hitches can't explode the sim.
===============
*/
static void CG_RagdollStep( cgRagdoll_t *rag ) {
	int		i, k, n, count = rag->skel ? RS_COUNT : RP_COUNT;
	float	dt, damp;
	vec3_t	temp, delta;
	float	dist, diff;
	float	maxVel2;
	float	carry, friction, stepScale;
	qboolean	grounded;

	dt = ( cg.time - rag->lastTime ) * 0.001f;
	rag->lastTime = cg.time;
	if ( dt <= 0 ) {
		return;
	}
	if ( dt > 0.05f ) {
		dt = 0.05f;			// clamp hitches to 50ms
	}

	damp = cg_ragdollDamp.value;
	if ( damp < 0 ) damp = 0;
	if ( damp > 1 ) damp = 1;

	// everything per-step is scaled by this step's length: the world clock runs
	// slow in bullet time, and at a tenth of the step a body would keep moving a
	// full step's distance, ten times too fast
	stepScale = dt / 0.015f;
	carry = dt / rag->lastDt * pow( damp, stepScale );
	friction = pow( 0.5f, stepScale );
	rag->lastDt = dt;

	n = cg_ragdollIterations.integer;
	if ( n < 1 ) n = 1;
	if ( n > 16 ) n = 16;

	// advance the tumble phase for severed pieces. This only runs while the
	// body is live (the step stops once it settles), so the spin freezes at
	// rest instead of spinning a grounded corpse forever.
	rag->spin += RAG_SPIN_RATE * dt;

	// integrate
	for ( i = 0 ; i < count ; i++ ) {
		VectorCopy( rag->pos[i], temp );
		// x += (x - xprev) * damp + g*dt^2
		VectorSubtract( rag->pos[i], rag->prev[i], delta );
		VectorScale( delta, carry, delta );
		VectorAdd( rag->pos[i], delta, rag->pos[i] );
		rag->pos[i][2] -= RAG_GRAVITY * dt * dt;
		VectorCopy( temp, rag->prev[i] );

		// clamp per-step displacement so one bad frame (hitch, deep
		// constraint correction, collision shove) can't fling a particle and
		// make the model spasm
		VectorSubtract( rag->pos[i], rag->prev[i], delta );
		dist = VectorLength( delta );
		if ( dist > RAG_MAXSTEP * stepScale ) {
			VectorScale( delta, RAG_MAXSTEP * stepScale / dist, delta );
			VectorAdd( rag->prev[i], delta, rag->pos[i] );
		}
	}

	// satisfy distance constraints (position-only; no collision inside the
	// loop -- running the contact response n times per frame was what made
	// grounded corpses jitter/explode)
	for ( k = 0 ; k < n ; k++ ) {
		const ragConstraint_t *c;
		if ( rag->skel ) {
			CG_RagdollSkelLinks( rag );
			continue;
		}
		for ( i = 0 ; i < RAG_NUM_CONSTRAINTS ; i++ ) {
			c = &ragConstraints[i];
			if ( CG_RagdollConstraintSevered( rag->cut, c->a, c->b ) ) {
				continue;		// blade cut -- this link no longer holds
			}
			VectorSubtract( rag->pos[c->b], rag->pos[c->a], delta );
			dist = VectorLength( delta );
			if ( dist < 0.0001f ) {
				continue;
			}
			diff = ( dist - c->rest ) / dist * c->stiff;
			VectorScale( delta, 0.5f * diff, delta );
			VectorAdd( rag->pos[c->a], delta, rag->pos[c->a] );
			VectorSubtract( rag->pos[c->b], delta, rag->pos[c->b] );
		}
	}

	// one collision pass after the constraints are satisfied, so particles
	// end the frame resting out of solids
	grounded = qfalse;
	for ( i = 0 ; i < count ; i++ ) {
		if ( CG_RagdollCollide( rag->pos[i], rag->prev[i], rag->skel ? ragSkelRadius[i] : RAG_HALFBOX, friction ) ) {
			grounded = qtrue;
		}
	}

	// rest detection: once everything is nearly stationary AND the body is
	// actually resting on the ground, stop simulating so settled corpses cost
	// nothing. The ground requirement is what stops a corpse from latching
	// "settled" at the apex of a toss (vertical velocity passes through zero)
	// and freezing in midair.
	maxVel2 = 0;
	for ( i = 0 ; i < count ; i++ ) {
		float v2;
		VectorSubtract( rag->pos[i], rag->prev[i], delta );
		v2 = DotProduct( delta, delta );
		if ( v2 > maxVel2 ) {
			maxVel2 = v2;
		}
	}
	// stuck-in-air recovery: a piece that has gone quiet WITHOUT ground support
	// is wedged/hung (e.g. a severed half flung into a wall), not freely falling
	// -- a free fall is never quiet for long. Require the quiet to persist
	// (tracked from airQuietTime) so a toss apex, where vertical velocity passes
	// momentarily through zero, doesn't trip it. Then snap it down to the floor.
	maxVel2 /= stepScale * stepScale;
	if ( grounded || maxVel2 >= RAG_SETTLE_VEL2 ) {
		rag->airQuietTime = 0;
	} else if ( rag->airQuietTime == 0 ) {
		rag->airQuietTime = cg.time;
	} else if ( cg.time - rag->airQuietTime > RAG_AIRHANG_MS ) {
		CG_RagdollGroundSnap( rag );
		grounded = qtrue;
		rag->airQuietTime = 0;
	}

	if ( grounded && maxVel2 < RAG_SETTLE_VEL2 && cg.time - rag->startTime > 400 ) {
		rag->settled = qtrue;
	}
	if ( rag->skel ) {
		CG_RagdollSkelTurn( rag );
		if ( cg.time - rag->startTime > RAG_SKEL_MAXMS ) {
			rag->settled = qtrue;
		}
	}
}

/*
===============
CG_RagdollSegment

Build and add one MD3 segment oriented along (from -> to), rolled to the
captured death yaw. originPart anchors the segment's model origin.
===============
*/
static void CG_RagdollSegment( cgRagdoll_t *rag, qhandle_t model, qhandle_t skin,
		ragPart_t originPart, ragPart_t fromPart, ragPart_t toPart,
		centity_t *cent, int renderfx, float shadowPlane, float spin ) {
	refEntity_t	ent;
	vec3_t		up, fwd, side;

	if ( !model ) {
		return;
	}

	memset( &ent, 0, sizeof( ent ) );

	// segment "up" runs along the bone, or along the particle's own velocity
	// for a free-tumbling lone piece (fromPart == toPart, e.g. a severed head)
	if ( fromPart == toPart ) {
		VectorSubtract( rag->pos[fromPart], rag->prev[fromPart], up );
	} else {
		VectorSubtract( rag->pos[toPart], rag->pos[fromPart], up );
	}
	if ( VectorNormalize( up ) == 0 ) {
		VectorSet( up, 0, 0, 1 );
	}

	// forward from the death yaw, made perpendicular to up
	fwd[0] = cos( DEG2RAD( rag->yaw ) );
	fwd[1] = sin( DEG2RAD( rag->yaw ) );
	fwd[2] = 0;
	VectorMA( fwd, -DotProduct( fwd, up ), up, fwd );
	if ( VectorNormalize( fwd ) == 0 ) {
		MakeNormalVectors( up, fwd, side );
	}
	CrossProduct( up, fwd, side );
	VectorNormalize( side );

	// severed pieces tumble: roll the in-plane axes (fwd, side) about the bone
	// so the detached half visibly spins off rather than sliding rigidly. spin
	// is frozen once the body settles, so a grounded corpse holds its pose.
	if ( spin != 0 ) {
		float	c = cos( spin ), s = sin( spin );
		vec3_t	nf, ns;
		int		j;
		for ( j = 0 ; j < 3 ; j++ ) {
			nf[j] = fwd[j] * c + side[j] * s;
			ns[j] = side[j] * c - fwd[j] * s;
		}
		VectorCopy( nf, fwd );
		VectorCopy( ns, side );
	}

	// MD3 axis: [0]=forward, [1]=left, [2]=up
	VectorCopy( fwd, ent.axis[0] );
	VectorCopy( side, ent.axis[1] );
	VectorCopy( up,  ent.axis[2] );

	VectorCopy( rag->pos[originPart], ent.origin );
	VectorCopy( rag->pos[originPart], ent.lightingOrigin );
	VectorCopy( ent.origin, ent.oldorigin );

	ent.hModel = model;
	ent.customSkin = skin;
	ent.shadowPlane = shadowPlane;
	ent.renderfx = renderfx;
	// frame 0: a neutral limp pose; the physical orientation sells the death
	ent.frame = ent.oldframe = 0;
	ent.backlerp = 0;

	CG_AddRefEntityWithPowerups( &ent, &cent->currentState, cgs.clientinfo[ cent->currentState.clientNum ].team );
}

// a turn measured from the death pose, as the joint's local delta
static int RagBoneDelta( qhandle_t model, int pose, const char *joint, const quat_t rel, vec3_t axis[3] ) {
	quat_t	d;

	d[0] = DotProduct( rel, axis[0] );
	d[1] = DotProduct( rel, axis[1] );
	d[2] = DotProduct( rel, axis[2] );
	d[3] = rel[3];
	return trap_R_BoneDelta( model, pose, joint, d );
}

/*
===============
CG_RagdollSkelPiece

Draws the mannequin hung from one bone. The entity carries that bone's turn and
puts it on its particle, so a severed piece needs nothing from the body it left;
the bones below it are local deltas. mask is hidden, or with keep, all that shows.
===============
*/
static void CG_RagdollSkelPiece( cgRagdoll_t *rag, centity_t *cent, int root, int rootPart, int group,
		const char *mask, qboolean keep, int renderfx, float shadowPlane ) {
	static const float	maskQuat[2][4] = { { 0, 0, 0, 0 }, { 0, 0, 0, 2 } };
	qhandle_t	model = cgs.media.skelModel;
	refEntity_t	ent;
	const float	*rot = rag->boneRot[root];
	vec3_t		d;
	quat_t		rel;
	int			i;

	memset( &ent, 0, sizeof( ent ) );
	ent.hModel = model;
	ent.frame = rag->frame;
	ent.oldframe = rag->oldframe;
	ent.backlerp = rag->backlerp;
	for ( i = 0; i < 3; i++ ) {
		RagQuatRotate( rot, rag->seedAxis[i], ent.axis[i] );
	}
	VectorSubtract( rag->seed[rootPart], rag->seedOrigin, d );
	RagQuatRotate( rot, d, d );
	VectorSubtract( rag->pos[rootPart], d, ent.origin );
	VectorCopy( ent.origin, ent.oldorigin );
	VectorCopy( rag->pos[rootPart], ent.lightingOrigin );
	ent.shadowPlane = shadowPlane;
	ent.renderfx = renderfx;

	if ( root == RB_PELVIS && rag->group[RP_CHEST] == group ) {
		// the spine bends over its three joints, not at one
		float	ang, s;
		vec3_t	axis;

		RagQuatRelative( rag->boneRot[RB_PELVIS], rag->boneRot[RB_CHEST], rel );
		VectorCopy( rel, axis );
		s = VectorNormalize( axis );
		ang = atan2( s, rel[3] ) / 3.0f;
		VectorScale( axis, sin( ang ), rel );
		rel[3] = cos( ang );
		for ( i = 0; i < 3; i++ ) {
			ent.bonePose = RagBoneDelta( model, ent.bonePose, ragSpineJoints[i], rel, rag->spineAxis[i] );
		}
	}
	for ( i = RB_NECK; i < RB_COUNT; i++ ) {
		int		parent = ragBones[i].parent;

		if ( i == root || rag->group[RagBoneFrom( rag, i )] != group || rag->group[RagBoneFrom( rag, parent )] != group ) {
			continue;
		}
		RagQuatRelative( rag->boneRot[parent], rag->boneRot[i], rel );
		ent.bonePose = RagBoneDelta( model, ent.bonePose, ragBones[i].joint, rel, rag->boneAxis[i] );
	}
	if ( mask ) {
		ent.bonePose = trap_R_BoneDelta( model, ent.bonePose, mask, maskQuat[keep] );
		if ( !ent.bonePose ) {
			return;		// no bone poses on this renderer: no whole body for a piece
		}
	}
	CG_AddRefEntityWithPowerups( &ent, &cent->currentState, cgs.clientinfo[ cent->currentState.clientNum ].team );
}

/*
===============
CG_RagdollAddSkeletal
===============
*/
static void CG_RagdollAddSkeletal( cgRagdoll_t *rag, centity_t *cent, int renderfx, float shadowPlane ) {
	const char	*off = NULL;

	if ( rag->cut == RAG_CUT_BISECT ) {
		off = "spine_01";
		CG_RagdollSkelPiece( rag, cent, RB_CHEST, RP_CHEST, 1, off, qtrue, renderfx, shadowPlane );
	} else if ( rag->cut == RAG_CUT_DECAP ) {
		off = "neck_01";
		CG_RagdollSkelPiece( rag, cent, RB_NECK, RS_NECK, 1, off, qtrue, renderfx, shadowPlane );
	} else if ( rag->cut == RAG_CUT_LIMB ) {
		off = ragBones[ragLimbBone[rag->limb]].joint;
		CG_RagdollSkelPiece( rag, cent, ragLimbBone[rag->limb], RS_LIMB, 1, off, qtrue, renderfx, shadowPlane );
	}
	CG_RagdollSkelPiece( rag, cent, RB_PELVIS, RP_PELVIS, 0, off, qfalse, renderfx, shadowPlane );
}

/*
===============
CG_RagdollAdd

Intercept point for CG_Player: if cg_ragdoll is on and the entity is dead,
simulate + render the ragdoll and return qtrue so the caller skips the stock
animated player. Returns qfalse to fall through to normal rendering.
===============
*/
qboolean CG_RagdollAdd( centity_t *cent, int renderfx, float shadowPlane ) {
	cgRagdoll_t		*rag;
	clientInfo_t	*ci;
	int				num;
	int				cut;
	vec3_t			d;

	if ( !cg_ragdoll.integer ) {
		return qfalse;
	}
	if ( !( cent->currentState.eFlags & EF_DEAD ) ) {
		// alive again -> retire any corpse on this slot so the next death reseeds
		cg_ragdolls[ cent->currentState.number ].active = qfalse;
		return qfalse;
	}

	num = cent->currentState.number;
	rag = &cg_ragdolls[ num ];
	ci = &cgs.clientinfo[ cent->currentState.clientNum ];
	if ( !ci->infoValid ) {
		return qfalse;
	}

	// the blade cut tag rides in entityState.time2 (0 = none); it survives the
	// copy into the body queue, so a sliced corpse stays sliced after respawn
	cut = cent->currentState.time2;

	// (re)seed on a fresh death, or if the slot was recycled for a body that
	// teleported far from where we last simulated it
	if ( !rag->active ) {
		// a body-queue corpse carries on from the ragdoll its owner left behind
		const cgRagdoll_t	*from = &cg_ragdolls[ cent->currentState.clientNum ];

		VectorSubtract( cent->lerpOrigin, from->pos[RP_PELVIS], d );
		if ( num >= MAX_CLIENTS && from->startTime && from->cut == cut && VectorLengthSquared( d ) < 256.0f * 256.0f ) {
			*rag = *from;
			rag->active = qtrue;
		} else if ( cg_skeletal.integer && cgs.media.skelModel ) {
			CG_RagdollSeedSkel( rag, cent, cut );
		} else {
			CG_RagdollSeed( rag, cent, cut );
		}
	} else {
		VectorSubtract( cent->lerpOrigin, rag->pos[RP_FEET], d );
		if ( cg.time - rag->startTime < 50 && VectorLengthSquared( d ) > 200.0f * 200.0f ) {
			if ( cg_skeletal.integer && cgs.media.skelModel ) {
				CG_RagdollSeedSkel( rag, cent, cut );
			} else {
				CG_RagdollSeed( rag, cent, cut );
			}
		}
	}

	if ( !rag->settled ) {
		CG_RagdollStep( rag );
	} else {
		rag->lastTime = cg.time;	// keep dt sane if it wakes back up
	}

	if ( rag->skel ) {
		CG_RagdollAddSkeletal( rag, cent, renderfx, shadowPlane );
		return qtrue;
	}

	// legs always ride the lower spine (never spun -- they stay the grounded half)
	CG_RagdollSegment( rag, ci->legsModel, ci->legsSkin, RP_FEET, RP_FEET, RP_PELVIS, cent, renderfx, shadowPlane, 0 );

	if ( rag->cut == RAG_CUT_BISECT ) {
		// torso rides the detached upper half (chest -> head), separating from
		// the legs at the waist and tumbling as it goes
		CG_RagdollSegment( rag, ci->torsoModel, ci->torsoSkin, RP_CHEST, RP_CHEST, RP_HEAD, cent, renderfx, shadowPlane, rag->spin );
		CG_RagdollSegment( rag, ci->headModel,  ci->headSkin,  RP_HEAD,  RP_CHEST, RP_HEAD, cent, renderfx, shadowPlane, rag->spin );
	} else if ( rag->cut == RAG_CUT_DECAP ) {
		// torso stays with the body; the head tumbles free (from==to)
		CG_RagdollSegment( rag, ci->torsoModel, ci->torsoSkin, RP_PELVIS, RP_PELVIS, RP_CHEST, cent, renderfx, shadowPlane, 0 );
		CG_RagdollSegment( rag, ci->headModel,  ci->headSkin,  RP_HEAD,   RP_HEAD,   RP_HEAD,  cent, renderfx, shadowPlane, rag->spin );
	} else {
		// intact spine
		CG_RagdollSegment( rag, ci->torsoModel, ci->torsoSkin, RP_PELVIS, RP_PELVIS, RP_CHEST, cent, renderfx, shadowPlane, 0 );
		CG_RagdollSegment( rag, ci->headModel,  ci->headSkin,  RP_HEAD,   RP_CHEST,  RP_HEAD,  cent, renderfx, shadowPlane, 0 );
	}

	return qtrue;
}

/*
===============
CG_RagdollWound

STRAFE 64: live world position of one spine particle of an active corpse, so a
blood geyser can ride a severed stump as the body (or each sliced half) tumbles
and falls. `part` is a RAG_PART_* index. Returns qfalse if no corpse is
simulating on that entity slot (caller keeps its last origin).
===============
*/
qboolean CG_RagdollWound( int entnum, int part, vec3_t out ) {
	cgRagdoll_t	*rag;

	if ( entnum < 0 || entnum >= MAX_GENTITIES ) {
		return qfalse;
	}
	rag = &cg_ragdolls[ entnum ];
	if ( part == RAG_PART_LIMB ) {			// the flying limb's own stump
		if ( !rag->active || !rag->limb ) {
			return qfalse;
		}
		VectorCopy( rag->pos[rag->skel ? RS_LIMB : RP_CHEST], out );
		return qtrue;
	}
	if ( rag->skel && rag->cut == RAG_CUT_LIMB && part == RAG_PART_CHEST ) {
		part = ragLimbRoot[rag->limb];		// the stump left on the body
		VectorCopy( rag->pos[part], out );
		return rag->active;
	}
	if ( part < 0 || part >= RP_COUNT ) {
		return qfalse;
	}
	if ( !rag->active ) {
		return qfalse;
	}
	VectorCopy( rag->pos[ part ], out );
	return qtrue;
}
