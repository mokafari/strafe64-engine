// Copyright (C) STRAFE 64
//
// g_lattice.c -- LATTICE: last-pilot-alive battle royale.
//
// Three pilots, one shrinking arena, and a third opponent that is never on the
// scoreboard: the lattice. Every pilot lays a damaging speed-trail behind them
// as they carve. Touch a trail -- your own or a rival's -- and it chips your
// short health pool. The rising void collapses the safe floor from below
// (G_RunVoid, reused as-is). A heat ends when one pilot is left standing: you
// either got bodied or you ran your retreat into your own lattice.
//
// Design intent (the three magic numbers, all live cvars so they co-tune):
//   g_latticeHealth  -- short pool: a duel has rhythm, not a coinflip
//   g_latticeDamage  -- chip per contact tick; time-to-kill must beat collapse
//   g_latticeRadius  -- how thick the trail reads as a wall
// TTK should arrive at the finish together with the void: the collapse flushes
// a chipped, cornered pilot into the killing blow.
//
// Self-contained: all trail state lives in this file keyed by client number,
// so nothing is added to the networked playerState/gclient layout. The trail
// is drawn client-side (cg_lattice.c) off the same observed positions.

#include "g_local.h"

#define LATTICE_PTS			128		// trail points retained per pilot (a finite carving wall)
#define LATTICE_STEP		20.0f	// min distance (u) between stored points
#define LATTICE_HURT_MS		220		// min ms between chip ticks on one pilot
#define LATTICE_SELF_MS		700		// ignore your own trail younger than this (the bit you're laying now)

typedef struct {
	vec3_t	xyz;
	int		time;					// level.time the point was laid
} latticePoint_t;

typedef struct {
	latticePoint_t	pts[LATTICE_PTS];
	int				count;			// number of valid points (<= LATTICE_PTS)
	int				head;			// index of the next write (ring)
	int				nextHurt;		// level.time this pilot may be chipped again
} latticeTrail_t;

static latticeTrail_t	trails[MAX_CLIENTS];

/*
================
G_LatticeClientSpawn

Reset a pilot's trail on (re)spawn and hand them a short health pool. Combat in
LATTICE is a few hits, not one: a graze chips, a clean strike hurts, and you can
be winning a fight and still die to the floor or your own carve.
================
*/
void G_LatticeClientSpawn( gentity_t *ent ) {
	int			n;
	gclient_t	*client;

	if ( !ent->client ) {
		return;
	}
	n = ent - g_entities;
	if ( n < 0 || n >= MAX_CLIENTS ) {
		return;
	}

	trails[n].count = 0;
	trails[n].head = 0;
	trails[n].nextHurt = 0;

	if ( !g_lattice.integer ) {
		return;
	}

	client = ent->client;
	if ( client->sess.sessionTeam == TEAM_SPECTATOR ) {
		return;
	}

	// short pool, no spawn over-heal (vanilla ClientSpawn grants max+25)
	{
		int hp = g_latticeHealth.integer;
		if ( hp < 1 ) {
			hp = 60;
		}
		client->ps.stats[STAT_MAX_HEALTH] = hp;
		ent->health = client->ps.stats[STAT_HEALTH] = hp;
	}
}

/*
================
G_LatticeEmitTrail

Lay one trail point behind a live pilot. Called from ClientEndFrame, once per
server frame. Only emits when the pilot has carved far enough since the last
point, so a stationary pilot stops extending their lattice (the void punishes
standing still; the trail just records motion).
================
*/
void G_LatticeEmitTrail( gentity_t *ent ) {
	int			n, last;
	float		d;
	vec3_t		delta;
	latticeTrail_t	*t;

	if ( !g_lattice.integer || !ent->client || ent->health <= 0 ) {
		return;
	}
	if ( ent->client->sess.sessionTeam == TEAM_SPECTATOR ) {
		return;
	}
	if ( ent->client->ps.pm_type != PM_NORMAL ) {
		return;
	}
	n = ent - g_entities;
	if ( n < 0 || n >= MAX_CLIENTS ) {
		return;
	}
	t = &trails[n];

	// require minimum carve distance from the most-recent point
	if ( t->count > 0 ) {
		last = ( t->head - 1 + LATTICE_PTS ) % LATTICE_PTS;
		VectorSubtract( ent->r.currentOrigin, t->pts[last].xyz, delta );
		d = VectorLength( delta );
		if ( d < LATTICE_STEP ) {
			return;
		}
	}

	VectorCopy( ent->r.currentOrigin, t->pts[t->head].xyz );
	t->pts[t->head].time = level.time;
	t->head = ( t->head + 1 ) % LATTICE_PTS;
	if ( t->count < LATTICE_PTS ) {
		t->count++;
	}
}

/*
================
G_LatticeTouches

Is point O within radius r of pilot owner's trail? When self is true the freshest
points (the segment the pilot is currently laying around their own body) are
skipped so you don't instantly collide with your own tail.
================
*/
static qboolean G_LatticeTouches( int owner, const vec3_t o, float r2, qboolean self ) {
	int				i, idx;
	latticeTrail_t	*t = &trails[owner];
	vec3_t			d;

	for ( i = 0 ; i < t->count ; i++ ) {
		idx = ( t->head - 1 - i + LATTICE_PTS * 2 ) % LATTICE_PTS;
		if ( self && level.time - t->pts[idx].time < LATTICE_SELF_MS ) {
			continue;			// the part of the trail you're still inside
		}
		VectorSubtract( o, t->pts[idx].xyz, d );
		if ( DotProduct( d, d ) <= r2 ) {
			return qtrue;
		}
	}
	return qfalse;
}

