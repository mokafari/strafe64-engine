/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.
Copyright (C) 2005 Stuart Dalton (badcdev@gmail.com)

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

#include "client.h"
#include "snd_codec.h"
#include "snd_local.h"
#include "snd_public.h"

cvar_t *s_volume;
cvar_t *s_muted;
cvar_t *s_musicVolume;
cvar_t *s_doppler;
cvar_t *s_backend;
cvar_t *s_muteWhenMinimized;
cvar_t *s_muteWhenUnfocused;
cvar_t *s_musicShuffle;
cvar_t *s_musicPlaylistLoop;

static soundInterface_t si;

/*
===============================================================================

MUSIC PLAYLIST

A backend-agnostic queue of music tracks (mp3/ogg/opus/wav/mod). The active
sound backend (OpenAL or DMA) auto-loops the current track at exactly one
point; S_NextPlaylistTrack hooks that point to advance through the queue
instead. With no playlist loaded the backends keep their original single-track
loop behaviour, so this is fully backwards compatible.

===============================================================================
*/

#define MAX_PLAYLIST_TRACKS 256

static char s_playlist[MAX_PLAYLIST_TRACKS][MAX_QPATH];
static int  s_playlistCount;
static int  s_playlistIndex;

// audio extensions we accept when scanning a directory, longest/typical first
static const char *s_musicExtensions[] = {
	".mp3", ".ogg", ".opus", ".wav", ".it", ".xm", ".s3m", ".mod"
};

/*
=================
S_ClearPlaylist
=================
*/
static void S_ClearPlaylist( void )
{
	s_playlistCount = 0;
	s_playlistIndex = 0;
}

/*
=================
S_PlaylistAddTrack
=================
*/
static qboolean S_PlaylistAddTrack( const char *track )
{
	if( !track || !*track )
		return qfalse;

	if( s_playlistCount >= MAX_PLAYLIST_TRACKS ) {
		Com_Printf( S_COLOR_YELLOW "playlist full (%d tracks), ignoring %s\n",
				MAX_PLAYLIST_TRACKS, track );
		return qfalse;
	}

	Q_strncpyz( s_playlist[s_playlistCount], track, MAX_QPATH );
	s_playlistCount++;
	return qtrue;
}

/*
=================
S_ShufflePlaylist

Fisher-Yates over whatever is currently queued.
=================
*/
static void S_ShufflePlaylist( void )
{
	int i, j;
	char tmp[MAX_QPATH];
	static qboolean seeded = qfalse;

	// Seed once from the clock so the shuffle order differs between launches
	// (a bare rand() with no srand() repeats the same sequence every run).
	if( !seeded ) {
		srand( (unsigned)Sys_Milliseconds() );
		seeded = qtrue;
	}

	for( i = s_playlistCount - 1; i > 0; i-- ) {
		j = rand() % ( i + 1 );
		if( j == i )
			continue;
		Q_strncpyz( tmp, s_playlist[i], MAX_QPATH );
		Q_strncpyz( s_playlist[i], s_playlist[j], MAX_QPATH );
		Q_strncpyz( s_playlist[j], tmp, MAX_QPATH );
	}
}

/*
=================
S_PlaylistAddDir

Enqueue every supported audio file in a directory (searches pk3s and disk),
sorted by name so playback order is stable.
=================
*/
static int S_PlaylistAddDir( const char *dir )
{
	int   added = 0;
	int   e;
	char  path[MAX_QPATH];

	for( e = 0; e < ARRAY_LEN( s_musicExtensions ); e++ ) {
		int    numFiles, i;
		char **files = FS_ListFiles( dir, s_musicExtensions[e], &numFiles );

		for( i = 0; i < numFiles; i++ ) {
			Com_sprintf( path, sizeof( path ), "%s/%s", dir, files[i] );
			if( S_PlaylistAddTrack( path ) )
				added++;
		}

		FS_FreeFileList( files );
	}

	return added;
}

/*
=================
S_PlaylistAddM3U

Parse a simple .m3u/.m3u8 file: one path per line, blank lines and lines
starting with '#' (comments / extended-M3U directives) are skipped.
=================
*/
static int S_PlaylistAddM3U( const char *file )
{
	char *buffer = NULL;
	long  len;
	char *p, *line;
	int   added = 0;

	len = FS_ReadFile( file, (void **)&buffer );
	if( len <= 0 || !buffer ) {
		Com_Printf( S_COLOR_YELLOW "playlist: couldn't read %s\n", file );
		return 0;
	}

	p = buffer;
	while( p < buffer + len ) {
		// isolate one line and NUL-terminate it in place
		line = p;
		while( p < buffer + len && *p != '\n' && *p != '\r' )
			p++;
		while( p < buffer + len && ( *p == '\n' || *p == '\r' ) )
			*p++ = '\0';

		// trim leading whitespace
		while( *line == ' ' || *line == '\t' )
			line++;

		if( *line && *line != '#' ) {
			if( S_PlaylistAddTrack( line ) )
				added++;
		}
	}

	FS_FreeFile( buffer );
	return added;
}

