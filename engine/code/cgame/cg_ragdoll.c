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

typedef struct {
	int		a, b;			// particle indices
	float	rest;			// rest length
	float	stiff;			// 0..1 fraction of the correction applied per pass
} ragConstraint_t;

typedef struct {
	qboolean	active;
	int			startTime;
	int			lastTime;		// last sim time, to derive a stable dt
	vec3_t		pos[RP_COUNT];
	vec3_t		prev[RP_COUNT];
	float		rest[RP_COUNT];	// seed height above the feet, for re-seed sanity checks
	float		yaw;			// facing captured at death, for the forward axis
	qboolean	settled;		// all particles at rest -> stop simulating
	int			cut;			// blade cut tag (entityState.time2): 0 none,
								// 1 limb, 2 decap, 3 bisect
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
#define RAG_GRAVITY		800.0f		// matches g_gravity feel
#define RAG_SETTLE_VEL2	1.0f		// squared speed below which a particle is "stopped"
#define RAG_MAXSTEP		20.0f		// max units a particle may move per step (anti-explode)

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

/*
===============
CG_RagdollReset

Drop all corpses + pending cut flashes (level change / vid_restart).
===============
*/
void CG_RagdollReset( void ) {
	memset( cg_ragdolls, 0, sizeof( cg_ragdolls ) );
	memset( cg_swordCuts, 0, sizeof( cg_swordCuts ) );
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
		trap_R_AddPolyToScene( cgs.media.swordSlashShader, 4, verts );
	}
}

