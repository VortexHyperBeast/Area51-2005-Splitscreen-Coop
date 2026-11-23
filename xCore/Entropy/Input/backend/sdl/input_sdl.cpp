//==============================================================================
//
//  input_sdl.cpp
//
//  SDL3 input backend.
//
//==============================================================================

#include "x_target.hpp"

#if !defined( TARGET_DESKTOP ) && !defined( TARGET_MOBILE )
#error This file should only be compiled for desktop or mobile platforms.
#endif

#include "x_files.hpp"
#include "e_Input.hpp"
#include "Input/backend/input_backend.hpp"
#include "SDLEngine/sdleng_window.hpp"

#include "SDL3/SDL.h"
#include "Input/backend/sdl/qa_bot.hpp"

#include <cmath>

//==============================================================================
// CONSTANTS
//==============================================================================

static s32 const MAX_GAMEPADS = INPUT_MAX_DEVICES;

static byte const DIGITAL_ON = (1 << 0);

static f32 const LEFT_STICK_DEAD_ZONE  = 0.30f;
static f32 const RIGHT_STICK_DEAD_ZONE = 0.30f;
static f32 const TRIGGER_THRESHOLD     = 30.0f   / 255.0f;

enum
{
    DIGITAL_COUNT_MOUSE   = INPUT_MOUSE_BTN_4              - INPUT_MOUSE_BTN_L + 1,
    DIGITAL_COUNT_KBD     = 256,
    RELATIVE_COUNT_MOUSE  = 3,

    XBOX_DIGITAL_COUNT    = INPUT_XBOX__DIGITAL_BUTTONS_END - INPUT_XBOX__DIGITAL_BUTTONS_BEGIN - 1,
    XBOX_ANALOG_BTN_COUNT = INPUT_XBOX__ANALOG_BUTTONS_END  - INPUT_XBOX__ANALOG_BUTTONS_BEGIN  - 1,
    XBOX_STICK_COUNT      = INPUT_XBOX__STICKS_END           - INPUT_XBOX__STICKS_BEGIN           - 1,
};

//==============================================================================
// TYPES
//==============================================================================

struct input_mouse
{
    byte Digital [ DIGITAL_COUNT_MOUSE  ];
    f32  Relative[ RELATIVE_COUNT_MOUSE ];
};

//------------------------------------------------------------------------------

struct input_keyboard
{
    byte Digital[ DIGITAL_COUNT_KBD ];
};

//------------------------------------------------------------------------------

struct input_gamepad
{
    byte Digital  [ XBOX_DIGITAL_COUNT    ];
    byte AnalogBtn[ XBOX_ANALOG_BTN_COUNT ];
    f32  Trigger  [ 2                     ];
    f32  Stick    [ XBOX_STICK_COUNT      ];
};

//------------------------------------------------------------------------------

struct gamepad_slot
{
    SDL_Gamepad*    pGamepad;
    SDL_JoystickID  InstanceID;
    input_gamepad   State;
    xbool           PendingDisconnect;
};

//==============================================================================
// STORAGE
//==============================================================================

static struct
{
    input_keyboard Keyboard;
    input_mouse    Mouse;
    gamepad_slot   Gamepad[ MAX_GAMEPADS ];

    f32             PendingMouseWheel;
    xbool           RumbleEnabled[ MAX_GAMEPADS ];
    xbool           GamepadSubsystemOwned;
    xbool           ExitApp;
    xbool           SuppressRumble;
    xbool           Initialized;
} s_Input = {};

static s32   s_SDLToDIK[ SDL_SCANCODE_COUNT ];
static xbool s_KeyMapInitialized = FALSE;

// Window pumping also runs during render-only and suspended frames. Preserve
// digital edges until the next input capture instead of relying on final state.
static input_event_buffer s_PendingEvents;

//==============================================================================
// HELPERS
//==============================================================================

static
void SetKeyMapping( SDL_Scancode ScanCode, s32 DIKCode )
{
    if( (ScanCode >= 0) && (ScanCode < SDL_SCANCODE_COUNT) )
    {
        s_SDLToDIK[ScanCode] = DIKCode;
    }
}

//==============================================================================

