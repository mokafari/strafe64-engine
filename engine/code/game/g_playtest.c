/*
===========================================================================
Copyright (C) 2026 STRAFE 64

This file is part of Quake III Arena source code (GPL). See g_main.c.
===========================================================================
*/
// g_playtest.c -- headless bot playtest telemetry.
//
// With `g_playtest 1` the server records per-bot run metrics — completion,
// death cause/place, flow proxies (time at speed, airtime), moveset usage
// (bhop/wallrun/double-jump), and void near-misses — as one JSON line per
// finished / dead / timed-out run to playtest/<g_playtestTag>.jsonl. The
// strafegen playtest harness fans out headless ioq3ded instances and
// aggregates these. Zero cost when g_playtest is 0.

#include "g_local.h"

#define PT_RUNSPEED		320.0f		// g_speed: the "in flow" threshold
#define PT_STUCK_UPS	40.0f		// below this horizontal u/s = "stuck" (dt-normalised)

typedef struct {
	qboolean	active;
	int			spawnTime;
	int			frames;
	int			framesFast;		// frames above run speed
	int			framesAir;		// airborne frames
	int			wallrunFrames;
	int			slideFrames;	// crouch-slide frames (EF_SLIDING)
	float		sumSpeed;
	float		maxSpeed;
	int			maxBhop;
	int			walljumps;
	int			doublejumps;
	int			lastWj;			// edge trackers (counts reset on landing)
	int			lastDj;
	float		minVoidDist;	// closest the void ever got (units above it)
	int			stuckMs;		// time barely moving while alive
	int			frags;			// kills this run (ARENA archetype)
	int			midairFrags;	// kills with both fighters airborne
	float		sumKillSpeed;	// attacker speed summed over kills
	int			lastTrailMs;	// throttle for position-trail logging
	vec3_t		lastOrigin;
} pt_client_t;

static pt_client_t	pt[MAX_CLIENTS];
static int			pt_lastTime;

static void PT_Write( const char *line ) {
	fileHandle_t	f;
	char			path[MAX_QPATH];
	const char		*tag;

	tag = g_playtestTag.string;
	if ( !tag || !tag[0] ) {
		tag = "playtest";
	}
	Com_sprintf( path, sizeof( path ), "playtest/%s.jsonl", tag );
	trap_FS_FOpenFile( path, &f, FS_APPEND );
	if ( !f ) {
		return;
	}
	trap_FS_Write( line, strlen( line ), f );
	trap_FS_FCloseFile( f );
}

static float PT_VoidZ( void ) {
	// real-ms clock, matching G_RunVoid (voidStartTime is a real-time stamp)
	if ( !level.voidActive || trap_Milliseconds() < level.voidStartTime ) {
		return -1.0e9f;		// no void / not started yet
	}
	return level.voidBase
		+ level.voidRise * ( trap_Milliseconds() - level.voidStartTime ) / 1000.0f;
}

static void PT_Init( int cn, gentity_t *ent ) {
	pt_client_t	*p = &pt[cn];

	memset( p, 0, sizeof( *p ) );
	p->active = qtrue;
	p->spawnTime = level.time;
	p->minVoidDist = 1.0e9f;
	p->lastWj = ent->client->ps.stats[STAT_WALLJUMP_COUNT];
	p->lastDj = ent->client->ps.stats[STAT_AIRJUMP_COUNT];
	VectorCopy( ent->r.currentOrigin, p->lastOrigin );
}

static void PT_Summary( int cn, const char *ev, const char *extra ) {
	pt_client_t	*p = &pt[cn];
	char		line[512];
	int			dur     = level.time - p->spawnTime;
	int			avg     = p->frames > 0 ? (int)( p->sumSpeed / p->frames ) : 0;
	int			fastPct = p->frames > 0 ? ( p->framesFast    * 100 / p->frames ) : 0;
	int			airPct  = p->frames > 0 ? ( p->framesAir     * 100 / p->frames ) : 0;
	int			wrPct   = p->frames > 0 ? ( p->wallrunFrames * 100 / p->frames ) : 0;
	int			slidePct= p->frames > 0 ? ( p->slideFrames   * 100 / p->frames ) : 0;
	int			killSpd = p->frags > 0 ? (int)( p->sumKillSpeed / p->frags ) : 0;

	Com_sprintf( line, sizeof( line ),
		"{\"ev\":\"%s\",\"cn\":%i,\"durms\":%i,\"avgspd\":%i,\"maxspd\":%i,"
		"\"flowpct\":%i,\"airpct\":%i,\"wallrunpct\":%i,\"slidepct\":%i,\"maxbhop\":%i,"
		"\"wj\":%i,\"dj\":%i,\"minvoid\":%i,\"stuckms\":%i,"
		"\"frags\":%i,\"midair\":%i,\"killspd\":%i%s}\n",
		ev, cn, dur, avg, (int)p->maxSpeed,
		fastPct, airPct, wrPct, slidePct, p->maxBhop,
		p->walljumps, p->doublejumps,
		(int)( p->minVoidDist > 1.0e8f ? -1.0f : p->minVoidDist ), p->stuckMs,
		p->frags, p->midairFrags, killSpd,
		extra ? extra : "" );
	PT_Write( line );
}

