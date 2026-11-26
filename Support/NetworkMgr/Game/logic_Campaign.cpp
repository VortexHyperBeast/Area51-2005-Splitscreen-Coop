//==============================================================================
//
//  logic_Campaign.cpp
//
//==============================================================================

//==============================================================================
//    INCLUDES
//==============================================================================

#include "logic_Campaign.hpp"
#include "PerceptionMgr/PerceptionMgr.hpp"
#include "Objects/Actor/Actor.hpp"
#include "Animation/AnimData.hpp"
#include "CollisionMgr/CollisionMgr.hpp"  // For actor class
#include "Objects/Render/PostEffectMgr.hpp"
#include "StateMgr/StateMgr.hpp"
#ifdef A51_LOCAL_COOP
#include "InputMgr/GamePad.hpp"
#endif
#ifdef A51_LOCAL_COOP_QA
#include "Objects/NewWeapon.hpp"
#include "Characters/Character.hpp"
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
    m_PlayerSpawnZone       ( -1                ),
    m_LevelSelectLoadoutTimer( 0.0f             ),
    m_LevelSelectLoadoutCheck( 0.0f             )
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

//  Level Select starts a level without the weapons the earlier levels would
//  have given (the owner had no scanner).  The level's own start-up scripts
//  set the inventory too, so a few seconds after the start, every local player
//  who still holds no weapon gets the level's starting weapons.
//  Returns TRUE when every player holds a weapon (nothing left to do).
static xbool GiveLevelSelectLoadout( const guid* pGuids, s32 nPlayers )
{
    xbool bAllArmed = TRUE;
    for( s32 i = 0; i < nPlayers; i++ )
    {
        player* pPlayer = (player*)g_ObjMgr.GetObjectByGuid( pGuids[i] );
        if( !pPlayer || pPlayer->IsDead() || pPlayer->IsCinemaRunning() )
        {
            bAllArmed = FALSE;          // not ready yet: try again later
            continue;
        }
        inventory2& Inv = pPlayer->GetInventory2();
        const xbool bArmed = (Inv.GetAmount( INVEN_WEAPON_SCANNER ) > 0) || (Inv.GetAmount( INVEN_WEAPON_DESERT_EAGLE ) > 0) ||
                             (Inv.GetAmount( INVEN_WEAPON_SMP ) > 0);
        if( !bArmed )
        {
            pPlayer->CoopGiveLevelLoadout( g_ActiveConfig.GetLevelPath() );
#ifdef A51_LOCAL_COOP_QA
            qa_bot_Log( "loadout player %d: scanner=%d smp=%d pistol=%d given=1", (int)i,
                        (int)Inv.GetAmount( INVEN_WEAPON_SCANNER ), (int)Inv.GetAmount( INVEN_WEAPON_SMP ),
                        (int)Inv.GetAmount( INVEN_WEAPON_DESERT_EAGLE ) );
#endif
            if( (Inv.GetAmount( INVEN_WEAPON_SCANNER ) <= 0) && (Inv.GetAmount( INVEN_WEAPON_DESERT_EAGLE ) <= 0) )
                bAllArmed = FALSE;      // the game did not take it yet
        }
    }
    return bAllArmed;
}

static void UpdateStartSurfaceHold( const guid* pGuids, s32 nPlayers, f32 DeltaTime );   // below BeginGame

