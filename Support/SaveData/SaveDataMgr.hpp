//=========================================================================
//
//  SaveDataMgr.hpp
//
//=========================================================================

#ifndef SAVE_DATA_MGR_HPP
#define SAVE_DATA_MGR_HPP

//=========================================================================
//  INCLUDES
//=========================================================================

#include "SaveDataTypes.hpp"
#include "StateMgr/PlayerProfile.hpp"

#include <functional>

//=========================================================================
//  SAVE DATA MGR
//=========================================================================

class SaveDataMgr
{
public:

    SaveDataMgr ( void );
    ~SaveDataMgr( void );

    void  Init                ( void );
    void  Kill                ( void );
    void  Update              ( f32 DeltaTime );
    xbool LoadStartupSettings ( void );

    void RefreshProfiles( void );

    template<class TYPE>
    void RefreshProfiles( TYPE* pObject, void (TYPE::*pMethod)(void) )
    {
        QueueRefreshProfiles( pObject, Bind( pObject, pMethod ) );
    }

    template<class TYPE>
    void CreateProfile( s32 PlayerID,
                         TYPE* pObject,
                         void (TYPE::*pMethod)(void) )
    {
        QueueCreateProfile( PlayerID, pObject, Bind( pObject, pMethod ) );
    }

    template<class TYPE>
    void LoadProfile( const profile_info& Info,
                      s32 PlayerID,
                      TYPE* pObject,
                      void (TYPE::*pMethod)(void) )
    {
        QueueProfileOperation( SaveDataOperation::LoadProfile,
                               Info, PlayerID, pObject,
                               Bind( pObject, pMethod ) );
    }

    template<class TYPE>
    void SaveProfile( const profile_info& Info,
                      s32 PlayerID,
                      TYPE* pObject,
                      void (TYPE::*pMethod)(void) )
    {
        QueueProfileOperation( SaveDataOperation::SaveProfile,
                               Info, PlayerID, pObject,
                               Bind( pObject, pMethod ) );
    }

    template<class TYPE>
    void SaveProfileSnapshot( const profile_info& Info,
                              s32 PlayerID,
                              const player_profile& Profile,
                              TYPE* pObject,
                              void (TYPE::*pMethod)(void) )
    {
        QueueProfileSnapshot( Info, PlayerID, Profile, pObject, Bind( pObject, pMethod ) );
    }

    template<class TYPE>
    void OverwriteProfile( const profile_info& Info,
                           s32 PlayerID,
                            TYPE* pObject,
                            void (TYPE::*pMethod)(void) )
    {
        SaveProfile( Info, PlayerID, pObject, pMethod );
    }

    template<class TYPE>
    void DeleteProfile( const profile_info& Info,
                         TYPE* pObject,
                         void (TYPE::*pMethod)(void) )
    {
        QueueProfileOperation( SaveDataOperation::DeleteProfile,
                               Info, -1, pObject,
                               Bind( pObject, pMethod ) );
    }

    template<class TYPE>
    void SaveSettings( TYPE* pObject,
                       void (TYPE::*pMethod)(void) )
    {
        QueueSaveSettings( pObject, Bind( pObject, pMethod ) );
    }

    void                    CancelCallbacks     ( void* pOwner );
    void                    ClearCachedProfiles ( void );
    xbool                   IsBusy              ( void ) const;
    const SaveDataResult& GetLastResult       ( void ) const;
    profile_info&           GetProfileInfo      ( s32 PlayerID );
    void                    GetProfileNames     ( xarray<profile_info*>& Result );

#ifdef A51_LOCAL_COOP
    // Local co-op: player 2's own profile, read and written on the calling
    // thread.  Both refuse (return FALSE) while a queued request is running.
    // LoadOrCreate: finds the profile named pName, or writes Profile (already
    // set up by the caller) as a new one; records it as PlayerID's selection.
    xbool CoopLoadOrCreateProfileSync( const char* pName, s32 PlayerID,
                                       player_profile& Profile, xbool& bCreated );
    xbool CoopSaveProfileSync        ( s32 PlayerID, const player_profile& Profile );
    // Runs queued requests to the end (at most MaxSeconds), e.g. at shutdown.
    void  CoopFlush                  ( f32 MaxSeconds );
#endif

private:

    struct Impl;

    typedef std::function<void(const SaveDataResult&)> CompletionCallback;

    void QueueProfileSnapshot( const profile_info& Info, s32 PlayerID, const player_profile& Profile,
                                void* pOwner, CompletionCallback Callback );

    template<class TYPE>
    static CompletionCallback Bind( TYPE* pObject, void (TYPE::*pMethod)(void) )
    {
        return [pObject, pMethod]( const SaveDataResult& )
        {
            (pObject->*pMethod)();
        };
    }

    void QueueRefreshProfiles  ( void* pOwner,
                                 CompletionCallback Callback );
    void QueueCreateProfile    ( s32 PlayerID,
                                 void* pOwner,
                                 CompletionCallback Callback );
    void QueueProfileOperation ( SaveDataOperation Operation,
                                 const profile_info& Info,
                                 s32 PlayerID,
                                 void* pOwner,
                                 CompletionCallback Callback );
    void QueueSaveSettings     ( void* pOwner,
                                 CompletionCallback Callback );

    Impl* m_pImpl;
};

//=========================================================================
extern SaveDataMgr g_SaveDataMgr;
//=========================================================================

//=========================================================================
#endif // SAVE_DATA_MGR_HPP
//=========================================================================