/*
================
G_PlaytestSample

Called once per server frame from G_RunFrame: accumulate live metrics for
every alive bot.
================
*/
void G_PlaytestSample( void ) {
	int			i, dms;
	gentity_t	*ent;
	gclient_t	*cl;
	pt_client_t	*p;
	float		speed, voidZ, vdist, move;
	vec3_t		d;

	if ( !g_playtest.integer ) {
		return;
	}

	dms = level.time - pt_lastTime;
	pt_lastTime = level.time;
	if ( dms < 0 || dms > 1000 ) {
		dms = 0;		// first frame / hitch: don't credit garbage stuck time
	}

	voidZ = PT_VoidZ();

	for ( i = 0 ; i < level.maxclients ; i++ ) {
		ent = &g_entities[i];
		if ( !ent->inuse || !ent->client ) {
			continue;
		}
		if ( !( ent->r.svFlags & SVF_BOT ) ) {
			continue;
		}
		cl = ent->client;
		if ( cl->ps.pm_type != PM_NORMAL || ent->health <= 0 ) {
			continue;		// death is logged by the death hook
		}

		p = &pt[i];
		if ( !p->active ) {
			PT_Init( i, ent );
		}

		speed = sqrt( cl->ps.velocity[0] * cl->ps.velocity[0]
			+ cl->ps.velocity[1] * cl->ps.velocity[1] );

		p->frames++;
		p->sumSpeed += speed;
		if ( speed > p->maxSpeed )    p->maxSpeed = speed;
		if ( speed > PT_RUNSPEED )    p->framesFast++;
		if ( cl->ps.groundEntityNum == ENTITYNUM_NONE ) p->framesAir++;
		if ( cl->ps.stats[STAT_WALLRUN] != 0 ) p->wallrunFrames++;
		if ( cl->ps.eFlags & EF_SLIDING ) p->slideFrames++;
		if ( cl->ps.stats[STAT_BHOP_STREAK] > p->maxBhop ) {
			p->maxBhop = cl->ps.stats[STAT_BHOP_STREAK];
		}
		if ( cl->ps.stats[STAT_WALLJUMP_COUNT] > p->lastWj ) {
			p->walljumps += cl->ps.stats[STAT_WALLJUMP_COUNT] - p->lastWj;
		}
		p->lastWj = cl->ps.stats[STAT_WALLJUMP_COUNT];
		if ( cl->ps.stats[STAT_AIRJUMP_COUNT] > p->lastDj ) {
			p->doublejumps += cl->ps.stats[STAT_AIRJUMP_COUNT] - p->lastDj;
		}
		p->lastDj = cl->ps.stats[STAT_AIRJUMP_COUNT];

		if ( voidZ > -1.0e8f ) {
			vdist = ent->r.currentOrigin[2] - voidZ;
			if ( vdist < p->minVoidDist ) {
				p->minVoidDist = vdist;
			}
		}

		// "stuck" = barely moving this frame. The threshold must scale with the
		// frame's game-ms (dms), or it silently changes meaning under g_timeBind
		// slow-mo: the server steps game-frames at sv_fps REAL rate but each one
		// advances only (1000/sv_fps)*timescale game-ms, so per-frame displacement
		// shrinks with timescale. A fixed `move < 2.0` then flags moving bots as
		// stuck (at ts 0.3 it became "speed < 133" instead of "< 40"), which made
		// slow-mo look like a bot glitch when bot game-logic is actually timescale-
		// invariant. Normalising by dms keeps it a true "< ~40 u/s" test at any
		// timescale and is byte-identical at ts 1.0 (dms 50 -> 2.0u), so the dojo
		// baselines are unchanged.
		VectorSubtract( ent->r.currentOrigin, p->lastOrigin, d );
		d[2] = 0;
		move = VectorLength( d );
		if ( dms > 0 && move < PT_STUCK_UPS * dms / 1000.0f ) {
			p->stuckMs += dms;
		}
		VectorCopy( ent->r.currentOrigin, p->lastOrigin );

		// position trail (for heatmaps + tracers): a sample every 100ms so
		// the harness can draw how each bot actually moved through the map
		if ( g_playtestTrail.integer
			&& level.time - p->lastTrailMs >= 100 ) {
			char	tl[160];

			p->lastTrailMs = level.time;
			Com_sprintf( tl, sizeof( tl ),
				"{\"ev\":\"trail\",\"cn\":%i,\"t\":%i,\"x\":%i,\"y\":%i,"
				"\"z\":%i,\"spd\":%i}\n",
				i, level.time - p->spawnTime,
				(int)ent->r.currentOrigin[0], (int)ent->r.currentOrigin[1],
				(int)ent->r.currentOrigin[2], (int)speed );
			PT_Write( tl );
		}
	}
}