// SDL scancodes map to legacy DirectInput scan codes.
static
void InitializeKeyMap( void )
{
    if( s_KeyMapInitialized )
    {
        return;
    }

    for( s32 i = 0; i < SDL_SCANCODE_COUNT; i++ )
    {
        s_SDLToDIK[i] = -1;
    }

    SetKeyMapping( SDL_SCANCODE_ESCAPE, 0x01 );

    static SDL_Scancode const NumberKeys[] =
    {
        SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_3, SDL_SCANCODE_4,
        SDL_SCANCODE_5, SDL_SCANCODE_6, SDL_SCANCODE_7, SDL_SCANCODE_8,
        SDL_SCANCODE_9, SDL_SCANCODE_0
    };
    for( s32 i = 0; i < (s32)(sizeof(NumberKeys) / sizeof(NumberKeys[0])); i++ )
    {
        SetKeyMapping( NumberKeys[i], 0x02 + i );
    }

    SetKeyMapping( SDL_SCANCODE_MINUS,       0x0C );
    SetKeyMapping( SDL_SCANCODE_EQUALS,      0x0D );
    SetKeyMapping( SDL_SCANCODE_BACKSPACE,   0x0E );
    SetKeyMapping( SDL_SCANCODE_TAB,         0x0F );

    SetKeyMapping( SDL_SCANCODE_Q, 0x10 );
    SetKeyMapping( SDL_SCANCODE_W, 0x11 );
    SetKeyMapping( SDL_SCANCODE_E, 0x12 );
    SetKeyMapping( SDL_SCANCODE_R, 0x13 );
    SetKeyMapping( SDL_SCANCODE_T, 0x14 );
    SetKeyMapping( SDL_SCANCODE_Y, 0x15 );
    SetKeyMapping( SDL_SCANCODE_U, 0x16 );
    SetKeyMapping( SDL_SCANCODE_I, 0x17 );
    SetKeyMapping( SDL_SCANCODE_O, 0x18 );
    SetKeyMapping( SDL_SCANCODE_P, 0x19 );

    SetKeyMapping( SDL_SCANCODE_LEFTBRACKET,  0x1A );
    SetKeyMapping( SDL_SCANCODE_RIGHTBRACKET, 0x1B );
    SetKeyMapping( SDL_SCANCODE_RETURN,       0x1C );
    SetKeyMapping( SDL_SCANCODE_LCTRL,        0x1D );

    SetKeyMapping( SDL_SCANCODE_A, 0x1E );
    SetKeyMapping( SDL_SCANCODE_S, 0x1F );
    SetKeyMapping( SDL_SCANCODE_D, 0x20 );
    SetKeyMapping( SDL_SCANCODE_F, 0x21 );
    SetKeyMapping( SDL_SCANCODE_G, 0x22 );
    SetKeyMapping( SDL_SCANCODE_H, 0x23 );
    SetKeyMapping( SDL_SCANCODE_J, 0x24 );
    SetKeyMapping( SDL_SCANCODE_K, 0x25 );
    SetKeyMapping( SDL_SCANCODE_L, 0x26 );

    SetKeyMapping( SDL_SCANCODE_SEMICOLON,  0x27 );
    SetKeyMapping( SDL_SCANCODE_APOSTROPHE, 0x28 );
    SetKeyMapping( SDL_SCANCODE_GRAVE,      0x29 );
    SetKeyMapping( SDL_SCANCODE_LSHIFT,     0x2A );
    SetKeyMapping( SDL_SCANCODE_BACKSLASH,  0x2B );

    SetKeyMapping( SDL_SCANCODE_Z, 0x2C );
    SetKeyMapping( SDL_SCANCODE_X, 0x2D );
    SetKeyMapping( SDL_SCANCODE_C, 0x2E );
    SetKeyMapping( SDL_SCANCODE_V, 0x2F );
    SetKeyMapping( SDL_SCANCODE_B, 0x30 );
    SetKeyMapping( SDL_SCANCODE_N, 0x31 );
    SetKeyMapping( SDL_SCANCODE_M, 0x32 );

    SetKeyMapping( SDL_SCANCODE_COMMA,  0x33 );
    SetKeyMapping( SDL_SCANCODE_PERIOD, 0x34 );
    SetKeyMapping( SDL_SCANCODE_SLASH,  0x35 );
    SetKeyMapping( SDL_SCANCODE_RSHIFT, 0x36 );

    SetKeyMapping( SDL_SCANCODE_KP_MULTIPLY, 0x37 );
    SetKeyMapping( SDL_SCANCODE_LALT,        0x38 );
    SetKeyMapping( SDL_SCANCODE_SPACE,       0x39 );
    SetKeyMapping( SDL_SCANCODE_CAPSLOCK,   0x3A );

    static SDL_Scancode const FunctionKeys[] =
    {
        SDL_SCANCODE_F1, SDL_SCANCODE_F2, SDL_SCANCODE_F3, SDL_SCANCODE_F4,
        SDL_SCANCODE_F5, SDL_SCANCODE_F6, SDL_SCANCODE_F7, SDL_SCANCODE_F8,
        SDL_SCANCODE_F9, SDL_SCANCODE_F10
    };
    for( s32 i = 0; i < (s32)(sizeof(FunctionKeys) / sizeof(FunctionKeys[0])); i++ )
    {
        SetKeyMapping( FunctionKeys[i], 0x3B + i );
    }

    SetKeyMapping( SDL_SCANCODE_NUMLOCKCLEAR, 0x45 );
    SetKeyMapping( SDL_SCANCODE_SCROLLLOCK,   0x46 );

    static SDL_Scancode const KeypadKeys[] =
    {
        SDL_SCANCODE_KP_7, SDL_SCANCODE_KP_8, SDL_SCANCODE_KP_9,
        SDL_SCANCODE_KP_4, SDL_SCANCODE_KP_5, SDL_SCANCODE_KP_6,
        SDL_SCANCODE_KP_1, SDL_SCANCODE_KP_2, SDL_SCANCODE_KP_3,
        SDL_SCANCODE_KP_0, SDL_SCANCODE_KP_PERIOD
    };
    static s32 const KeypadDIK[] =
    {
        0x47, 0x48, 0x49, 0x4B, 0x4C, 0x4D,
        0x4F, 0x50, 0x51, 0x52, 0x53
    };
    for( s32 i = 0; i < (s32)(sizeof(KeypadKeys) / sizeof(KeypadKeys[0])); i++ )
    {
        SetKeyMapping( KeypadKeys[i], KeypadDIK[i] );
    }

    SetKeyMapping( SDL_SCANCODE_KP_MINUS,  0x4A );
    SetKeyMapping( SDL_SCANCODE_KP_PLUS,   0x4E );
    SetKeyMapping( SDL_SCANCODE_NONUSBACKSLASH, 0x56 );
    SetKeyMapping( SDL_SCANCODE_F11, 0x57 );
    SetKeyMapping( SDL_SCANCODE_F12, 0x58 );
    SetKeyMapping( SDL_SCANCODE_F13, 0x64 );
    SetKeyMapping( SDL_SCANCODE_F14, 0x65 );
    SetKeyMapping( SDL_SCANCODE_F15, 0x66 );
    SetKeyMapping( SDL_SCANCODE_INTERNATIONAL3, 0x7D );
    SetKeyMapping( SDL_SCANCODE_KP_EQUALS,     0x8D );
    SetKeyMapping( SDL_SCANCODE_KP_ENTER,      0x9C );
    SetKeyMapping( SDL_SCANCODE_KP_COMMA,      0xB3 );
    SetKeyMapping( SDL_SCANCODE_RCTRL,      0x9D );
    SetKeyMapping( SDL_SCANCODE_KP_DIVIDE, 0xB5 );
    SetKeyMapping( SDL_SCANCODE_RALT,       0xB8 );

    SetKeyMapping( SDL_SCANCODE_PAUSE,       0xC5 );
    SetKeyMapping( SDL_SCANCODE_HOME,        0xC7 );
    SetKeyMapping( SDL_SCANCODE_UP,          0xC8 );
    SetKeyMapping( SDL_SCANCODE_PAGEUP,      0xC9 );
    SetKeyMapping( SDL_SCANCODE_LEFT,        0xCB );
    SetKeyMapping( SDL_SCANCODE_RIGHT,       0xCD );
    SetKeyMapping( SDL_SCANCODE_END,         0xCF );
    SetKeyMapping( SDL_SCANCODE_DOWN,        0xD0 );
    SetKeyMapping( SDL_SCANCODE_PAGEDOWN,    0xD1 );
    SetKeyMapping( SDL_SCANCODE_INSERT,      0xD2 );
    SetKeyMapping( SDL_SCANCODE_DELETE,      0xD3 );
    SetKeyMapping( SDL_SCANCODE_LGUI,        0xDB );
    SetKeyMapping( SDL_SCANCODE_RGUI,        0xDC );
    SetKeyMapping( SDL_SCANCODE_APPLICATION, 0xDD );
    SetKeyMapping( SDL_SCANCODE_POWER,       0xDE );
    SetKeyMapping( SDL_SCANCODE_SLEEP,       0xDF );
    SetKeyMapping( SDL_SCANCODE_WAKE,        0xE3 );

    SetKeyMapping( SDL_SCANCODE_PRINTSCREEN, 0xB7 );
    SetKeyMapping( SDL_SCANCODE_MUTE,        0xA0 );
    SetKeyMapping( SDL_SCANCODE_VOLUMEDOWN,  0xAE );
    SetKeyMapping( SDL_SCANCODE_VOLUMEUP,    0xB0 );
    SetKeyMapping( SDL_SCANCODE_MEDIA_PLAY_PAUSE, 0xA2 );
    SetKeyMapping( SDL_SCANCODE_MEDIA_STOP,       0xA4 );
    SetKeyMapping( SDL_SCANCODE_MEDIA_PREVIOUS_TRACK, 0x90 );
    SetKeyMapping( SDL_SCANCODE_MEDIA_NEXT_TRACK,     0x99 );
    SetKeyMapping( SDL_SCANCODE_AC_HOME,          0xB2 );
    SetKeyMapping( SDL_SCANCODE_AC_SEARCH,        0xE5 );
    SetKeyMapping( SDL_SCANCODE_AC_BOOKMARKS,     0xE6 );
    SetKeyMapping( SDL_SCANCODE_AC_REFRESH,       0xE7 );
    SetKeyMapping( SDL_SCANCODE_AC_STOP,          0xE8 );
    SetKeyMapping( SDL_SCANCODE_AC_FORWARD,       0xE9 );
    SetKeyMapping( SDL_SCANCODE_AC_BACK,          0xEA );
    SetKeyMapping( SDL_SCANCODE_MEDIA_SELECT,     0xED );

    s_KeyMapInitialized = TRUE;
}

//==============================================================================

static
xbool IsEventForWindow( SDL_Event const& Event, SDL_WindowID WindowID )
{
    if( !WindowID )
    {
        return TRUE;
    }

    switch( Event.type )
    {
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
        {
            return (Event.key.windowID == 0) || (Event.key.windowID == WindowID);
        }

        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
        {
            return (Event.button.windowID == 0) || (Event.button.windowID == WindowID);
        }

        case SDL_EVENT_MOUSE_WHEEL:
        {
            return (Event.wheel.windowID == 0) || (Event.wheel.windowID == WindowID);
        }

        case SDL_EVENT_WINDOW_FOCUS_LOST:
        case SDL_EVENT_WINDOW_MINIMIZED:
        {
            return (Event.window.windowID == 0) || (Event.window.windowID == WindowID);
        }

        default:
        {
            return TRUE;
        }
    }
}

//==============================================================================

static
void ClearRelativeState( void )
{
    for( s32 i = 0; i < RELATIVE_COUNT_MOUSE; i++ )
    {
        s_Input.Mouse.Relative[i] = 0.0f;
    }
}

//==============================================================================

static
void AppendDigitalEvent( input_event_buffer& Events,
                         input_gadget       GadgetID,
                         s32                DeviceID,
                         byte&              State,
                         xbool              IsDown,
                         f32                Value,
                         u32                TimeStamp )
{
    xbool const WasDown = (State & DIGITAL_ON) != 0;
    if( WasDown == IsDown )
    {
        return;
    }

    if( IsDown )
    {
        if( Events.Append( GadgetID, DeviceID, INPUT_EVENT_PRESSED, Value, TimeStamp ) )
        {
            State = DIGITAL_ON;
        }
    }
    else
    {
        if( Events.Append( GadgetID, DeviceID, INPUT_EVENT_RELEASED, 0.0f, TimeStamp ) )
        {
            State = 0;
        }
    }
}

