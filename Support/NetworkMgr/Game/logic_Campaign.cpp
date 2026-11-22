//==============================================================================
//
//  logic_Campaign.cpp
//
//==============================================================================

//==============================================================================
//    INCLUDES
//==============================================================================

#include "logic_Campaign.hpp"
#include "Objects/Actor/Actor.hpp"  // For actor class
#include "Objects/Render/PostEffectMgr.hpp"
#include "StateMgr/StateMgr.hpp"
#ifdef A51_LOCAL_COOP
#include "InputMgr/GamePad.hpp"
#endif

//==============================================================================
//    FUNCTIONS
//==============================================================================

logic_campaign::logic_campaign( void ) :
    m_Alive                 ( TRUE              ),
    m_bSpawnInfoSet         ( FALSE             ),
    m_PlayerSpawnPosition   ( 0.0f, 0.0f, 0.0f  ),
    m_PlayerSpawnPitch      ( 0.0f              ),
    m_PlayerSpawnYaw        ( 0.0f              ),
    m_PlayerSpawnZone       ( -1                )
{
}

//==============================================================================

logic_campaign::~logic_campaign( void )
{
}

//==============================================================================

void logic_campaign::Activate( void )
{
    // Do nothing.  This function prevents the base class function (which does 
    // do stuff which is possibly campaign unfriendly) from getting called.
}

//==============================================================================

void logic_campaign::PlayerDied( s32 Victim, s32 Killer, s32 PainType )
{
    (void)Killer;
    (void)PainType;

    // Kill the player and reset the respawn timer.
#ifdef A51_LOCAL_COOP
    // Co-op prototype (shared fate): the death of any local player ends the attempt.
    if( ((Victim == 0) || (LocalCoop_IsActive() && (Victim == 1))) && m_Alive )
#else
    if( (Victim == 0) && m_Alive )
#endif
    {
        m_Alive         = FALSE;
        m_RespawnDelay  = 4.0f;
        g_PostEffectMgr.StartScreenFade( xcolor(0,0,0,255), m_RespawnDelay );
    }
}

//==============================================================================

void logic_campaign::RequestSpawn( s32 PlayerIndex, xbool /*Immediate*/ )
{
    (void)PlayerIndex;
    if( !m_Alive && (m_RespawnDelay == 0.0f) )
    {
        g_ActiveConfig.SetExitReason( GAME_EXIT_RELOAD_CHECKPOINT );
/* rmb - old way
        player* pPlayer = (player*)NetObjMgr.GetObjFromSlot( PlayerIndex );
        ASSERT( pPlayer );

        m_Alive = TRUE;
        pPlayer->OnSpawn();

        // restore the player to the last saved position
        g_SaveMgr.RestoreGame( TRUE );

        // Setup the weapon objects when spawning from a game restore
        pPlayer->CreateAllWeaponObjects();
        pPlayer->SetNextWeapon2( pPlayer->GetCurrentWeapon2() );

        g_PostEffectMgr.StartScreenFade( xcolor(0,0,0,0), 3.0f );
*/
    }
}

//==============================================================================

void logic_campaign::AdvanceTime( f32 DeltaTime )
{
#ifdef A51_LOCAL_COOP
    WriteProbe( DeltaTime );
#endif

    if( m_RespawnDelay > 0.0f )
    {
        m_RespawnDelay -= DeltaTime;
    }

    if( m_RespawnDelay <= 0.0f )
    {
        m_RespawnDelay = 0.0f;

        if ( !g_StateMgr.IsPaused() )
        {
            RequestSpawn( 0 );  // Force respawn!
        }
    }
}

//==============================================================================

void logic_campaign::SetPlayerSpawnInfo( const vector3& Position, 
                                               radian   Pitch, 
                                               radian   Yaw, 
                                               s32      Zone, 
                                         const guid&    Guid )
{
    m_bSpawnInfoSet         = TRUE;
    m_PlayerSpawnPosition   = Position;
    m_PlayerSpawnPitch      = Pitch;
    m_PlayerSpawnYaw        = Yaw;
    m_PlayerSpawnZone       = Zone;
    m_PlayerSpawnGuid       = Guid;
}

