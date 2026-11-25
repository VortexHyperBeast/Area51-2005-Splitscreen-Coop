//==============================================================================
//
//  logic_Campaign.hpp
//
//==============================================================================

#ifndef LOGIC_CAMPAIGN_HPP
#define LOGIC_CAMPAIGN_HPP    

//==============================================================================
//    INCLUDES
//==============================================================================

#include "logic_Base.hpp"
#include "LocalCoop.hpp"

//==============================================================================
//    TYPES
//==============================================================================

class logic_campaign : public logic_base
{

//------------------------------------------------------------------------------
//  Public Functions
//------------------------------------------------------------------------------
   
public:
                logic_campaign      ( void );
virtual        ~logic_campaign      ( void );

virtual void    Activate            ( void );
virtual void    PlayerDied          ( s32 Victim, s32 Killer, s32 PainType );
virtual void    RequestSpawn        ( s32 PlayerIndex, xbool Immediate = FALSE );
#ifdef A51_LOCAL_COOP
        guid    GetCampaignPlayerGuid( s32 PlayerIndex ) const;
#endif
virtual void    SetPlayerSpawnInfo  ( const vector3& Position, 
                                            radian   Pitch, 
                                            radian   Yaw, 
                                            s32      Zone, 
                                      const guid&    Guid );

//------------------------------------------------------------------------------
//  Private Functions
//------------------------------------------------------------------------------
   
protected:

virtual void    AdvanceTime     ( f32 DeltaTime );
virtual void    BeginGame       ( void );
#ifdef A51_LOCAL_COOP
        void    CreateExtraPlayer( s32 Index, const vector3& Position, radian Pitch, radian Yaw, s32 Zone1, s32 Zone2 );
        void    WriteProbe      ( f32 DeltaTime );
        void    SyncCoopCinema  ( f32 DeltaTime );
#endif
#ifdef A51_LOCAL_COOP_QA
        void    ArmQaBot        ( f32 DeltaTime );
        void    QaEquipPlayers  ( void );
static  void    QaCommand       ( const char* pName );
        void    WriteQaProbe    ( f32 DeltaTime );
#endif

/*
virtual void    Initialize      ( void );

virtual void    Connect         ( s32 PlayerIndex );
virtual void    EnterGame       ( s32 PlayerIndex );
virtual void    ExitGame        ( s32 PlayerIndex );
virtual void    Disconnect      ( s32 PlayerIndex );

virtual void    ChangeTeam      ( s32 PlayerIndex );

virtual void    EndGame         ( void );

virtual void    AcceptUpdate    ( const bitstream& BitStream );
virtual void    ProvideUpdate   (       bitstream& BitStream, u32& DirtyBits );
*/

//------------------------------------------------------------------------------
//  Private Storage
//------------------------------------------------------------------------------
   
        f32     m_RespawnDelay;
        xbool   m_Alive;

        xbool   m_bSpawnInfoSet;
        vector3 m_PlayerSpawnPosition;
        radian  m_PlayerSpawnPitch;
        radian  m_PlayerSpawnYaw;
        s32     m_PlayerSpawnZone;
        guid    m_PlayerSpawnGuid;
#ifdef A51_LOCAL_COOP
        guid    m_PlayerGuid[ LOCAL_COOP_MAX_PLAYERS ];     // [0] is m_PlayerSpawnGuid
        f32     m_ProbeTimer;
        f32     m_LevelSelectLoadoutTimer;  // > 0: give the level's starting weapons when it runs out
        f32     m_CoopCinemaMismatch[ LOCAL_COOP_MAX_PLAYERS ];
#endif
#ifdef A51_LOCAL_COOP_QA
        f32     m_QaProbeTimer;
        f32     m_QaSinceBegin;     // game seconds since the level began
        f32     m_QaFreeTime;       // game seconds in a row without a cinematic on any local player
        xbool   m_QaArmed;
#endif
};

//==============================================================================
#endif // LOGIC_CAMPAIGN_HPP
//==============================================================================
