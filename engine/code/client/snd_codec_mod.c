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

// Audio-reactive band analysis (au_bass/au_mid/au_high/au_level) is no longer
// done here: it now rides on S_CodecReadStream in snd_codec.c so every codec
// — .wav/.ogg/.opus/.mp3 as well as tracker modules — drives the shaders.
// See snd_analyzer.c.

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

	return (int)( got * 4 );
}

void S_MOD_CodecCloseStream( snd_stream_t *stream )
{
	if ( stream->ptr ) {
		openmpt_module_destroy( (openmpt_module *)stream->ptr );
	}
	Z_Free( stream );
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