//==============================================================================

void logic_campaign::BeginGame( void )
{
    player* pPlayer = NULL;
#ifdef A51_LOCAL_COOP
    vector3 CoopPosition = m_PlayerSpawnPosition;
    radian  CoopPitch    = m_PlayerSpawnPitch;
    radian  CoopYaw      = m_PlayerSpawnYaw;
    s32     CoopZone1    = m_PlayerSpawnZone;
    s32     CoopZone2    = 0;
#endif

    logic_base::BeginGame();

    GameMgr.SetScoreDisplay( SCORE_DISPLAY_NONE );

    //
    // Create the player
    //
    if( m_bSpawnInfoSet )
    {
        g_ObjMgr.CreateObject( "Player", m_PlayerSpawnGuid );
        pPlayer = (player*)g_ObjMgr.GetObjectByGuid( m_PlayerSpawnGuid );

        ASSERTS( pPlayer, "Unable to create player in logic_campaign::BeginGame()" );

        if( pPlayer )
        {
            // Set this player up.
            pPlayer->Teleport( m_PlayerSpawnPosition,
                               m_PlayerSpawnPitch,
                               m_PlayerSpawnYaw,
                               static_cast<zone_mgr::zone_id>( m_PlayerSpawnZone ),
                               0,
                               PlayerTeleportVelocityPolicy::Clear,
                               FALSE,
                               FALSE );
        }
    }
    else
    {
        // Should always have spawn info setup by here.
        LOG_ERROR( "logic_campagin::BeginGame", "Spawn info not set." );
        LOG_ERROR( "logic_campagin::BeginGame", "Attempting to use a spawn point." );

        // Let's try some trickery to get a player anyway.
        spawn_point* pSpawnPoint = GameMgr.SelectSpawnPoint(1);
        if( pSpawnPoint )
        {
            m_PlayerSpawnGuid = guid_New();
            g_ObjMgr.CreateObject( "Player", m_PlayerSpawnGuid );
            pPlayer = (player*)g_ObjMgr.GetObjectByGuid( m_PlayerSpawnGuid );

            ASSERTS( pPlayer, "Unable to create player in logic_campaign::BeginGame()" );

            if( pPlayer )
            {
                //  Lookup spawn point info
                vector3 Position;
                radian3 Rotation;
                u16     Zone1;
                u16     Zone2;
                pSpawnPoint->GetSpawnInfo( pPlayer->GetGuid(), Position, Rotation, Zone1, Zone2 );
#ifdef A51_LOCAL_COOP
                CoopPosition = Position;
                CoopPitch    = Rotation.Pitch;
                CoopYaw      = Rotation.Yaw;
                CoopZone1    = Zone1;
                CoopZone2    = Zone2;
#endif

                // Set this player up.
                pPlayer->Teleport( Position,
                                   Rotation.Pitch,
                                   Rotation.Yaw,
                                   static_cast<zone_mgr::zone_id>( Zone1 ),
                                   static_cast<zone_mgr::zone_id>( Zone2 ),
                                   PlayerTeleportVelocityPolicy::Clear,
                                   FALSE,
                                   FALSE );
            }
        }
    }

    pPlayer->OnSpawn();

    // turn off shadows for the player--they're only needed for split-screen
#ifdef A51_LOCAL_COOP
    if( !LocalCoop_IsActive() )
    {
        pPlayer->SetAttrBits( pPlayer->GetAttrBits() & ~object::ATTR_CAST_SHADOWS );
    }

    // Co-op prototype: create the other local player(s) beside the first one.
    m_PlayerGuid[0] = m_PlayerSpawnGuid;
    for( s32 i = 1; i < LOCAL_COOP_MAX_PLAYERS; i++ )
    {
        m_PlayerGuid[i] = NULL_GUID;
    }
    m_ProbeTimer = 0.0f;

    for( s32 i = 1; i < LocalCoop_PlayerCount(); i++ )
    {
        CreateExtraPlayer( i, CoopPosition, CoopPitch, CoopYaw, CoopZone1, CoopZone2 );
    }
#else
    pPlayer->SetAttrBits( pPlayer->GetAttrBits() & ~object::ATTR_CAST_SHADOWS );
#endif

    m_Alive = TRUE;
}

