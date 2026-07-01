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
// g_weapon.c 
// perform the server side effects of a weapon firing

#include "g_local.h"

static	float	s_quadFactor;
static	vec3_t	forward, right, up;
static	vec3_t	muzzle;

// STRAFE 64: the old hitscan guns are now visible, deflectable bolts. Travel
// speeds (ups) — fast enough to threaten, slow enough to read and parry.
#define	BULLET_SPEED_MG		6000
#define	BULLET_SPEED_SG		5200
#define	BULLET_SPEED_LG		8000
#define	BULLET_SPEED_RG		12000

#define NUM_NAILSHOTS 15

/*
================
G_BounceProjectile
================
*/
void G_BounceProjectile( vec3_t start, vec3_t impact, vec3_t dir, vec3_t endout ) {
	vec3_t v, newv;
	float dot;

	VectorSubtract( impact, start, v );
	dot = DotProduct( v, dir );
	VectorMA( v, -2*dot, dir, newv );

	VectorNormalize(newv);
	VectorMA(impact, 8192, newv, endout);
}


/*
======================================================================

GAUNTLET

======================================================================
*/

void Weapon_Gauntlet( gentity_t *ent ) {

}

/*
===============
CheckGauntletAttack
===============
*/
qboolean CheckGauntletAttack( gentity_t *ent ) {
	trace_t		tr;
	vec3_t		end;
	gentity_t	*tent;
	gentity_t	*traceEnt;
	int			damage;

	// set aiming directions
	AngleVectors (ent->client->ps.viewangles, forward, right, up);

	CalcMuzzlePoint ( ent, forward, right, up, muzzle );

	VectorMA (muzzle, 48, forward, end);	// Slipstream: longer reach so a fast pass-by connects

	trap_Trace (&tr, muzzle, NULL, NULL, end, ent->s.number, MASK_SHOT);
	if ( tr.surfaceFlags & SURF_NOIMPACT ) {
		return qfalse;
	}

	if ( ent->client->noclip ) {
		return qfalse;
	}

	traceEnt = &g_entities[ tr.entityNum ];

	// send blood impact
	if ( traceEnt->takedamage && traceEnt->client ) {
		tent = G_TempEntity( tr.endpos, EV_MISSILE_HIT );
		tent->s.otherEntityNum = traceEnt->s.number;
		tent->s.eventParm = DirToByte( tr.plane.normal );
		tent->s.weapon = ent->s.weapon;
	}

	if ( !traceEnt->takedamage) {
		return qfalse;
	}

	if (ent->client->ps.powerups[PW_QUAD] ) {
		G_AddEvent( ent, EV_POWERUP_QUAD, 0 );
		s_quadFactor = g_quadfactor.value;
	} else {
		s_quadFactor = 1;
	}
#ifdef MISSIONPACK
	if( ent->client->persistantPowerup && ent->client->persistantPowerup->item && ent->client->persistantPowerup->item->giTag == PW_DOUBLER ) {
		s_quadFactor *= 2;
	}
#endif

	damage = 50 * s_quadFactor;
	G_Damage( traceEnt, ent, ent, forward, tr.endpos,
		damage, 0, MOD_GAUNTLET );

	return qtrue;
}


/*
======================================================================

SWORD

A fluid melee blade. Each swing is a horizontal arc sweep (several traces
fanned across the view) so a slash can catch multiple foes and feels like a
real cut rather than a point poke. Damage scales with the player's horizontal
speed — the same "speed = lethality" rule as the vectorgun — so blade work
rewards staying in the movement chain. A lethal blow triggers dismemberment.

======================================================================
*/
#define	SWORD_RANGE			80		// reach in units — longer than the Slipstream poke
#define	SWORD_ARC			28.0f	// half-angle of the swing sweep, degrees
#define	SWORD_NUM_TRACES	5		// traces fanned across the arc
#define	SWORD_DMG_MIN		45		// damage at a standstill
#define	SWORD_DMG_MAX		120		// damage at full chain speed
#define	SWORD_SPEED_CAP		960.0f	// horizontal speed that pegs max damage
// STRAFE 64 flow loop — the Slice phase: the cut aligns to where you FLY, not
// where you look, and a clean kill feeds the chain (enemies as momentum gates).
#define	SWORD_VEL_ALIGN		500.0f	// speed at which the cut fully aligns to your flight path
#define	SWORD_RANGE_BONUS	80		// extra reach at full speed so a fast fly-by still connects
#define	SWORD_KILL_SPEED	140.0f	// forward kick per clean kill — routing THROUGH is the fast line
#define	SWORD_CLEAVE_KICK	340.0f	// base launch (ups) flung along the cut when a swing catches
									// 2+ bodies, or on a finisher — scaled by g_swordKnockback
#define	SWORD_CHAIN_RANGE	700.0f	// how far the on-kill kick looks for the next body to flow toward

