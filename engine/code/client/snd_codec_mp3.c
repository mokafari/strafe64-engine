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

// snd_codec_mp3.c -- MP3 background music, decoded by libmpg123. Mirrors the
// tracker-module codec: the whole file is slurped into memory and handed to
// libmpg123's feed reader, then S_CodecReadStream pulls interleaved signed-16
// PCM out a chunk at a time. The audio-reactive band analysis rides on
// S_CodecReadStream (see snd_analyzer.c), so an .mp3 drives the shaders just
// like an .ogg or a tracker tune.

#include "client.h"
#include "snd_codec.h"

#ifdef USE_CODEC_MP3

#include <mpg123.h>

// how much PCM to pull while probing for the stream's output format
#define MP3_PROBE_BYTES		32768
// safety bound on the probe loop so a bogus file can never spin forever
#define MP3_PROBE_TRIES		256

typedef struct {
	mpg123_handle	*mh;
	byte			*pending;		// PCM decoded during the format probe
	int				pendingLen;		// bytes valid in 'pending'
	int				pendingPos;		// bytes already handed back to the caller
} mp3Context_t;

static qboolean	mp3_initialized = qfalse;

static void S_MP3_EnsureInit( void )
{
	if ( !mp3_initialized ) {
		// no-op on libmpg123 >= 1.27 (auto-initialised) but harmless and
		// keeps us correct against older copies
		mpg123_init();
		mp3_initialized = qtrue;
	}
}

snd_stream_t *S_MP3_CodecOpenStream( const char *filename )
{
	fileHandle_t	hnd;
	int				length;
	void			*filedata;
	mpg123_handle	*mh;
	mp3Context_t	*ctx;
	snd_stream_t	*stream;
	const long		*rates;
	size_t			numRates, i;
	byte			probe[MP3_PROBE_BYTES];
	size_t			done;
	int				ret, tries;
	long			rate;
	int				channels, encoding;
	int				err;

	// MP3 files are pulled wholesale into memory and parsed by libmpg123 from
	// its own copy (mpg123_feed copies the data), so no file handle is kept.
	length = FS_FOpenFileRead( filename, &hnd, qtrue );
	if ( !hnd ) {
		Com_DPrintf( "Can't read mp3 file %s\n", filename );
		return NULL;
	}
	if ( length <= 0 ) {
		FS_FCloseFile( hnd );
		return NULL;
	}

	S_MP3_EnsureInit();

	mh = mpg123_new( NULL, &err );
	if ( !mh ) {
		Com_DPrintf( "mpg123_new failed for %s: %s\n", filename,
			mpg123_plain_strerror( err ) );
		FS_FCloseFile( hnd );
		return NULL;
	}

	// quiet, and decode everything to native-rate signed-16 (mono or stereo) —
	// no resampling, the engine resamples the background stream to dma.speed
	mpg123_param( mh, MPG123_ADD_FLAGS, MPG123_QUIET, 0.0 );
	mpg123_format_none( mh );
	mpg123_rates( &rates, &numRates );
	for ( i = 0; i < numRates; i++ ) {
		mpg123_format( mh, rates[i], MPG123_MONO | MPG123_STEREO,
			MPG123_ENC_SIGNED_16 );
	}

	if ( mpg123_open_feed( mh ) != MPG123_OK ) {
		Com_DPrintf( "mpg123_open_feed failed for %s\n", filename );
		mpg123_delete( mh );
		FS_FCloseFile( hnd );
		return NULL;
	}

	filedata = Z_Malloc( length );
	FS_Read( filedata, length, hnd );
	FS_FCloseFile( hnd );
	ret = mpg123_feed( mh, (const unsigned char *)filedata, length );
	Z_Free( filedata );

	if ( ret != MPG123_OK ) {
		Com_DPrintf( "mpg123_feed failed for %s\n", filename );
		mpg123_delete( mh );
		return NULL;
	}

	// decode until the first PCM arrives; that also nails down the output
	// format. Anything that yields no audio at all isn't a usable MP3.
	done = 0;
	for ( tries = 0; tries < MP3_PROBE_TRIES; tries++ ) {
		ret = mpg123_read( mh, probe, sizeof( probe ), &done );
		if ( done > 0 ) {
			break;
		}
		if ( ret == MPG123_NEW_FORMAT || ret == MPG123_OK ) {
			continue;
		}
		break;	// MPG123_DONE / MPG123_NEED_MORE / error with no audio
	}

	if ( done == 0 ) {
		Com_DPrintf( "%s is not a usable MP3 stream\n", filename );
		mpg123_delete( mh );
		return NULL;
	}

	if ( mpg123_getformat( mh, &rate, &channels, &encoding ) != MPG123_OK ) {
		mpg123_delete( mh );
		return NULL;
	}

	ctx = Z_Malloc( sizeof( mp3Context_t ) );
	ctx->mh = mh;
	ctx->pendingLen = (int)done;
	ctx->pendingPos = 0;
	ctx->pending = Z_Malloc( (int)done );
	Com_Memcpy( ctx->pending, probe, done );

	stream = Z_Malloc( sizeof( snd_stream_t ) );
	stream->codec = &mp3_codec;
	stream->file = 0;
	stream->ptr = ctx;
	stream->info.rate = (int)rate;
	stream->info.width = 2;			// MPG123_ENC_SIGNED_16
	stream->info.channels = channels;
	stream->length = length;
	stream->pos = 0;

	return stream;
}

int S_MP3_CodecReadStream( snd_stream_t *stream, int bytes, void *buffer )
{
	mp3Context_t	*ctx = (mp3Context_t *)stream->ptr;
	byte			*out = (byte *)buffer;
	int				total = 0;

	if ( !ctx || bytes <= 0 ) {
		return 0;
	}

	// hand back the PCM we had to decode during the format probe first
	if ( ctx->pendingPos < ctx->pendingLen ) {
		int avail = ctx->pendingLen - ctx->pendingPos;
		int n = avail < bytes ? avail : bytes;
		Com_Memcpy( out, ctx->pending + ctx->pendingPos, n );
		ctx->pendingPos += n;
		total += n;
		if ( ctx->pendingPos >= ctx->pendingLen ) {
			Z_Free( ctx->pending );
			ctx->pending = NULL;
			ctx->pendingLen = ctx->pendingPos = 0;
		}
	}

	while ( total < bytes ) {
		size_t	done = 0;
		int		ret = mpg123_read( ctx->mh, out + total, bytes - total, &done );

		total += (int)done;

		if ( ret == MPG123_NEW_FORMAT ) {
			continue;	// VBR/format hiccup — keep decoding
		}
		if ( ret == MPG123_OK && done > 0 ) {
			continue;
		}
		break;	// MPG123_DONE / MPG123_NEED_MORE / error / nothing produced
	}

	return total;
}

void S_MP3_CodecCloseStream( snd_stream_t *stream )
{
	mp3Context_t	*ctx = (mp3Context_t *)stream->ptr;

	if ( ctx ) {
		if ( ctx->mh ) {
			mpg123_delete( ctx->mh );
		}
		if ( ctx->pending ) {
			Z_Free( ctx->pending );
		}
		Z_Free( ctx );
	}
	Z_Free( stream );
}

void *S_MP3_CodecLoad( const char *filename, snd_info_t *info )
{
	// MP3 is streaming background music only, never an in-memory sound effect
	return NULL;
}

snd_codec_t mp3_codec = {
	"mp3",
	S_MP3_CodecLoad,
	S_MP3_CodecOpenStream,
	S_MP3_CodecReadStream,
	S_MP3_CodecCloseStream,
	NULL
};

#endif // USE_CODEC_MP3
