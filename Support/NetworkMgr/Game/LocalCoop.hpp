//==============================================================================
//
//  LocalCoop.hpp
//
//  EXPERIMENTAL - two local players in the campaign (split-screen co-op).
//
//  This is compiled in only when A51_LOCAL_COOP is defined (CMake option
//  A51_LOCAL_COOP).  Even then it does nothing unless the environment variable
//  A51_LOCAL_COOP is set to 2 before the game starts.  With the option off,
//  every function here is a constant and the game builds and behaves exactly
//  like the unpatched source.
//
//  Stage 1 scope: two players in the same campaign world, one pad each, the
//  engine's own horizontal split-screen.  Enemy targeting, triggers, cinematics
//  and checkpoints are NOT generalised yet.  See games/area-51-2005/NOTES.md in
//  the coop-anything hub for what is known and unknown.
//
//==============================================================================

#ifndef LOCAL_COOP_HPP
#define LOCAL_COOP_HPP

#include "x_files.hpp"

#ifdef A51_LOCAL_COOP

#include <stdio.h>
#include <stdarg.h>

const s32 LOCAL_COOP_MAX_PLAYERS = 2;

//------------------------------------------------------------------------------
//  Number of local campaign players asked for: 1 (default) or 2.
//  Read once; set the environment variable A51_LOCAL_COOP=2 to ask for co-op.
//------------------------------------------------------------------------------

inline s32 LocalCoop_PlayerCount( void )
{
    static s32 s_Count = 0;

    if( s_Count == 0 )
    {
        const char* pValue = x_getenv( "A51_LOCAL_COOP" );
        s_Count = ( pValue && (pValue[0] == '2') && (pValue[1] == 0) ) ? 2 : 1;
    }

    return s_Count;
}

//------------------------------------------------------------------------------
//  Probe output.  The LOG_* macros are compiled out in this source port and
//  x_DebugMsg is compiled out in retail builds, so the probe appends plain
//  lines to a text file in the working directory.  Used to check, from the
//  outside, that both players exist and move independently.
//------------------------------------------------------------------------------

inline void LocalCoop_Probe( const char* pFormat, ... )
{
    FILE* pFile = fopen( "a51_local_coop_probe.log", "a" );

    if( pFile )
    {
        va_list Args;
        va_start( Args, pFormat );
        vfprintf( pFile, pFormat, Args );
        va_end( Args );

        fputc( '\n', pFile );
        fclose( pFile );
    }
}

#else

const s32 LOCAL_COOP_MAX_PLAYERS = 1;

inline s32 LocalCoop_PlayerCount( void ) { return 1; }

#endif // A51_LOCAL_COOP

inline xbool LocalCoop_IsActive( void ) { return LocalCoop_PlayerCount() > 1; }

//==============================================================================
#endif // LOCAL_COOP_HPP
//==============================================================================