/*
================
G_SwordFindTarget

STRAFE 64 sword flow assist. Find the best enemy body in a cone: a living entity
that takes damage (a client, or a slice gate), within `range` of `from` and within
`cosHalf` of `axis`. With byAngle set, the best-*aligned* target wins — used for
aim-snap, bending the cut onto a near-miss. Otherwise the *nearest* wins — used for
the kill-to-kill redirect, kicking toward the next body. NULL when the cone is
empty. `self` is always skipped.
================
*/
static gentity_t *G_SwordFindTarget( gentity_t *self, const vec3_t from,
		const vec3_t axis, float range, float cosHalf, qboolean byAngle ) {
	gentity_t	*t, *best;
	vec3_t		delta, dir;
	float		dist, dot, score, bestScore;
	int			i;

	best = NULL;
	bestScore = byAngle ? cosHalf : range;		// must beat the cone/range to count

	for ( i = 0 ; i < level.num_entities ; i++ ) {
		t = &g_entities[i];
		if ( t == self || !t->inuse || !t->takedamage || t->health <= 0 ) {
			continue;
		}
		if ( !t->client && !( t->flags & FL_SLICE_GATE ) ) {
			continue;
		}
		VectorSubtract( t->r.currentOrigin, from, delta );
		dist = VectorLength( delta );
		if ( dist < 1.0f || dist > range ) {
			continue;
		}
		VectorScale( delta, 1.0f / dist, dir );
		dot = DotProduct( dir, axis );
		if ( dot < cosHalf ) {
			continue;							// outside the cone
		}
		score = byAngle ? dot : dist;
		if ( byAngle ? ( score > bestScore ) : ( score < bestScore ) ) {
			bestScore = score;
			best = t;
		}
	}
	return best;
}

