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
// The wall is a band CENTRED on the pilot that fattens symmetrically with the music --
// it emanates from the player: a thin line at the centre when quiet, growing equally up
// and down, and at the loudest peak it reaches exactly the PLAYER BOX -- bottom at the
// feet (floor), top at the head. Sized to the box (feet origin-24, head origin+32) so it
// never towers past the pilot, and started thin so only the loudest hits reach the floor
// (quiet passages stay well clear of the deck instead of clipping it).
#define LAT_WALL_MID		4.0f	// vertical centre = player box centre, so the wall comes FROM the pilot
#define LAT_WALL_MIN_HALF	2.0f	// quiet half-height -- a thin centred line emanating from the player
#define LAT_WALL_MAX_HALF	28.0f	// LOUD CAP = player half-height (centre+28 = head, centre-28 = feet/floor)
#define LAT_ALPHA			0.82f	// head opacity (wall presence)
#define LAT_WAVE_HALF		26.0f	// amplitude->half-height gain (clamped at MAX_HALF), * cg_latticeWave

// ARENA velocity ribbon (cg_arenaTrails, NOT full LATTICE mode): a SHORT trail
// that DISSOLVES on a real-time TTL, so its visible length tracks how far you
// moved in the last few hundred ms = your speed. Stop and it fades to nothing.
#define LAT_ARENA_SEGS		48		// max tail; the TTL below is the real limiter
#define LAT_ARENA_TTL		360.0f	// base game-ms a point lives before it dissolves
#define LAT_ARENA_STRETCH	3.2f	// slow-mo stretch: trail lives (1 + STRETCH*latSlow)x
									// longer in bullet-time -> it lengthens as time dilates
#define LAT_ARENA_HALF		9.0f	// band base half-height; pumps out from the player on bass

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
static float	latAmp[MAX_CLIENTS][LAT_PTS];	// music amplitude captured AS each point was laid -> the wall-top waveform
static int		latTime[MAX_CLIENTS][LAT_PTS];	// real-ms each point was laid -> arena-trail TTL dissolve
static int		latCount[MAX_CLIENTS];
static int		latHead[MAX_CLIENTS];
static int		latLastSample;
static int		latLastTime;
static float	latWaveNow;		// current frame's amplitude sample, frozen into latAmp on each push

// Animation is driven by a REAL-time clock (trap_Milliseconds, unscaled) instead
// of cg.time, so the pulse/glitch/flow stay alive when g_timeBind dilates the
// world. latSlow rises toward 1 the deeper the slow-mo, so the wall BREATHES
// harder in bullet-time (the effect peaks exactly when you have time to admire it).
static int		latRealMs;
static int		latPrevReal;
static int		latPrevCg;
static float	latSlow;		// 0 = normal speed, ->1 = near-frozen bullet-time
static qboolean	latArenaDraw;	// current CG_LatticeDraw pass is the arena ribbon (saturated, centred pump)

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
	// freeze the music amplitude into this point so the wall remembers how loud
	// the track was where the pilot ran — the crest traces the waveform in space
	latAmp[client][latHead[client]] = latWaveNow;
	// stamp on cg.time (the SAME clock the sampling cadence uses) so the arena
	// dissolve is consistent with point creation: in bullet-time both slow
	// together, so the trail accumulates and lingers instead of expiring between
	// samples. Length then = game-distance over the TTL window = your speed.
	latTime[client][latHead[client]] = cg.time;
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
	// arena ribbon: an EVEN bar centred on the player (no floor-heavy gradient)
	// and far LESS white blowout, so the neon stays saturated and rich instead of
	// washing to white — vivid but not tacky.
	float	vmul  = latArenaDraw ? 0.90f : ( top ? 0.5f : 1.0f );
	float	al    = LAT_ALPHA * age * pulse * vmul * aMul;
	float	w     = hot * ( latArenaDraw ? 0.38f : 0.85f );		// whiten toward the head

	if ( al < 0 ) al = 0; else if ( al > 1 ) al = 1;
	VectorCopy( xyz, v->xyz );
	v->modulate[0] = (byte)( col[0] + ( 255 - col[0] ) * w );
	v->modulate[1] = (byte)( col[1] + ( 255 - col[1] ) * w );
	v->modulate[2] = (byte)( col[2] + ( 255 - col[2] ) * w );
	v->modulate[3] = (byte)( al * 255.0f );
	v->st[0] = s;
	v->st[1] = t;
}