#ifdef A51_LOCAL_COOP

//==============================================================================

guid logic_campaign::GetCampaignPlayerGuid( s32 PlayerIndex ) const
{
    if( (PlayerIndex >= 0) && (PlayerIndex < LOCAL_COOP_MAX_PLAYERS) )
    {
        return m_PlayerGuid[PlayerIndex];
    }

    return NULL_GUID;
}

//==============================================================================
//  Creates local player number Index (1 or more) next to the first player.
//  World units are centimetres (derived: gravity is 980 and eye height 172.5),
//  so the offset is one metre per player index.

void logic_campaign::CreateExtraPlayer( s32 Index, const vector3& Position,
                                        radian Pitch, radian Yaw,
                                        s32 Zone1, s32 Zone2 )
{
    ASSERT( (Index > 0) && (Index < LOCAL_COOP_MAX_PLAYERS) );

    guid Guid = guid_New();
    g_ObjMgr.CreateObject( "Player", Guid );
    player* pPlayer = (player*)g_ObjMgr.GetObjectByGuid( Guid );

    ASSERTS( pPlayer, "Unable to create extra local player in logic_campaign::CreateExtraPlayer()" );

    if( !pPlayer )
    {
        return;
    }

    m_PlayerGuid[Index] = Guid;

    vector3 Offset( 100.0f * (f32)Index, 0.0f, 0.0f );
    Offset.RotateY( Yaw );

    pPlayer->Teleport( Position + Offset,
                       Pitch,
                       Yaw,
                       static_cast<zone_mgr::zone_id>( Zone1 ),
                       static_cast<zone_mgr::zone_id>( Zone2 ),
                       PlayerTeleportVelocityPolicy::Clear,
                       FALSE,
                       FALSE );

    pPlayer->OnSpawn();
}

//==============================================================================
//  Every two seconds, appends one line per local player to the probe file:
//  which pad it is bound to, where it is and how healthy it is.  Two players
//  moving independently show two position columns changing on their own.

void logic_campaign::WriteProbe( f32 DeltaTime )
{
    if( !LocalCoop_IsActive() )
    {
        return;
    }

    m_ProbeTimer -= DeltaTime;
    if( m_ProbeTimer > 0.0f )
    {
        return;
    }
    m_ProbeTimer = 2.0f;

    for( s32 i = 0; i < LocalCoop_PlayerCount(); i++ )
    {
        player* pPlayer = (player*)g_ObjMgr.GetObjectByGuid( m_PlayerGuid[i] );

        if( !pPlayer )
        {
            LocalCoop_Probe( "player %d: not created", i );
            continue;
        }

        const vector3 Pos       = pPlayer->GetPosition();
        const s32     LocalSlot = pPlayer->GetLocalSlot();
        const s32     Pad       = (LocalSlot >= 0) ? g_GameInput.GetPlayerDevice( LocalSlot ) : -1;

        LocalCoop_Probe( "player %d: guid=%08X localslot=%d netslot=%d pad=%d pos=(%d,%d,%d) health=%d",
                         i,
                         (unsigned)m_PlayerGuid[i].GetLow(),
                         (int)LocalSlot,
                         (int)pPlayer->net_GetSlot(),
                         (int)Pad,
                         (int)Pos.GetX(), (int)Pos.GetY(), (int)Pos.GetZ(),
                         (int)pPlayer->GetHealth() );
    }
}

#endif // A51_LOCAL_COOP

//==============================================================================