//==============================================================================

static
input_gadget GetMouseButtonGadget( s32 ButtonIndex )
{
    static input_gadget const Gadgets[DIGITAL_COUNT_MOUSE] =
    {
        INPUT_MOUSE_BTN_L,
        INPUT_MOUSE_BTN_R,
        INPUT_MOUSE_BTN_C,
        INPUT_MOUSE_BTN_0,
        INPUT_MOUSE_BTN_1,
        INPUT_MOUSE_BTN_2,
        INPUT_MOUSE_BTN_3,
        INPUT_MOUSE_BTN_4
    };

    ASSERT( (ButtonIndex >= 0) && (ButtonIndex < DIGITAL_COUNT_MOUSE) );
    return (ButtonIndex >= 0) && (ButtonIndex < DIGITAL_COUNT_MOUSE)
    ? Gadgets[ButtonIndex]
    : INPUT_UNDEFINED;
}

//==============================================================================

static
s32 GetMouseButtonIndex( input_gadget GadgetID )
{
    for( s32 i = 0; i < DIGITAL_COUNT_MOUSE; i++ )
    {
        if( GadgetID == GetMouseButtonGadget( i ) )
        {
            return i;
        }
    }

    return -1;
}

//==============================================================================

static
Uint8 GetSDLMouseButtonID( s32 ButtonIndex )
{
    static Uint8 const ButtonIDs[DIGITAL_COUNT_MOUSE] =
    {
        SDL_BUTTON_LEFT,
        SDL_BUTTON_RIGHT,
        SDL_BUTTON_MIDDLE,
        SDL_BUTTON_X1,
        SDL_BUTTON_X2,
        6,
        7,
        8
    };

    ASSERT( (ButtonIndex >= 0) && (ButtonIndex < DIGITAL_COUNT_MOUSE) );
    return (ButtonIndex >= 0) && (ButtonIndex < DIGITAL_COUNT_MOUSE)
    ? ButtonIDs[ButtonIndex]
    : 0;
}

//==============================================================================

static
s32 GetMouseButtonIndexFromSDL( Uint8 ButtonID )
{
    for( s32 i = 0; i < DIGITAL_COUNT_MOUSE; i++ )
    {
        if( ButtonID == GetSDLMouseButtonID( i ) )
        {
            return i;
        }
    }

    return -1;
}

//==============================================================================

static
f32 NormalizeStickAxis( Sint16 Value )
{
    return (f32)Value / 32768.0f;
}

//==============================================================================

static
void ApplyStickDeadZone( f32& X, f32& Y, f32 DeadZone )
{
    f32 Magnitude = sqrtf( X * X + Y * Y );

    if( Magnitude > 1.0f )
    {
        X /= Magnitude;
        Y /= Magnitude;
        Magnitude = 1.0f;
    }

    if( Magnitude < DeadZone )
    {
        X = 0.0f;
        Y = 0.0f;
        return;
    }

    f32 const Scale = (Magnitude - DeadZone) /
    (Magnitude * (1.0f - DeadZone));
    X *= Scale;
    Y *= Scale;
}

//==============================================================================

static
s32 FindGamepadSlot( SDL_JoystickID InstanceID )
{
    for( s32 i = 0; i < MAX_GAMEPADS; i++ )
    {
        if( s_Input.Gamepad[i].pGamepad &&
            (s_Input.Gamepad[i].InstanceID == InstanceID) )
        {
            return i;
        }
    }

    return -1;
}

//==============================================================================

static
s32 FindFreeGamepadSlot( void )
{
    for( s32 i = 0; i < MAX_GAMEPADS; i++ )
    {
        if( !s_Input.Gamepad[i].pGamepad && !s_Input.Gamepad[i].PendingDisconnect )
        {
            return i;
        }
    }

    return -1;
}

//==============================================================================

static
void StopGamepadRumble( s32 Slot )
{
    if( (Slot >= 0) && (Slot < MAX_GAMEPADS) && s_Input.Gamepad[Slot].pGamepad )
    {
        SDL_RumbleGamepad( s_Input.Gamepad[Slot].pGamepad, 0, 0, 0 );
    }
}

//==============================================================================

static
void CloseGamepad( s32 Slot, xbool PreserveState )
{
    if( (Slot < 0) || (Slot >= MAX_GAMEPADS) )
    {
        return;
    }

    gamepad_slot& Pad = s_Input.Gamepad[Slot];

    StopGamepadRumble( Slot );
    if( Pad.pGamepad )
    {
        SDL_CloseGamepad( Pad.pGamepad );
    }

    Pad.pGamepad   = NULL;
    Pad.InstanceID = 0;

    if( PreserveState )
    {
        Pad.PendingDisconnect = TRUE;
    }
    else
    {
        x_memset( &Pad.State, 0, sizeof(Pad.State) );
        Pad.PendingDisconnect = FALSE;
    }
}

//==============================================================================

static
void OpenGamepad( SDL_JoystickID InstanceID )
{
    if( FindGamepadSlot( InstanceID ) >= 0 )
    {
        return;
    }

    s32 const Slot = FindFreeGamepadSlot();
    if( Slot < 0 )
    {
        return;
    }

    SDL_Gamepad* const pGamepad = SDL_OpenGamepad( InstanceID );
    if( !pGamepad )
    {
        x_DebugMsg( "SDL input: failed to open gamepad %u: %s\n",
                    (u32)InstanceID,
                    SDL_GetError() );
        return;
    }

    gamepad_slot& Pad = s_Input.Gamepad[Slot];
    x_memset( &Pad.State, 0, sizeof(Pad.State) );
    Pad.pGamepad          = pGamepad;
    Pad.InstanceID        = InstanceID;
    Pad.PendingDisconnect = FALSE;

    x_DebugMsg( "SDL input: opened gamepad %d '%s'\n",
                Slot,
                SDL_GetGamepadName( pGamepad ) ? SDL_GetGamepadName( pGamepad ) : "unknown" );
}

//==============================================================================

static
void OpenExistingGamepads( void )
{
    s32 Count = 0;
    SDL_JoystickID* const pIDs = SDL_GetGamepads( &Count );
    if( !pIDs )
    {
        return;
    }

    for( s32 i = 0; i < Count; i++ )
    {
        OpenGamepad( pIDs[i] );
    }

    SDL_free( pIDs );
}

//==============================================================================

static
void CaptureKeyboard( input_event_buffer& Events, u32 TimeStamp )
{
    s32 Count = 0;
    bool const* const pState = SDL_GetKeyboardState( &Count );
    bool NextState[DIGITAL_COUNT_KBD];
    x_memset( NextState, 0, sizeof(NextState) );

    if( sdleng_WindowIsActive() )
    {
        for( s32 i = 0; pState && (i < Count); i++ )
        {
            SDL_Scancode const ScanCode = (SDL_Scancode)i;
            if( (ScanCode >= 0) && (ScanCode < SDL_SCANCODE_COUNT) )
            {
                s32 const DIKCode = s_SDLToDIK[ScanCode];
                if( (DIKCode >= 0) && (DIKCode < DIGITAL_COUNT_KBD) )
                {
                    NextState[DIKCode] = pState[i];
                }
            }
        }
    }

    for( s32 i = 0; i < DIGITAL_COUNT_KBD; i++ )
    {
        AppendDigitalEvent( Events,
                            static_cast<input_gadget>( INPUT_KBD__BEGIN + i ),
                            0,
                            s_Input.Keyboard.Digital[i],
                            NextState[i],
                            1.0f,
                            TimeStamp );
    }
}

//==============================================================================

