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

    // Sound: voices in use (to compare with the cutscene sound on a real PC).
    {
        s32         nUsed = 0, TopCount = 0;
        const char* pTop  = "";
        g_AudioMgr.DebugVoiceStats( nUsed, pTop, TopCount );
        LocalCoop_Probe( "t=%.1f sound voices=%d top=%s x%d", x_GetTimeSec(), (int)nUsed,
                         (pTop && pTop[0]) ? pTop : "-", (int)TopCount );
    }

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

//  TEST ONLY.  Script commands ("cmd <ms> <name>" in the bot script).
//    scanner   give the scanner the way the first level's script does (the
//              "player inventory" action: AddItemToInventory2 on the first
//              player) and switch every local player to it; the script then
//              fires it with each pad.  The owner's game crashed there (R1
//              with the scanner, 2026-11-24).

static logic_campaign* s_pQaCampaign = NULL;

void logic_campaign::QaCommand( const char* pName )
{
    if( !s_pQaCampaign || !pName )
        return;

    if( x_strcmp( pName, "swaptest" ) == 0 )
    {
        // The pause menu's "Controllers" row, twice: swap, then back.
        const xbool Was = LocalCoop_SwapPads();
        g_StateMgr.CoopSetSwapPads( !Was );
        qa_bot_Log( "swaptest flipped: player 0 pad=%d player 1 pad=%d",
                    (int)g_GameInput.GetPlayerDevice( 0 ), (int)g_GameInput.GetPlayerDevice( 1 ) );
        g_StateMgr.CoopSetSwapPads( Was );
        qa_bot_Log( "swaptest restored: player 0 pad=%d player 1 pad=%d",
                    (int)g_GameInput.GetPlayerDevice( 0 ), (int)g_GameInput.GetPlayerDevice( 1 ) );
        return;
    }

    if( x_strcmp( pName, "down1" ) == 0 )
    {
        // Player 2 takes lethal damage: with player 1 up, he goes down (revivable)
        // instead of dying.
        player* p1 = (player*)g_ObjMgr.GetObjectByGuid( s_pQaCampaign->m_PlayerGuid[1] );
        qa_bot_Log( "down player 1: %s", p1 ? "lethal damage" : "no player" );
        if( p1 )
            p1->DebugSuicide();
        return;
    }

    if( x_strcmp( pName, "pistol1" ) == 0 )
    {
        // Player 2 takes the pistol out (so a shot while downed would show in his rounds).
        player* p1 = (player*)g_ObjMgr.GetObjectByGuid( s_pQaCampaign->m_PlayerGuid[1] );
        qa_bot_Log( "pistol player 1: %s", p1 ? "switching" : "no player" );
        if( p1 )
            p1->SetNextWeapon2( INVEN_WEAPON_DESERT_EAGLE, TRUE );
        return;
    }

    if( x_strcmp( pName, "downstat" ) == 0 )
    {
        // What player 2 holds while downed: shots and grenades must not change.
        player* p1 = (player*)g_ObjMgr.GetObjectByGuid( s_pQaCampaign->m_PlayerGuid[1] );
        if( p1 )
        {
            new_weapon* pWeapon = p1->GetCurrentWeaponPtr();
            const s32 Rounds = pWeapon ? (s32)( pWeapon->GetAmmoCount( new_weapon::AMMO_PRIMARY ) ) : -1;
            qa_bot_Log( "downstat player 1: downed=%d grenades=%d rounds=%d",
                        (int)p1->CoopIsDowned(),
                        (int)p1->GetInventory2().GetAmount( INVEN_GRENADE_FRAG ), (int)Rounds );
        }
        return;
    }

    if( x_strcmp( pName, "kill0" ) == 0 )
    {
        // Player 1 dies: shared fate, the level restarts from the checkpoint.
        // The owner's game crashed there once (a cinema still running while the
        // level unloaded, 2026-11-24).
        player* p0 = (player*)g_ObjMgr.GetObjectByGuid( s_pQaCampaign->m_PlayerGuid[0] );
        qa_bot_Log( "kill player 0: %s", p0 ? "dying" : "no player" );
        if( p0 )
            p0->DebugSuicide();
        return;
    }

    if( x_strcmp( pName, "carryover" ) == 0 )
    {
        // What a level change does with the inventory: store it (from player 1),
        // the new level's players start empty, restore it.  The owner's player 1
        // lost his weapons at a level change (2026-11-24).
        g_StateMgr.BackupPlayerInventory();
        for( s32 i = 0; i < LocalCoop_PlayerCount(); i++ )
        {
            player* pPlayer = (player*)g_ObjMgr.GetObjectByGuid( s_pQaCampaign->m_PlayerGuid[i] );
            if( pPlayer )
                pPlayer->GetInventory2().Clear();
        }
        g_StateMgr.RestorePlayerInventory();
        for( s32 i = 0; i < LocalCoop_PlayerCount(); i++ )
        {
            player* pPlayer = (player*)g_ObjMgr.GetObjectByGuid( s_pQaCampaign->m_PlayerGuid[i] );
            if( pPlayer )
                qa_bot_Log( "carryover player %d: scanner=%d pistol=%d",
                            (int)i,
                            (int)pPlayer->GetInventory2().GetAmount( INVEN_WEAPON_SCANNER ),
                            (int)pPlayer->GetInventory2().GetAmount( INVEN_WEAPON_DESERT_EAGLE ) );
        }
        return;
    }

    if( x_strcmp( pName, "scanner" ) == 0 )
    {
        player* p0 = (player*)g_ObjMgr.GetObjectByGuid( s_pQaCampaign->m_PlayerGuid[0] );
        if( p0 )
        {
            p0->AddItemToInventory2( INVEN_WEAPON_SCANNER );
        }

        for( s32 i = 0; i < LocalCoop_PlayerCount(); i++ )
        {
            player* pPlayer = (player*)g_ObjMgr.GetObjectByGuid( s_pQaCampaign->m_PlayerGuid[i] );
            if( !pPlayer )
                continue;

            const s32 Has = (s32)pPlayer->GetInventory2().GetAmount( INVEN_WEAPON_SCANNER );
            pPlayer->SetNextWeapon2( INVEN_WEAPON_SCANNER, TRUE );
            qa_bot_Log( "scanner player %d: in inventory=%d", (int)i, (int)Has );
        }
    }
}

