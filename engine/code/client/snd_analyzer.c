/*
===========================================================================
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

// snd_analyzer.c -- backend-agnostic audio-reactive analysis.
//
// As background music decodes, we split it into three frequency bands with
// cheap one-pole filters and run an asymmetric envelope follower on each:
// near-instant attack, slow release. The slow release is what makes the world
// "bounce" on the kick instead of jittering. The envelopes are published to
// cvars (au_bass/au_mid/au_high/au_level) that the renderer samples as a
// waveform source, so any shader's deformVertexes / rgbGen / tcMod can ride
// the music. Works for any format with zero per-track config.
//
// This used to live inside snd_codec_mod.c and only ran for tracker tunes.
// It now sits on S_CodecReadStream — the single chokepoint both the DMA and
// the OpenAL music backends pull decoded PCM through — so .wav, .ogg, .opus,
// .mp3 and tracker modules all drive the shaders identically. The analyser
// takes the stream's real rate/width/channels, so the filter corners stay put
// in Hz regardless of the file's sample rate.
//
// One-pole coefficient for a corner frequency fc: a = 1 - exp(-2*pi*fc/fs),
// approximated here as 2*pi*fc/fs (fc << fs, plenty accurate for VU).

#include "client.h"
#include "snd_codec.h"

#define AU_BASS_HZ		150.0f	// everything below ~ kick + sub-bass
#define AU_HIGH_HZ		3000.0f	// everything above ~ hats + cymbals; mid is between
#define AU_ENV_ATTACK	0.5f	// per-sample rise toward a louder peak (snappy)

// release/gains were tuned at 44.1 kHz against the bundled DnB/jungle masters
// so each band peaks near ~1 on a hot track; quieter material simply warps
// less. The release is rate-compensated below so the ~190 ms tail (the bounce)
// holds at any sample rate.
#define AU_ENV_RELEASE_44K	0.00012f	// per-sample fall at 44100 Hz
#define AU_REF_RATE			44100.0f

// per-band post gains: highs are quieter than bass, so lift them to land each
// envelope roughly in 0..1 for typical material.
#define AU_GAIN_BASS	3.0f
#define AU_GAIN_MID		3.0f
#define AU_GAIN_HIGH	7.0f
#define AU_GAIN_LEVEL	2.5f

typedef struct {
	// filter memory (carried across decode blocks)
	float	lpBass;		// running low-pass at AU_BASS_HZ
	float	lpHigh;		// running low-pass at AU_HIGH_HZ (high = x - this)
	// envelope state
	float	envBass, envMid, envHigh, envLevel;
} audioAnalysis_t;

static audioAnalysis_t	s_analysis;

void S_AudioAnalyzeReset( void )
{
	Com_Memset( &s_analysis, 0, sizeof( s_analysis ) );
	Cvar_SetValue( "au_bass",  0.0f );
	Cvar_SetValue( "au_mid",   0.0f );
	Cvar_SetValue( "au_high",  0.0f );
	Cvar_SetValue( "au_level", 0.0f );
}

static ID_INLINE void S_FollowEnv( float *env, float rectified, float release )
{
	if ( rectified > *env ) {
		*env += AU_ENV_ATTACK * ( rectified - *env );
	} else {
		*env += release * ( rectified - *env );
	}
}

// Decompose a block of interleaved PCM into band envelopes and push the result
// out to the audio-reactive cvars. Handles 8-bit unsigned / 16-bit signed and
// mono / stereo, since codecs differ; everything is folded to a mono float in
// roughly [-1,1] before the filter bank.
void S_AudioAnalyze( const void *pcm, int frames, int rate, int width, int channels )
{
	audioAnalysis_t	*a = &s_analysis;
	float			aBass, aHigh, release;
	int				i;

	if ( frames <= 0 || rate <= 0 || channels < 1 ) {
		return;
	}

	aBass = 2.0f * M_PI * AU_BASS_HZ / (float)rate;
	aHigh = 2.0f * M_PI * AU_HIGH_HZ / (float)rate;
	// keep the decay tail constant in real time across sample rates
	release = AU_ENV_RELEASE_44K * ( AU_REF_RATE / (float)rate );
	if ( release > 1.0f ) {
		release = 1.0f;
	}

	for ( i = 0; i < frames; i++ ) {
		float x;

		if ( width == 2 ) {
			const int16_t *s = (const int16_t *)pcm + i * channels;
			if ( channels >= 2 ) {
				x = ( (float)s[0] + (float)s[1] ) * ( 0.5f / 32768.0f );
			} else {
				x = (float)s[0] * ( 1.0f / 32768.0f );
			}
		} else {
			// 8-bit unsigned PCM, centred at 128
			const unsigned char *s = (const unsigned char *)pcm + i * channels;
			if ( channels >= 2 ) {
				x = ( ( (float)s[0] - 128.0f ) + ( (float)s[1] - 128.0f ) ) * ( 0.5f / 128.0f );
			} else {
				x = ( (float)s[0] - 128.0f ) * ( 1.0f / 128.0f );
			}
		}

		a->lpBass += aBass * ( x - a->lpBass );	// < AU_BASS_HZ
		a->lpHigh += aHigh * ( x - a->lpHigh );	// < AU_HIGH_HZ

		{
			float bass = a->lpBass;
			float mid  = a->lpHigh - a->lpBass;	// AU_BASS_HZ .. AU_HIGH_HZ
			float high = x - a->lpHigh;			// > AU_HIGH_HZ

			S_FollowEnv( &a->envBass,  bass < 0 ? -bass : bass, release );
			S_FollowEnv( &a->envMid,   mid  < 0 ? -mid  : mid,  release );
			S_FollowEnv( &a->envHigh,  high < 0 ? -high : high, release );
			S_FollowEnv( &a->envLevel, x    < 0 ? -x    : x,    release );
		}
	}

	Cvar_SetValue( "au_bass",  a->envBass  * AU_GAIN_BASS  );
	Cvar_SetValue( "au_mid",   a->envMid   * AU_GAIN_MID   );
	Cvar_SetValue( "au_high",  a->envHigh  * AU_GAIN_HIGH  );
	Cvar_SetValue( "au_level", a->envLevel * AU_GAIN_LEVEL );
}