/*
================
G_PlaytestFinish  -- a bot crossed trigger_race_finish
================
*/
void G_PlaytestFinish( gentity_t *ent, int ms ) {
	int		cn;
	char	extra[64];

	if ( !g_playtest.integer || !ent->client ) {
		return;
	}
	if ( !( ent->r.svFlags & SVF_BOT ) ) {
		return;
	}
	cn = ent - g_entities;
	if ( cn < 0 || cn >= MAX_CLIENTS || !pt[cn].active ) {
		return;
	}
	Com_sprintf( extra, sizeof( extra ), ",\"racems\":%i", ms );
	PT_Summary( cn, "finish", extra );
	PT_Init( cn, ent );		// keep racing: fresh aggregate for the next lap
}

/*
================
G_PlaytestDeath  -- a bot died (meansOfDeath; MOD_FALLING == void / pit)
================
*/
void G_PlaytestDeath( gentity_t *ent, const char *mod ) {
	int		cn;
	char	extra[160];
	float	voidZ, vdist;

	if ( !g_playtest.integer || !ent->client ) {
		return;
	}
	if ( !( ent->r.svFlags & SVF_BOT ) ) {
		return;
	}
	cn = ent - g_entities;
	if ( cn < 0 || cn >= MAX_CLIENTS || !pt[cn].active ) {
		return;
	}
	voidZ = PT_VoidZ();
	vdist = ( voidZ > -1.0e8f ) ? ( ent->r.currentOrigin[2] - voidZ ) : -9999.0f;
	Com_sprintf( extra, sizeof( extra ),
		",\"mod\":\"%s\",\"deathz\":%i,\"voiddist\":%i,\"racems\":%i,\"deathx\":%i,\"deathy\":%i",
		mod, (int)ent->r.currentOrigin[2], (int)vdist,
		ent->client->raceStartTime ? ( trap_Milliseconds() - ent->client->raceStartTime ) : -1,
		(int)ent->r.currentOrigin[0], (int)ent->r.currentOrigin[1] );
	PT_Summary( cn, "death", extra );
	pt[cn].active = qfalse;
}

/*
================
G_PlaytestKill  -- a bot got a frag (records on the attacker's run)
================
*/
void G_PlaytestKill( gentity_t *attacker, gentity_t *victim ) {
	int			cn;
	pt_client_t	*p;
	float		speed;

	if ( !g_playtest.integer || !attacker || !attacker->client ) {
		return;
	}
	if ( attacker == victim || !( attacker->r.svFlags & SVF_BOT ) ) {
		return;
	}
	cn = attacker - g_entities;
	if ( cn < 0 || cn >= MAX_CLIENTS || !pt[cn].active ) {
		return;
	}
	p = &pt[cn];
	speed = sqrt( attacker->client->ps.velocity[0] * attacker->client->ps.velocity[0]
		+ attacker->client->ps.velocity[1] * attacker->client->ps.velocity[1] );
	p->frags++;
	p->sumKillSpeed += speed;
	if ( attacker->client->ps.groundEntityNum == ENTITYNUM_NONE
		&& victim->client
		&& victim->client->ps.groundEntityNum == ENTITYNUM_NONE ) {
		p->midairFrags++;		// both airborne — a true midair frag
	}
}

/*
================
G_PlaytestFlush  -- level shutdown / time limit: flush still-running bots
================
*/
void G_PlaytestFlush( void ) {
	int		i;

	if ( !g_playtest.integer ) {
		return;
	}
	for ( i = 0 ; i < level.maxclients ; i++ ) {
		if ( pt[i].active ) {
			PT_Summary( i, "timeout", NULL );
			pt[i].active = qfalse;
		}
	}
}