/*
=================
S_StartPlaylist

Kick off playback from the current queue. The chosen track is handed to the
backend as both intro and loop; when it ends the backend asks the playlist for
the next track via S_NextPlaylistTrack.
=================
*/
static void S_StartPlaylist( void )
{
	if( s_playlistCount <= 0 )
		return;

	if( s_musicShuffle && s_musicShuffle->integer )
		S_ShufflePlaylist();

	s_playlistIndex = 0;

	if( si.StartBackgroundTrack )
		si.StartBackgroundTrack( s_playlist[0], s_playlist[0] );
}

/*
=================
S_NextPlaylistTrack

Called by the active backend when the current track ends. Returns qtrue when a
playlist is loaded and supplies the next track in 'out' (which may be "" to
signal the playlist finished and playback should stop). Returns qfalse when no
playlist is active, leaving the backend's own single-track loop behaviour.
=================
*/
qboolean S_NextPlaylistTrack( char *out, int outSize )
{
	if( s_playlistCount <= 0 )
		return qfalse;

	s_playlistIndex++;

	if( s_playlistIndex >= s_playlistCount ) {
		if( s_musicPlaylistLoop && s_musicPlaylistLoop->integer ) {
			if( s_musicShuffle && s_musicShuffle->integer )
				S_ShufflePlaylist();
			s_playlistIndex = 0;
		} else {
			// finished, and not looping: tell the backend to stop
			S_ClearPlaylist();
			if( outSize > 0 )
				out[0] = '\0';
			return qtrue;
		}
	}

	Q_strncpyz( out, s_playlist[s_playlistIndex], outSize );
	return qtrue;
}

/*
=================
S_Playlist_f

  playlist                       show the current queue
  playlist clear                 stop and empty the queue
  playlist <dir/>                enqueue every track in a directory and play
  playlist <file.m3u>            load an .m3u/.m3u8 and play
  playlist <trk1> <trk2> ...     enqueue an explicit list of tracks and play
=================
*/
static void S_Playlist_f( void )
{
	int  c = Cmd_Argc();
	int  i;
	int  added = 0;
	const char *arg1;

	if( c < 2 ) {
		// status
		if( s_playlistCount <= 0 ) {
			Com_Printf( "playlist empty\n" );
		} else {
			Com_Printf( "playlist: %d track(s)%s%s\n", s_playlistCount,
					( s_musicShuffle && s_musicShuffle->integer ) ? ", shuffle" : "",
					( s_musicPlaylistLoop && s_musicPlaylistLoop->integer ) ? ", loop" : "" );
			for( i = 0; i < s_playlistCount; i++ )
				Com_Printf( "%s%3d: %s\n", ( i == s_playlistIndex ) ? S_COLOR_GREEN : "",
						i + 1, s_playlist[i] );
		}
		Com_Printf( "Usage: playlist <dir/ | file.m3u | track1 track2 ...> | clear\n" );
		return;
	}

	arg1 = Cmd_Argv( 1 );

	if( !Q_stricmp( arg1, "clear" ) ) {
		S_ClearPlaylist();
		if( si.StopBackgroundTrack )
			si.StopBackgroundTrack();
		Com_Printf( "playlist cleared\n" );
		return;
	}

	S_ClearPlaylist();

	if( c == 2 ) {
		// single argument: m3u file or directory
		const char *ext = COM_GetExtension( arg1 );
		if( !Q_stricmp( ext, "m3u" ) || !Q_stricmp( ext, "m3u8" ) )
			added = S_PlaylistAddM3U( arg1 );
		else
			added = S_PlaylistAddDir( arg1 );
	} else {
		// explicit track list
		for( i = 1; i < c; i++ ) {
			if( S_PlaylistAddTrack( Cmd_Argv( i ) ) )
				added++;
		}
	}

	if( added <= 0 ) {
		Com_Printf( S_COLOR_YELLOW "playlist: no tracks found\n" );
		return;
	}

	Com_Printf( "playlist: queued %d track(s)\n", added );
	S_StartPlaylist();
}