/*
===============
CG_RagdollSeed

Stand the spine up at the entity's feet and inherit its death velocity so the
corpse carries on from however it was moving when it died.
===============
*/
static void CG_RagdollSeed( cgRagdoll_t *rag, centity_t *cent, int cut ) {
	int		i, j;
	vec3_t	vel, fwd;
	float	dt;

	rag->active = qtrue;
	rag->settled = qfalse;
	rag->startTime = cg.time;
	rag->lastTime = cg.time;
	rag->yaw = cent->lerpAngles[YAW];
	rag->cut = cut;

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
			rag->prev[i][0] -= fwd[0] * 150.0f * dt;
			rag->prev[i][1] -= fwd[1] * 150.0f * dt;
			rag->prev[i][2] -= 110.0f * dt;
		}
		// lower body settles back the other way a touch
		rag->prev[RP_PELVIS][0] += fwd[0] * 40.0f * dt;
		rag->prev[RP_PELVIS][1] += fwd[1] * 40.0f * dt;
	} else if ( cut == RAG_CUT_DECAP ) {
		// head pops up and tumbles off
		rag->prev[RP_HEAD][0] -= fwd[0] * 90.0f * dt;
		rag->prev[RP_HEAD][1] -= fwd[1] * 90.0f * dt;
		rag->prev[RP_HEAD][2] -= 200.0f * dt;
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
static qboolean CG_RagdollCollide( vec3_t pos, vec3_t prev ) {
	trace_t	tr;
	vec3_t	mins, maxs, vel;
	float	into;

	VectorSet( mins, -RAG_HALFBOX, -RAG_HALFBOX, -RAG_HALFBOX );
	VectorSet( maxs,  RAG_HALFBOX,  RAG_HALFBOX,  RAG_HALFBOX );

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
	VectorScale( vel, 0.5f, vel );						// surface friction
	VectorSubtract( pos, vel, prev );

	// supported only if the surface can hold the body up (a floor, not a wall)
	return ( tr.plane.normal[2] > 0.3f );
}

/*
===============
CG_RagdollStep

One Verlet integration + constraint relaxation + collision pass. Uses a clamped
dt so frame hitches can't explode the sim.
===============
*/
static void CG_RagdollStep( cgRagdoll_t *rag ) {
	int		i, k, n;
	float	dt, damp;
	vec3_t	temp, delta;
	float	dist, diff;
	float	maxVel2;
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

	n = cg_ragdollIterations.integer;
	if ( n < 1 ) n = 1;
	if ( n > 16 ) n = 16;

	// integrate
	for ( i = 0 ; i < RP_COUNT ; i++ ) {
		VectorCopy( rag->pos[i], temp );
		// x += (x - xprev) * damp + g*dt^2
		VectorSubtract( rag->pos[i], rag->prev[i], delta );
		VectorScale( delta, damp, delta );
		VectorAdd( rag->pos[i], delta, rag->pos[i] );
		rag->pos[i][2] -= RAG_GRAVITY * dt * dt;
		VectorCopy( temp, rag->prev[i] );

		// clamp per-step displacement so one bad frame (hitch, deep
		// constraint correction, collision shove) can't fling a particle and
		// make the model spasm
		VectorSubtract( rag->pos[i], rag->prev[i], delta );
		dist = VectorLength( delta );
		if ( dist > RAG_MAXSTEP ) {
			VectorScale( delta, RAG_MAXSTEP / dist, delta );
			VectorAdd( rag->prev[i], delta, rag->pos[i] );
		}
	}

	// satisfy distance constraints (position-only; no collision inside the
	// loop -- running the contact response n times per frame was what made
	// grounded corpses jitter/explode)
	for ( k = 0 ; k < n ; k++ ) {
		const ragConstraint_t *c;
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
	for ( i = 0 ; i < RP_COUNT ; i++ ) {
		if ( CG_RagdollCollide( rag->pos[i], rag->prev[i] ) ) {
			grounded = qtrue;
		}
	}

	// rest detection: once everything is nearly stationary AND the body is
	// actually resting on the ground, stop simulating so settled corpses cost
	// nothing. The ground requirement is what stops a corpse from latching
	// "settled" at the apex of a toss (vertical velocity passes through zero)
	// and freezing in midair.
	maxVel2 = 0;
	for ( i = 0 ; i < RP_COUNT ; i++ ) {
		float v2;
		VectorSubtract( rag->pos[i], rag->prev[i], delta );
		v2 = DotProduct( delta, delta );
		if ( v2 > maxVel2 ) {
			maxVel2 = v2;
		}
	}
	if ( grounded && maxVel2 < RAG_SETTLE_VEL2 && cg.time - rag->startTime > 400 ) {
		rag->settled = qtrue;
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
		centity_t *cent, int renderfx, float shadowPlane ) {
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
		CG_RagdollSeed( rag, cent, cut );
	} else {
		VectorSubtract( cent->lerpOrigin, rag->pos[RP_FEET], d );
		if ( cg.time - rag->startTime < 50 && VectorLengthSquared( d ) > 200.0f * 200.0f ) {
			CG_RagdollSeed( rag, cent, cut );
		}
	}

	if ( !rag->settled ) {
		CG_RagdollStep( rag );
	} else {
		rag->lastTime = cg.time;	// keep dt sane if it wakes back up
	}

	// legs always ride the lower spine
	CG_RagdollSegment( rag, ci->legsModel, ci->legsSkin, RP_FEET, RP_FEET, RP_PELVIS, cent, renderfx, shadowPlane );

	if ( rag->cut == RAG_CUT_BISECT ) {
		// torso rides the detached upper half (chest -> head), separating from
		// the legs at the waist
		CG_RagdollSegment( rag, ci->torsoModel, ci->torsoSkin, RP_CHEST, RP_CHEST, RP_HEAD, cent, renderfx, shadowPlane );
		CG_RagdollSegment( rag, ci->headModel,  ci->headSkin,  RP_HEAD,  RP_CHEST, RP_HEAD, cent, renderfx, shadowPlane );
	} else if ( rag->cut == RAG_CUT_DECAP ) {
		// torso stays with the body; the head tumbles free (from==to)
		CG_RagdollSegment( rag, ci->torsoModel, ci->torsoSkin, RP_PELVIS, RP_PELVIS, RP_CHEST, cent, renderfx, shadowPlane );
		CG_RagdollSegment( rag, ci->headModel,  ci->headSkin,  RP_HEAD,   RP_HEAD,   RP_HEAD,  cent, renderfx, shadowPlane );
	} else {
		// intact spine
		CG_RagdollSegment( rag, ci->torsoModel, ci->torsoSkin, RP_PELVIS, RP_PELVIS, RP_CHEST, cent, renderfx, shadowPlane );
		CG_RagdollSegment( rag, ci->headModel,  ci->headSkin,  RP_HEAD,   RP_CHEST,  RP_HEAD,  cent, renderfx, shadowPlane );
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
	if ( part < 0 || part >= RP_COUNT ) {
		return qfalse;
	}
	rag = &cg_ragdolls[ entnum ];
	if ( !rag->active ) {
		return qfalse;
	}
	VectorCopy( rag->pos[ part ], out );
	return qtrue;
}