void logic_campaign::AdvanceTime( f32 DeltaTime )
{
    // Level Select: every 2 s for the first minute, any player without a
    // weapon gets the level's starting weapons (the level's own start-up, the
    // spawn and checkpoints can empty the inventory after the start).
    if( m_LevelSelectLoadoutTimer > 0.0f )
    {
        m_LevelSelectLoadoutTimer -= DeltaTime;
        m_LevelSelectLoadoutCheck -= DeltaTime;
        if( m_LevelSelectLoadoutCheck <= 0.0f )
        {
            m_LevelSelectLoadoutCheck = 2.0f;
            if( GiveLevelSelectLoadout( m_PlayerGuid, LocalCoop_PlayerCount() ) && (m_LevelSelectLoadoutTimer < 50.0f) )
                m_LevelSelectLoadoutTimer = 0.0f;
        }
        if( m_LevelSelectLoadoutTimer < 0.0f )
            m_LevelSelectLoadoutTimer = 0.0f;
    }

    UpdateStartSurfaceHold( m_PlayerGuid, LocalCoop_PlayerCount(), DeltaTime );

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

//==============================================================================
//  The Last Exit started from Level Select: the players appear on the train, but it is
//  already moving and leaves before they land on it (same in single player).  For the first
//  seconds of a level, a rideable animated surface right under a player who is not standing
//  on anything is held still until every player stands on something (or 8 s pass).

extern guid g_HoldAnimSurfaceGuid;
extern guid g_HoldStartMovers[4];
#include "Objects/Coupler.hpp"
static f32  s_StartHoldTime = 0.0f;         // seconds left to look for / hold a start surface
static f32  s_StartHoldSettled = 0.0f;      // how long every player has stood on something

static void UpdateStartSurfaceHold( const guid* pGuids, s32 nPlayers, f32 DeltaTime )
{
    if( s_StartHoldTime <= 0.0f )
        return;
    s_StartHoldTime -= DeltaTime;

    xbool AllSupported = TRUE;
    for( s32 i = 0; i < nPlayers; i++ )
    {
        player* pPlayer = (player*)g_ObjMgr.GetObjectByGuid( pGuids[i] );
        if( !pPlayer || !pPlayer->GetLocoPointer() )
            continue;
        const character_physics& Phys = pPlayer->GetLocoPointer()->m_Physics;
        // Standing: the physics says so, or the player has stopped dropping (the bot showed a
        // player resting on the held train while the support state still read "none").
        static f32 s_LastY[4] = { 0, 0, 0, 0 };
        const f32  Y          = pPlayer->GetPosition().GetY();
        const xbool NotFalling = ( i < 4 ) && ( x_abs( Y - s_LastY[i] ) < 0.5f );
        if( i < 4 ) s_LastY[i] = Y;
        const xbool Supported = ( Phys.GetSupportState() == CharacterSupportState::Walkable ) ||
                                ( Phys.GetMovingPlatformGuid() != 0 ) || NotFalling;
        if( Supported )
            continue;
        AllSupported = FALSE;

        // Not standing on anything: a rideable surface within 3 m?  Hold it.
        if( g_HoldAnimSurfaceGuid == 0 )
        {
            const vector3 Pos = pPlayer->GetPosition();
            for( slot_id Slot = g_ObjMgr.GetFirst( object::TYPE_ANIM_SURFACE ); Slot != SLOT_NULL; Slot = g_ObjMgr.GetNext( Slot ) )
            {
                object* pObj = g_ObjMgr.GetObjectBySlot( Slot );
                if( pObj && ( pObj->GetAttrBits() & object::ATTR_ACTOR_RIDEABLE ) &&
                    ( ( pObj->GetPosition() - Pos ).LengthSquared() < 300.0f * 300.0f ) )
                {
                    g_HoldAnimSurfaceGuid = pObj->GetGuid();
                    // what moves it: the parents of the couplers it hangs on, up to 4 levels
                    {
                        guid Child = g_HoldAnimSurfaceGuid;
                        for( s32 k = 0; k < 4; k++ )
                        {
                            guid Parent = coupler::FindParentOf( Child );
                            g_HoldStartMovers[k] = Parent;
                            if( Parent == 0 ) break;
#ifdef A51_LOCAL_COOP
                            object* pParent = g_ObjMgr.GetObjectByGuid( Parent );
                            LocalCoop_Probe( "start: it is moved by %08X (%s)", (unsigned)Parent.GetLow(),
                                             pParent ? pParent->GetTypeDesc().GetTypeName() : "?" );
#endif
                            Child = Parent;
                        }
                    }
#ifdef A51_LOCAL_COOP
                    LocalCoop_Probe( "start: holding moving surface %08X under player %d until everyone stands on it",
                                     (unsigned)g_HoldAnimSurfaceGuid.GetLow(), (int)i );
#endif
                    break;
                }
            }
        }
    }

    s_StartHoldSettled = AllSupported ? ( s_StartHoldSettled + DeltaTime ) : 0.0f;
    if( ( g_HoldAnimSurfaceGuid != 0 ) && ( ( s_StartHoldSettled > 1.0f ) || ( s_StartHoldTime <= 0.0f ) ) )
    {
#ifdef A51_LOCAL_COOP
        for( s32 k = 0; k < 4; k++ )
        {
            object* pMover = g_ObjMgr.GetObjectByGuid( g_HoldStartMovers[k] );
            if( pMover )
                LocalCoop_Probe( "start: also held %08X (%s)", (unsigned)g_HoldStartMovers[k].GetLow(),
                                 pMover->GetTypeDesc().GetTypeName() );
        }
        LocalCoop_Probe( "start: released surface %08X (%s)", (unsigned)g_HoldAnimSurfaceGuid.GetLow(),
                         ( s_StartHoldSettled > 1.0f ) ? "everyone is standing" : "time is up" );
#endif
        g_HoldAnimSurfaceGuid = 0;
        for( s32 k = 0; k < 4; k++ ) g_HoldStartMovers[k] = 0;
        s_StartHoldTime = 0.0f;
    }
    else if( ( g_HoldAnimSurfaceGuid == 0 ) && ( s_StartHoldSettled > 1.0f ) )
    {
        s_StartHoldTime = 0.0f;             // everyone stands: nothing to hold this level
    }
}

void logic_campaign::BeginGame( void )
{
    g_HoldAnimSurfaceGuid = 0;
    for( s32 k = 0; k < 4; k++ ) g_HoldStartMovers[k] = 0;
    s_StartHoldTime       = 8.0f;
    s_StartHoldSettled    = 0.0f;

    // Level Select: the level's starting weapons a few seconds in (see GiveLevelSelectLoadout).
    if( g_StateMgr.LevelSelectLoadoutPending() )
    {
        m_LevelSelectLoadoutTimer = 60.0f;
        m_LevelSelectLoadoutCheck = 3.0f;
    }

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

    // Once per level start: how full the shared animation-data pool is.  It is only emptied when
    // every anim group is unloaded; the owner's game crashed filling it after many level loads in
    // one session (Now Boarding, Dr. Cray), and levels there also lost their floors.
    {
        static guid s_LastP2 = 0;
        if( ( LocalCoop_PlayerCount() > 1 ) && ( m_PlayerGuid[1] != s_LastP2 ) && ( m_PlayerGuid[1] != 0 ) )
        {
            s_LastP2 = m_PlayerGuid[1];
            s32 nGroups = 0, nPools = 0, nBytes = 0;
            anim_group::DebugPoolStats( nGroups, nPools, nBytes );
#ifdef A51_LOCAL_COOP_QA
            qa_bot_Log( "level %d start: anim groups %d, anim pool %d of 256 blocks, %d KB", (int)g_ActiveConfig.GetLevelID(),
                        (int)nGroups, (int)nPools, (int)( nBytes / 1024 ) );
#endif
            LocalCoop_Probe( "level %d start: anim groups %d, anim pool %d of 256 blocks, %d KB", (int)g_ActiveConfig.GetLevelID(),
                             (int)nGroups, (int)nPools, (int)( nBytes / 1024 ) );

            // Is there a floor under each player?  (The owner fell through every floor after
            // a long session; this shows whether the level's collision is there at all.)
            for( s32 i = 0; i < LocalCoop_PlayerCount(); i++ )
            {
                player* pPlayer = (player*)g_ObjMgr.GetObjectByGuid( m_PlayerGuid[i] );
                if( !pPlayer )
                    continue;
                const vector3 Top    = pPlayer->GetPosition() + vector3( 0.0f, 100.0f, 0.0f );
                const vector3 Bottom = Top - vector3( 0.0f, 3000.0f, 0.0f );
                g_CollisionMgr.RaySetup( pPlayer->GetGuid(), Top, Bottom );
                g_CollisionMgr.CheckCollisions( object::TYPE_ALL_TYPES, object::ATTR_COLLIDABLE,
                                                object::ATTR_LIVING | object::ATTR_COLLISION_PERMEABLE );
                if( g_CollisionMgr.m_nCollisions > 0 )
                {
                    const collision_mgr::collision& C = g_CollisionMgr.m_Collisions[0];
                    object* pHit = g_ObjMgr.GetObjectByGuid( C.ObjectHitGuid );
                    LocalCoop_Probe( "level %d start: player %d floor %d cm below (%s)", (int)g_ActiveConfig.GetLevelID(), (int)i,
                                     (int)( C.T * 3000.0f - 100.0f ), pHit ? pHit->GetTypeDesc().GetTypeName() : "static" );
                }
                else
                {
                    LocalCoop_Probe( "level %d start: player %d NO FLOOR within 29 m below", (int)g_ActiveConfig.GetLevelID(), (int)i );
                }
            }
        }
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

unsigned g_QaWarpUntilWall = 0;
guid     g_QaWarpPlayer    = 0;

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

    if( (x_strcmp( pName, "mutate1" ) == 0) || (x_strcmp( pName, "unmutate1" ) == 0) )
    {
        // Player 2 alone turns into the mutant (and back): player 1 must stay as
        // he is, nothing global (time, vision) may follow player 2.
        const xbool bMutate = ( pName[0] == 'm' );
        player* p1 = (player*)g_ObjMgr.GetObjectByGuid( s_pQaCampaign->m_PlayerGuid[1] );
        qa_bot_Log( "%s player 1: %s", pName, p1 ? "changing" : "no player" );
        if( p1 )
        {
            if( bMutate )
            {
                p1->AddNewWeapon2( INVEN_WEAPON_MUTATION );
                p1->SetMutagen( 100.0f );
            }
            p1->CoopForceMutation( bMutate );
        }
        return;
    }

    if( x_strcmp( pName, "mutstat" ) == 0 )
    {
        player* p0 = (player*)g_ObjMgr.GetObjectByGuid( s_pQaCampaign->m_PlayerGuid[0] );
        player* p1 = (player*)g_ObjMgr.GetObjectByGuid( s_pQaCampaign->m_PlayerGuid[1] );
        qa_bot_Log( "mutstat p0=%d p1=%d speed=%d abil0=%d abil1=%d body1=%x",
                    p0 ? (int)p0->IsMutated() : -1, p1 ? (int)p1->IsMutated() : -1,
                    (int)( g_PerceptionMgr.GetGlobalTimeDialation() * 100.0f ),
                    p0 ? (int)p0->CoopMutationAbilities() : -1, p1 ? (int)p1->CoopMutationAbilities() : -1,
                    p1 ? (unsigned)p1->CoopBodyMeshMask() : 0u );
        return;
    }

    if( x_strcmp( pName, "pause" ) == 0 )
    {
        // The pause menu, as a player's Start press opens it; the game resumes
        // by itself after 15 s of wall-clock time (main.cpp).  Screenshots in
        // that time show what both views look like while paused.
        extern unsigned g_QaUnpauseAtWall;
        g_StateMgr.SetPaused( TRUE, 0 );
        g_QaUnpauseAtWall = qa_bot_WallMs() + 15000;
        qa_bot_Log( "pause: on paused=%d", (int)g_StateMgr.IsPaused() );
        return;
    }

    if( ( x_strcmp( pName, "warpfeet" ) == 0 ) || ( x_strcmp( pName, "warpraw" ) == 0 ) )
    {
        // A grav-charge screen warp at player 1's feet for 6 s, drawn in both views.  "warpraw"
        // draws it the old way (camera inside the sphere not skipped) to show the stall.
        extern unsigned g_QaWarpUntilWall;
        extern guid     g_QaWarpPlayer;
        extern xbool    g_RenderWarpKeepInside;
        g_QaWarpPlayer    = s_pQaCampaign->m_PlayerGuid[0];
        g_RenderWarpKeepInside = ( x_strcmp( pName, "warpraw" ) == 0 );
        g_QaWarpUntilWall = qa_bot_WallMs() + 6000;
        qa_bot_Log( "warp feet: on raw=%d", (int)g_RenderWarpKeepInside );
        return;
    }

    if( x_strcmp( pName, "dmgtest" ) == 0 )
    {
        // Enemy health rule: player 1 hits the nearest living enemy with a small generic pain
        // three times; the "enemy damage:" lines show raw vs taken damage and the health.
        player* p0 = (player*)g_ObjMgr.GetObjectByGuid( s_pQaCampaign->m_PlayerGuid[0] );
        character* pBest = NULL;
        f32 Best = F32_MAX;
        for( s32 t = 0; p0 && ( t < object::TYPE_END_OF_LIST ); t++ )
        {
            for( slot_id Slot = g_ObjMgr.GetFirst( (object::type)t ); Slot != SLOT_NULL; Slot = g_ObjMgr.GetNext( Slot ) )
            {
                object* pObj = g_ObjMgr.GetObjectBySlot( Slot );
                if( !pObj || !pObj->IsKindOf( character::GetRTTI() ) ) continue;
                character& C = character::GetSafeType( *pObj );
                if( C.GetHealth() <= 0.0f ) continue;
                // an enemy of the players (friendly NPCs ignore the players' pain)
                if( !C.IsEnemyFaction( C.GetFactionForGuid( p0->GetGuid() ) ) ) continue;
                const f32 D = ( pObj->GetPosition() - p0->GetPosition() ).LengthSquared();
                if( D < Best ) { Best = D; pBest = &C; }
            }
        }
        if( !pBest ) { qa_bot_Log( "dmgtest: no living enemy in this level" ); return; }
        extern guid g_QaDmgTarget;
        g_QaDmgTarget = pBest->GetGuid();
        for( s32 k = 0; k < 3; k++ )
        {
            const f32 Before = pBest->GetHealth();
            pain Pain;
            Pain.Setup( "GENERIC_1", p0->GetGuid(), pBest->GetPosition() );
            Pain.SetDirectHitGuid( pBest->GetGuid() );
            Pain.ApplyToObject( pBest );
            qa_bot_Log( "dmgtest: %s health %.1f -> %.1f (max %.1f)", pBest->GetTypeDesc().GetTypeName(),
                        Before, pBest->GetHealth(), pBest->GetMaxHealth() );
        }
        g_QaDmgTarget = 0;
        return;
    }

    if( x_strcmp( pName, "geominfo" ) == 0 )
    {
        // The owner's hazmat players look brighter than the hazmat NPCs and have no visor:
        // list the skin geometry of player 1 and of the nearest NPC (meshes, virtual meshes
        // and the mask in use, virtual textures, materials) to compare them.
        object* pTargets[2] = { g_ObjMgr.GetObjectByGuid( s_pQaCampaign->m_PlayerGuid[0] ), NULL };
        {
            f32 Best = F32_MAX;
            const vector3 P0 = pTargets[0] ? pTargets[0]->GetPosition() : vector3( 0, 0, 0 );
            for( s32 t = 0; t < object::TYPE_END_OF_LIST; t++ )
            {
                for( slot_id Slot = g_ObjMgr.GetFirst( (object::type)t ); Slot != SLOT_NULL; Slot = g_ObjMgr.GetNext( Slot ) )
                {
                    object* pObj = g_ObjMgr.GetObjectBySlot( Slot );
                    if( !pObj || !pObj->IsKindOf( actor::GetRTTI() ) || pObj->IsKindOf( player::GetRTTI() ) )
                        continue;
                    const f32 D = ( pObj->GetPosition() - P0 ).LengthSquared();
                    if( D < Best ) { Best = D; pTargets[1] = pObj; }
                }
            }
        }
        for( s32 k = 0; k < 2; k++ )
        {
            if( !pTargets[k] ) { qa_bot_Log( "geominfo %d: none", k ); continue; }
            actor&     A  = actor::GetSafeType( *pTargets[k] );
            skin_inst& SI = A.GetSkinInst();
            skin_geom* pG = SI.GetSkinGeom();
            qa_bot_Log( "geominfo %d: %s geom=%s mask=%08X", k, pTargets[k]->GetTypeDesc().GetTypeName(),
                        SI.GetSkinGeomName() ? SI.GetSkinGeomName() : "?", (unsigned)SI.GetVMeshMask().VMeshMask );
            if( !pG ) continue;
            for( s32 i = 0; i < pG->m_nMeshes; i++ )
            {
                const geom::mesh& M = pG->m_pMesh[i];
                qa_bot_Log( "geominfo %d: mesh %d %s", k, (int)i, pG->GetMeshName( i ) );
                for( s32 j = 0; j < M.nSubMeshes; j++ )
                {
                    const s32 iMat = pG->m_pSubMesh[ M.iSubMesh + j ].iMaterial;
                    if( ( iMat < 0 ) || ( iMat >= pG->m_nMaterials ) ) continue;
                    const geom::material& Mat = pG->m_pMaterial[iMat];
                    const char* pTex = ( Mat.nTextures > 0 )
                                     ? &pG->m_pStringData[ pG->m_pTexture[ Mat.iTexture ].FileNameOffset ] : "-";
                    qa_bot_Log( "geominfo %d:   submesh %d material %d type %d flags %04X tex %s", k, (int)j, (int)iMat,
                                (int)Mat.Type, (unsigned)Mat.Flags, pTex );
                }
            }
            for( s32 i = 0; i < pG->m_nVirtualMeshes; i++ )
                qa_bot_Log( "geominfo %d: vmesh %d %s", k, (int)i, pG->GetVMeshName( i ) );
            for( s32 i = 0; i < pG->m_nVirtualTextures; i++ )
                qa_bot_Log( "geominfo %d: vtexture %d %s materials=%08X", k, (int)i,
                            &pG->m_pStringData[ pG->m_pVirtualTextures[i].NameOffset ], (unsigned)pG->m_pVirtualTextures[i].MaterialMask );
            qa_bot_Log( "geominfo %d: %d materials", k, (int)pG->m_nMaterials );
        }
        return;
    }

    if( x_strcmp( pName, "tomenu" ) == 0 )
    {
        // Quit to the main menu the way the pause menu's Quit does; the bot then presses A
        // through the menus again and the next level of A51_QA_LEVEL_ID starts.
        qa_bot_Log( "to menu" );
        qa_bot_Disarm( "quit to the main menu" );
        g_ActiveConfig.SetExitReason( GAME_EXIT_PLAYER_QUIT );
        return;
    }

    if( x_strcmp( pName, "warpmark" ) == 0 )
    {
        qa_bot_Log( "warp mark: the trigger is pulled" );
        return;
    }

    if( x_strcmp( pName, "bbg0" ) == 0 )
    {
        // Player 1 takes the blue gun whose shot warps the screen (the owner's
        // views froze when it was fired, 2026-11-25).
        player* p0 = (player*)g_ObjMgr.GetObjectByGuid( s_pQaCampaign->m_PlayerGuid[0] );
        qa_bot_Log( "bbg player 0: %s", p0 ? "switching" : "no player" );
        if( p0 )
        {
            p0->AddNewWeapon2( INVEN_WEAPON_BBG );
            new_weapon* pWeapon = p0->GetWeaponPtr( INVEN_WEAPON_BBG );
            if( pWeapon )
            {
                const s32 PerClip = pWeapon->GetAmmoPerClip( new_weapon::AMMO_PRIMARY );
                pWeapon->SetAmmoState2( new_weapon::AMMO_PRIMARY, PerClip * 4, PerClip );
            }
            p0->SetNextWeapon2( INVEN_WEAPON_BBG, TRUE );
            qa_bot_Log( "bbg player 0: have=%d weapon_object=%d current=%d", (int)p0->GetInventory2().HasItem( INVEN_WEAPON_BBG ),
                        pWeapon ? 1 : 0, (int)p0->GetCurrentWeaponItem() );
        }
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
    // What each player starts the level with (level select gives the level's
    // starting weapons, scanner included).
    for( s32 i = 0; i < LocalCoop_PlayerCount(); i++ )
    {
        player* pPlayer = (player*)g_ObjMgr.GetObjectByGuid( m_PlayerGuid[i] );
        if( pPlayer )
            qa_bot_Log( "loadout player %d: scanner=%d smp=%d pistol=%d", (int)i,
                        (int)pPlayer->GetInventory2().GetAmount( INVEN_WEAPON_SCANNER ),
                        (int)pPlayer->GetInventory2().GetAmount( INVEN_WEAPON_SMP ),
                        (int)pPlayer->GetInventory2().GetAmount( INVEN_WEAPON_DESERT_EAGLE ) );
    }

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
            case object::TYPE_WEAPON_GAUSS:        pWeaponName = "BBG"; break;
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

        // Moving platforms (The Last Exit train): what the player stands on, and the nearest
        // animated surface within 40 m with its position, so a run shows whether the train is
        // there at the start and leaves without him.
        {
            guid Plat = 0;
            if( pPlayer->GetLocoPointer() )
                Plat = pPlayer->GetLocoPointer()->m_Physics.GetMovingPlatformGuid();
            object* pNear = NULL;
            f32     Best  = 4000.0f * 4000.0f;
            for( slot_id Slot = g_ObjMgr.GetFirst( object::TYPE_ANIM_SURFACE ); Slot != SLOT_NULL; Slot = g_ObjMgr.GetNext( Slot ) )
            {
                object* pObj = g_ObjMgr.GetObjectBySlot( Slot );
                if( !pObj ) continue;
                const f32 D = ( pObj->GetPosition() - Pos ).LengthSquared();
                if( D < Best ) { Best = D; pNear = pObj; }
            }
            if( pNear )
            {
                const vector3 NP = pNear->GetPosition();
                const bbox    NB = pNear->GetBBox();
                const u32     A  = pNear->GetAttrBits();
                const s32     Sup = pPlayer->GetLocoPointer() ? (s32)pPlayer->GetLocoPointer()->m_Physics.GetSupportState() : -1;
                qa_bot_Log( "platform %d: on=%08X support=%d nearest=%08X at %d,%d,%d top=%d dist=%d collidable=%d rideable=%d",
                            (int)i, (unsigned)Plat.GetLow(), (int)Sup, (unsigned)pNear->GetGuid().GetLow(),
                            (int)NP.GetX(), (int)NP.GetY(), (int)NP.GetZ(), (int)NB.Max.GetY(), (int)x_sqrt( Best ),
                            ( A & object::ATTR_COLLIDABLE ) ? 1 : 0, ( A & object::ATTR_ACTOR_RIDEABLE ) ? 1 : 0 );
            }
            else
            {
                qa_bot_Log( "platform %d: on=%08X nearest=none", (int)i, (unsigned)Plat.GetLow() );
            }
        }
    }
}

#endif // A51_LOCAL_COOP_QA

//==============================================================================