void Weapon_Sword( gentity_t *ent ) {
	int			i, t;
	int			damage;
	float		speed, frac, arc, combomul, range, blend, spread;
	int			ntraces, step;
	int			startQuad, endQuad;
	float		sr, su, er, eu;
	qboolean	finisher;
	vec3_t		dir, end, sliceAngles, velDir, viewDir, axis;
	vec3_t		swFwd, swRight, swUp;
	trace_t		tr;
	gentity_t	*traceEnt, *tent;
	int			hit[ SWORD_NUM_TRACES * 2 ];	// entities already damaged this swing
	int			numHit, kills;

	// --- combo: chained swings within the window ramp damage; every 3rd is a
	// wide, heavy finisher. The combo decays if you stop swinging. ---
	if ( level.time - ent->client->swordComboTime > SWORD_COMBO_WINDOW ) {
		ent->client->swordComboStep = 0;
	} else {
		ent->client->swordComboStep++;
	}
	ent->client->swordComboTime = level.time;
	step = ent->client->swordComboStep % 3;
	finisher = ( step == 2 );

	combomul = 1.0f + 0.15f * step;			// +0,15,30% across the 3-hit chain
	arc = SWORD_ARC;
	ntraces = SWORD_NUM_TRACES;
	if ( finisher ) {
		combomul += 0.25f;					// heavy closer
		arc = SWORD_ARC * 1.6f;				// sweeping wide cut
		ntraces = SWORD_NUM_TRACES * 2 - 1;	// denser fan so the wide arc connects
	}

	// speed-scaled damage: standing still barely scratches, a full-speed
	// pass-by cleaves
	speed = sqrt( ent->client->ps.velocity[0] * ent->client->ps.velocity[0]
		+ ent->client->ps.velocity[1] * ent->client->ps.velocity[1] );
	if ( speed > SWORD_SPEED_CAP ) {
		speed = SWORD_SPEED_CAP;
	}
	frac = speed / SWORD_SPEED_CAP;
	damage = (int)( ( SWORD_DMG_MIN + frac * ( SWORD_DMG_MAX - SWORD_DMG_MIN ) ) * combomul );
	damage *= s_quadFactor;

	numHit = 0;
	kills = 0;

	// --- the slice axis: where you LOOK at a standstill, where you FLY at speed.
	// The faster you move, the more the cut aligns to your flight path, so you
	// "kill what you fly through" — speed is the targeting computer. View still
	// biases the centre line, giving the small aim-cone for adjustment. ---
	AngleVectors( ent->client->ps.viewangles, viewDir, NULL, NULL );
	VectorCopy( ent->client->ps.velocity, velDir );
	blend = ( VectorNormalize( velDir ) > 1.0f ) ? ( speed / SWORD_VEL_ALIGN ) : 0.0f;
	if ( blend > 1.0f ) {
		blend = 1.0f;
	}
	for ( i = 0 ; i < 3 ; i++ ) {
		axis[i] = viewDir[i] + ( velDir[i] - viewDir[i] ) * blend;
	}
	if ( VectorNormalize( axis ) == 0.0f ) {
		VectorCopy( viewDir, axis );
	}

	// --- AIM-SNAP: bend the cut onto a near-miss enemy so slightly-off aim still
	// connects clean. Only assists misses within g_swordAimSnap degrees of the cut
	// line — never a hard turn — and nudges the blade, not the camera. ---
	if ( g_swordAimSnap.value > 0.0f ) {
		float		snapCos = cos( DEG2RAD( g_swordAimSnap.value ) );
		float		reach = SWORD_RANGE + SWORD_RANGE_BONUS;
		gentity_t	*snap = G_SwordFindTarget( ent, muzzle, axis, reach, snapCos, qtrue );
		if ( snap ) {
			VectorSubtract( snap->r.currentOrigin, muzzle, axis );
			if ( VectorNormalize( axis ) == 0.0f ) {
				VectorCopy( viewDir, axis );
			}
		}
	}

	vectoangles( axis, sliceAngles );

	// reach grows with speed so a fast fly-by still connects the cut
	range = SWORD_RANGE + frac * SWORD_RANGE_BONUS;

	// --- DIRECTIONAL SWEEP (OpenJK-style): the swing is a discrete move that
	// travels the blade from a start quadrant to an end quadrant. We sample the
	// blade along that screen-space arc, so a diagonal kesa-giri cuts on the
	// diagonal and an overhead cuts top-to-bottom — not just a flat yaw fan. The
	// samples cover the swept volume, so a fast swing can't slip between traces. ---
	startQuad = SWORD_START_QUAD( ent->client->swordSwingParm );
	endQuad   = SWORD_END_QUAD( ent->client->swordSwingParm );
	BG_SwordQuadDir( startQuad, &sr, &su );
	BG_SwordQuadDir( endQuad,   &er, &eu );

	// basis: forward = the slice/flight axis, right/up span the swing plane
	AngleVectors( sliceAngles, swFwd, swRight, swUp );
	// how far off the forward axis the blade tip swings (tangent of the arc half-angle)
	spread = tan( DEG2RAD( arc ) );

	// sweep the blade across the start->end screen line, one trace per step
	for ( t = 0 ; t < ntraces ; t++ ) {
		float	a = ( ntraces > 1 ) ? (float)t / ( ntraces - 1 ) : 0.5f;
		float	dx = sr + ( er - sr ) * a;		// screen-space pos along the arc
		float	dy = su + ( eu - su ) * a;

		for ( i = 0 ; i < 3 ; i++ ) {
			dir[i] = swFwd[i] + spread * ( swRight[i] * dx + swUp[i] * dy );
		}
		VectorNormalize( dir );
		VectorMA( muzzle, range, dir, end );

		trap_Trace( &tr, muzzle, NULL, NULL, end, ent->s.number, MASK_SHOT );
		if ( tr.surfaceFlags & SURF_NOIMPACT ) {
			continue;
		}
		if ( ent->client->noclip ) {
			continue;
		}
		if ( tr.entityNum == ENTITYNUM_NONE || tr.entityNum == ENTITYNUM_WORLD ) {
			continue;
		}

		traceEnt = &g_entities[ tr.entityNum ];
		if ( !traceEnt->takedamage ) {
			continue;
		}

		// MIN-RANGE: too close to get the blade moving — a cut needs room, so an
		// enemy in your face whiffs and you must step to distance (kills the
		// occupy-the-same-voxel ram). Speed-gated: a fast fly-by ignores it, since
		// at flow speed you're passing through, not standing on them.
		if ( g_swordMinRange.value > 0.0f && speed < SWORD_VEL_ALIGN
				&& tr.fraction * range < g_swordMinRange.value ) {
			continue;
		}

		// only hit each entity once per swing
		for ( i = 0 ; i < numHit ; i++ ) {
			if ( hit[i] == tr.entityNum ) {
				break;
			}
		}
		if ( i < numHit ) {
			continue;
		}
		hit[ numHit++ ] = tr.entityNum;

		// blood spurt at the contact point (reuses the missile-hit flesh fx) —
		// slice gates burst the same way so cutting one reads as a kill
		if ( traceEnt->client || ( traceEnt->flags & FL_SLICE_GATE ) ) {
			tent = G_TempEntity( tr.endpos, EV_MISSILE_HIT );
			tent->s.otherEntityNum = traceEnt->s.number;
			tent->s.eventParm = DirToByte( tr.plane.normal );
			tent->s.weapon = ent->s.weapon;
		}

		// drive the knockback along the cut so victims are flung off the blade
		G_Damage( traceEnt, ent, ent, dir, tr.endpos,
			damage, 0, MOD_SWORD );

		// a clean kill on an enemy you flew through is a momentum waypoint —
		// player or slice gate, either feeds the chain below
		if ( traceEnt->health <= 0
			&& ( traceEnt->client || ( traceEnt->flags & FL_SLICE_GATE ) ) ) {
			kills++;
		}
	}

	// --- enemies are momentum gates: a clean kill at speed FEEDS the chain.
	// A forward kick along your line keeps the pass-through fast, and refunded
	// air moves let a mid-air slice stay airborne to chain into the next space —
	// so routing THROUGH the cluster is the fast line, not an interruption. ---
	if ( kills > 0 && speed > 1.0f ) {
		gentity_t	*nextTgt = NULL;

		// aim the kick at the NEXT nearest body so clearing a cluster reads as one
		// flowing line instead of a shove straight ahead; fall back to the flight
		// line when the room is empty.
		if ( g_swordChainRedirect.integer ) {
			nextTgt = G_SwordFindTarget( ent, ent->r.currentOrigin, axis,
				SWORD_CHAIN_RANGE, -1.0f, qfalse );
		}
		if ( nextTgt ) {
			VectorSubtract( nextTgt->r.currentOrigin, ent->r.currentOrigin, velDir );
		} else {
			VectorCopy( ent->client->ps.velocity, velDir );
		}
		velDir[2] = 0;
		if ( VectorNormalize( velDir ) > 0.0f ) {
			float add = SWORD_KILL_SPEED * kills * frac;	// gated by entry speed
			ent->client->ps.velocity[0] += velDir[0] * add;
			ent->client->ps.velocity[1] += velDir[1] * add;
		}
		// refund air mobility + keep the flow chain awake through the kill
		ent->client->ps.stats[STAT_WALLJUMP_COUNT] = 0;
		ent->client->ps.stats[STAT_AIRJUMP_COUNT] = 0;
		ent->client->ps.stats[STAT_GROUND_MS] = 0;
		if ( ent->client->ps.stats[STAT_BHOP_STREAK] < 1 ) {
			ent->client->ps.stats[STAT_BHOP_STREAK] = 1;
		}
	}

	// --- CLEAVE LAUNCH: catching two or more bodies in one swing (or landing
	// the heavy finisher) FLINGS them off the blade along the cut line. A
	// multi-hit reads as a single heavy, dynamic blow — bodies launched the way
	// you swept — instead of a quiet multi-tag. Scaled live by g_swordKnockback
	// and by how many it caught; the finisher hits hardest. ---
	if ( ( numHit >= 2 || finisher ) && g_swordKnockback.value > 0.0f ) {
		vec3_t	launch;
		float	kick = SWORD_CLEAVE_KICK * g_swordKnockback.value;

		kick *= 1.0f + 0.20f * ( numHit - 1 );		// more bodies caught, bigger fling
		if ( finisher ) {
			kick *= 1.4f;							// the closer launches hardest
		}

		VectorCopy( axis, launch );					// fling along the slice/flight line
		launch[2] = 0.0f;
		if ( VectorNormalize( launch ) == 0.0f ) {
			VectorCopy( viewDir, launch );
		}
		for ( i = 0 ; i < numHit ; i++ ) {
			gentity_t *vic = &g_entities[ hit[i] ];
			if ( !vic->client ) {
				continue;
			}
			VectorMA( vic->client->ps.velocity, kick, launch, vic->client->ps.velocity );
			vic->client->ps.velocity[2] += kick * 0.45f;	// loft them off their feet
		}
	}

	// blade connected: tell the attacker so the client lands an impact "chunk"
	// + view punch (heavier on a finisher). This is what makes hacking bite.
	if ( numHit > 0 ) {
		G_AddEvent( ent, EV_SWORD_HIT, finisher ? SWORDHIT_FINISHER : SWORDHIT_NORMAL );

		// WHIFF PUNISH: a CONNECTING hit refunds recovery toward the fast value, so
		// a landed cut flows straight into the next while a MISS eats the full
		// committed recovery pmove set. This is what makes whiffing the exposed
		// choice — the core of the neutral game. g_swordWhiffScale 0 disables.
		if ( g_swordWhiffScale.value > 0.0f ) {
			int	floorT = (int)pm_swordRecoveryMin;
			int	refund = (int)( ( pm_swordRecovery - pm_swordRecoveryMin ) * g_swordWhiffScale.value );

			if ( ent->client->ps.weaponTime - refund > floorT ) {
				ent->client->ps.weaponTime -= refund;
			} else if ( ent->client->ps.weaponTime > floorT ) {
				ent->client->ps.weaponTime = floorT;
			}
		}
	}
}