static
void CaptureMouse( input_event_buffer& Events, u32 TimeStamp )
{
    f32 X = 0.0f;
    f32 Y = 0.0f;
    SDL_MouseButtonFlags Buttons = SDL_GetRelativeMouseState( &X, &Y );

    if( !sdleng_WindowIsActive() )
    {
        X       = 0.0f;
        Y       = 0.0f;
        Buttons = 0;
    }

    s_Input.Mouse.Relative[0] = X;
    s_Input.Mouse.Relative[1] = Y;

    if( X != 0.0f )
    {
        Events.Append( INPUT_MOUSE_X_REL, 0, INPUT_EVENT_RELATIVE, X, TimeStamp );
    }
    if( Y != 0.0f )
    {
        Events.Append( INPUT_MOUSE_Y_REL, 0, INPUT_EVENT_RELATIVE, Y, TimeStamp );
    }

    for( s32 i = 0; i < DIGITAL_COUNT_MOUSE; i++ )
    {
        Uint8 const ButtonID = GetSDLMouseButtonID( i );
        xbool const IsDown = ButtonID && ((Buttons & SDL_BUTTON_MASK( ButtonID )) != 0);

        AppendDigitalEvent( Events,
                            GetMouseButtonGadget( i ),
                            0,
                            s_Input.Mouse.Digital[i],
                            IsDown,
                            1.0f,
                            TimeStamp );
    }
}

//==============================================================================

static
void CaptureMouseWheel( input_event_buffer& Events, u32 TimeStamp )
{
    f32 const Value = s_Input.PendingMouseWheel;
    s_Input.PendingMouseWheel = 0.0f;

    if( Value == 0.0f )
    {
        return;
    }

    s_Input.Mouse.Relative[2] += Value;
    Events.Append( INPUT_MOUSE_WHEEL_REL,
                   0,
                   INPUT_EVENT_RELATIVE,
                   Value,
                   TimeStamp );
}

//==============================================================================

static
void CaptureGamepad( s32 Slot, input_event_buffer& Events, u32 TimeStamp )
{
    gamepad_slot& Pad = s_Input.Gamepad[Slot];
    input_gamepad const Previous = Pad.State;
    input_gamepad Current;
    x_memset( &Current, 0, sizeof(Current) );

    xbool const Connected = Pad.pGamepad && SDL_GamepadConnected( Pad.pGamepad );
    if( Connected )
    {
        static SDL_GamepadButton const DigitalButtons[XBOX_DIGITAL_COUNT] =
        {
            SDL_GAMEPAD_BUTTON_START,
            SDL_GAMEPAD_BUTTON_BACK,
            SDL_GAMEPAD_BUTTON_DPAD_LEFT,
            SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
            SDL_GAMEPAD_BUTTON_DPAD_UP,
            SDL_GAMEPAD_BUTTON_DPAD_DOWN,
            SDL_GAMEPAD_BUTTON_LEFT_STICK,
            SDL_GAMEPAD_BUTTON_RIGHT_STICK
        };

        static SDL_GamepadButton const AnalogButtons[XBOX_ANALOG_BTN_COUNT - 2] =
        {
            SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,
            SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,
            SDL_GAMEPAD_BUTTON_SOUTH,
            SDL_GAMEPAD_BUTTON_EAST,
            SDL_GAMEPAD_BUTTON_WEST,
            SDL_GAMEPAD_BUTTON_NORTH
        };

        for( s32 i = 0; i < XBOX_DIGITAL_COUNT; i++ )
        {
            Current.Digital[i] = SDL_GetGamepadButton( Pad.pGamepad, DigitalButtons[i] )
            ? DIGITAL_ON : 0;
        }

        for( s32 i = 0; i < (XBOX_ANALOG_BTN_COUNT - 2); i++ )
        {
            Current.AnalogBtn[i] = SDL_GetGamepadButton( Pad.pGamepad, AnalogButtons[i] )
            ? DIGITAL_ON : 0;
        }

        Sint16 const LeftTrigger  = SDL_GetGamepadAxis( Pad.pGamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER );
        Sint16 const RightTrigger = SDL_GetGamepadAxis( Pad.pGamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER );
        Current.Trigger[0] = (LeftTrigger  > 0) ? ((f32)LeftTrigger  / 32767.0f) : 0.0f;
        Current.Trigger[1] = (RightTrigger > 0) ? ((f32)RightTrigger / 32767.0f) : 0.0f;
        Current.AnalogBtn[XBOX_ANALOG_BTN_COUNT - 2] = (Current.Trigger[0] > TRIGGER_THRESHOLD) ? DIGITAL_ON : 0;
        Current.AnalogBtn[XBOX_ANALOG_BTN_COUNT - 1] = (Current.Trigger[1] > TRIGGER_THRESHOLD) ? DIGITAL_ON : 0;

        Current.Stick[0] =  NormalizeStickAxis( SDL_GetGamepadAxis( Pad.pGamepad, SDL_GAMEPAD_AXIS_LEFTX  ) );
        Current.Stick[1] = -NormalizeStickAxis( SDL_GetGamepadAxis( Pad.pGamepad, SDL_GAMEPAD_AXIS_LEFTY  ) );
        Current.Stick[2] =  NormalizeStickAxis( SDL_GetGamepadAxis( Pad.pGamepad, SDL_GAMEPAD_AXIS_RIGHTX ) );
        Current.Stick[3] = -NormalizeStickAxis( SDL_GetGamepadAxis( Pad.pGamepad, SDL_GAMEPAD_AXIS_RIGHTY ) );
        ApplyStickDeadZone( Current.Stick[0], Current.Stick[1], LEFT_STICK_DEAD_ZONE  );
        ApplyStickDeadZone( Current.Stick[2], Current.Stick[3], RIGHT_STICK_DEAD_ZONE );
    }

    Pad.State = Current;

    static input_gadget const DigitalGadgets[XBOX_DIGITAL_COUNT] =
    {
        INPUT_XBOX_BTN_START,
        INPUT_XBOX_BTN_BACK,
        INPUT_XBOX_BTN_LEFT,
        INPUT_XBOX_BTN_RIGHT,
        INPUT_XBOX_BTN_UP,
        INPUT_XBOX_BTN_DOWN,
        INPUT_XBOX_BTN_L_STICK,
        INPUT_XBOX_BTN_R_STICK
    };

    for( s32 i = 0; i < XBOX_DIGITAL_COUNT; i++ )
    {
        xbool const WasDown = (Previous.Digital[i] & DIGITAL_ON) != 0;
        xbool const IsDown  = (Current.Digital[i]  & DIGITAL_ON) != 0;
        if( WasDown != IsDown )
        {
            Events.Append( DigitalGadgets[i],
                           Slot,
                           IsDown ? INPUT_EVENT_PRESSED : INPUT_EVENT_RELEASED,
                           IsDown ? 1.0f : 0.0f,
                           TimeStamp );
        }
    }

    static input_gadget const AnalogGadgets[XBOX_ANALOG_BTN_COUNT] =
    {
        INPUT_XBOX_BTN_WHITE,
        INPUT_XBOX_BTN_BLACK,
        INPUT_XBOX_BTN_A,
        INPUT_XBOX_BTN_B,
        INPUT_XBOX_BTN_X,
        INPUT_XBOX_BTN_Y,
        INPUT_XBOX_L_TRIGGER,
        INPUT_XBOX_R_TRIGGER
    };

    for( s32 i = 0; i < XBOX_ANALOG_BTN_COUNT; i++ )
    {
        xbool const WasDown = (Previous.AnalogBtn[i] & DIGITAL_ON) != 0;
        xbool const IsDown  = (Current.AnalogBtn[i]  & DIGITAL_ON) != 0;
        if( WasDown != IsDown )
        {
            f32 const Value = (i >= (XBOX_ANALOG_BTN_COUNT - 2))
            ? Current.Trigger[i - (XBOX_ANALOG_BTN_COUNT - 2)]
            : (IsDown ? 1.0f : 0.0f);
            Events.Append( AnalogGadgets[i],
                           Slot,
                           IsDown ? INPUT_EVENT_PRESSED : INPUT_EVENT_RELEASED,
                           Value,
                           TimeStamp );
        }
    }

    static input_gadget const TriggerGadgets[2] =
    {
        INPUT_XBOX_L_TRIGGER,
        INPUT_XBOX_R_TRIGGER
    };

    for( s32 i = 0; i < 2; i++ )
    {
        if( Current.Trigger[i] != Previous.Trigger[i] )
        {
            Events.Append( TriggerGadgets[i],
                           Slot,
                           INPUT_EVENT_ABSOLUTE,
                           Current.Trigger[i],
                           TimeStamp );
        }
    }

    static input_gadget const StickGadgets[XBOX_STICK_COUNT] =
    {
        INPUT_XBOX_STICK_LEFT_X,
        INPUT_XBOX_STICK_LEFT_Y,
        INPUT_XBOX_STICK_RIGHT_X,
        INPUT_XBOX_STICK_RIGHT_Y
    };

    for( s32 i = 0; i < XBOX_STICK_COUNT; i++ )
    {
        if( Current.Stick[i] != Previous.Stick[i] )
        {
            Events.Append( StickGadgets[i],
                           Slot,
                           INPUT_EVENT_ABSOLUTE,
                           Current.Stick[i],
                           TimeStamp );
        }
    }

    if( !Connected && Pad.pGamepad )
    {
        CloseGamepad( Slot, FALSE );
    }
    else if( Pad.PendingDisconnect )
    {
        Pad.PendingDisconnect = FALSE;
    }
}