/*
=================
S_PlayNext_f
=================
*/
static void S_PlayNext_f( void )
{
	char next[MAX_QPATH];

	if( s_playlistCount <= 0 ) {
		Com_Printf( "no playlist loaded\n" );
		return;
	}

	if( S_NextPlaylistTrack( next, sizeof( next ) ) && next[0] && si.StartBackgroundTrack )
		si.StartBackgroundTrack( next, next );
	else if( si.StopBackgroundTrack )
		si.StopBackgroundTrack();
}

/*
=================
S_PlayPrev_f
=================
*/
static void S_PlayPrev_f( void )
{
	if( s_playlistCount <= 0 ) {
		Com_Printf( "no playlist loaded\n" );
		return;
	}

	// step back two so the upcoming auto-advance lands on the previous track
	s_playlistIndex -= 2;
	while( s_playlistIndex < -1 )
		s_playlistIndex += s_playlistCount;

	if( si.StartBackgroundTrack ) {
		char prev[MAX_QPATH];
		if( S_NextPlaylistTrack( prev, sizeof( prev ) ) && prev[0] )
			si.StartBackgroundTrack( prev, prev );
	}
}

/*
=================
S_ValidateInterface
=================
*/
static qboolean S_ValidSoundInterface( soundInterface_t *pSi )
{
	if( !pSi->Shutdown ) return qfalse;
	if( !pSi->StartSound ) return qfalse;
	if( !pSi->StartLocalSound ) return qfalse;
	if( !pSi->StartBackgroundTrack ) return qfalse;
	if( !pSi->StopBackgroundTrack ) return qfalse;
	if( !pSi->RawSamples ) return qfalse;
	if( !pSi->StopAllSounds ) return qfalse;
	if( !pSi->ClearLoopingSounds ) return qfalse;
	if( !pSi->AddLoopingSound ) return qfalse;
	if( !pSi->AddRealLoopingSound ) return qfalse;
	if( !pSi->StopLoopingSound ) return qfalse;
	if( !pSi->Respatialize ) return qfalse;
	if( !pSi->UpdateEntityPosition ) return qfalse;
	if( !pSi->Update ) return qfalse;
	if( !pSi->DisableSounds ) return qfalse;
	if( !pSi->BeginRegistration ) return qfalse;
	if( !pSi->RegisterSound ) return qfalse;
	if( !pSi->ClearSoundBuffer ) return qfalse;
	if( !pSi->SoundInfo ) return qfalse;
	if( !pSi->SoundList ) return qfalse;

#ifdef USE_VOIP
	if( !pSi->StartCapture ) return qfalse;
	if( !pSi->AvailableCaptureSamples ) return qfalse;
	if( !pSi->Capture ) return qfalse;
	if( !pSi->StopCapture ) return qfalse;
	if( !pSi->MasterGain ) return qfalse;
#endif

	return qtrue;
}

/*
=================
S_StartSound
=================
*/
void S_StartSound( vec3_t origin, int entnum, int entchannel, sfxHandle_t sfx )
{
	if( si.StartSound ) {
		si.StartSound( origin, entnum, entchannel, sfx );
	}
}

/*
=================
S_StartLocalSound
=================
*/
void S_StartLocalSound( sfxHandle_t sfx, int channelNum )
{
	if( si.StartLocalSound ) {
		si.StartLocalSound( sfx, channelNum );
	}
}

/*
=================
S_StartBackgroundTrack
=================
*/
void S_StartBackgroundTrack( const char *intro, const char *loop )
{
	if( si.StartBackgroundTrack ) {
		si.StartBackgroundTrack( intro, loop );
	}
}

/*
=================
S_StopBackgroundTrack
=================
*/
void S_StopBackgroundTrack( void )
{
	if( si.StopBackgroundTrack ) {
		si.StopBackgroundTrack( );
	}
}

/*
=================
S_RawSamples
=================
*/
void S_RawSamples (int stream, int samples, int rate, int width, int channels,
		   const byte *data, float volume, int entityNum)
{
	if(si.RawSamples)
		si.RawSamples(stream, samples, rate, width, channels, data, volume, entityNum);
}

/*
=================
S_StopAllSounds
=================
*/
void S_StopAllSounds( void )
{
	if( si.StopAllSounds ) {
		si.StopAllSounds( );
	}
}

/*
=================
S_ClearLoopingSounds
=================
*/
void S_ClearLoopingSounds( qboolean killall )
{
	if( si.ClearLoopingSounds ) {
		si.ClearLoopingSounds( killall );
	}
}

