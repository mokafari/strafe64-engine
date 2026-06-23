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

#define LAT_PTS			288		// points retained per pilot (client-side render ring)
#define LAT_SAMPLE_MS	16		// sample cadence (~60 Hz), independent of render rate
#define LAT_STEP		8.0f	// min carve distance between stored points (u) -- finer = denser/smoother curve
#define LAT_TRAIL_SEGS	144		// how many tail segments to actually draw
// The wall is a band CENTRED on the pilot that fattens symmetrically with the music --
// it emanates from the player: a thin line at the centre when quiet, growing equally up
// and down, and at the loudest peak it reaches exactly the PLAYER BOX -- bottom at the
// feet (floor), top at the head. Sized to the box (feet origin-24, head origin+32) so it
// never towers past the pilot, and started thin so only the loudest hits reach the floor
// (quiet passages stay well clear of the deck instead of clipping it).
#define LAT_WALL_MID		4.0f	// vertical centre = player box centre, so the wall comes FROM the pilot
#define LAT_WALL_MIN_HALF	2.0f	// quiet half-height -- a thin centred line emanating from the player
#define LAT_WALL_MAX_HALF	28.0f	// LOUD CAP = player half-height (centre+28 = head, centre-28 = feet/floor)
#define LAT_ALPHA			0.92f	// head opacity (wall presence)
#define LAT_WAVE_HALF		26.0f	// amplitude->half-height gain (clamped at MAX_HALF), * cg_latticeWave

// ARENA velocity ribbon (cg_arenaTrails, NOT full LATTICE mode): a SHORT trail
// that DISSOLVES on a real-time TTL, so its visible length tracks how far you
// moved in the last few hundred ms = your speed. Stop and it fades to nothing.
#define LAT_ARENA_SEGS		110		// max tail; the TTL below is the real limiter
#define LAT_ARENA_TTL		360.0f	// base game-ms a point lives before it dissolves
#define LAT_ARENA_STRETCH	3.2f	// slow-mo stretch: trail lives (1 + STRETCH*latSlow)x
									// longer in bullet-time -> it lengthens as time dilates
#define LAT_ARENA_HALF		9.0f	// band base half-height; pumps out from the player on bass
#define LAT_ARENA_MID		14.0f	// centre the arena band + chips on the BODY (chest), off the ground

// Distinct per-pilot wall colours so three carving trails read apart at speed,
// regardless of the players' chosen model colours. Indexed by client number.
// CYBERPUNK ORANGE<->TEAL DUOTONE: the classic blockbuster pairing, kept neon and
// saturated so it BLOOMS as coloured light rather than washing to white. Clients
// alternate warm/cool (0 you = hot orange, 1 = electric teal -> instant arena duel
// read), with a hot-magenta and acid-lime accent for 3+ player spice. Values are
// pushed toward primaries (one channel near 0) so the hue survives the bloom.
static const byte latPalette[8][3] = {
	{ 255,  96,   0 },	// 0 hot neon orange
	{   0, 224, 205 },	// 1 electric teal
	{ 255, 158,  20 },	// 2 amber / tangerine
	{   0, 196, 255 },	// 3 aqua cyan
	{ 255,  40, 150 },	// 4 hot magenta (accent pop)
	{  40, 255, 170 },	// 5 spring mint-teal
	{ 255,  70,  40 },	// 6 molten coral
	{ 180, 110, 255 },	// 7 ultraviolet (accent pop)
};

// cheap integer hash -> pseudo-random, for the deterministic per-segment glitch
// (no Math.random in this VM; key on a stable segment id + a time bucket)
static unsigned latHash( unsigned a, unsigned b ) {
	unsigned h = a * 374761393u + b * 668265263u;
	h = ( h ^ ( h >> 13 ) ) * 1274126177u;
	return h ^ ( h >> 16 );
}