/*
======================================================================

MACHINEGUN

======================================================================
*/

/*
======================
SnapVectorTowards

Round a vector to integers for more efficient network
transmission, but make sure that it rounds towards a given point
rather than blindly truncating.  This prevents it from truncating 
into a wall.
======================
*/
void SnapVectorTowards( vec3_t v, vec3_t to ) {
	int		i;

	for ( i = 0 ; i < 3 ; i++ ) {
		if ( to[i] <= v[i] ) {
			v[i] = floor(v[i]);
		} else {
			v[i] = ceil(v[i]);
		}
	}
}

#ifdef MISSIONPACK
#define CHAINGUN_SPREAD		600
#define CHAINGUN_DAMAGE		7
#endif
#define MACHINEGUN_SPREAD	200
#define	MACHINEGUN_DAMAGE	7
#define	MACHINEGUN_TEAM_DAMAGE	5		// wimpier MG in teamplay

void Bullet_Fire (gentity_t *ent, float spread, int damage, int mod ) {
	vec3_t		end, dir;
	float		r, u;

	damage *= s_quadFactor;

	// speed = accuracy: spread tightens as the attacker moves faster
	if ( g_speedDamage.integer && ent->client ) {
		float	speed, baseline;

		baseline = g_speed.value > 0 ? g_speed.value : 320;
		speed = sqrt( ent->client->ps.velocity[0] * ent->client->ps.velocity[0]
			+ ent->client->ps.velocity[1] * ent->client->ps.velocity[1] );
		if ( speed > baseline ) {
			spread *= baseline / speed;
		}
	}

	// scatter the aim by the spread, then throw a deflectable bolt down it
	r = random() * M_PI * 2.0f;
	u = sin(r) * crandom() * spread * 16;
	r = cos(r) * crandom() * spread * 16;
	VectorMA (muzzle, 8192*16, forward, end);
	VectorMA (end, r, right, end);
	VectorMA (end, u, up, end);
	VectorSubtract( end, muzzle, dir );

	fire_bullet( ent, muzzle, dir, damage, BULLET_SPEED_MG, mod, ent->s.weapon );
}


/*
======================================================================

BFG

======================================================================
*/

void BFG_Fire ( gentity_t *ent ) {
	gentity_t	*m;

	m = fire_bfg (ent, muzzle, forward);
	m->damage *= s_quadFactor;
	m->splashDamage *= s_quadFactor;

//	VectorAdd( m->s.pos.trDelta, ent->client->ps.velocity, m->s.pos.trDelta );	// "real" physics
}


/*
======================================================================

SHOTGUN

======================================================================
*/

// DEFAULT_SHOTGUN_SPREAD and DEFAULT_SHOTGUN_COUNT	are in bg_public.h, because
// client predicts same spreads
#define	DEFAULT_SHOTGUN_DAMAGE	10

