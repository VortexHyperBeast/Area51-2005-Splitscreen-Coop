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
//  Co-op settings.
//    Controllers: swapped or not.  Changed in the pause menu ("Co-op"), kept
//    in the file a51_coop.cfg next to the game.  The environment variable
//    A51_COOP_SWAP_PADS=0/1 (tests) overrides the file at start-up.
//    Bodies: each player's multiplayer avatar skin (0..15: 0-3 hazmat, 4-7
//    special forces, 8-11 technician, 12-15 grey) comes from that player's
//    profile (saved with it).  A51_COOP_SKIN_P1/P2 (tests) override it.
//------------------------------------------------------------------------------

#define LOCAL_COOP_CONFIG_FILE "a51_coop.cfg"

inline s32& LocalCoop_SwapState( void )
{
    static s32 s_Swap = -1;
    return s_Swap;
}

inline xbool LocalCoop_SwapPads( void )
{
    s32& Swap = LocalCoop_SwapState();

    if( Swap == -1 )
    {
        Swap = 0;

        FILE* pFile = fopen( LOCAL_COOP_CONFIG_FILE, "r" );
        if( pFile )
        {
            char Line[64];
            while( fgets( Line, sizeof(Line), pFile ) )
            {
                if( x_strncmp( Line, "swap_pads=1", 11 ) == 0 )
                    Swap = 1;
            }
            fclose( pFile );
        }

        const char* pValue = x_getenv( "A51_COOP_SWAP_PADS" );
        if( pValue && ((pValue[0] == '0') || (pValue[0] == '1')) && (pValue[1] == 0) )
            Swap = pValue[0] - '0';
    }

    return (Swap == 1);
}

inline void LocalCoop_SetSwapPads( xbool bSwap )
{
    LocalCoop_SwapState() = bSwap ? 1 : 0;

    FILE* pFile = fopen( LOCAL_COOP_CONFIG_FILE, "w" );
    if( pFile )
    {
        fprintf( pFile, "swap_pads=%d\n", bSwap ? 1 : 0 );
        fclose( pFile );
    }
}

//  Test override of a player's body, -1 if not set.
inline s32 LocalCoop_SkinOverride( s32 LocalIndex )
{
    const char* pValue = x_getenv( (LocalIndex == 0) ? "A51_COOP_SKIN_P1" : "A51_COOP_SKIN_P2" );
    s32 Value = -1;

    if( pValue && (pValue[0] >= '0') && (pValue[0] <= '9') )
    {
        Value = pValue[0] - '0';
        if( (pValue[1] >= '0') && (pValue[1] <= '9') && (pValue[2] == 0) )
            Value = Value * 10 + (pValue[1] - '0');
        else if( pValue[1] != 0 )
            Value = -1;
    }

    return ((Value >= 0) && (Value <= 15)) ? Value : -1;
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
inline xbool LocalCoop_SwapPads( void ) { return FALSE; }
inline s32 LocalCoop_SkinOverride( s32 ) { return -1; }
inline void LocalCoop_SetSwapPads( xbool ) {}

#endif // A51_LOCAL_COOP

inline xbool LocalCoop_IsActive( void ) { return LocalCoop_PlayerCount() > 1; }

//==============================================================================
#endif // LOCAL_COOP_HPP
//==============================================================================