// Per-point ragged edge: break up the otherwise crisp top/bottom silhouette. The
// offset is keyed on the point's RING INDEX (so the two segments that share a point
// agree -> the broken edge stays continuous, not torn at every seam) plus a gentle
// shimmer, and scales with the wall's height + the audio highs, so a tall loud wall
// crackles more at the crest than a quiet thin line. half = the point's half-height;
// returns the absolute top/bottom z-offsets from the trail point.
static void CG_LatticeEdge( int ringIdx, float half, float *top, float *bot ) {
	float		frac   = 0.45f + half / LAT_WALL_MAX_HALF;		// taller -> more ragged
	float		ebreak = ( 2.5f + 5.0f * ( latHigh > 1.0f ? 1.0f : latHigh ) ) * frac;
	unsigned	h      = latHash( (unsigned)ringIdx * 2654435761u, 0x9E37u );
	float		shim   = 0.6f + 0.4f * sin( ringIdx * 0.7f + latRealMs * 0.004f );
	float		nt     = ( ( h        & 255 ) / 255.0f ) * shim;
	float		nb     = ( ( ( h >> 8 ) & 255 ) / 255.0f ) * shim;
	*top = LAT_WALL_MID + half + ebreak * nt;
	*bot = LAT_WALL_MID - half - ebreak * nb;
}

