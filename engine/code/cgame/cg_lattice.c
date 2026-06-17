// Copyright (C) STRAFE 64
//
// cg_lattice.c -- LATTICE mode: render every pilot's damaging speed-trail.
//
// In LATTICE the trail is the third player, so it has to READ at speed -- you
// must parse three carving light-walls the way you parse a wallrun surface.
// Each pilot's trail is a colour-coded billboarded ribbon stitched through the
// positions we observe for them, tinted by their player colour, brightest at
// the head and fading toward the tail.
//
// This is purely client-side eye-candy off observed positions; the damage that
// the ribbon represents is server-authoritative (g_lattice.c). Self-contained:
// hooked from one line at the end of CG_AddPacketEntities, drawn with the
// always-present white shader so it needs no map asset and never touches the
// concurrent flow/glitch HUD layer.

#include "cg_local.h"

#define LAT_PTS			128		// points retained per pilot (mirrors the server ring)
#define LAT_SAMPLE_MS	33		// sample cadence (~30 Hz), independent of render rate
#define LAT_STEP		20.0f	// min carve distance between stored points (u)
#define LAT_TRAIL_SEGS	64		// how many tail segments to actually draw
#define LAT_WALL_LO		-22.0f	// wall extends from here (below the pilot origin)...
#define LAT_WALL_HI		56.0f	// ...up to here -- a vertical light-wall you must read
#define LAT_ALPHA		0.82f	// head opacity (wall presence)

// Distinct per-pilot wall colours so three carving trails read apart at speed,
// regardless of the players' chosen model colours. Indexed by client number.
// Warm/high-contrast hues first: most arenas lean cool (blue banks, grey floor),
// so the early pilots get colours that pop against them; cool hues come later.
static const byte latPalette[8][3] = {
	{ 255, 120,  30 },	// 0 hot orange
	{ 255,  60, 200 },	// 1 magenta
	{ 120, 255,  80 },	// 2 green
	{ 255, 235,  60 },	// 3 yellow
	{ 255,  60,  60 },	// 4 red
	{  60, 230, 255 },	// 5 cyan
	{ 170, 120, 255 },	// 6 violet
	{ 255, 255, 255 },	// 7 white
};

// cheap integer hash -> pseudo-random, for the deterministic per-segment glitch
// (no Math.random in this VM; key on a stable segment id + a time bucket)
static unsigned latHash( unsigned a, unsigned b ) {
	unsigned h = a * 374761393u + b * 668265263u;
	h = ( h ^ ( h >> 13 ) ) * 1274126177u;
	return h ^ ( h >> 16 );
}

static vec3_t	latPts[MAX_CLIENTS][LAT_PTS];
static int		latCount[MAX_CLIENTS];
static int		latHead[MAX_CLIENTS];
static int		latLastSample;
static int		latLastTime;

// Animation is driven by a REAL-time clock (trap_Milliseconds, unscaled) instead
// of cg.time, so the pulse/glitch/flow stay alive when g_timeBind dilates the
// world. latSlow rises toward 1 the deeper the slow-mo, so the wall BREATHES
// harder in bullet-time (the effect peaks exactly when you have time to admire it).
static int		latRealMs;
static int		latPrevReal;
static int		latPrevCg;
static float	latSlow;		// 0 = normal speed, ->1 = near-frozen bullet-time

// Music band envelopes (au_* cvars, set by the sound codec), already scaled by the
// cg_latticeAudio master. The lattice IS the third player, so it pulses to the track:
// bass pumps the wall brightness + neon light, highs spike the glitch, level lifts
// the floor glow, mid quickens the energy flow.
static float	latBass, latMid, latHigh, latLevel;

/*
================
CG_LatticeReset
================
*/
static void CG_LatticeReset( void ) {
	int i;
	for ( i = 0 ; i < MAX_CLIENTS ; i++ ) {
		latCount[i] = 0;
		latHead[i] = 0;
	}
	latLastSample = cg.time;
}

/*
================
CG_LatticePush

Append one observed position to a pilot's ring, gated on carve distance so a
stationary pilot stops extending their wall.
================
*/
static void CG_LatticePush( int client, const vec3_t origin ) {
	int		last;
	vec3_t	d;

	if ( client < 0 || client >= MAX_CLIENTS ) {
		return;
	}
	if ( latCount[client] > 0 ) {
		last = ( latHead[client] - 1 + LAT_PTS ) % LAT_PTS;
		VectorSubtract( origin, latPts[client][last], d );
		if ( VectorLengthSquared( d ) < LAT_STEP * LAT_STEP ) {
			return;
		}
	}
	VectorCopy( origin, latPts[client][latHead[client]] );
	latHead[client] = ( latHead[client] + 1 ) % LAT_PTS;
	if ( latCount[client] < LAT_PTS ) {
		latCount[client]++;
	}
}

