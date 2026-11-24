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

inline s32& LocalCoop_PlayerCountRef( void )
{
    static s32 s_Count = 0;

    if( s_Count == 0 )
    {
        const char* pValue = x_getenv( "A51_LOCAL_COOP" );
        s_Count = ( pValue && (pValue[0] == '2') && (pValue[1] == 0) ) ? 2 : 1;
    }

    return s_Count;
}

inline s32 LocalCoop_PlayerCount( void )
{
    return LocalCoop_PlayerCountRef();
}

//  The main menu chooses: "Single Player" sets 1, "Splitscreen Co-op" sets 2
//  (the environment variable is only the start value, used by the tests).
inline void LocalCoop_SetPlayerCount( s32 Count )
{
    LocalCoop_PlayerCountRef() = ( Count == 2 ) ? 2 : 1;
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

//  The splitscreen co-op saves: two profiles of their own, never shared with
//  single player (profile names are at most 15 characters).  Player 1's holds
//  the co-op campaign progress and player 1's settings; player 2's holds
//  player 2's settings and body.
#define LOCAL_COOP_PROFILE_P1  "Splitscreen P1"
#define LOCAL_COOP_PROFILE_P2  "Splitscreen P2"

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

    // Keep the file's other settings (downed/revive tuning).
    char Keep[1024];
    s32  nKeep = 0;
    Keep[0] = 0;
    FILE* pIn = fopen( LOCAL_COOP_CONFIG_FILE, "r" );
    if( pIn )
    {
        char Line[128];
        while( fgets( Line, sizeof(Line), pIn ) )
        {
            if( x_strncmp( Line, "swap_pads=", 10 ) == 0 )
                continue;
            s32 Len = x_strlen( Line );
            if( nKeep + Len < (s32)sizeof(Keep) - 1 )
            {
                x_strcpy( Keep + nKeep, Line );
                nKeep += Len;
            }
        }
        fclose( pIn );
    }

    FILE* pFile = fopen( LOCAL_COOP_CONFIG_FILE, "w" );
    if( pFile )
    {
        fprintf( pFile, "swap_pads=%d\n", bSwap ? 1 : 0 );
        if( nKeep > 0 )
            fputs( Keep, pFile );
        fclose( pFile );
    }
}

//  A number from a51_coop.cfg ("key=value" lines), or Default.  The environment
//  variable pEnv (tests) wins over the file.
inline f32 LocalCoop_ConfigF32( const char* pKey, const char* pEnv, f32 Default )
{
    f32 Value = Default;
    FILE* pFile = fopen( LOCAL_COOP_CONFIG_FILE, "r" );
    if( pFile )
    {
        char Line[128];
        const s32 KeyLen = x_strlen( pKey );
        while( fgets( Line, sizeof(Line), pFile ) )
        {
            if( (x_strncmp( Line, pKey, KeyLen ) == 0) && (Line[KeyLen] == '=') )
                Value = (f32)x_atof( Line + KeyLen + 1 );
        }
        fclose( pFile );
    }
    const char* pValue = pEnv ? x_getenv( pEnv ) : NULL;
    if( pValue && pValue[0] )
        Value = (f32)x_atof( pValue );
    return Value;
}

//  Downed / revive rules (owner's brief): 25 s to bleed out, 4 s to revive,
//  back up with 35% health.  downed=0 in a51_coop.cfg turns it off (shared fate).
struct local_coop_revive_rules
{
    xbool   bEnabled;
    f32     BleedoutSec;
    f32     ReviveSec;
    f32     ReviveHealthPct;
    f32     ReviveRange;        // cm
};

inline const local_coop_revive_rules& LocalCoop_ReviveRules( void )
{
    static local_coop_revive_rules s_Rules;
    static xbool s_bRead = FALSE;
    if( !s_bRead )
    {
        s_bRead = TRUE;
        s_Rules.bEnabled        = LocalCoop_ConfigF32( "downed",               "A51_COOP_DOWNED",          1.0f  ) != 0.0f;
        s_Rules.BleedoutSec     = LocalCoop_ConfigF32( "bleedout_seconds",     "A51_COOP_BLEEDOUT",        25.0f );
        s_Rules.ReviveSec       = LocalCoop_ConfigF32( "revive_seconds",       "A51_COOP_REVIVE_TIME",     4.0f  );
        s_Rules.ReviveHealthPct = LocalCoop_ConfigF32( "revive_health_percent","A51_COOP_REVIVE_HEALTH",   35.0f );
        s_Rules.ReviveRange     = LocalCoop_ConfigF32( "revive_range_cm",      "A51_COOP_REVIVE_RANGE",    200.0f );
        if( s_Rules.BleedoutSec < 1.0f )   s_Rules.BleedoutSec = 1.0f;
        if( s_Rules.ReviveSec < 0.1f )     s_Rules.ReviveSec = 0.1f;
        if( s_Rules.ReviveHealthPct < 1.0f )   s_Rules.ReviveHealthPct = 1.0f;
        if( s_Rules.ReviveHealthPct > 100.0f ) s_Rules.ReviveHealthPct = 100.0f;
    }
    return s_Rules;
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
inline void LocalCoop_SetPlayerCount( s32 ) {}
inline xbool LocalCoop_SwapPads( void ) { return FALSE; }
inline s32 LocalCoop_SkinOverride( s32 ) { return -1; }
inline void LocalCoop_SetSwapPads( xbool ) {}

#endif // A51_LOCAL_COOP

inline xbool LocalCoop_IsActive( void ) { return LocalCoop_PlayerCount() > 1; }

//==============================================================================
#endif // LOCAL_COOP_HPP
//==============================================================================