qboolean ShotgunPellet( vec3_t start, vec3_t end, gentity_t *ent ) {
	trace_t		tr;
	int			damage, i, passent;
	gentity_t	*traceEnt;
#ifdef MISSIONPACK
	vec3_t		impactpoint, bouncedir;
#endif
	vec3_t		tr_start, tr_end;
	qboolean	hitClient = qfalse;

	passent = ent->s.number;
	VectorCopy( start, tr_start );
	VectorCopy( end, tr_end );
	for (i = 0; i < 10; i++) {
		trap_Trace (&tr, tr_start, NULL, NULL, tr_end, passent, MASK_SHOT);
		traceEnt = &g_entities[ tr.entityNum ];

		// send bullet impact
		if (  tr.surfaceFlags & SURF_NOIMPACT ) {
			return qfalse;
		}

		if ( traceEnt->takedamage) {
			damage = DEFAULT_SHOTGUN_DAMAGE * s_quadFactor;
#ifdef MISSIONPACK
			if ( traceEnt->client && traceEnt->client->invulnerabilityTime > level.time ) {
				if (G_InvulnerabilityEffect( traceEnt, forward, tr.endpos, impactpoint, bouncedir )) {
					G_BounceProjectile( tr_start, impactpoint, bouncedir, tr_end );
					VectorCopy( impactpoint, tr_start );
					// the player can hit him/herself with the bounced rail
					passent = ENTITYNUM_NONE;
				}
				else {
					VectorCopy( tr.endpos, tr_start );
					passent = traceEnt->s.number;
				}
				continue;
			}
#endif
			if( LogAccuracyHit( traceEnt, ent ) ) {
				hitClient = qtrue;
			}
			G_Damage( traceEnt, ent, ent, forward, tr.endpos, damage, 0, MOD_SHOTGUN);
			return hitClient;
		}
		return qfalse;
	}
	return qfalse;
}

// this should match CG_ShotgunPattern
void ShotgunPattern( vec3_t origin, vec3_t origin2, int seed, gentity_t *ent ) {
	int			i;
	float		r, u;
	vec3_t		end;
	vec3_t		localForward, localRight, localUp;
	qboolean	hitClient = qfalse;

	// derive the right and up vectors from the forward vector, because
	// the client won't have any other information
	VectorNormalize2( origin2, localForward );
	PerpendicularVector( localRight, localForward );
	CrossProduct( localForward, localRight, localUp );

	// generate the "random" spread pattern
	for ( i = 0 ; i < DEFAULT_SHOTGUN_COUNT ; i++ ) {
		r = Q_crandom( &seed ) * DEFAULT_SHOTGUN_SPREAD * 16;
		u = Q_crandom( &seed ) * DEFAULT_SHOTGUN_SPREAD * 16;
		VectorMA( origin, 8192 * 16, localForward, end);
		VectorMA (end, r, localRight, end);
		VectorMA (end, u, localUp, end);
		if( ShotgunPellet( origin, end, ent ) && !hitClient ) {
			hitClient = qtrue;
			ent->client->accuracy_hits++;
		}
	}
}


void weapon_supershotgun_fire (gentity_t *ent) {
	int		i, seed;
	float	r, u;
	vec3_t	end, dir;

	// a fan of deflectable pellet bolts instead of a hitscan pattern
	seed = rand();
	for ( i = 0 ; i < DEFAULT_SHOTGUN_COUNT ; i++ ) {
		r = Q_crandom( &seed ) * DEFAULT_SHOTGUN_SPREAD * 16;
		u = Q_crandom( &seed ) * DEFAULT_SHOTGUN_SPREAD * 16;
		VectorMA( muzzle, 8192 * 16, forward, end );
		VectorMA( end, r, right, end );
		VectorMA( end, u, up, end );
		VectorSubtract( end, muzzle, dir );
		fire_bullet( ent, muzzle, dir, DEFAULT_SHOTGUN_DAMAGE * s_quadFactor,
			BULLET_SPEED_SG, MOD_SHOTGUN, WP_SHOTGUN );
	}
}


/*
======================================================================

GRENADE LAUNCHER

======================================================================
*/

void weapon_grenadelauncher_fire (gentity_t *ent) {
	gentity_t	*m;

	// extra vertical velocity
	forward[2] += 0.2f;
	VectorNormalize( forward );

	m = fire_grenade (ent, muzzle, forward);
	m->damage *= s_quadFactor;
	m->splashDamage *= s_quadFactor;

//	VectorAdd( m->s.pos.trDelta, ent->client->ps.velocity, m->s.pos.trDelta );	// "real" physics
}

/*
======================================================================

ROCKET

======================================================================
*/

void Weapon_RocketLauncher_Fire (gentity_t *ent) {
	gentity_t	*m;

	m = fire_rocket (ent, muzzle, forward);
	m->damage *= s_quadFactor;
	m->splashDamage *= s_quadFactor;

//	VectorAdd( m->s.pos.trDelta, ent->client->ps.velocity, m->s.pos.trDelta );	// "real" physics
}


/*
======================================================================

PLASMA GUN

======================================================================
*/

void Weapon_Plasmagun_Fire (gentity_t *ent) {
	gentity_t	*m;

	m = fire_plasma (ent, muzzle, forward);
	m->damage *= s_quadFactor;
	m->splashDamage *= s_quadFactor;

//	VectorAdd( m->s.pos.trDelta, ent->client->ps.velocity, m->s.pos.trDelta );	// "real" physics
}

/*
======================================================================

RAILGUN

======================================================================
*/


/*
=================
weapon_railgun_fire
=================
*/
void weapon_railgun_fire (gentity_t *ent) {
	// a single very fast, heavy slug — still a projectile you can dodge or
	// parry. No more pierce-through; accuracy is tracked at impact.
	fire_bullet( ent, muzzle, forward, 100 * s_quadFactor,
		BULLET_SPEED_RG, MOD_RAILGUN, WP_RAILGUN );
}