/*
================
G_RunLattice

Per-frame: chip any pilot standing inside a trail. O(pilots^2 * points), trivial
for an FFA-3 heat. Damage is throttled per victim so contact is a steady chip,
not an instant delete -- the lattice is pressure, the strike is the burst.
================
*/
void G_RunLattice( void ) {
	int			i, j;
	gentity_t	*vic, *owner;
	float		radius, r2;

	if ( !g_lattice.integer ) {
		return;
	}

	radius = g_latticeRadius.value;
	if ( radius < 1 ) {
		radius = 40;
	}
	r2 = radius * radius;

	for ( i = 0 ; i < level.maxclients ; i++ ) {
		vic = &g_entities[i];
		if ( !vic->inuse || !vic->client || vic->health <= 0 ) {
			continue;
		}
		if ( vic->client->sess.sessionTeam == TEAM_SPECTATOR ) {
			continue;
		}
		if ( vic->client->ps.pm_type != PM_NORMAL ) {
			continue;
		}
		if ( level.time < trails[i].nextHurt ) {
			continue;		// still in the post-chip grace window
		}

		for ( j = 0 ; j < level.maxclients ; j++ ) {
			qboolean self = ( i == j );
			if ( !g_entities[j].inuse || !g_entities[j].client ) {
				continue;
			}
			if ( trails[j].count == 0 ) {
				continue;
			}
			if ( !G_LatticeTouches( j, vic->r.currentOrigin, r2, self ) ) {
				continue;
			}

			// contact. The trail owner gets the credit (your own lattice is a suicide).
			owner = self ? vic : &g_entities[j];
			G_Damage( vic, owner, owner, NULL, NULL,
				g_latticeDamage.integer, DAMAGE_NO_KNOCKBACK, MOD_LATTICE );
			trails[i].nextHurt = level.time + LATTICE_HURT_MS;
			break;			// one chip per victim per frame
		}
	}
}

/*
================
G_LatticeEliminate

A dead pilot does not respawn -- they drop to a free-fly spectator and watch the
rest of the heat. Returns qtrue if the pilot was eliminated this call.
================
*/
qboolean G_LatticeEliminate( gentity_t *ent ) {
	gclient_t	*client;

	if ( !g_lattice.integer || !ent->client ) {
		return qfalse;
	}
	client = ent->client;
	if ( client->sess.sessionTeam == TEAM_SPECTATOR ) {
		return qfalse;		// already out
	}

	trap_SendServerCommand( ent - g_entities, "cp \"ELIMINATED\"" );

	client->sess.sessionTeam = TEAM_SPECTATOR;
	client->sess.spectatorState = SPECTATOR_FREE;
	client->sess.spectatorClient = -1;
	ClientSpawn( ent );		// respawns as a floating spectator
	return qtrue;
}

/*
================
G_LatticeCheckWin

Count the pilots still flying. Once a heat has had two or more live pilots and
only one (or none) remains, the heat is over -- declare the champion. Returns
qtrue if the heat was resolved this call (caller should stop processing rules).
================
*/
qboolean G_LatticeCheckWin( void ) {
	int			i, alive, winner;
	gclient_t	*cl;

	if ( level.latticeEnded ) {
		return qtrue;		// already resolved, waiting on intermission
	}
	if ( level.warmupTime != 0 ) {
		return qfalse;		// not live yet
	}

	alive = 0;
	winner = -1;
	for ( i = 0 ; i < level.maxclients ; i++ ) {
		cl = &level.clients[i];
		if ( cl->pers.connected != CON_CONNECTED ) {
			continue;
		}
		if ( cl->sess.sessionTeam != TEAM_FREE ) {
			continue;
		}
		if ( g_entities[i].health <= 0 ) {
			continue;
		}
		alive++;
		winner = i;
	}

	if ( alive > level.latticeMaxPlayers ) {
		level.latticeMaxPlayers = alive;
	}

	// only a contest that actually had rivals can be won
	if ( level.latticeMaxPlayers < 2 ) {
		return qfalse;
	}
	if ( alive > 1 ) {
		return qfalse;
	}

	level.latticeEnded = qtrue;
	if ( winner >= 0 ) {
		trap_SendServerCommand( -1, va( "cp \"%s" S_COLOR_WHITE " IS THE LAST PILOT\"",
			level.clients[winner].pers.netname ) );
		trap_SendServerCommand( -1, va( "print \"%s" S_COLOR_WHITE " wins the heat.\n\"",
			level.clients[winner].pers.netname ) );
		level.clients[winner].ps.persistant[PERS_SCORE]++;
	} else {
		trap_SendServerCommand( -1, "print \"The lattice took everyone.\n\"" );
	}
	LogExit( "Last pilot standing." );
	return qtrue;
}
