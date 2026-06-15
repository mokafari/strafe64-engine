/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.
Copyright (C) 2026 STRAFE 64

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

// snd_codec_mod.c -- tracker module (.it/.xm/.s3m/.mod/.mptm) background
// music, decoded by libopenmpt. The jukebox of the demoscene: load a
// jungle/DnB tracker tune as the level's music and it loops forever at
// its own song loop points.

#include "client.h"
#include "snd_codec.h"

#ifdef USE_CODEC_MOD

#include <libopenmpt/libopenmpt.h>

// libopenmpt renders at whatever rate we ask for; the engine resamples
// the background stream from info.rate to dma.speed, so any rate is fine.
// 44100 keeps the breakbeats crisp.
#define MOD_SAMPLERATE	44100

// ---------------------------------------------------------------------------
// Audio-reactive analysis
//
// As the module decodes, we split the music into three frequency bands with
// cheap one-pole filters and run an asymmetric envelope follower on each:
// near-instant attack, slow release. The slow release is what makes the world
// "bounce" on the kick instead of jittering. The envelopes are published to
// cvars (au_bass/au_mid/au_high/au_level) that the renderer samples as a
// waveform source, so any shader's deformVertexes / rgbGen / tcMod can ride
// the music. Works for any tracker tune with zero per-track config — DnB and
// jungle have huge sub-bass and a hard kick, so au_bass tracks the beat for
// free.
//
// One-pole coefficient for a corner frequency fc: a = 1 - exp(-2*pi*fc/fs),
// approximated here as 2*pi*fc/fs (fc << fs, plenty accurate for VU).
// ---------------------------------------------------------------------------
#define MOD_BASS_HZ		150.0f	// everything below ~ kick + sub-bass
#define MOD_HIGH_HZ		3000.0f	// everything above ~ hats + cymbals; mid is between
#define MOD_ENV_ATTACK	0.5f	// per-sample rise toward a louder peak (snappy)
#define MOD_ENV_RELEASE	0.00012f	// per-sample fall (~190 ms tail = the bounce)

// per-band post gains: highs are quieter than bass, so lift them to land each
// envelope roughly in 0..1 for typical material. Tune to taste with the music.
// tuned against the bundled DnB/jungle masters so each band peaks near ~1 on
// a hot track; quieter ambient tunes simply warp less.
#define MOD_GAIN_BASS	3.0f
#define MOD_GAIN_MID	3.0f
#define MOD_GAIN_HIGH	7.0f
#define MOD_GAIN_LEVEL	2.5f

typedef struct {
	// filter memory (carried across decode blocks)
	float	lpBass;		// running low-pass at MOD_BASS_HZ
	float	lpHigh;		// running low-pass at MOD_HIGH_HZ (high = x - this)
	// envelope state
	float	envBass, envMid, envHigh, envLevel;
} modAnalysis_t;

static modAnalysis_t	mod_analysis;

static void MOD_AnalysisReset( void )
{
	Com_Memset( &mod_analysis, 0, sizeof( mod_analysis ) );
}

static ID_INLINE void MOD_FollowEnv( float *env, float rectified )
{
	if ( rectified > *env ) {
		*env += MOD_ENV_ATTACK * ( rectified - *env );
	} else {
		*env += MOD_ENV_RELEASE * ( rectified - *env );
	}
}

// Decompose a block of interleaved stereo int16 PCM into band envelopes and
// push the result out to the audio-reactive cvars.
static void MOD_Analyze( const int16_t *pcm, int frames )
{
	const float		aBass = 2.0f * M_PI * MOD_BASS_HZ / (float)MOD_SAMPLERATE;
	const float		aHigh = 2.0f * M_PI * MOD_HIGH_HZ / (float)MOD_SAMPLERATE;
	modAnalysis_t	*a = &mod_analysis;
	int				i;

	for ( i = 0; i < frames; i++ ) {
		// mono mix, normalised to roughly [-1,1]
		float x = ( (float)pcm[i * 2] + (float)pcm[i * 2 + 1] ) * ( 0.5f / 32768.0f );

		a->lpBass += aBass * ( x - a->lpBass );	// < MOD_BASS_HZ
		a->lpHigh += aHigh * ( x - a->lpHigh );	// < MOD_HIGH_HZ

		{
			float bass = a->lpBass;
			float mid  = a->lpHigh - a->lpBass;	// MOD_BASS_HZ .. MOD_HIGH_HZ
			float high = x - a->lpHigh;			// > MOD_HIGH_HZ

			MOD_FollowEnv( &a->envBass,  bass < 0 ? -bass : bass );
			MOD_FollowEnv( &a->envMid,   mid  < 0 ? -mid  : mid  );
			MOD_FollowEnv( &a->envHigh,  high < 0 ? -high : high );
			MOD_FollowEnv( &a->envLevel, x    < 0 ? -x    : x    );
		}
	}

	Cvar_SetValue( "au_bass",  a->envBass  * MOD_GAIN_BASS  );
	Cvar_SetValue( "au_mid",   a->envMid   * MOD_GAIN_MID   );
	Cvar_SetValue( "au_high",  a->envHigh  * MOD_GAIN_HIGH  );
	Cvar_SetValue( "au_level", a->envLevel * MOD_GAIN_LEVEL );
}