// HOLOGRAPHIC RAMP: HSV->RGB (h wraps, s/v in 0..1) baked into byte rgb. The
// datamosh chips ride this oil-slick ramp (hue swept by chip id + real time +
// the bass band) so the corruption reads as iridescent neon film instead of
// dim grey squares.
static void CG_LatticeHueRGB( float h, float s, float v, byte out[3] ) {
	float	r, g, b, f, p, q, t;
	int		i;

	h -= (float)( (int)h );			// wrap to 0..1
	if ( h < 0.0f ) h += 1.0f;
	i = (int)( h * 6.0f );
	f = h * 6.0f - (float)i;
	p = v * ( 1.0f - s );
	q = v * ( 1.0f - s * f );
	t = v * ( 1.0f - s * ( 1.0f - f ) );
	switch ( i % 6 ) {
		case 0:  r = v; g = t; b = p; break;
		case 1:  r = q; g = v; b = p; break;
		case 2:  r = p; g = v; b = t; break;
		case 3:  r = p; g = q; b = v; break;
		case 4:  r = t; g = p; b = v; break;
		default: r = v; g = p; b = q; break;
	}
	out[0] = (byte)( r * 255.0f );
	out[1] = (byte)( g * 255.0f );
	out[2] = (byte)( b * 255.0f );
}

static vec3_t	latPts[MAX_CLIENTS][LAT_PTS];
static float	latAmp[MAX_CLIENTS][LAT_PTS];	// music amplitude captured AS each point was laid -> the wall-top waveform
static int		latTime[MAX_CLIENTS][LAT_PTS];	// real-ms each point was laid -> arena-trail TTL dissolve
static int		latCount[MAX_CLIENTS];
static int		latHead[MAX_CLIENTS];
// live (uncommitted) head: the pilot's CURRENT position, updated every frame. The
// leading bar stretches from here to the newest committed point, so fresh wall grows
// in continuously instead of a full step-length bar popping in on each sample tick.
static vec3_t	latLive[MAX_CLIENTS];
static float	latLiveAmp[MAX_CLIENTS];	// waveform amplitude at the live head
static qboolean	latLiveOn[MAX_CLIENTS];		// this pilot was tracked this frame
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
		latLiveOn[i] = qfalse;
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
CG_LatticeTrack