//==============================================================================

static
void Rumble( s32 DeviceID, f32 Intensity, f32 Duration )
{
    if( (DeviceID < 0) || (DeviceID >= MAX_GAMEPADS) ||
        s_Input.SuppressRumble || !s_Input.RumbleEnabled[DeviceID] ||
        !s_Input.Gamepad[DeviceID].pGamepad ||
        !SDL_GamepadConnected( s_Input.Gamepad[DeviceID].pGamepad ) )
    {
        return;
    }

    f32 const ClampedIntensity = (Intensity < 0.0f) ? 0.0f
    : (Intensity > 1.0f) ? 1.0f
    : Intensity;
    f32 const ClampedDuration  = (Duration < 0.0f) ? 0.0f : Duration;
    Uint16 const Strength      = (Uint16)(ClampedIntensity * 65535.0f);
    Uint32 const DurationMs    = (Uint32)(ClampedDuration * 1000.0f);

    SDL_RumbleGamepad( s_Input.Gamepad[DeviceID].pGamepad,
                       Strength,
                       Strength,
                       DurationMs );
}

//==============================================================================
// CURSOR
//==============================================================================

static
xbool SetRelativeMouseMode( xbool Enabled )
{
    SDL_Window* const pWindow = sdleng_WindowGetSDLWindow();
    if( !pWindow )
    {
        return FALSE;
    }

    xbool const IsEnabled = SDL_GetWindowRelativeMouseMode( pWindow ) ? TRUE : FALSE;
    if( IsEnabled == Enabled )
    {
        return TRUE;
    }

    if( !SDL_SetWindowRelativeMouseMode( pWindow, Enabled != FALSE ) )
    {
        x_DebugMsg( "SDL input: relative mouse mode failed: %s\n", SDL_GetError() );
        return FALSE;
    }

    return TRUE;
}

//==============================================================================
// EVENTS
//==============================================================================

static
void HandleKeyboardEvent( SDL_Event const& Event )
{
    if( !sdleng_WindowIsActive() )
    {
        return;
    }

    SDL_Scancode const ScanCode = Event.key.scancode;
    if( (ScanCode < 0) || (ScanCode >= SDL_SCANCODE_COUNT) )
    {
        return;
    }

    s32 const DIKCode = s_SDLToDIK[ScanCode];
    if( (DIKCode < 0) || (DIKCode >= DIGITAL_COUNT_KBD) )
    {
        return;
    }

    AppendDigitalEvent( s_PendingEvents,
                        static_cast<input_gadget>( INPUT_KBD__BEGIN + DIKCode ),
                        0,
                        s_Input.Keyboard.Digital[DIKCode],
                        Event.type == SDL_EVENT_KEY_DOWN,
                        1.0f,
                        (u32)SDL_GetTicks() );
}

//==============================================================================

static
void HandleMouseButtonEvent( SDL_Event const& Event )
{
    if( !sdleng_WindowIsActive() )
    {
        return;
    }

    s32 const ButtonIndex = GetMouseButtonIndexFromSDL( Event.button.button );
    if( ButtonIndex < 0 )
    {
        return;
    }

    AppendDigitalEvent( s_PendingEvents,
                        GetMouseButtonGadget( ButtonIndex ),
                        0,
                        s_Input.Mouse.Digital[ButtonIndex],
                        Event.type == SDL_EVENT_MOUSE_BUTTON_DOWN,
                        1.0f,
                        (u32)SDL_GetTicks() );
}

//==============================================================================

static
void HandleWindowEvent( void* pContext, SDL_Event const& Event, SDL_WindowID WindowID )
{
    (void)pContext;

    if( !IsEventForWindow( Event, WindowID ) )
    {
        return;
    }

    switch( Event.type )
    {
        case SDL_EVENT_WINDOW_FOCUS_LOST:
        case SDL_EVENT_WINDOW_MINIMIZED:
        {
            // Retain releases even if focus returns before input is consumed.
            u32 const TimeStamp = (u32)SDL_GetTicks();

            for( s32 i = 0; i < DIGITAL_COUNT_KBD; ++i )
            {
                AppendDigitalEvent( s_PendingEvents,
                                    static_cast<input_gadget>( INPUT_KBD__BEGIN + i ),
                                    0, s_Input.Keyboard.Digital[i], FALSE, 0.0f, TimeStamp );
            }

            for( s32 i = 0; i < DIGITAL_COUNT_MOUSE; ++i )
            {
                AppendDigitalEvent( s_PendingEvents, GetMouseButtonGadget( i ),
                                    0, s_Input.Mouse.Digital[i], FALSE, 0.0f, TimeStamp );
            }

            break;
        }

        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
        {
            HandleKeyboardEvent( Event );
            break;
        }

        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
        {
            HandleMouseButtonEvent( Event );
            break;
        }

        case SDL_EVENT_MOUSE_WHEEL:
        {
            if( sdleng_WindowIsActive() )
            {
                f32 Value = Event.wheel.y;
                if( Value == 0.0f )
                {
                    Value = (f32)Event.wheel.integer_y;
                }
                if( Event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED )
                {
                    Value = -Value;
                }
                s_Input.PendingMouseWheel += Value * 120.0f;
            }
        }
        break;

        case SDL_EVENT_GAMEPAD_ADDED:
        {
            OpenGamepad( Event.gdevice.which );
            break;
        }

        case SDL_EVENT_GAMEPAD_REMOVED:
        {
            s32 const Slot = FindGamepadSlot( Event.gdevice.which );
            if( Slot >= 0 )
            {
                CloseGamepad( Slot, TRUE );
            }
        }
        break;

        default:
        {
            break;
        }
    }
}

#ifdef A51_LOCAL_COOP_QA

//==============================================================================
// QA BOT  (TEST ONLY - compiled in with the CMake option A51_LOCAL_COOP_QA)
//
// Two virtual SDL gamepads driven by a timed script.  See qa_bot.hpp.
//
// Script file (path in the environment variable A51_QA_SCRIPT), one line each:
//     <start_ms> <end_ms> <pad 0|1> <control> [value]
//     quit <ms>
// Times are game time since the game called qa_bot_Arm().  Controls:
//     lx ly rx ry   sticks, value -100..100 (+100 on ly/ry = forward/up)
//     lt rt         triggers, value 0..100
//     a b x y back guide start ls rs lb rb up down left right   buttons (value ignored)
// '#' starts a comment.
//
// Other environment variables:  A51_QA_MAX_SECONDS (wall-clock limit, default 1500)
//                               A51_QA_MENU_PULSE=0 (do not press A on pad 0 in the menus)
//==============================================================================

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

namespace qa
{
    static s32 const MAX_PADS   = 2;
    static s32 const MAX_EVENTS = 2048;

    // Axes first, then buttons, in the order SDL's virtual gamepad mapping uses:
    // axis index = control, button index = control - C_A.
    enum
    {
        C_LX, C_LY, C_RX, C_RY, C_LT, C_RT,
        C_A, C_B, C_X, C_Y, C_BACK, C_GUIDE, C_START, C_LS, C_RS, C_LB, C_RB, C_UP, C_DOWN, C_LEFT, C_RIGHT,
        C_COUNT
    };

    static char const* const ControlNames[C_COUNT] =
    {
        "lx", "ly", "rx", "ry", "lt", "rt",
        "a", "b", "x", "y", "back", "guide", "start", "ls", "rs", "lb", "rb", "up", "down", "left", "right"
    };