/*
=================
S_AddLoopingSound
=================
*/
void S_AddLoopingSound( int entityNum, const vec3_t origin,
		const vec3_t velocity, sfxHandle_t sfx )
{
	if( si.AddLoopingSound ) {
		si.AddLoopingSound( entityNum, origin, velocity, sfx );
	}
}

/*
=================
S_AddRealLoopingSound
=================
*/
void S_AddRealLoopingSound( int entityNum, const vec3_t origin,
		const vec3_t velocity, sfxHandle_t sfx )
{
	if( si.AddRealLoopingSound ) {
		si.AddRealLoopingSound( entityNum, origin, velocity, sfx );
	}
}

/*
=================
S_StopLoopingSound
=================
*/
void S_StopLoopingSound( int entityNum )
{
	if( si.StopLoopingSound ) {
		si.StopLoopingSound( entityNum );
	}
}

/*
=================
S_Respatialize
=================
*/
void S_Respatialize( int entityNum, const vec3_t origin,
		vec3_t axis[3], int inwater )
{
	if( si.Respatialize ) {
		si.Respatialize( entityNum, origin, axis, inwater );
	}
}

/*
=================
S_UpdateEntityPosition
=================
*/
void S_UpdateEntityPosition( int entityNum, const vec3_t origin )
{
	if( si.UpdateEntityPosition ) {
		si.UpdateEntityPosition( entityNum, origin );
	}
}

/*
=================
S_Update
=================
*/
void S_Update( void )
{
	if(s_muted->integer)
	{
		if(!(s_muteWhenMinimized->integer && com_minimized->integer) &&
		   !(s_muteWhenUnfocused->integer && com_unfocused->integer))
		{
			s_muted->integer = qfalse;
			s_muted->modified = qtrue;
		}
	}
	else
	{
		if((s_muteWhenMinimized->integer && com_minimized->integer) ||
		   (s_muteWhenUnfocused->integer && com_unfocused->integer))
		{
			s_muted->integer = qtrue;
			s_muted->modified = qtrue;
		}
	}
	
	if( si.Update ) {
		si.Update( );
	}
}

/*
=================
S_DisableSounds
=================
*/
void S_DisableSounds( void )
{
	if( si.DisableSounds ) {
		si.DisableSounds( );
	}
}

/*
=================
S_BeginRegistration
=================
*/
void S_BeginRegistration( void )
{
	if( si.BeginRegistration ) {
		si.BeginRegistration( );
	}
}

/*
=================
S_RegisterSound
=================
*/
sfxHandle_t	S_RegisterSound( const char *sample, qboolean compressed )
{
	if( si.RegisterSound ) {
		return si.RegisterSound( sample, compressed );
	} else {
		return 0;
	}
}

/*
=================
S_ClearSoundBuffer
=================
*/
void S_ClearSoundBuffer( void )
{
	if( si.ClearSoundBuffer ) {
		si.ClearSoundBuffer( );
	}
}

/*
=================
S_SoundInfo
=================
*/
void S_SoundInfo( void )
{
	if( si.SoundInfo ) {
		si.SoundInfo( );
	}
}

/*
=================
S_SoundList
=================
*/
void S_SoundList( void )
{
	if( si.SoundList ) {
		si.SoundList( );
	}
}


#ifdef USE_VOIP
/*
=================
S_StartCapture
=================
*/
void S_StartCapture( void )
{
	if( si.StartCapture ) {
		si.StartCapture( );
	}
}

/*
=================
S_AvailableCaptureSamples
=================
*/
int S_AvailableCaptureSamples( void )
{
	if( si.AvailableCaptureSamples ) {
		return si.AvailableCaptureSamples( );
	}
	return 0;
}

/*
=================
S_Capture
=================
*/
void S_Capture( int samples, byte *data )
{
	if( si.Capture ) {
		si.Capture( samples, data );
	}
}

/*
=================
S_StopCapture
=================
*/
void S_StopCapture( void )
{
	if( si.StopCapture ) {
		si.StopCapture( );
	}
}

/*
=================
S_MasterGain
=================
*/
void S_MasterGain( float gain )
{
	if( si.MasterGain ) {
		si.MasterGain( gain );
	}
}
#endif

//=============================================================================