/*
======================================================================

GRAPPLING HOOK

======================================================================
*/

void Weapon_GrapplingHook_Fire (gentity_t *ent)
{
	if (!ent->client->fireHeld && !ent->client->hook)
		fire_grapple (ent, muzzle, forward);

	ent->client->fireHeld = qtrue;
}

void Weapon_HookFree (gentity_t *ent)
{
	ent->parent->client->hook = NULL;
	ent->parent->client->ps.pm_flags &= ~PMF_GRAPPLE_PULL;
	ent->parent->client->ps.stats[STAT_GRAPPLE_LEN] = 0;	// re-seed length next attach
	G_FreeEntity( ent );
}

void Weapon_HookThink (gentity_t *ent)
{
	if (ent->enemy) {
		vec3_t v, oldorigin;

		VectorCopy(ent->r.currentOrigin, oldorigin);
		v[0] = ent->enemy->r.currentOrigin[0] + (ent->enemy->r.mins[0] + ent->enemy->r.maxs[0]) * 0.5;
		v[1] = ent->enemy->r.currentOrigin[1] + (ent->enemy->r.mins[1] + ent->enemy->r.maxs[1]) * 0.5;
		v[2] = ent->enemy->r.currentOrigin[2] + (ent->enemy->r.mins[2] + ent->enemy->r.maxs[2]) * 0.5;
		SnapVectorTowards( v, oldorigin );	// save net bandwidth

		G_SetOrigin( ent, v );
	}

	VectorCopy( ent->r.currentOrigin, ent->parent->client->ps.grapplePoint);
}

/*
======================================================================

LIGHTNING GUN

======================================================================
*/

void Weapon_LightningFire( gentity_t *ent ) {
	gentity_t	*m;

	// a rapid stream of short-lived bolts (one per ~50ms fire) instead of a
	// hitscan beam — fast, close-range, and still a projectile you can parry
	m = fire_bullet( ent, muzzle, forward, 8 * s_quadFactor,
		BULLET_SPEED_LG, MOD_LIGHTNING, WP_LIGHTNING );
	m->nextthink = level.time + 150;	// short whip reach (~1200u)
}

#ifdef MISSIONPACK
/*
======================================================================

NAILGUN

======================================================================
*/

void Weapon_Nailgun_Fire (gentity_t *ent) {
	gentity_t	*m;
	int			count;

	for( count = 0; count < NUM_NAILSHOTS; count++ ) {
		m = fire_nail (ent, muzzle, forward, right, up );
		m->damage *= s_quadFactor;
		m->splashDamage *= s_quadFactor;
	}

//	VectorAdd( m->s.pos.trDelta, ent->client->ps.velocity, m->s.pos.trDelta );	// "real" physics
}


/*
======================================================================

PROXIMITY MINE LAUNCHER

======================================================================
*/

void weapon_proxlauncher_fire (gentity_t *ent) {
	gentity_t	*m;

	// extra vertical velocity
	forward[2] += 0.2f;
	VectorNormalize( forward );

	m = fire_prox (ent, muzzle, forward);
	m->damage *= s_quadFactor;
	m->splashDamage *= s_quadFactor;

//	VectorAdd( m->s.pos.trDelta, ent->client->ps.velocity, m->s.pos.trDelta );	// "real" physics
}

#endif

//======================================================================


/*
===============
LogAccuracyHit
===============
*/
qboolean LogAccuracyHit( gentity_t *target, gentity_t *attacker ) {
	if( !target->takedamage ) {
		return qfalse;
	}

	if ( target == attacker ) {
		return qfalse;
	}

	if( !target->client ) {
		return qfalse;
	}

	if( !attacker->client ) {
		return qfalse;
	}

	if( target->client->ps.stats[STAT_HEALTH] <= 0 ) {
		return qfalse;
	}

	if ( OnSameTeam( target, attacker ) ) {
		return qfalse;
	}

	return qtrue;
}


/*
===============
CalcMuzzlePoint

set muzzle location relative to pivoting eye
===============
*/
void CalcMuzzlePoint ( gentity_t *ent, vec3_t localForward, vec3_t localRight, vec3_t localUp, vec3_t muzzlePoint ) {
	VectorCopy( ent->s.pos.trBase, muzzlePoint );
	muzzlePoint[2] += ent->client->ps.viewheight;
	VectorMA( muzzlePoint, 14, localForward, muzzlePoint );
	// snap to integer coordinates for more efficient network bandwidth usage
	SnapVector( muzzlePoint );
}

/*
===============
CalcMuzzlePointOrigin

set muzzle location relative to pivoting eye
===============
*/
void CalcMuzzlePointOrigin ( gentity_t *ent, vec3_t origin, vec3_t localForward, vec3_t localRight, vec3_t localUp, vec3_t muzzlePoint ) {
	VectorCopy( ent->s.pos.trBase, muzzlePoint );
	muzzlePoint[2] += ent->client->ps.viewheight;
	VectorMA( muzzlePoint, 14, localForward, muzzlePoint );
	// snap to integer coordinates for more efficient network bandwidth usage
	SnapVector( muzzlePoint );
}