/*
================
CG_LatticeDraw

A vertical light-wall stitched through one pilot's stored points, newest-first,
fading toward the tail. Each segment is an upright quad (floor-ish to head-high)
so the lattice reads as a barrier from any angle, not a thin floor streak --
the trail you must parse and route around at speed.
================
*/
// Set one vertex: colour mixed toward white-hot near the pilot, alpha = age fade
// * travelling energy pulse * vertical gradient (solid base, dissipating top).
static void CG_LatticeVert( polyVert_t *v, const vec3_t xyz, float s, float t,
		const byte *col, int idx, int n, float scroll, qboolean top, float aMul, float slow ) {
	float	age   = 1.0f - ( idx / (float)( n - 1 ) );		// 1 at head -> 0 at tail
	// breathing band running down the wall: oscillates between a visible floor and
	// a bright peak that swells in slow-mo (never drops to 0, so it never blinks out).
	// The music drives it too: level lifts the floor, bass slams the peak (the wall
	// pumps on the kick) — so the lattice breathes ON THE BEAT, hardest in slow-mo.
	float	lo    = 0.50f + 0.22f * latLevel;
	float	hi    = 1.00f + 0.60f * slow + 0.75f * latBass;	// kick brightens, stays coloured
	float	pulse = lo + ( hi - lo ) * 0.5f * ( 1.0f + sin( idx * 0.45f - scroll ) );
	float	hot   = ( idx < 5 ) ? ( 1.0f - idx * 0.2f ) : 0.0f;	// molten white leading edge
	float	vmul  = top ? 0.5f : 1.0f;							// energy is solid at the floor, fades up
	float	al    = LAT_ALPHA * age * pulse * vmul * aMul;
	float	w     = hot * 0.85f;								// whiten toward the head

	if ( al < 0 ) al = 0; else if ( al > 1 ) al = 1;
	VectorCopy( xyz, v->xyz );
	v->modulate[0] = (byte)( col[0] + ( 255 - col[0] ) * w );
	v->modulate[1] = (byte)( col[1] + ( 255 - col[1] ) * w );
	v->modulate[2] = (byte)( col[2] + ( 255 - col[2] ) * w );
	v->modulate[3] = (byte)( al * 255.0f );
	v->st[0] = s;
	v->st[1] = t;
}

// One upright wall quad between points a (newer, segment index ia) and b (older,
// index ib), tinted by col, faded by aMul. Used for the base wall and for the
// offset chromatic-split ghosts.
static void CG_LatticeQuad( qhandle_t shader, const vec3_t a, const vec3_t b,
		const byte *col, int ia, int ib, int n, float scroll, float aMul, float slow ) {
	polyVert_t	verts[4];
	vec3_t		at, ab, bt, bb;

	VectorCopy( a, at ); at[2] += LAT_WALL_HI;
	VectorCopy( a, ab ); ab[2] += LAT_WALL_LO;
	VectorCopy( b, bt ); bt[2] += LAT_WALL_HI;
	VectorCopy( b, bb ); bb[2] += LAT_WALL_LO;

	CG_LatticeVert( &verts[0], at, 0, 0, col, ia, n, scroll, qtrue,  aMul, slow );
	CG_LatticeVert( &verts[1], ab, 0, 1, col, ia, n, scroll, qfalse, aMul, slow );
	CG_LatticeVert( &verts[2], bb, 1, 1, col, ib, n, scroll, qfalse, aMul, slow );
	CG_LatticeVert( &verts[3], bt, 1, 0, col, ib, n, scroll, qtrue,  aMul, slow );

	trap_R_AddPolyToScene( shader, 4, verts );
}

// glitch corruption tints (channel-pure for the chromatic split, plus flash hues)
static const byte latGlitchWhite[3] = { 255, 255, 255 };
static const byte latGlitchCyan[3]  = {  80, 255, 255 };
static const byte latGlitchRed[3]   = { 255,  40,  40 };
static const byte latGlitchBlue[3]  = {  60,  90, 255 };

