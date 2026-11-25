//==============================================================================
//  
//  dlg_PauseMain.hpp
//  
//==============================================================================

#ifndef DLG_PAUSE_MAIN_HPP
#define DLG_PAUSE_MAIN_HPP

//==============================================================================
//  INCLUDES
//==============================================================================

#include "UI/ui_dialog.hpp"
#include "UI/ui_frame.hpp"
#include "UI/ui_text.hpp"
#include "UI/ui_combo.hpp"

#include "dlg_PopUp.hpp"

//==============================================================================
//  DEFINES
//==============================================================================

enum pause_main_controls
{
    IDC_PAUSE_MENU_RESUME,
    IDC_PAUSE_MENU_QUIT,
    IDC_PAUSE_MENU_OPTIONS,
    IDC_PAUSE_MENU_SETTINGS,
    IDC_PAUSE_MENU_INVERTY,
    IDC_PAUSE_MENU_ZONE,  
    IDC_PAUSE_MENU_LEVEL,     
    IDC_PAUSE_MENU_CHANGE,    
    IDC_PAUSE_MENU_BUILDDATE, 
    IDC_PAUSE_MENU_VIBRATION,
    IDC_PAUSE_MENU_COOP_SWAP,       // local co-op: swap the controllers
    IDC_PAUSE_MENU_COOP_BODY1,      // local co-op: player 1's body
    IDC_PAUSE_MENU_COOP_BODY2,      // local co-op: player 2's body
    IDC_PAUSE_MENU_COOP_OPEN,       // local co-op: "Splitscreen Co-op" (opens the co-op page)
    IDC_PAUSE_MENU_COOP_BACK,       // local co-op: back to the main page
};

#ifndef CONFIG_RETAIL
#define DISPLAY_DEBUG_INFO
#endif

//==============================================================================
//  dlg_pause_main
//==============================================================================

extern void     dlg_pause_main_register  ( ui_manager* pManager );
extern ui_win*  dlg_pause_main_factory   ( s32 UserID, ui_manager* pManager, ui_manager::dialog_tem* pDialogTem, const irect& Position, ui_win* pParent, s32 Flags, void* pUserData );

class ui_button;

class dlg_pause_main : public ui_dialog
{
public:
                        dlg_pause_main       ( void );
    virtual            ~dlg_pause_main       ( void );

    xbool               Create              ( s32                       UserID,
                                              ui_manager*               pManager,
                                              ui_manager::dialog_tem*   pDialogTem,
                                              const irect&              Position,
                                              ui_win*                   pParent,
                                              s32                       Flags,
                                              void*                     pUserData);
    virtual void        Destroy             ( void );

    virtual void        Render              ( s32 ox=0, s32 oy=0 );

    virtual void        OnAccept         ( ui_win* pWin );
    virtual void        OnCancel           ( ui_win* pWin );
    virtual void        OnUpdate            ( ui_win* pWin, f32 DeltaTime );

protected:
    ui_frame*           m_pFrame1;
    ui_button*          m_pButtonResume;
    ui_button*          m_pButtonQuit;
    ui_button*          m_pButtonOptions;
    ui_button*          m_pButtonSettings;
    s32                 m_CurrHL;
    ui_button*          m_pButtonInvert;
    ui_button*          m_pButtonVibration;
    ui_button*          m_pButtonCoop[3];       // swap, body 1, body 2 (NULL when not co-op)
    ui_button*          m_pButtonCoopOpen;      // "Splitscreen Co-op" on the main page
    ui_button*          m_pButtonCoopBack;      // "Back" on the co-op page
    xbool               m_bCoopPage;            // the co-op page is showing
    void                CoopShowPage        ( xbool bCoopPage );
    void                CoopUpdateLabels    ( void );
    void                CoopOnAccept        ( s32 Row );
    void                CoopRenderPortrait  ( void );
    s32                 m_CoopPortrait[16];     // avatar portraits (multiplayer avatar select pictures)

#if defined(DISPLAY_DEBUG_INFO)
    ui_text*            m_pInvertYText;
    ui_text*            m_pZoneText;
    ui_text*            m_pLevelText;
#endif
    ui_text*            m_pChangeText;
    ui_text*            m_pBuildText;

    dlg_popup*          m_PopUp;
    s32                 m_PopUpResult;

};

//==============================================================================
#endif // DLG_PAUSE_MAIN_HPP
//==============================================================================