    struct script_event
    {
        s32 StartMs;
        s32 EndMs;
        s32 Pad;
        s32 Control;
        s32 Value;
    };

    static script_event  s_Events[MAX_EVENTS];
    static s32           s_EventCount = 0;
    static s32           s_QuitMs     = 0;
    static xbool         s_Enabled    = FALSE;
    static xbool         s_Armed      = FALSE;
    static xbool         s_Finished   = FALSE;
    static double        s_ScriptSec  = 0.0;
    static Uint64        s_MaxWallMs  = 1500000;
    static xbool         s_MenuPulses = TRUE;
    static s32           s_PulseFrames = 0;          // frames the menu A press still has to be held
    static Uint64        s_NextPulseMs = 5000;       // wall-clock time of the next menu A press
    static SDL_Joystick* s_pPad[MAX_PADS];
    static Sint16        s_Sent[MAX_PADS][C_COUNT];
    static xbool         s_SetFailed  = FALSE;
    static FILE*         s_pLog       = NULL;

    //--------------------------------------------------------------------------

    static
    Sint16 RestValue( s32 Control )
    {
        return (Sint16)(((Control == C_LT) || (Control == C_RT)) ? SDL_JOYSTICK_AXIS_MIN : 0);
    }

    //--------------------------------------------------------------------------

    static
    Sint16 ToSDL( s32 Control, s32 Value )
    {
        if( Control < C_LT )
        {
            // Sticks.  +100 on Y means forward/up, which SDL reports as negative.
            s32 const V   = (Value > 100) ? 100 : ((Value < -100) ? -100 : Value);
            s32 const Raw = (V * 32767) / 100;
            return (Sint16)(((Control == C_LY) || (Control == C_RY)) ? -Raw : Raw);
        }

        if( Control < C_A )
        {
            // Triggers: 0 (released, SDL minimum) .. 100 (fully pressed).
            s32 const V = (Value > 100) ? 100 : ((Value < 0) ? 0 : Value);
            return (Sint16)(-32768 + ((V * 65535) / 100));
        }

        return (Sint16)((Value != 0) ? 1 : 0);
    }

    //--------------------------------------------------------------------------

    static
    void LoadScript( char const* pPath )
    {
        FILE* pFile = fopen( pPath, "r" );
        if( !pFile )
        {
            qa_bot_Log( "ERROR cannot open script '%s'", pPath );
            return;
        }

        char Line[256];
        s32  LineNo = 0;
        s32  MaxEnd = 0;
        s_EventCount = 0;
        s_QuitMs     = 0;

        while( fgets( Line, sizeof(Line), pFile ) )
        {
            LineNo++;

            char* pHash = strchr( Line, '#' );
            if( pHash )
            {
                *pHash = 0;
            }

            int Quit = 0;
            if( sscanf( Line, " quit %d", &Quit ) == 1 )
            {
                s_QuitMs = Quit;
                continue;
            }

            int  Start = 0, End = 0, Pad = 0, Value = 100;
            char Name[32];
            int  Count = sscanf( Line, " %d %d %d %31s %d", &Start, &End, &Pad, Name, &Value );
            if( Count < 4 )
            {
                continue;
            }

            s32 Control = -1;
            for( s32 i = 0; i < C_COUNT; i++ )
            {
                if( strcmp( Name, ControlNames[i] ) == 0 )
                {
                    Control = i;
                    break;
                }
            }

            if( (Control < 0) || (Pad < 0) || (Pad >= MAX_PADS) || (End <= Start) || (s_EventCount >= MAX_EVENTS) )
            {
                qa_bot_Log( "ERROR script line %d rejected", (int)LineNo );
                continue;
            }

            script_event& E = s_Events[s_EventCount++];
            E.StartMs = Start;
            E.EndMs   = End;
            E.Pad     = Pad;
            E.Control = Control;
            E.Value   = Value;
            if( End > MaxEnd )
            {
                MaxEnd = End;
            }
        }

        fclose( pFile );

        if( s_QuitMs < MaxEnd )
        {
            s_QuitMs = MaxEnd + 1000;
        }

        qa_bot_Log( "script '%s' loaded: %d events, quit at %d ms", pPath, (int)s_EventCount, (int)s_QuitMs );
    }

    //--------------------------------------------------------------------------