/*
===============
FireWeapon
===============
*/
void FireWeapon( gentity_t *ent ) {
	if (ent->client->ps.powerups[PW_QUAD] ) {
		s_quadFactor = g_quadfactor.value;
	} else {
		s_quadFactor = 1;
	}
#ifdef MISSIONPACK
	if( ent->client->persistantPowerup && ent->client->persistantPowerup->item && ent->client->persistantPowerup->item->giTag == PW_DOUBLER ) {
		s_quadFactor *= 2;
	}
#endif

	// track shots taken for accuracy tracking.  Grapple is not a weapon and gauntet is just not tracked
	if( ent->s.weapon != WP_GRAPPLING_HOOK && ent->s.weapon != WP_GAUNTLET ) {
#ifdef MISSIONPACK
		if( ent->s.weapon == WP_NAILGUN ) {
			ent->client->accuracy_shots += NUM_NAILSHOTS;
		} else {
			ent->client->accuracy_shots++;
		}
#else
		ent->client->accuracy_shots++;
#endif
	}

	// set aiming directions
	AngleVectors (ent->client->ps.viewangles, forward, right, up);

	CalcMuzzlePointOrigin ( ent, ent->client->oldOrigin, forward, right, up, muzzle );

	// fire the specific weapon
	switch( ent->s.weapon ) {
	case WP_GAUNTLET:
		Weapon_Gauntlet( ent );
		break;
	case WP_SWORD:
		Weapon_Sword( ent );
		break;
	case WP_LIGHTNING:
		Weapon_LightningFire( ent );
		break;
	case WP_SHOTGUN:
		weapon_supershotgun_fire( ent );
		break;
	case WP_MACHINEGUN:
		if ( g_gametype.integer != GT_TEAM ) {
			Bullet_Fire( ent, MACHINEGUN_SPREAD, MACHINEGUN_DAMAGE, MOD_MACHINEGUN );
		} else {
			Bullet_Fire( ent, MACHINEGUN_SPREAD, MACHINEGUN_TEAM_DAMAGE, MOD_MACHINEGUN );
		}
		break;
	case WP_GRENADE_LAUNCHER:
		weapon_grenadelauncher_fire( ent );
		break;
	case WP_ROCKET_LAUNCHER:
		Weapon_RocketLauncher_Fire( ent );
		break;
	case WP_PLASMAGUN:
		Weapon_Plasmagun_Fire( ent );
		break;
	case WP_RAILGUN:
		weapon_railgun_fire( ent );
		break;
	case WP_BFG:
		BFG_Fire( ent );
		break;
	case WP_GRAPPLING_HOOK:
		Weapon_GrapplingHook_Fire( ent );
		break;
#ifdef MISSIONPACK
	case WP_NAILGUN:
		Weapon_Nailgun_Fire( ent );
		break;
	case WP_PROX_LAUNCHER:
		weapon_proxlauncher_fire( ent );
		break;
	case WP_CHAINGUN:
		Bullet_Fire( ent, CHAINGUN_SPREAD, CHAINGUN_DAMAGE, MOD_CHAINGUN );
		break;
#endif
	default:
// FIXME		G_Error( "Bad ent->s.weapon" );
		break;
	}
}


#ifdef MISSIONPACK

/*
===============
KamikazeRadiusDamage
===============
*/
static void KamikazeRadiusDamage( vec3_t origin, gentity_t *attacker, float damage, float radius ) {
	float		dist;
	gentity_t	*ent;
	int			entityList[MAX_GENTITIES];
	int			numListedEntities;
	vec3_t		mins, maxs;
	vec3_t		v;
	vec3_t		dir;
	int			i, e;

	if ( radius < 1 ) {
		radius = 1;
	}

	for ( i = 0 ; i < 3 ; i++ ) {
		mins[i] = origin[i] - radius;
		maxs[i] = origin[i] + radius;
	}

	numListedEntities = trap_EntitiesInBox( mins, maxs, entityList, MAX_GENTITIES );

	for ( e = 0 ; e < numListedEntities ; e++ ) {
		ent = &g_entities[entityList[ e ]];

		if (!ent->takedamage) {
			continue;
		}

		// don't hit things we have already hit
		if( ent->kamikazeTime > level.time ) {
			continue;
		}

		// find the distance from the edge of the bounding box
		for ( i = 0 ; i < 3 ; i++ ) {
			if ( origin[i] < ent->r.absmin[i] ) {
				v[i] = ent->r.absmin[i] - origin[i];
			} else if ( origin[i] > ent->r.absmax[i] ) {
				v[i] = origin[i] - ent->r.absmax[i];
			} else {
				v[i] = 0;
			}
		}

		dist = VectorLength( v );
		if ( dist >= radius ) {
			continue;
		}

//		if( CanDamage (ent, origin) ) {
			VectorSubtract (ent->r.currentOrigin, origin, dir);
			// push the center of mass higher than the origin so players
			// get knocked into the air more
			dir[2] += 24;
			G_Damage( ent, NULL, attacker, dir, origin, damage, DAMAGE_RADIUS|DAMAGE_NO_TEAM_PROTECTION, MOD_KAMIKAZE );
			ent->kamikazeTime = level.time + 3000;
//		}
	}
}