// One upright wall quad between points a (newer, segment index ia) and b (older,
// index ib), tinted by col, faded by aMul. topA/botA/topB/botB are the (already
// broken-up) z-offsets of each end's top and bottom edge from the trail point.
// Used for the base wall and for the offset chromatic-split ghosts.
static void CG_LatticeQuad( qhandle_t shader, const vec3_t a, const vec3_t b,
		const byte *col, int ia, int ib, int n, float scroll, float aMul, float slow,
		float topA, float botA, float topB, float botB ) {
	polyVert_t	verts[4];
	vec3_t		at, ab, bt, bb;

	VectorCopy( a, at ); at[2] += topA;
	VectorCopy( a, ab ); ab[2] += botA;
	VectorCopy( b, bt ); bt[2] += topB;
	VectorCopy( b, bb ); bb[2] += botB;

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

// MICRO-GLITCH BLOCKS: small datamosh chips scattered over a wall segment, finer
// than the segment-level tear above. Each candidate flickers in/out on the ~14 Hz
// time bucket; how many fire (and how bright) rides the audio highs, so hats and
// snares spray the wall with little corrupted squares — the "alive" detail layer.
// Chips are pushed a hair toward the camera so they pop off the wall (no z-fight)
// and read as floating data, not paint.
#define LAT_BLK_CAND	5		// candidate chips considered per segment
static void CG_LatticeBlocks( qhandle_t shader, const vec3_t a, const vec3_t b,
		const vec3_t perp, const byte *col, float zc, float halfH,
		unsigned key, unsigned tbucket, float glitch, float highs, float age ) {
	vec3_t	segdir;
	float	seglen, dense;
	int		k, j;

	VectorSubtract( b, a, segdir );
	seglen = VectorNormalize( segdir );
	if ( seglen < 1.0f || age <= 0.05f ) {
		return;
	}
	// fraction of candidates that fire: sparse when quiet, spraying on loud highs
	dense = 0.18f + 0.55f * highs;
	if ( dense > 0.9f ) dense = 0.9f;

	for ( k = 0 ; k < LAT_BLK_CAND ; k++ ) {
		unsigned	h = latHash( key * 2246822519u + k * 2654435761u, tbucket );
		float		t, zt, sz, tear, al;
		const byte	*bc;
		vec3_t		c, du, toCam;
		polyVert_t	v[4];

		if ( ( h & 1023 ) / 1023.0f > dense ) {
			continue;					// this chip is dark this tick
		}
		t    = ( ( h >> 4 ) & 255 ) / 255.0f;			// position along the segment
		zt   = ( ( h >> 12 ) & 255 ) / 255.0f;			// height within the band
		sz   = 2.0f + ( ( h >> 20 ) & 7 );				// 2..9u — small
		tear = ( (int)( ( h >> 6 ) & 15 ) - 8 ) * 0.5f * glitch;	// sideways jitter

		VectorMA( a, t * seglen, segdir, c );
		// cluster the chips toward the centre line (the stream core that flows from
		// the player) — cube the [-1,1] height so most hug the centre and only a few
		// reach the edges, instead of spreading evenly up the whole band.
		{
			float vt = zt * 2.0f - 1.0f;
			vt = vt * vt * vt;
			c[2] = zc + vt * halfH;
		}
		VectorMA( c, tear, perp, c );
		// nudge toward the camera so the chip sits just proud of the wall
		VectorSubtract( cg.refdef.vieworg, c, toCam );
		VectorNormalizeFast( toCam );
		VectorMA( c, 1.5f, toCam, c );

		switch ( ( h >> 17 ) & 3 ) {		// corrupted channel flash
		case 0:  bc = latGlitchWhite; break;
		case 1:  bc = latGlitchCyan;  break;
		case 2:  bc = latGlitchRed;   break;
		default: bc = col;            break;
		}
		al = ( 0.55f + 0.45f * ( ( ( h >> 24 ) & 7 ) / 7.0f ) ) * age;

		// four corners of the chip in the wall plane: c +/- du (along trail) +/- sz (up)
		VectorScale( segdir, sz, du );
		VectorSubtract( c, du, v[0].xyz ); v[0].xyz[2] -= sz; v[0].st[0]=0; v[0].st[1]=1;
		VectorAdd( c, du, v[1].xyz );      v[1].xyz[2] -= sz; v[1].st[0]=1; v[1].st[1]=1;
		VectorAdd( c, du, v[2].xyz );      v[2].xyz[2] += sz; v[2].st[0]=1; v[2].st[1]=0;
		VectorSubtract( c, du, v[3].xyz ); v[3].xyz[2] += sz; v[3].st[0]=0; v[3].st[1]=0;
		for ( j = 0 ; j < 4 ; j++ ) {
			v[j].modulate[0] = bc[0];
			v[j].modulate[1] = bc[1];
			v[j].modulate[2] = bc[2];
			v[j].modulate[3] = (byte)( al * 255.0f );
		}
		trap_R_AddPolyToScene( shader, 4, v );
	}
}

static void CG_LatticeDraw( int client ) {
	int			i, n, idx0, idx1, head, maxSegs;
	const byte	*col;
	qhandle_t	shader;
	float		scroll, glitch, capHalf, arenaTtl;
	unsigned	tbucket;
	qboolean	arena;

	float		waveScale;

	n = latCount[client];
	if ( n < 2 ) {
		return;
	}
	// arena ribbon: short, thin and time-dissolving (velocity read). Full
	// LATTICE: the long, tall, persistent damaging wall.
	arena   = ( !cgs.lattice && cg_arenaTrails.integer );
	maxSegs = arena ? LAT_ARENA_SEGS : LAT_TRAIL_SEGS;
	capHalf = arena ? LAT_ARENA_HALF : LAT_WALL_MAX_HALF;
	// INVERTED slow-mo scaling: the ribbon LIVES LONGER (so reads longer) the
	// deeper the bullet-time, instead of shrinking — the trail unfurls as time
	// dilates. latSlow is 0 at full speed, ->1 near-frozen.
	arenaTtl = LAT_ARENA_TTL * ( 1.0f + LAT_ARENA_STRETCH * latSlow );
	if ( n > maxSegs + 1 ) {
		n = maxSegs + 1;
	}
	latArenaDraw = arena;	// tells CG_LatticeVert to use the saturated arena look

	// amplitude -> half-height gain (clamped to the player half-height per segment)
	waveScale = cg_latticeWave.value * LAT_WAVE_HALF;

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
		float		arenaFade = 1.0f;
		qboolean	torn = qfalse;
		float		split = 0.0f;
		float		riseA, riseB, halfA, halfB, topA, botA, topB, botB;

		// idx0 = newer point, idx1 = older; walk back from the head
		idx0 = ( latHead[client] - 1 - i + LAT_PTS * 2 ) % LAT_PTS;
		idx1 = ( latHead[client] - 2 - i + LAT_PTS * 2 ) % LAT_PTS;
		VectorCopy( latPts[client][idx0], a );
		VectorCopy( latPts[client][idx1], b );

		// ARENA dissolve: fade each segment by how long ago its point was laid,
		// and stop the trail once points have aged past the TTL. Short tail whose
		// length tracks your speed; fades to nothing when you stop.
		if ( arena ) {
			float ageMs = (float)( cg.time - latTime[client][idx0] );
			if ( ageMs >= arenaTtl ) {
				break;
			}
			arenaFade = 1.0f - ageMs / arenaTtl;
		}

		// half-height = quiet base + the amplitude frozen at each endpoint (the
		// waveform crest), capped at the player half-height. Then break up the crisp
		// top/bottom into a ragged, gently-shimmering edge (continuous across seams).
		if ( arena ) {
			// the whole short ribbon pumps OUT from the player centre on the kick
			// — one uniform live-bass height so the bar pulses as a unit, emanating
			// from the pilot, rather than a travelling waveform. cg_latticeWave
			// scales the swell; level keeps a thin resting line between hits.
			float pump = LAT_ARENA_HALF
				* ( 0.28f + 1.25f * latBass + 0.22f * latLevel ) * cg_latticeWave.value;
			if ( pump > LAT_ARENA_HALF * 2.0f ) pump = LAT_ARENA_HALF * 2.0f;
			if ( pump < 1.5f ) pump = 1.5f;
			halfA = halfB = pump;
		} else {
			riseA = latAmp[client][idx0] * waveScale;
			riseB = latAmp[client][idx1] * waveScale;
			halfA = LAT_WALL_MIN_HALF + riseA;
			halfB = LAT_WALL_MIN_HALF + riseB;
			if ( halfA > capHalf ) halfA = capHalf;
			if ( halfB > capHalf ) halfB = capHalf;
		}
		CG_LatticeEdge( idx0, halfA, &topA, &botA );
		CG_LatticeEdge( idx1, halfB, &topB, &botB );

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
			unsigned gmask = ( latHigh > 0.7f ) ? 7 : 15;	// sparse tears (~1/16 segs); only very loud highs double it
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

		aMul *= arenaFade;	// arena TTL dissolve (1.0 in full LATTICE mode)

		// chromatic split: faint red/blue ghosts shoved opposite ways (drawn
		// first, under the core) — the classic datamosh colour fringe
		if ( torn && split > 0 ) {
			vec3_t ra, rb;
			VectorMA( a,  split, perp, ra ); VectorMA( b,  split, perp, rb );
			CG_LatticeQuad( shader, ra, rb, latGlitchRed, i, i + 1, n, scroll, aMul * 0.6f, latSlow, topA, botA, topB, botB );
			VectorMA( a, -split, perp, ra ); VectorMA( b, -split, perp, rb );
			CG_LatticeQuad( shader, ra, rb, latGlitchBlue, i, i + 1, n, scroll, aMul * 0.6f, latSlow, topA, botA, topB, botB );
		}

		CG_LatticeQuad( shader, a, b, segCol, i, i + 1, n, scroll, aMul, latSlow, topA, botA, topB, botB );

		// micro-glitch chips: the fine "alive" detail, sprayed by the audio highs.
		// Uses the (possibly torn) a/b so chips follow a glitched segment's shove.
		if ( glitch > 0 ) {
			float halfMid = 0.5f * ( halfA + halfB );
			float age = 1.0f - ( i / (float)( n - 1 ) );
			CG_LatticeBlocks( shader, a, b, perp, segCol, LAT_WALL_MID, halfMid,
				idx0 * 2654435761u + client, tbucket, glitch, latHigh, age );
		}
	}

	// the head index is reused by the dynamic lights below. NOTE: the billboarded
	// additive "emitter" glow sprite that used to pulse at the pilot's newest trail
	// point was REMOVED — it rendered right on the player and read as a glowing blob
	// stuck to them. The trail wall + the head dynamic light below carry the
	// energy-extrusion read without a sprite sitting on the body.
	head = ( latHead[client] - 1 + LAT_PTS ) % LAT_PTS;

	// arena: the head light fades with the ribbon (gone once it has dissolved),
	// so a stationary fighter isn't haloed by a light with no visible trail.
	if ( arena ) {
		float headAge = (float)( cg.time - latTime[client][head] );
		if ( headAge >= arenaTtl ) {
			return;
		}
	}

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
CG_LatticeTrailWall

Draw a lattice-style vertical light-wall through an arbitrary trail of world
points (NEWEST-FIRST in `pts`, `n` of them), tinted `col`, alpha scaled by
`aMul`. Reuses the lattice quad + ragged-edge builders so the race GHOST wears
the same speed-trail wall a pilot leaves — without the per-client ring buffer
or the audio glitch/chip layer (kept clean as a replay overlay). The pulse
scroll still rides the real clock so it shimmers even in bullet-time.
================
*/
void CG_LatticeTrailWall( const vec3_t *pts, int n, const byte *col, float aMul ) {
	qhandle_t	shader;
	float		scroll;
	int			i;

	if ( n < 2 || !col ) {
		return;
	}
	if ( n > LAT_TRAIL_SEGS + 1 ) {
		n = LAT_TRAIL_SEGS + 1;
	}
	shader = cgs.media.latticeShader ? cgs.media.latticeShader : cgs.media.whiteShader;
	scroll = latRealMs * 0.006f * ( 1.0f - 0.4f * latSlow ) * ( 1.0f + 0.6f * latMid );

	for ( i = 0 ; i < n - 1 ; i++ ) {
		vec3_t	a, b;
		float	half, topA, botA, topB, botB;

		VectorCopy( pts[i], a );
		VectorCopy( pts[i + 1], b );
		half = LAT_WALL_MIN_HALF + 14.0f;		// modest fixed wall (no audio amp to ride)
		if ( half > LAT_WALL_MAX_HALF ) {
			half = LAT_WALL_MAX_HALF;
		}
		CG_LatticeEdge( i,     half, &topA, &botA );
		CG_LatticeEdge( i + 1, half, &topB, &botB );
		CG_LatticeQuad( shader, a, b, col, i, i + 1, n, scroll, aMul, latSlow,
			topA, botA, topB, botB );
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

	// Trails draw in LATTICE mode, OR whenever cg_arenaTrails is on — the
	// rendering is pure client-side (reconstructed from observed positions +
	// the live music bands), so it works in any gametype as a visual-only
	// effect with no damage/elimination. Lets bots leave flowing, pumping
	// speed-trails in the sword arena.
	if ( ( !cgs.lattice && !cg_arenaTrails.integer ) || !cg.snap ) {
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

	// the wall-top waveform sample: overall loudness for the body of the curve,
	// with the kick punching extra peaks. Captured into each point as it's laid
	// (CG_LatticePush) so the height is locked to WHERE you were, not when it draws.
	latWaveNow = latLevel * 0.6f + latBass * 0.7f;

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