    static
    void AttachPads( void )
    {
        // Without this SDL drops joystick input while the window has no focus,
        // which is the normal case on a headless test machine.
        SDL_SetHint( SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1" );

        for( s32 p = 0; p < MAX_PADS; p++ )
        {
            SDL_VirtualJoystickDesc Desc;
            SDL_INIT_INTERFACE( &Desc );
            Desc.type     = SDL_JOYSTICK_TYPE_GAMEPAD;
            Desc.naxes    = 6;
            Desc.nbuttons = 15;
            Desc.name     = (p == 0) ? "QA Pad 0" : "QA Pad 1";

            SDL_JoystickID const Id = SDL_AttachVirtualJoystick( &Desc );
            if( Id == 0 )
            {
                qa_bot_Log( "ERROR attach pad %d failed: %s", (int)p, SDL_GetError() );
                continue;
            }

            s_pPad[p] = SDL_OpenJoystick( Id );
            if( !s_pPad[p] )
            {
                qa_bot_Log( "ERROR open pad %d failed: %s", (int)p, SDL_GetError() );
                continue;
            }

            for( s32 c = 0; c < C_COUNT; c++ )
            {
                s_Sent[p][c] = RestValue( c );
            }

            qa_bot_Log( "pad %d attached: instance id %u, is_gamepad=%d", (int)p, (unsigned)Id, (int)SDL_IsGamepad( Id ) );
        }
    }

    //--------------------------------------------------------------------------
    // Sends every control that differs from what was sent last.  NowMs < 0 means
    // "not armed yet": only the menu pulses on pad 0 are active.

    static
    void Apply( s32 NowMs )
    {
        Sint16 Want[MAX_PADS][C_COUNT];

        for( s32 p = 0; p < MAX_PADS; p++ )
        {
            for( s32 c = 0; c < C_COUNT; c++ )
            {
                Want[p][c] = RestValue( c );
            }
        }

        if( NowMs < 0 )
        {
            // Press A on pad 0 every 2 s of wall-clock time (first press after 5 s),
            // held for 3 frames so that a slow software renderer cannot miss it.
            // Gets through movies and the level-select list when the game starts
            // by itself.
            if( s_PulseFrames > 0 )
            {
                s_PulseFrames--;
                Want[0][C_A] = 1;
            }
            else if( s_MenuPulses )
            {
                Uint64 const Wall = SDL_GetTicks();
                if( Wall >= s_NextPulseMs )
                {
                    s_NextPulseMs = Wall + 2000;
                    s_PulseFrames = 2;
                    Want[0][C_A]  = 1;
                }
            }
        }
        else
        {
            for( s32 i = 0; i < s_EventCount; i++ )
            {
                script_event const& E = s_Events[i];
                if( (NowMs >= E.StartMs) && (NowMs < E.EndMs) )
                {
                    Want[E.Pad][E.Control] = ToSDL( E.Control, E.Value );
                }
            }
        }

        for( s32 p = 0; p < MAX_PADS; p++ )
        {
            if( !s_pPad[p] )
            {
                continue;
            }

            for( s32 c = 0; c < C_COUNT; c++ )
            {
                if( Want[p][c] == s_Sent[p][c] )
                {
                    continue;
                }

                bool const Ok = (c < C_A)
                    ? SDL_SetJoystickVirtualAxis( s_pPad[p], c, Want[p][c] )
                    : SDL_SetJoystickVirtualButton( s_pPad[p], c - C_A, Want[p][c] != 0 );

                if( !Ok && !s_SetFailed )
                {
                    s_SetFailed = TRUE;
                    qa_bot_Log( "ERROR setting pad %d control %s failed: %s", (int)p, ControlNames[c], SDL_GetError() );
                }

                s_Sent[p][c] = Want[p][c];

                // Menu pulses are too chatty to log; script changes are the evidence.
                if( NowMs >= 0 )
                {
                    qa_bot_Log( "pad %d %s = %d", (int)p, ControlNames[c], (int)Want[p][c] );
                }
            }
        }
    }

    //--------------------------------------------------------------------------
    // The test build is an OptDebug build, which has assertions; the build that
    // people play has none.  A failing assertion would stop the test with a
    // crash that the played build never sees, so it is logged and skipped.

    static
    xbool AssertHandler( char const* pFile, s32 Line, char const* pExpr, char const* pMsg )
    {
        static s32 s_Count = 0;

        s_Count++;
        if( s_Count <= 100 )
        {
            qa_bot_Log( "ASSERT #%d %s:%d expr=[%s] msg=[%s]", (int)s_Count,
                        pFile ? pFile : "?", (int)Line, pExpr ? pExpr : "", pMsg ? pMsg : "" );
        }

        return FALSE;
    }

    //--------------------------------------------------------------------------

    static
    void Init( void )
    {
        char const* pScript = SDL_getenv( "A51_QA_SCRIPT" );
        if( !pScript || !pScript[0] )
        {
            return;
        }

        char const* pMax = SDL_getenv( "A51_QA_MAX_SECONDS" );
        if( pMax && (atoi( pMax ) > 0) )
        {
            s_MaxWallMs = (Uint64)atoi( pMax ) * 1000;
        }

        char const* pPulse = SDL_getenv( "A51_QA_MENU_PULSE" );
        if( pPulse && (pPulse[0] == '0') )
        {
            s_MenuPulses = FALSE;
        }

        qa_bot_Log( "QA bot starting (wall limit %u s, menu pulses %d)", (unsigned)(s_MaxWallMs / 1000), (int)s_MenuPulses );
        x_SetRTFHandler( AssertHandler );
        LoadScript( pScript );
        AttachPads();
        s_Enabled = (s_EventCount > 0);
    }

    //--------------------------------------------------------------------------

    static
    void Shutdown( void )
    {
        if( s_Enabled )
        {
            qa_bot_Log( "QA bot shutting down (armed=%d finished=%d)", (int)s_Armed, (int)s_Finished );
        }

        for( s32 p = 0; p < MAX_PADS; p++ )
        {
            s_pPad[p] = NULL;
        }
        s_Enabled = FALSE;
    }

    //--------------------------------------------------------------------------
    // Called once per input capture, before SDL's events are pumped.

    static
    void Update( void )
    {
        if( !s_Enabled || s_Finished )
        {
            return;
        }

        // Frame rate, every 5 seconds: a software renderer can be slow, and the script
        // runs on game time, so this says how long a run takes.
        {
            static Uint64 s_PerfLast   = 0;
            static s32    s_PerfFrames = 0;

            Uint64 const Now = SDL_GetTicks();
            s_PerfFrames++;
            if( (Now - s_PerfLast) >= 5000 )
            {
                if( s_PerfLast != 0 )
                {
                    qa_bot_Log( "perf frames=%d in %u ms", (int)s_PerfFrames, (unsigned)(Now - s_PerfLast) );
                }
                s_PerfLast   = Now;
                s_PerfFrames = 0;
            }
        }

        if( SDL_GetTicks() > s_MaxWallMs )
        {
            qa_bot_Log( "wall-clock limit reached (armed=%d), asking the game to exit", (int)s_Armed );
            s_Finished        = TRUE;
            s_Input.ExitApp   = TRUE;
            return;
        }

        if( !s_Armed )
        {
            Apply( -1 );
            return;
        }

        s32 const NowMs = (s32)(s_ScriptSec * 1000.0);
        if( NowMs >= s_QuitMs )
        {
            qa_bot_Log( "script finished, asking the game to exit" );
            Apply( 0x7FFFFFFF );
            s_Finished      = TRUE;
            s_Input.ExitApp = TRUE;
            return;
        }

        Apply( NowMs );
    }
}

void qa_bot_Log( char const* pFormat, ... )
{
    if( !qa::s_pLog )
    {
        qa::s_pLog = fopen( "a51_qa_bot.log", "a" );
        if( !qa::s_pLog )
        {
            return;
        }
    }

    va_list Args;
    va_start( Args, pFormat );
    fprintf( qa::s_pLog, "wall=%u qt=%d ", (unsigned)SDL_GetTicks(), qa_bot_ScriptMs() );
    vfprintf( qa::s_pLog, pFormat, Args );
    fputc( '\n', qa::s_pLog );
    fflush( qa::s_pLog );
    va_end( Args );
}

void qa_bot_Arm( char const* pWhy )
{
    if( !qa::s_Enabled || qa::s_Armed )
    {
        return;
    }

    qa::s_Armed     = TRUE;
    qa::s_ScriptSec = 0.0;
    qa_bot_Log( "armed: %s", pWhy ? pWhy : "" );
}

void qa_bot_Tick( float DeltaSeconds )
{
    if( qa::s_Armed && !qa::s_Finished )
    {
        qa::s_ScriptSec += (double)DeltaSeconds;
    }
}

int qa_bot_ScriptMs( void )
{
    return qa::s_Armed ? (int)(qa::s_ScriptSec * 1000.0) : -1;
}

unsigned qa_bot_WallMs( void )
{
    return (unsigned)SDL_GetTicks();
}

#endif // A51_LOCAL_COOP_QA

//==============================================================================
// BACKEND
//==============================================================================

class sdl_input_backend : public input_backend
{
public:
    xbool Init( input_init_desc const& Desc ) override
    {
        (void)Desc;

        if( s_Input.Initialized )
        {
            return TRUE;
        }

        if( !sdleng_WindowGetSDLWindow() )
        {
            x_DebugMsg( "SDL input: no SDL window available\n" );
            return FALSE;
        }

        InitializeKeyMap();
        x_memset( &s_Input, 0, sizeof(s_Input) );
        s_PendingEvents.BeginCapture( (u32)SDL_GetTicks() );

        for( s32 i = 0; i < MAX_GAMEPADS; i++ )
        {
            s_Input.RumbleEnabled[i] = TRUE;
        }

        if( !(SDL_WasInit( SDL_INIT_GAMEPAD ) & SDL_INIT_GAMEPAD) )
        {
            if( !SDL_InitSubSystem( SDL_INIT_GAMEPAD ) )
            {
                x_DebugMsg( "SDL input: SDL_INIT_GAMEPAD failed: %s\n", SDL_GetError() );
                return FALSE;
            }
            s_Input.GamepadSubsystemOwned = TRUE;
        }

        sdleng_WindowSetEventCallback( &HandleWindowEvent, NULL );
#ifdef A51_LOCAL_COOP_QA
        qa::Init();
#endif
        OpenExistingGamepads();

        if( !SetRelativeMouseMode( TRUE ) )
        {
            Kill();
            return FALSE;
        }

        s_Input.Initialized = TRUE;
        return TRUE;
    }

    void Kill( void ) override
    {
#ifdef A51_LOCAL_COOP_QA
        qa::Shutdown();
#endif
        s_PendingEvents.BeginCapture( 0 );
        sdleng_WindowSetEventCallback( NULL, NULL );
        SetRelativeMouseMode( FALSE );
        ClearFeedback();

        for( s32 i = 0; i < MAX_GAMEPADS; i++ )
        {
            CloseGamepad( i, FALSE );
        }

        if( s_Input.GamepadSubsystemOwned )
        {
            SDL_QuitSubSystem( SDL_INIT_GAMEPAD );
        }

        s_Input.GamepadSubsystemOwned = FALSE;
        s_Input.ExitApp               = FALSE;
        s_Input.Initialized           = FALSE;
    }

    xbool CaptureFrameInput( input_event_buffer& Events ) override
    {
        u32 const StartTime = (u32)SDL_GetTicks();
        Events.BeginCapture( StartTime );
        ClearRelativeState();

#ifdef A51_LOCAL_COOP_QA
        qa::Update();
#endif

        if( !sdleng_WindowPumpMessages() )
        {
            s_Input.ExitApp = TRUE;
        }

        if( sdleng_WindowIsCloseRequested() )
        {
            s_Input.ExitApp = TRUE;
        }

        u32 const TimeStamp = (u32)SDL_GetTicks();

        for( s32 i = 0; i < s_PendingEvents.GetCount(); ++i )
        {
            input_event const& Event = s_PendingEvents.GetEvent( i );
            Events.Append( Event.GadgetID, Event.DeviceID, Event.Type, Event.Value, Event.TimeStamp );
        }

        if( s_PendingEvents.HasOverflowed() )
        {
            Events.MarkOverflow();
        }

        s_PendingEvents.BeginCapture( TimeStamp );

        CaptureMouseWheel( Events, TimeStamp );
        CaptureMouse( Events, TimeStamp );
        CaptureKeyboard( Events, TimeStamp );

        for( s32 i = 0; i < MAX_GAMEPADS; i++ )
        {
            CaptureGamepad( i, Events, TimeStamp );
        }

        if( s_Input.ExitApp )
        {
            Events.Append( INPUT_MSG_EXIT, 0, INPUT_EVENT_EXIT, 1.0f, TimeStamp );
        }

        Events.EndCapture( (u32)SDL_GetTicks() );
        return s_Input.ExitApp;
    }

