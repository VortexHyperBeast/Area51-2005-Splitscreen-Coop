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
#ifdef A51_LOCAL_COOP_QA
#include "Objects/NewWeapon.hpp"
#include "Input/backend/sdl/qa_bot.hpp"
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
    SyncCoopCinema( DeltaTime );
#endif
#ifdef A51_LOCAL_COOP_QA
    ArmQaBot( DeltaTime );
    qa_bot_Tick( DeltaTime );
    WriteQaProbe( DeltaTime );
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
    for( s32 i = 0; i < LOCAL_COOP_MAX_PLAYERS; i++ )
    {
        m_CoopCinemaMismatch[i] = 0.0f;
    }

    for( s32 i = 1; i < LocalCoop_PlayerCount(); i++ )
    {
        CreateExtraPlayer( i, CoopPosition, CoopPitch, CoopYaw, CoopZone1, CoopZone2 );
    }
#else
    pPlayer->SetAttrBits( pPlayer->GetAttrBits() & ~object::ATTR_CAST_SHADOWS );
#endif

    m_Alive = TRUE;

#ifdef A51_LOCAL_COOP_QA
    m_QaProbeTimer = 0.0f;
    m_QaSinceBegin = 0.0f;
    m_QaFreeTime   = 0.0f;
    m_QaArmed      = FALSE;
    qa_bot_Log( "campaign BeginGame done: players=%d level=%d", (int)LocalCoop_PlayerCount(), (int)g_ActiveConfig.GetLevelID() );
#endif
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
//  Scripted scenes are copied from player to player when they start or end
//  (player::OnProperty).  A scene that started before player 2 existed (the
//  start of a level) is caught here: if a player's scene state differs from
//  player 1's for half a second, it follows player 1.