snd_stream_t *S_MOD_CodecOpenStream( const char *filename )
{
	fileHandle_t		hnd;
	int					length;
	void				*filedata;
	openmpt_module		*mod;
	snd_stream_t		*stream;

	// tracker modules are small; libopenmpt parses them fully into its
	// own state, so we read the file into a temp buffer, hand it over,
	// and free the buffer — no file handle is kept open.
	length = FS_FOpenFileRead( filename, &hnd, qtrue );
	if ( !hnd ) {
		Com_DPrintf( "Can't read module file %s\n", filename );
		return NULL;
	}
	if ( length <= 0 ) {
		FS_FCloseFile( hnd );
		return NULL;
	}

	filedata = Z_Malloc( length );
	FS_Read( filedata, length, hnd );
	FS_FCloseFile( hnd );

	mod = openmpt_module_create_from_memory2( filedata, length,
		NULL, NULL, NULL, NULL, NULL, NULL, NULL );
	Z_Free( filedata );

	if ( !mod ) {
		Com_DPrintf( "%s is not a recognised tracker module\n", filename );
		return NULL;
	}

	// loop the song forever at its own loop points — jungle never stops
	openmpt_module_set_repeat_count( mod, -1 );

	// fresh tune, fresh envelopes
	MOD_AnalysisReset();

	stream = Z_Malloc( sizeof( snd_stream_t ) );
	// every entry shares these stream functions, so any one identifies
	// the close()/read() path for S_CodecCloseStream
	stream->codec = &mod_codec_it;
	stream->file = 0;
	stream->ptr = mod;
	stream->info.rate = MOD_SAMPLERATE;
	stream->info.width = 2;		// int16 samples
	stream->info.channels = 2;	// interleaved stereo
	stream->length = length;
	stream->pos = 0;

	return stream;
}

int S_MOD_CodecReadStream( snd_stream_t *stream, int bytes, void *buffer )
{
	openmpt_module	*mod = (openmpt_module *)stream->ptr;
	size_t			frames, got;

	if ( !mod ) {
		return 0;
	}

	frames = bytes / 4;	// stereo * int16 = 4 bytes per frame
	if ( frames == 0 ) {
		return 0;
	}

	got = openmpt_module_read_interleaved_stereo( mod, MOD_SAMPLERATE,
		frames, (int16_t *)buffer );

	// ride the beat: feed what we just decoded to the band analyser
	MOD_Analyze( (const int16_t *)buffer, (int)got );

	return (int)( got * 4 );
}

void S_MOD_CodecCloseStream( snd_stream_t *stream )
{
	if ( stream->ptr ) {
		openmpt_module_destroy( (openmpt_module *)stream->ptr );
	}
	Z_Free( stream );

	// music gone — collapse the warp so the world settles instead of
	// freezing at whatever the last envelope happened to be
	MOD_AnalysisReset();
	Cvar_SetValue( "au_bass",  0.0f );
	Cvar_SetValue( "au_mid",   0.0f );
	Cvar_SetValue( "au_high",  0.0f );
	Cvar_SetValue( "au_level", 0.0f );
}

void *S_MOD_CodecLoad( const char *filename, snd_info_t *info )
{
	// tracker modules are streaming background music only, never loaded
	// as in-memory sound effects
	return NULL;
}

// One codec table entry per recognised extension, all sharing the
// libopenmpt-backed stream functions. The lookup in snd_codec.c matches
// a single extension per entry, so we register each.
#define MOD_CODEC( name, extension ) \
	snd_codec_t name = { \
		extension, \
		S_MOD_CodecLoad, \
		S_MOD_CodecOpenStream, \
		S_MOD_CodecReadStream, \
		S_MOD_CodecCloseStream, \
		NULL \
	}

MOD_CODEC( mod_codec_it,   "it"   );
MOD_CODEC( mod_codec_xm,   "xm"   );
MOD_CODEC( mod_codec_s3m,  "s3m"  );
MOD_CODEC( mod_codec_mod,  "mod"  );
MOD_CODEC( mod_codec_mptm, "mptm" );

#endif // USE_CODEC_MOD