    xbool IsGadgetDown( input_gadget GadgetID, s32 DeviceID ) const override
    {
        return GetRawValue( GadgetID, DeviceID, FALSE ) != 0.0f;
    }

    f32 GetGadgetValue( input_gadget GadgetID, s32 DeviceID ) const override
    {
        return GetRawValue( GadgetID, DeviceID, TRUE );
    }

    xbool IsGadgetPresent( input_gadget GadgetID, s32 DeviceID ) const override
    {
        if( (DeviceID < 0) || (DeviceID >= INPUT_MAX_DEVICES) )
        {
            return FALSE;
        }

        if( GadgetID == INPUT_MSG_EXIT )
        {
            return TRUE;
        }

        if( GadgetID > INPUT_KBD__BEGIN && GadgetID < INPUT_KBD__END )
        {
            return (DeviceID == 0);
        }

        if( GadgetID > INPUT_MOUSE__BEGIN && GadgetID < INPUT_MOUSE__END )
        {
            return (DeviceID == 0);
        }

        if( GadgetID > INPUT_XBOX__BEGIN && GadgetID < INPUT_XBOX__END )
        {
            return (DeviceID < MAX_GAMEPADS) &&
            s_Input.Gamepad[DeviceID].pGamepad &&
            SDL_GamepadConnected( s_Input.Gamepad[DeviceID].pGamepad );
        }

        return FALSE;
    }

    xbool IsDevicePresent( input_device Device, s32 DeviceID ) const override
    {
        if( (DeviceID < 0) || (DeviceID >= INPUT_MAX_DEVICES) )
        {
            return FALSE;
        }

        switch( Device )
        {
        case INPUT_DEVICE_KEYBOARD:
        case INPUT_DEVICE_MOUSE:
        {
            return DeviceID == 0;
        }

        case INPUT_DEVICE_GAMEPAD:
        {
            return s_Input.Gamepad[DeviceID].pGamepad &&
                   SDL_GamepadConnected( s_Input.Gamepad[DeviceID].pGamepad );
        }

        default:
        {
            return FALSE;
        }
        }
    }

    s32 GetPadCount( void ) const override
    {
        s32 Count = 0;
        for( s32 i = 0; i < MAX_GAMEPADS; i++ )
        {
            if( s_Input.Gamepad[i].pGamepad &&
                SDL_GamepadConnected( s_Input.Gamepad[i].pGamepad ) )
            {
                Count++;
            }
        }
        return Count;
    }

    void Feedback( f32 Duration, f32 Intensity, s32 DeviceID ) override
    {
        if( g_Input.GetCurrentInputDevice() == INPUT_DEVICE_GAMEPAD )
        {
            Rumble( DeviceID, Intensity, Duration );
        }
    }

    void Feedback( s32 Count, feedback_envelope* pEnvelope, s32 DeviceID ) override
    {
        if( Count > 0 && pEnvelope )
        {
            Feedback( pEnvelope[0].Duration, pEnvelope[0].Intensity, DeviceID );
        }
    }

    void EnableFeedback( xbool State, s32 DeviceID ) override
    {
        if( (DeviceID < 0) || (DeviceID >= MAX_GAMEPADS) )
        {
            return;
        }

        s_Input.RumbleEnabled[DeviceID] = State;
        if( !State )
        {
            StopGamepadRumble( DeviceID );
        }
    }

    void SuppressFeedback( xbool Suppress ) override
    {
        s_Input.SuppressRumble = Suppress;
        if( Suppress )
        {
            ClearFeedback();
        }
    }

    void ClearFeedback( void ) override
    {
        for( s32 i = 0; i < MAX_GAMEPADS; i++ )
        {
            StopGamepadRumble( i );
        }
    }

private:
    static f32 GetRawValue( input_gadget GadgetID, s32 DeviceID, xbool ReadAnalogTrigger )
    {
        if( (DeviceID < 0) || (DeviceID >= INPUT_MAX_DEVICES) )
        {
            return 0.0f;
        }

        if( GadgetID > INPUT_KBD__BEGIN && GadgetID < INPUT_KBD__END )
        {
            if( DeviceID != 0 )
            {
                return 0.0f;
            }

            s32 const Index = GadgetID - INPUT_KBD__BEGIN;
            return (Index >= 0) && (Index < DIGITAL_COUNT_KBD) &&
            (s_Input.Keyboard.Digital[Index] & DIGITAL_ON)
            ? 1.0f : 0.0f;
        }

        if( GadgetID > INPUT_MOUSE__BEGIN && GadgetID < INPUT_MOUSE__END )
        {
            if( DeviceID != 0 )
            {
                return 0.0f;
            }

            s32 const ButtonIndex = GetMouseButtonIndex( GadgetID );
            if( ButtonIndex >= 0 )
            {
                return (s_Input.Mouse.Digital[ButtonIndex] & DIGITAL_ON) ? 1.0f : 0.0f;
            }

            switch( GadgetID )
            {
                case INPUT_MOUSE_X_REL:     return s_Input.Mouse.Relative[0]; break;
                case INPUT_MOUSE_Y_REL:     return s_Input.Mouse.Relative[1]; break;
                case INPUT_MOUSE_WHEEL_REL: return s_Input.Mouse.Relative[2]; break;
                default:                    break;
            }
        }

        if( GadgetID > INPUT_XBOX__BEGIN && GadgetID < INPUT_XBOX__END )
        {
            if( DeviceID >= MAX_GAMEPADS )
            {
                return 0.0f;
            }

            gamepad_slot const& Pad = s_Input.Gamepad[DeviceID];
            if( !Pad.pGamepad || !SDL_GamepadConnected( Pad.pGamepad ) )
            {
                return 0.0f;
            }

            if( GadgetID > INPUT_XBOX__DIGITAL_BUTTONS_BEGIN &&
                GadgetID < INPUT_XBOX__DIGITAL_BUTTONS_END )
            {
                s32 const Index = GadgetID - INPUT_XBOX__DIGITAL_BUTTONS_BEGIN - 1;
                return (Pad.State.Digital[Index] & DIGITAL_ON) ? 1.0f : 0.0f;
            }

            if( GadgetID > INPUT_XBOX__ANALOG_BUTTONS_BEGIN &&
                GadgetID < INPUT_XBOX__ANALOG_BUTTONS_END )
            {
                s32 const Index = GadgetID - INPUT_XBOX__ANALOG_BUTTONS_BEGIN - 1;
                if( ReadAnalogTrigger && (Index >= (XBOX_ANALOG_BTN_COUNT - 2)) )
                {
                    return Pad.State.Trigger[Index - (XBOX_ANALOG_BTN_COUNT - 2)];
                }
                return (Pad.State.AnalogBtn[Index] & DIGITAL_ON) ? 1.0f : 0.0f;
            }

            if( GadgetID > INPUT_XBOX__STICKS_BEGIN &&
                GadgetID < INPUT_XBOX__STICKS_END )
            {
                s32 const Index = GadgetID - INPUT_XBOX__STICKS_BEGIN - 1;
                return Pad.State.Stick[Index];
            }
        }

        if( GadgetID == INPUT_MSG_EXIT )
        {
            return s_Input.ExitApp ? 1.0f : 0.0f;
        }

        return 0.0f;
    }
};

//==============================================================================

input_backend* input_CreateDefaultBackend( void )
{
    return new sdl_input_backend;
}

//==============================================================================

void input_DestroyDefaultBackend( input_backend* pBackend )
{
    delete pBackend;
}