void logic_campaign::SyncCoopCinema( f32 DeltaTime )
{
    if( !LocalCoop_IsActive() )
    {
        return;
    }

    player* pLeader = (player*)g_ObjMgr.GetObjectByGuid( m_PlayerGuid[0] );
    if( !pLeader || pLeader->IsDead() )
    {
        return;
    }

    for( s32 i = 1; i < LocalCoop_PlayerCount(); i++ )
    {
        player* pPlayer = (player*)g_ObjMgr.GetObjectByGuid( m_PlayerGuid[i] );
        if( !pPlayer || pPlayer->IsDead() )
        {
            m_CoopCinemaMismatch[i] = 0.0f;
            continue;
        }

        if( pPlayer->IsCinemaRunning() != pLeader->IsCinemaRunning() )
        {
            m_CoopCinemaMismatch[i] += DeltaTime;
        }
        else
        {
            m_CoopCinemaMismatch[i] = 0.0f;
        }

        if( m_CoopCinemaMismatch[i] > 0.5f )
        {
            pPlayer->CoopFollowCinema( *pLeader );
            m_CoopCinemaMismatch[i] = 0.0f;
        }
    }
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

#ifdef A51_LOCAL_COOP_QA

//==============================================================================
//  TEST ONLY.  Twice a second of game time, one line per local player goes to
//  a51_qa_bot.log: who it is, which pad drives it, where it stands and looks,
//  what it holds.  The scorer in tools/coop-qa reads these lines, and nothing
//  else of the game, to decide whether the players really act independently.
//  Yaw and pitch are in tenths of a degree.

void logic_campaign::ArmQaBot( f32 DeltaTime )
{
    if( m_QaArmed )
    {
        return;
    }

    // The script starts once the level has been running for a while and no local
    // player is in a cinematic (the player cannot be steered during one).
    m_QaSinceBegin += DeltaTime;

    xbool Free = TRUE;
    for( s32 i = 0; i < LocalCoop_PlayerCount(); i++ )
    {
        player* pPlayer = (player*)g_ObjMgr.GetObjectByGuid( m_PlayerGuid[i] );
        if( !pPlayer || pPlayer->IsCinemaRunning() )
        {
            Free = FALSE;
        }
    }

    m_QaFreeTime = Free ? (m_QaFreeTime + DeltaTime) : 0.0f;

    if( (m_QaSinceBegin >= 5.0f) && (m_QaFreeTime >= 5.0f) )
    {
        m_QaArmed = TRUE;
        qa_bot_Arm( "level running, no cinematic on any local player" );
        QaEquipPlayers();
    }
}

//==============================================================================
//  TEST ONLY.  The first level starts unarmed (the pistol is found later in the
//  level), so the bot could not shoot or reload.  Hand each local player the
//  pistol with a clip that is not full and some spare ammo, through the same calls
//  the game's own pickups use.  The script gives the weapon a few seconds to come up.

void logic_campaign::QaEquipPlayers( void )
{
    for( s32 i = 0; i < LocalCoop_PlayerCount(); i++ )
    {
        player* pPlayer = (player*)g_ObjMgr.GetObjectByGuid( m_PlayerGuid[i] );
        if( !pPlayer )
        {
            qa_bot_Log( "equip player %d: no player object", (int)i );
            continue;
        }

        pPlayer->AddNewWeapon2( INVEN_WEAPON_DESERT_EAGLE );

        new_weapon* pWeapon = pPlayer->GetWeaponPtr( INVEN_WEAPON_DESERT_EAGLE );
        if( !pWeapon )
        {
            qa_bot_Log( "equip player %d: the pistol object does not exist", (int)i );
            continue;
        }

        const s32 PerClip = pWeapon->GetAmmoPerClip( new_weapon::AMMO_PRIMARY );
        pWeapon->SetAmmoState2( new_weapon::AMMO_PRIMARY, PerClip * 8, (PerClip > 2) ? (PerClip - 2) : PerClip );
        pPlayer->SetNextWeapon2( INVEN_WEAPON_DESERT_EAGLE, TRUE );

        qa_bot_Log( "equip player %d: pistol, %d rounds per clip", (int)i, (int)PerClip );
    }
}

//==============================================================================

void logic_campaign::WriteQaProbe( f32 DeltaTime )
{
    m_QaProbeTimer -= DeltaTime;
    if( m_QaProbeTimer > 0.0f )
    {
        return;
    }
    m_QaProbeTimer = 0.5f;

    for( s32 i = 0; i < LocalCoop_PlayerCount(); i++ )
    {
        player* pPlayer = (player*)g_ObjMgr.GetObjectByGuid( m_PlayerGuid[i] );

        if( !pPlayer )
        {
            qa_bot_Log( "player %d missing", (int)i );
            continue;
        }

        const vector3 Pos       = pPlayer->GetPosition();
        const s32     LocalSlot = pPlayer->GetLocalSlot();
        const s32     Pad       = (LocalSlot >= 0) ? g_GameInput.GetPlayerDevice( LocalSlot ) : -1;

        const char* pWeaponName = "none";
        s32         Ammo        = -1;
        s32         AmmoMax     = -1;
        s32         PerClip     = -1;
        s32         InClip      = -1;

        new_weapon* pWeapon = pPlayer->GetCurrentWeaponPtr();
        if( pWeapon )
        {
            pWeaponName = pWeapon->GetTypeDesc().GetTypeName();
            pWeapon->GetAmmoState( new_weapon::AMMO_PRIMARY, Ammo, AmmoMax, PerClip, InClip );
        }

        qa_bot_Log( "player %d guid=%08X slot=%d pad=%d pos=%d,%d,%d yaw=%d pitch=%d hp=%d weapon=%s ammo=%d inclip=%d perclip=%d cinema=%d",
                    (int)i,
                    (unsigned)m_PlayerGuid[i].GetLow(),
                    (int)LocalSlot,
                    (int)Pad,
                    (int)Pos.GetX(), (int)Pos.GetY(), (int)Pos.GetZ(),
                    (int)(RAD_TO_DEG( pPlayer->GetYaw()   ) * 10.0f),
                    (int)(RAD_TO_DEG( pPlayer->GetPitch() ) * 10.0f),
                    (int)pPlayer->GetHealth(),
                    pWeaponName,
                    (int)Ammo, (int)InClip, (int)PerClip,
                    (int)(pPlayer->IsCinemaRunning() ? 1 : 0) );
    }
}

#endif // A51_LOCAL_COOP_QA

//==============================================================================