/*
=================
S_Play_f
=================
*/
void S_Play_f( void ) {
	int 		i;
	int			c;
	sfxHandle_t	h;

	if( !si.RegisterSound || !si.StartLocalSound ) {
		return;
	}

	c = Cmd_Argc();

	if( c < 2 ) {
		Com_Printf ("Usage: play <sound filename> [sound filename] [sound filename] ...\n");
		return;
	}

	for( i = 1; i < c; i++ ) {
		h = si.RegisterSound( Cmd_Argv(i), qfalse );

		if( h ) {
			si.StartLocalSound( h, CHAN_LOCAL_SOUND );
		}
	}
}

/*
=================
S_Music_f
=================
*/
void S_Music_f( void ) {
	int		c;

	if( !si.StartBackgroundTrack ) {
		return;
	}

	c = Cmd_Argc();

	// an explicit 'music' command overrides any active playlist
	S_ClearPlaylist();

	if ( c == 2 ) {
		si.StartBackgroundTrack( Cmd_Argv(1), NULL );
	} else if ( c == 3 ) {
		si.StartBackgroundTrack( Cmd_Argv(1), Cmd_Argv(2) );
	} else {
		Com_Printf ("Usage: music <musicfile> [loopfile]\n");
		return;
	}

}

/*
=================
S_Music_f
=================
*/
void S_StopMusic_f( void )
{
	S_ClearPlaylist();

	if(!si.StopBackgroundTrack)
		return;

	si.StopBackgroundTrack();
}


//=============================================================================

/*
=================
S_Init
=================
*/
void S_Init( void )
{
	cvar_t		*cv;
	qboolean	started = qfalse;

	Com_Printf( "------ Initializing Sound ------\n" );

	s_volume = Cvar_Get( "s_volume", "0.8", CVAR_ARCHIVE );
	s_musicVolume = Cvar_Get( "s_musicvolume", "0.25", CVAR_ARCHIVE );
	s_muted = Cvar_Get("s_muted", "0", CVAR_ROM);
	s_doppler = Cvar_Get( "s_doppler", "1", CVAR_ARCHIVE );
	s_backend = Cvar_Get( "s_backend", "", CVAR_ROM );
	s_muteWhenMinimized = Cvar_Get( "s_muteWhenMinimized", "0", CVAR_ARCHIVE );
	s_muteWhenUnfocused = Cvar_Get( "s_muteWhenUnfocused", "0", CVAR_ARCHIVE );
	s_musicShuffle = Cvar_Get( "s_musicShuffle", "1", CVAR_ARCHIVE );
	s_musicPlaylistLoop = Cvar_Get( "s_musicPlaylistLoop", "1", CVAR_ARCHIVE );

	cv = Cvar_Get( "s_initsound", "1", 0 );
	if( !cv->integer ) {
		Com_Printf( "Sound disabled.\n" );
	} else {

		S_CodecInit( );

		Cmd_AddCommand( "play", S_Play_f );
		Cmd_AddCommand( "music", S_Music_f );
		Cmd_AddCommand( "stopmusic", S_StopMusic_f );
		Cmd_AddCommand( "playlist", S_Playlist_f );
		Cmd_AddCommand( "playnext", S_PlayNext_f );
		Cmd_AddCommand( "playprev", S_PlayPrev_f );
		Cmd_AddCommand( "s_list", S_SoundList );
		Cmd_AddCommand( "s_stop", S_StopAllSounds );
		Cmd_AddCommand( "s_info", S_SoundInfo );

		cv = Cvar_Get( "s_useOpenAL", "1", CVAR_ARCHIVE | CVAR_LATCH );
		if( cv->integer ) {
			//OpenAL
			started = S_AL_Init( &si );
			Cvar_Set( "s_backend", "OpenAL" );
		}

		if( !started ) {
			started = S_Base_Init( &si );
			Cvar_Set( "s_backend", "base" );
		}

		if( started ) {
			if( !S_ValidSoundInterface( &si ) ) {
				Com_Error( ERR_FATAL, "Sound interface invalid" );
			}

			S_SoundInfo( );
			Com_Printf( "Sound initialization successful.\n" );
		} else {
			Com_Printf( "Sound initialization failed.\n" );
		}
	}

	Com_Printf( "--------------------------------\n");
}

/*
=================
S_Shutdown
=================
*/
void S_Shutdown( void )
{
	if( si.Shutdown ) {
		si.Shutdown( );
	}

	Com_Memset( &si, 0, sizeof( soundInterface_t ) );

	Cmd_RemoveCommand( "play" );
	Cmd_RemoveCommand( "music");
	Cmd_RemoveCommand( "stopmusic");
	Cmd_RemoveCommand( "s_list" );
	Cmd_RemoveCommand( "s_stop" );
	Cmd_RemoveCommand( "s_info" );

	S_CodecShutdown( );
}