/*
===============
KamikazeShockWave
===============
*/
static void KamikazeShockWave( vec3_t origin, gentity_t *attacker, float damage, float push, float radius ) {
	float		dist;
	gentity_t	*ent;
	int			entityList[MAX_GENTITIES];
	int			numListedEntities;
	vec3_t		mins, maxs;
	vec3_t		v;
	vec3_t		dir;
	int			i, e;

	if ( radius < 1 )
		radius = 1;

	for ( i = 0 ; i < 3 ; i++ ) {
		mins[i] = origin[i] - radius;
		maxs[i] = origin[i] + radius;
	}

	numListedEntities = trap_EntitiesInBox( mins, maxs, entityList, MAX_GENTITIES );

	for ( e = 0 ; e < numListedEntities ; e++ ) {
		ent = &g_entities[entityList[ e ]];

		// don't hit things we have already hit
		if( ent->kamikazeShockTime > level.time ) {
			continue;
		}

		// find the distance from the edge of the bounding box
		for ( i = 0 ; i < 3 ; i++ ) {
			if ( origin[i] < ent->r.absmin[i] ) {
				v[i] = ent->r.absmin[i] - origin[i];
			} else if ( origin[i] > ent->r.absmax[i] ) {
				v[i] = origin[i] - ent->r.absmax[i];
			} else {
				v[i] = 0;
			}
		}

		dist = VectorLength( v );
		if ( dist >= radius ) {
			continue;
		}

//		if( CanDamage (ent, origin) ) {
			VectorSubtract (ent->r.currentOrigin, origin, dir);
			dir[2] += 24;
			G_Damage( ent, NULL, attacker, dir, origin, damage, DAMAGE_RADIUS|DAMAGE_NO_TEAM_PROTECTION, MOD_KAMIKAZE );
			//
			dir[2] = 0;
			VectorNormalize(dir);
			if ( ent->client ) {
				ent->client->ps.velocity[0] = dir[0] * push;
				ent->client->ps.velocity[1] = dir[1] * push;
				ent->client->ps.velocity[2] = 100;
			}
			ent->kamikazeShockTime = level.time + 3000;
//		}
	}
}

/*
===============
KamikazeDamage
===============
*/
static void KamikazeDamage( gentity_t *self ) {
	int i;
	float t;
	gentity_t *ent;
	vec3_t newangles;

	self->count += 100;

	if (self->count >= KAMI_SHOCKWAVE_STARTTIME) {
		// shockwave push back
		t = self->count - KAMI_SHOCKWAVE_STARTTIME;
		KamikazeShockWave(self->s.pos.trBase, self->activator, 25, 400,	(int) (float) t * KAMI_SHOCKWAVE_MAXRADIUS / (KAMI_SHOCKWAVE_ENDTIME - KAMI_SHOCKWAVE_STARTTIME) );
	}
	//
	if (self->count >= KAMI_EXPLODE_STARTTIME) {
		// do our damage
		t = self->count - KAMI_EXPLODE_STARTTIME;
		KamikazeRadiusDamage( self->s.pos.trBase, self->activator, 400,	(int) (float) t * KAMI_BOOMSPHERE_MAXRADIUS / (KAMI_IMPLODE_STARTTIME - KAMI_EXPLODE_STARTTIME) );
	}

	// either cycle or kill self
	if( self->count >= KAMI_SHOCKWAVE_ENDTIME ) {
		G_FreeEntity( self );
		return;
	}
	self->nextthink = level.time + 100;

	// add earth quake effect
	newangles[0] = crandom() * 2;
	newangles[1] = crandom() * 2;
	newangles[2] = 0;
	for (i = 0; i < MAX_CLIENTS; i++)
	{
		ent = &g_entities[i];
		if (!ent->inuse)
			continue;
		if (!ent->client)
			continue;

		if (ent->client->ps.groundEntityNum != ENTITYNUM_NONE) {
			ent->client->ps.velocity[0] += crandom() * 120;
			ent->client->ps.velocity[1] += crandom() * 120;
			ent->client->ps.velocity[2] = 30 + random() * 25;
		}

		ent->client->ps.delta_angles[0] += ANGLE2SHORT(newangles[0] - self->movedir[0]);
		ent->client->ps.delta_angles[1] += ANGLE2SHORT(newangles[1] - self->movedir[1]);
		ent->client->ps.delta_angles[2] += ANGLE2SHORT(newangles[2] - self->movedir[2]);
	}
	VectorCopy(newangles, self->movedir);
}

/*
===============
G_StartKamikaze
===============
*/
void G_StartKamikaze( gentity_t *ent ) {
	gentity_t	*explosion;
	gentity_t	*te;
	vec3_t		snapped;

	// start up the explosion logic
	explosion = G_Spawn();

	explosion->s.eType = ET_EVENTS + EV_KAMIKAZE;
	explosion->eventTime = level.time;

	if ( ent->client ) {
		VectorCopy( ent->s.pos.trBase, snapped );
	}
	else {
		VectorCopy( ent->activator->s.pos.trBase, snapped );
	}
	SnapVector( snapped );		// save network bandwidth
	G_SetOrigin( explosion, snapped );

	explosion->classname = "kamikaze";
	explosion->s.pos.trType = TR_STATIONARY;

	explosion->kamikazeTime = level.time;

	explosion->think = KamikazeDamage;
	explosion->nextthink = level.time + 100;
	explosion->count = 0;
	VectorClear(explosion->movedir);

	trap_LinkEntity( explosion );

	if (ent->client) {
		//
		explosion->activator = ent;
		//
		ent->s.eFlags &= ~EF_KAMIKAZE;
		// nuke the guy that used it
		G_Damage( ent, ent, ent, NULL, NULL, 100000, DAMAGE_NO_PROTECTION, MOD_KAMIKAZE );
	}
	else {
		if ( !strcmp(ent->activator->classname, "bodyque") ) {
			explosion->activator = &g_entities[ent->activator->r.ownerNum];
		}
		else {
			explosion->activator = ent->activator;
		}
	}

	// play global sound at all clients
	te = G_TempEntity(snapped, EV_GLOBAL_TEAM_SOUND );
	te->r.svFlags |= SVF_BROADCAST;
	te->s.eventParm = GTS_KAMIKAZE;
}
#endif