Per-frame tracking for one pilot. ALWAYS updates the live head (so the leading bar
follows the player every frame), but only commits a new ring point on the sample
cadence + carve-distance gate. Decoupling the two is what removes the head "pop":
the bar grows smoothly from zero to one step, then the commit lands exactly on the
live head, so there's no visible jump.
================
*/
static void CG_LatticeTrack( int client, const vec3_t origin, qboolean commit ) {
	if ( client < 0 || client >= MAX_CLIENTS ) {
		return;
	}
	VectorCopy( origin, latLive[client] );
	latLiveAmp[client] = latWaveNow;
	latLiveOn[client] = qtrue;
	if ( commit ) {
		CG_LatticePush( client, origin );
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
	float	lo    = 0.55f + 0.40f * latLevel;					// brighter resting floor + more level lift
	float	hi    = 1.05f + 0.65f * slow + 1.35f * latBass;	// kick SLAMS the brightness (stays coloured)
	// idx coefficient + molten-head span are tuned to SPATIAL wavelength: the carve
	// step shrank (denser ring), so these scale up to keep the same physical look.
	float	pulse = lo + ( hi - lo ) * 0.5f * ( 1.0f + sin( idx * 0.18f - scroll ) );
	float	hot   = ( idx < 12 ) ? ( 1.0f - idx * 0.083f ) : 0.0f;	// molten white leading edge
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
	float		mid    = latArenaDraw ? LAT_ARENA_MID : LAT_WALL_MID;	// arena: chest-centred
	*top = mid + half + ebreak * nt;
	*bot = mid - half - ebreak * nb;
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
#define LAT_BLK_CAND	6		// candidate chips considered per segment (wall)
#define LAT_ARENA_BLK	14		// arena ribbon: denser, finer datamosh grain
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
	// fraction of candidates that fire. ARENA: GLITCH ON THE BEAT — a low resting
	// base keeps the ribbon near-clean between hits, then the kick (bass) sprays
	// the datamosh across it and highs add sparkle, so the corruption punches in
	// time with the track instead of sitting as a constant grain. WALL keeps its
	// highs-driven spray.
	if ( latArenaDraw ) {
		dense = 0.10f + 0.85f * latBass + 0.20f * highs;
	} else {
		dense = 0.18f + 0.55f * highs;
	}
	if ( dense > 0.97f ) dense = 0.97f;

	for ( k = 0 ; k < ( latArenaDraw ? LAT_ARENA_BLK : LAT_BLK_CAND ) ; k++ ) {
		unsigned	h = latHash( key * 2246822519u + k * 2654435761u, tbucket );
		unsigned	h2 = latHash( h, 0x9E3779B9u );		// decorrelated entropy for chip shape
		float		t, zt, szw, szh, tear, al;
		const byte	*bc;
		byte		irid[3];
		vec3_t		c, du, toCam;
		polyVert_t	v[4];

		if ( ( h & 1023 ) / 1023.0f > dense ) {
			continue;					// this chip is dark this tick
		}
		t    = ( ( h >> 4 ) & 255 ) / 255.0f;			// position along the segment
		zt   = ( ( h >> 12 ) & 255 ) / 255.0f;			// height within the band
		// ARENA: varied RECTANGULAR datamosh macroblocks (independent width/height
		// from a decorrelated hash, wide range) so the grain reads as organic noise
		// instead of a tiling of identical squares. WALL: uniform square.
		if ( latArenaDraw ) {
			szw = 1.5f + ( h2 & 15 ) * 0.55f;			// ~1.5..9.8u wide
			szh = 1.5f + ( ( h2 >> 4 ) & 15 ) * 0.40f;	// ~1.5..7.5u tall
		} else {
			szw = szh = 2.0f + ( ( h >> 20 ) & 7 );
		}
		tear = ( (int)( ( h >> 6 ) & 15 ) - 8 ) * 0.5f * glitch;	// sideways jitter

		VectorMA( a, t * seglen, segdir, c );
		// vertical placement WITHIN the bar. Critical: c already holds the
		// interpolated trail-point z (the pilot's height), so we ADD the band
		// offset (centre mid + spread) instead of OVERWRITING z — otherwise the
		// chips pin to an absolute ~mid near the floor while the bar rides the
		// pilot, and they never sit on the stream. ARENA spreads across the full
		// band height (layer the whole ribbon); WALL cubes toward the centre core.
		{
			float vt = zt * 2.0f - 1.0f;
			if ( !latArenaDraw ) {
				vt = vt * vt * vt;
			}
			c[2] += zc + vt * halfH;
		}
		VectorMA( c, tear, perp, c );
		// nudge toward the camera so the chip sits just proud of the wall
		VectorSubtract( cg.refdef.vieworg, c, toCam );
		VectorNormalizeFast( toCam );
		VectorMA( c, 1.5f, toCam, c );

		// HOLOGRAPHIC DATAMOSH: every chip rides the oil-slick hue ramp instead of
		// dim pilot-grey or the old hardcoded channel flash. Hue sweeps along the
		// segment (t), drifts on the real clock, jitters per-chip (h2) and shoves
		// on the bass so the corruption shimmers through the full neon spectrum.
		{
			float hue = 0.55f					// teal-anchored start (oil-slick)
				+ t * 0.40f						// gradient down the segment
				+ ( ( h2 & 255 ) / 255.0f ) * 0.18f	// per-chip jitter
				+ latRealMs * 0.00010f			// slow iridescent drift
				+ latBass * 0.22f;				// beat shoves the band
			CG_LatticeHueRGB( hue, 0.92f, 1.0f, irid );
			bc = irid;
		}
		// chips glow as bright iridescent film. A real brightness FLOOR (not gated on
		// the bass envelope, which is ~0 without strong audio) keeps them reading as
		// neon — the kick then flares them harder on the beat.
		al = ( ( latArenaDraw ? 0.70f + 0.40f * latBass : 0.60f )
			+ ( latArenaDraw ? 0.22f : 0.40f ) * ( ( ( h >> 24 ) & 7 ) / 7.0f ) ) * age;
		if ( al > 1.0f ) al = 1.0f;

		// four corners of the chip: c +/- du (width along trail) +/- szh (height)
		VectorScale( segdir, szw, du );
		VectorSubtract( c, du, v[0].xyz ); v[0].xyz[2] -= szh; v[0].st[0]=0; v[0].st[1]=1;
		VectorAdd( c, du, v[1].xyz );      v[1].xyz[2] -= szh; v[1].st[0]=1; v[1].st[1]=1;
		VectorAdd( c, du, v[2].xyz );      v[2].xyz[2] += szh; v[2].st[0]=1; v[2].st[1]=0;
		VectorSubtract( c, du, v[3].xyz ); v[3].xyz[2] += szh; v[3].st[0]=0; v[3].st[1]=0;
		for ( j = 0 ; j < 4 ; j++ ) {
			v[j].modulate[0] = bc[0];
			v[j].modulate[1] = bc[1];
			v[j].modulate[2] = bc[2];
			v[j].modulate[3] = (byte)( al * 255.0f );
		}
		trap_R_AddPolyToScene( latArenaDraw ? cgs.media.datamoshShader : shader, 4, v );
	}
}

// HOT-WIRE CREST: a thin, bright filament hugging the wall's TOP edge so the
// silhouette reads as a glowing neon wire. Brightness rides level+bass and the
// molten head, and folds in the segment's age/glitch fade (aMul). Drawn on the
// wall shader (alpha-blended) so it fades cleanly and blooms on GL2.
static void CG_LatticeCrest( qhandle_t shader, const vec3_t a, const vec3_t b,
		float topA, float topB, const byte *col, int ia, int n, float aMul ) {
	polyVert_t	v[4];
	int			j;
	float		age    = 1.0f - ( ia / (float)( n - 1 ) );
	float		hot    = ( ia < 12 ) ? ( 1.0f - ia * 0.083f ) : 0.0f;
	float		bright = ( 0.55f + 0.45f * latLevel + 1.0f * latBass ) * age * aMul;
	float		w      = 0.55f + 0.45f * hot;		// crest is whiter than the body
	float		thick  = 3.5f + 2.5f * latBass;		// the wire fattens on the kick
	byte		cr, cg_, cb;

	if ( bright <= 0.01f ) {
		return;
	}
	if ( bright > 1.0f ) bright = 1.0f;
	cr  = (byte)( col[0] + ( 255 - col[0] ) * w );
	cg_ = (byte)( col[1] + ( 255 - col[1] ) * w );
	cb  = (byte)( col[2] + ( 255 - col[2] ) * w );

	VectorCopy( a, v[0].xyz ); v[0].xyz[2] += topA;         v[0].st[0]=0; v[0].st[1]=0;
	VectorCopy( a, v[1].xyz ); v[1].xyz[2] += topA - thick; v[1].st[0]=0; v[1].st[1]=1;
	VectorCopy( b, v[2].xyz ); v[2].xyz[2] += topB - thick; v[2].st[0]=1; v[2].st[1]=1;
	VectorCopy( b, v[3].xyz ); v[3].xyz[2] += topB;         v[3].st[0]=1; v[3].st[1]=0;
	for ( j = 0 ; j < 4 ; j++ ) {
		v[j].modulate[0] = cr;
		v[j].modulate[1] = cg_;
		v[j].modulate[2] = cb;
		v[j].modulate[3] = (byte)( bright * 255.0f );
	}
	trap_R_AddPolyToScene( shader, 4, v );
}

// CREST EMBERS: tiny soft sparks shed from the wall's top edge, twinkling on the
// audio highs and drifting UP as they fade -- the wall bleeding energy into the
// air. Deterministic (hash on a stable segment id + a slow time bucket) so they
// flicker without a RNG, and camera-billboarded so they read as round glows.
#define LAT_EMBERS	3		// candidate embers per segment
static void CG_LatticeEmbers( const vec3_t a, const vec3_t b, float topA, float topB,
		const byte *col, unsigned key, unsigned tbucket, float highs, float age ) {
	int		k, j;

	if ( age <= 0.05f || highs <= 0.02f ) {
		return;
	}
	for ( k = 0 ; k < LAT_EMBERS ; k++ ) {
		unsigned	h     = latHash( key * 2246822519u + k * 40503u, tbucket );
		float		dense = 0.10f + 0.55f * highs;
		float		t, life, lift, sz, al;
		vec3_t		c, right, upv;
		polyVert_t	v[4];

		if ( ( h & 1023 ) / 1023.0f > dense ) {
			continue;					// this ember is dark this tick
		}
		t    = ( ( h >> 4 ) & 255 ) / 255.0f;					// position along the segment
		life = ( ( tbucket * 2654435761u + h ) & 255 ) / 255.0f;	// 0..1 drift progress
		lift = 2.0f + life * 16.0f;								// rises off the crest as it ages
		sz   = 1.3f + ( ( h >> 12 ) & 7 ) * 0.4f;
		al   = ( 1.0f - life ) * age * ( 0.45f + 0.55f * highs );
		if ( al <= 0.02f ) {
			continue;
		}
		if ( al > 1.0f ) al = 1.0f;

		// point on the crest line, then lifted into the air
		VectorSubtract( b, a, c );
		VectorMA( a, t, c, c );
		c[2] += topA + ( topB - topA ) * t + lift;

		// camera-facing billboard from the view axes
		VectorScale( cg.refdef.viewaxis[1], sz, right );
		VectorScale( cg.refdef.viewaxis[2], sz, upv );
		VectorSubtract( c, right, v[0].xyz ); VectorSubtract( v[0].xyz, upv, v[0].xyz ); v[0].st[0]=0; v[0].st[1]=1;
		VectorAdd( c, right, v[1].xyz );      VectorSubtract( v[1].xyz, upv, v[1].xyz ); v[1].st[0]=1; v[1].st[1]=1;
		VectorAdd( c, right, v[2].xyz );      VectorAdd( v[2].xyz, upv, v[2].xyz );      v[2].st[0]=1; v[2].st[1]=0;
		VectorSubtract( c, right, v[3].xyz ); VectorAdd( v[3].xyz, upv, v[3].xyz );      v[3].st[0]=0; v[3].st[1]=0;
		for ( j = 0 ; j < 4 ; j++ ) {
			v[j].modulate[0] = (byte)( col[0] + ( 255 - col[0] ) * 0.5f );
			v[j].modulate[1] = (byte)( col[1] + ( 255 - col[1] ) * 0.5f );
			v[j].modulate[2] = (byte)( col[2] + ( 255 - col[2] ) * 0.5f );
			v[j].modulate[3] = (byte)( al * 255.0f );
		}
		trap_R_AddPolyToScene( cgs.media.trailGlowShader, 4, v );
	}
}

static void CG_LatticeDraw( int client ) {
	int			i, n, idx0, idx1, head, maxSegs;
	const byte	*col;
	qhandle_t	shader;
	float		scroll, glitch, capHalf, arenaTtl;
	unsigned	tbucket;
	qboolean	arena, isSelf;

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
	isSelf = ( cg.snap && client == cg.snap->ps.clientNum );	// our own trail -> fade near the eye

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

	// LEADING BAR: a live quad from the pilot's CURRENT position to the newest
	// committed point. It is ~zero-length right after a commit (invisible) and
	// stretches to one full step just before the next, so new wall GROWS IN
	// instead of popping. Kept clean (no glitch/chips) — it's only the freshest
	// step at the very head where the white-hot edge dominates anyway.
	if ( latLiveOn[client] ) {
		int			newest = ( latHead[client] - 1 + LAT_PTS ) % LAT_PTS;
		vec3_t		la, lb;
		float		halfA, halfB, topA, botA, topB, botB, aMul = 1.0f;
		qboolean	draw = qtrue;

		VectorCopy( latLive[client], la );
		VectorCopy( latPts[client][newest], lb );

		// self arena trail: hide right at the eye, matching the loop's near-eye fade
		if ( isSelf && arena ) {
			vec3_t	mid;
			float	d, nf;
			VectorAdd( la, lb, mid );
			VectorScale( mid, 0.5f, mid );
			d  = Distance( cg.refdef.vieworg, mid );
			nf = ( d - 96.0f ) / 150.0f;
			if ( nf < 0.0f ) nf = 0.0f; else if ( nf > 1.0f ) nf = 1.0f;
			aMul = nf;
			if ( aMul <= 0.0f ) draw = qfalse;
		}

		if ( draw ) {
			if ( arena ) {
				float pump = LAT_ARENA_HALF
					* ( 0.16f + 1.05f * latBass + 0.16f * latLevel ) * cg_latticeWave.value;
				if ( pump > LAT_ARENA_HALF * 1.5f ) pump = LAT_ARENA_HALF * 1.5f;
				if ( pump < 1.2f ) pump = 1.2f;
				halfA = halfB = pump;
			} else {
				halfA = LAT_WALL_MIN_HALF + latLiveAmp[client] * waveScale;
				halfB = LAT_WALL_MIN_HALF + latAmp[client][newest] * waveScale;
				if ( halfA > capHalf ) halfA = capHalf;
				if ( halfB > capHalf ) halfB = capHalf;
			}
			// b-end shares the newest committed point's ragged key so the edge stays
			// continuous with the loop's first segment; a-end keys on the slot the
			// live head will commit into (stable until then).
			CG_LatticeEdge( latHead[client], halfA, &topA, &botA );
			CG_LatticeEdge( newest,          halfB, &topB, &botB );
			CG_LatticeQuad( shader, la, lb, col, 0, 0, n, scroll, aMul, latSlow,
				topA, botA, topB, botB );
		}
	}

	for ( i = 0 ; i < n - 1 ; i++ ) {
		vec3_t		a, b, dir, perp;
		const byte	*segCol = col;
		float		aMul = 1.0f;
		float		arenaFade = 1.0f;
		float		selfFade = 1.0f;
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

		// SELF: fade out our OWN trail near the camera so the head doesn't smear
		// across the first-person view. Invisible up close, fading in with
		// distance so the extended trail still reads when you look back. In
		// third-person/dead the eye is far from the body, so it shows normally.
		if ( isSelf && arena ) {
			vec3_t	mid;
			float	d, nf;
			VectorAdd( a, b, mid );
			VectorScale( mid, 0.5f, mid );
			d  = Distance( cg.refdef.vieworg, mid );
			nf = ( d - 96.0f ) / 150.0f;		// 0 within 96u, full by ~246u
			if ( nf < 0.0f ) nf = 0.0f; else if ( nf > 1.0f ) nf = 1.0f;
			selfFade  = nf;
			arenaFade *= nf;
			if ( arenaFade <= 0.0f ) {
				continue;	// fully hidden right around the eye
			}
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
				* ( 0.16f + 1.05f * latBass + 0.16f * latLevel ) * cg_latticeWave.value;
			if ( pump > LAT_ARENA_HALF * 1.5f ) pump = LAT_ARENA_HALF * 1.5f;
			if ( pump < 1.2f ) pump = 1.2f;
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
				// FULL datamosh in BOTH arena + wall now. The arena glitch quads used to
				// read as BLACK blocks (alpha-blend on the lattice shader gets crushed by
				// the GL2 HDR/tonemap), so the ghosts + chips are now drawn ADDITIVELY
				// (dmShader below) — they bloom into neon glitch instead of going black.
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

		// chromatic split: red/blue ghosts shoved opposite ways (drawn first, under
		// the core) — the classic datamosh colour fringe. In ARENA they ride the
		// ADDITIVE datamosh shader so they bloom to neon instead of crushing to black
		// under HDR; the big LATTICE wall keeps the alpha-blend ghosts.
		if ( torn && split > 0 ) {
			qhandle_t	dmShader = arena ? cgs.media.datamoshShader : shader;
			vec3_t ra, rb;
			VectorMA( a,  split, perp, ra ); VectorMA( b,  split, perp, rb );
			CG_LatticeQuad( dmShader, ra, rb, latGlitchRed, i, i + 1, n, scroll, aMul * 0.6f, latSlow, topA, botA, topB, botB );
			VectorMA( a, -split, perp, ra ); VectorMA( b, -split, perp, rb );
			CG_LatticeQuad( dmShader, ra, rb, latGlitchBlue, i, i + 1, n, scroll, aMul * 0.6f, latSlow, topA, botA, topB, botB );
		}

		CG_LatticeQuad( shader, a, b, segCol, i, i + 1, n, scroll, aMul, latSlow, topA, botA, topB, botB );

		// --- detail layers riding this segment (shared age fade) ---
		{
			float	segAge  = ( 1.0f - ( i / (float)( n - 1 ) ) ) * selfFade;	// fade own near-eye detail
			float	halfMid = 0.5f * ( halfA + halfB );
			unsigned segKey = idx0 * 2654435761u + client;

			// glowing neon filament along the top edge (the wall's "hot wire")
			CG_LatticeCrest( shader, a, b, topA, topB, col, i, n, aMul );

			// sparks shed from the crest, sprayed by the audio highs
			CG_LatticeEmbers( a, b, topA, topB, col, segKey, tbucket, latHigh, segAge );

			// micro-glitch chips: the fine "alive" detail. Uses the (possibly torn)
			// a/b so chips follow a glitched segment's shove.
			if ( glitch > 0 ) {
				CG_LatticeBlocks( shader, a, b, perp, segCol,
					arena ? LAT_ARENA_MID : LAT_WALL_MID, halfMid,
					segKey, tbucket, glitch, latHigh, segAge );
			}
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

		// our OWN head light would flood the first-person view — skip it, and
		// start the trail lights further back so nothing glares right at the eye.
		if ( !isSelf ) {
			VectorCopy( latPts[client][head], lp );
			lp[2] += 30.0f;
			trap_R_AddLightToScene( lp, hb, lr, lg, lb );
		}

		// stride/start-back are in POINTS; the ring is ~2.5x denser now, so the
		// numbers grow to keep the same spacing in WORLD units. A couple more lights
		// + a bass pulse make the trail glow richer on the beat (cheap on GL2).
		lit = 0;
		for ( i = ( isSelf ? 44 : 0 ) ; i < n && lit < 9 ; i += 22, lit++ ) {
			float fade = 1.0f - ( i / (float)( n ) );
			lidx = ( latHead[client] - 1 - i + LAT_PTS * 2 ) % LAT_PTS;
			VectorCopy( latPts[client][lidx], lp );
			lp[2] += 30.0f;
			trap_R_AddLightToScene( lp, ( 45.0f + 55.0f * fade ) * ( 1.0f + 0.4f * latBass ), lr, lg, lb );
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

	// the live head follows every pilot EVERY frame (smooth leading bar); a new
	// ring point is only committed on the sample cadence. Clear presence first so
	// pilots who left this frame stop extending their live head.
	for ( i = 0 ; i < MAX_CLIENTS ; i++ ) {
		latLiveOn[i] = qfalse;
	}

	doSample = ( cg.time - latLastSample >= LAT_SAMPLE_MS );
	if ( doSample ) {
		latLastSample = cg.time;
	}

	// local pilot from the predicted state (smoother than the snapshot)
	if ( cg.snap->ps.stats[STAT_HEALTH] > 0
		&& cg.snap->ps.pm_type == PM_NORMAL ) {
		CG_LatticeTrack( cg.snap->ps.clientNum, cg.predictedPlayerState.origin, doSample );
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
		CG_LatticeTrack( client, cent->lerpOrigin, doSample );
	}

	// draw every pilot's wall
	for ( i = 0 ; i < MAX_CLIENTS ; i++ ) {
		CG_LatticeDraw( i );
	}
}