static void CG_LatticeDraw( int client ) {
	int			i, n, idx0, idx1, head;
	const byte	*col;
	qhandle_t	shader;
	float		scroll, glitch;
	unsigned	tbucket;

	n = latCount[client];
	if ( n < 2 ) {
		return;
	}
	if ( n > LAT_TRAIL_SEGS + 1 ) {
		n = LAT_TRAIL_SEGS + 1;
	}

	// alpha-blended wall shader; fall back to the additive white shader if a
	// map/build lacks strafe64/lattice (it still reads on dark surfaces)
	shader = cgs.media.latticeShader ? cgs.media.latticeShader : cgs.media.whiteShader;

	col = latPalette[client & 7];
	// REAL-time clock so the pulse/glitch keep moving under slow-mo (cg.time is
	// dilated by g_timeBind; latRealMs is not). The pulse slows a touch in deep
	// slow-mo for a languid bullet-time breath, but never freezes.
	scroll = latRealMs * 0.006f * ( 1.0f - 0.4f * latSlow ) * ( 1.0f + 0.6f * latMid );
	glitch = cg_latticeGlitch.value * ( 1.0f + 1.6f * latHigh );	// hats/snares tear the wall
	tbucket = (unsigned)( latRealMs / 70 );	// glitch re-rolls ~14x/sec (stuttery buffer)

	for ( i = 0 ; i < n - 1 ; i++ ) {
		vec3_t		a, b, dir, perp;
		const byte	*segCol = col;
		float		aMul = 1.0f;
		qboolean	torn = qfalse;
		float		split = 0.0f;

		// idx0 = newer point, idx1 = older; walk back from the head
		idx0 = ( latHead[client] - 1 - i + LAT_PTS * 2 ) % LAT_PTS;
		idx1 = ( latHead[client] - 2 - i + LAT_PTS * 2 ) % LAT_PTS;
		VectorCopy( latPts[client][idx0], a );
		VectorCopy( latPts[client][idx1], b );

		// lateral normal of this segment in the floor plane (the tear axis)
		VectorSubtract( b, a, dir );
		dir[2] = 0;
		perp[0] = -dir[1]; perp[1] = dir[0]; perp[2] = 0;
		if ( VectorNormalize( perp ) < 0.01f ) {
			VectorSet( perp, 1, 0, 0 );
		}

		// GLITCH BUFFER: key a hash on a stable segment id + the time bucket, so
		// chunks of the wall tear/jump/drop out in stuttering steps like a
		// corrupted framebuffer. cg_latticeGlitch scales the intensity (0 = off).
		if ( glitch > 0 ) {
			unsigned h = latHash( idx0 * 2654435761u + client, tbucket );
			unsigned gmask = ( latHigh > 0.45f ) ? 3 : 7;	// loud highs glitch ~2x the segments
			if ( ( h & gmask ) == 0 ) {				// glitch this segment this tick
				float lateral = ( (int)( ( h >> 3 ) & 63 ) - 32 ) * 0.55f * glitch;
				torn  = qtrue;
				split = 3.5f * glitch;				// chromatic-split offset
				VectorMA( a, lateral, perp, a );	// shove the chunk sideways
				VectorMA( b, lateral, perp, b );
				if ( ( ( h >> 9 ) & 3 ) == 0 ) {	// occasional vertical jump
					float z = ( (int)( ( h >> 11 ) & 15 ) - 8 ) * 1.6f * glitch;
					a[2] += z; b[2] += z;
				}
				switch ( ( h >> 16 ) & 3 ) {		// channel corruption flash
				case 0:  segCol = latGlitchWhite; break;
				case 1:  segCol = latGlitchCyan;  break;
				default: segCol = col;            break;
				}
				aMul = ( ( ( h >> 18 ) & 7 ) == 0 ) ? 0.12f : 1.35f;	// dropout vs flare
			}
		}

		// chromatic split: faint red/blue ghosts shoved opposite ways (drawn
		// first, under the core) — the classic datamosh colour fringe
		if ( torn && split > 0 ) {
			vec3_t ra, rb;
			VectorMA( a,  split, perp, ra ); VectorMA( b,  split, perp, rb );
			CG_LatticeQuad( shader, ra, rb, latGlitchRed, i, i + 1, n, scroll, aMul * 0.6f, latSlow );
			VectorMA( a, -split, perp, ra ); VectorMA( b, -split, perp, rb );
			CG_LatticeQuad( shader, ra, rb, latGlitchBlue, i, i + 1, n, scroll, aMul * 0.6f, latSlow );
		}

		CG_LatticeQuad( shader, a, b, segCol, i, i + 1, n, scroll, aMul, latSlow );
	}

	// the head index is reused by the dynamic lights below. NOTE: the billboarded
	// additive "emitter" glow sprite that used to pulse at the pilot's newest trail
	// point was REMOVED — it rendered right on the player and read as a glowing blob
	// stuck to them. The trail wall + the head dynamic light below carry the
	// energy-extrusion read without a sprite sitting on the body.
	head = ( latHead[client] - 1 + LAT_PTS ) % LAT_PTS;

	// DYNAMIC NEON LIGHTS: the wall is a real light source, but kept as a subtle
	// ACCENT (not a floodlight) so the arena still reads cleanly. A modest pulsing
	// light at the head + a few faint, fading lights down the trail. The pulse
	// runs on the real clock and swells in slow-mo so bullet-time glows warmer.
	{
		float	lr = col[0] / 255.0f, lg = col[1] / 255.0f, lb = col[2] / 255.0f;
		float	hb = 150.0f + ( 35.0f + 60.0f * latSlow ) * sin( latRealMs * 0.012f ) + 140.0f * latBass;
		int		lit, lidx;
		vec3_t	lp;

		VectorCopy( latPts[client][head], lp );
		lp[2] += 30.0f;
		trap_R_AddLightToScene( lp, hb, lr, lg, lb );

		lit = 0;
		for ( i = 0 ; i < n && lit < 6 ; i += 9, lit++ ) {		// every 9th point, max 6
			float fade = 1.0f - ( i / (float)( n ) );
			lidx = ( latHead[client] - 1 - i + LAT_PTS * 2 ) % LAT_PTS;
			VectorCopy( latPts[client][lidx], lp );
			lp[2] += 30.0f;
			trap_R_AddLightToScene( lp, 45.0f + 55.0f * fade, lr, lg, lb );
		}
	}
}