//==============================================================================

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
        s_pQaCampaign = this;
        qa_bot_SetCommandHook( QaCommand );
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

    // Each local player's profile slot, name and body (player 2 has its own "Splitscreen P2" profile).
    for( s32 i = 0; i < LocalCoop_PlayerCount(); i++ )
    {
        const s32 Index = g_StateMgr.GetProfileListIndex( i );
        qa_bot_Log( "profile player %d: index=%d name=%s avatar=%d",
                    (int)i, (int)Index,
                    (Index >= 0) ? g_StateMgr.GetActiveProfile( Index ).GetProfileName() : "-",
                    (Index >= 0) ? (int)g_StateMgr.GetActiveProfile( Index ).GetAvatarID() : -1 );
    }

    // Shared items: give player 0 one grenade the way a script does (the
    // "player inventory" trigger action calls AddItemToInventory2 on the first
    // player) and log what both players hold before and after.
    if( LocalCoop_PlayerCount() > 1 )
    {
        player* p0 = (player*)g_ObjMgr.GetObjectByGuid( m_PlayerGuid[0] );
        player* p1 = (player*)g_ObjMgr.GetObjectByGuid( m_PlayerGuid[1] );
        if( p0 && p1 )
        {
            const s32 Before0 = (s32)p0->GetInventory2().GetAmount( INVEN_GRENADE_FRAG );
            const s32 Before1 = (s32)p1->GetInventory2().GetAmount( INVEN_GRENADE_FRAG );
            p0->AddItemToInventory2( INVEN_GRENADE_FRAG );
            const s32 After0  = (s32)p0->GetInventory2().GetAmount( INVEN_GRENADE_FRAG );
            const s32 After1  = (s32)p1->GetInventory2().GetAmount( INVEN_GRENADE_FRAG );
            qa_bot_Log( "share item=grenade p0 %d->%d p1 %d->%d", (int)Before0, (int)After0, (int)Before1, (int)After1 );
        }
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

    // Audio: voices in use (a leak would make sounds cut out and finally stop;
    // the owner heard that in a co-op cutscene, not in single player).
    {
        static s32 s_AudioTick = 0;
        if( (s_AudioTick++ % 4) == 0 )
        {
            s32         nUsed = 0, TopCount = 0;
            const char* pTop  = "";
            g_AudioMgr.DebugVoiceStats( nUsed, pTop, TopCount );
            qa_bot_Log( "audio voices=%d top=%s x%d", (int)nUsed, (pTop && pTop[0]) ? pTop : "-", (int)TopCount );
        }
    }

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
            switch( pWeapon->GetType() )                      // short codes as player::GetCurrentWeaponName
            {
            case object::TYPE_WEAPON_DESERT_EAGLE: pWeaponName = "EGL"; break;
            case object::TYPE_WEAPON_SCANNER:      pWeaponName = "SCN"; break;
            case object::TYPE_WEAPON_SMP:          pWeaponName = "SMP"; break;
            case object::TYPE_WEAPON_SHOTGUN:      pWeaponName = "SHT"; break;
            default:                               pWeaponName = "other"; break;
            }
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