/*
================
CG_LatticeFrame

Called once per frame from CG_AddPacketEntities. Samples each visible pilot's
position on a fixed cadence, then draws all the ribbons.
================
*/
void CG_LatticeFrame( void ) {
	int				num, client, i;
	centity_t		*cent;
	entityState_t	*es;
	qboolean		doSample;

	if ( !cgs.lattice || !cg.snap ) {
		return;
	}

	// detect a time discontinuity (map restart / demo seek) and wipe stale walls
	if ( cg.time < latLastTime ) {
		CG_LatticeReset();
		latPrevReal = 0;
	}
	latLastTime = cg.time;

	// real (unscaled) clock + slow-mo estimate. timescale = (cg.time delta) /
	// (real delta); when g_timeBind dilates time that ratio drops, so latSlow
	// rises toward 1 and the wall breathes harder. Smoothed to avoid jitter.
	latRealMs = trap_Milliseconds();
	if ( latPrevReal > 0 ) {
		int		dr = latRealMs - latPrevReal;
		int		dc = cg.time - latPrevCg;
		if ( dr > 0 ) {
			float	ts = dc / (float)dr;
			float	want;
			if ( ts < 0 ) ts = 0; else if ( ts > 1 ) ts = 1;
			want = 1.0f - ts;					// 0 = full speed, 1 = frozen
			latSlow += ( want - latSlow ) * 0.15f;	// smooth toward target
		}
	}
	latPrevReal = latRealMs;
	latPrevCg = cg.time;

	// sample the live music bands (scaled by the cg_latticeAudio master, 0 = off)
	{
		float a = cg_latticeAudio.value;
		latBass  = au_bass.value  * a;
		latMid   = au_mid.value   * a;
		latHigh  = au_high.value  * a;
		latLevel = au_level.value * a;
	}

	doSample = ( cg.time - latLastSample >= LAT_SAMPLE_MS );
	if ( doSample ) {
		latLastSample = cg.time;

		// local pilot from the predicted state (smoother than the snapshot)
		if ( cg.snap->ps.stats[STAT_HEALTH] > 0
			&& cg.snap->ps.pm_type == PM_NORMAL ) {
			CG_LatticePush( cg.snap->ps.clientNum, cg.predictedPlayerState.origin );
		}

		// every other pilot present in this snapshot
		for ( num = 0 ; num < cg.snap->numEntities ; num++ ) {
			es = &cg.snap->entities[num];
			if ( es->eType != ET_PLAYER ) {
				continue;
			}
			if ( es->number == cg.snap->ps.clientNum ) {
				continue;		// already sampled from prediction
			}
			if ( es->eFlags & EF_DEAD ) {
				continue;
			}
			client = es->number;
			cent = &cg_entities[client];
			CG_LatticePush( client, cent->lerpOrigin );
		}
	}

	// draw every pilot's wall
	for ( i = 0 ; i < MAX_CLIENTS ; i++ ) {
		CG_LatticeDraw( i );
	}
}
