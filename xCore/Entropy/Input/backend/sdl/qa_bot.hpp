//==============================================================================
//
//  qa_bot.hpp
//
//  TEST ONLY.  Hooks for the local co-op QA bot (see tools/coop-qa).
//
//  The bot is compiled in only with the CMake option A51_LOCAL_COOP_QA (which
//  defines the macro of the same name).  Even then it does nothing unless the
//  environment variable A51_QA_SCRIPT names a script file.  With the option off,
//  every function below is an empty inline function and the game is unchanged.
//
//  What the bot does: it attaches two virtual SDL gamepads, presses A on the
//  first one now and then to get through the level-select menu, and once the
//  game calls qa_bot_Arm() it plays back the script (stick and button states
//  against game time) on the two pads.  When the script is over it asks the
//  game to exit.  The implementation is in input_sdl.cpp.
//
//==============================================================================

#ifndef QA_BOT_HPP
#define QA_BOT_HPP

#ifdef A51_LOCAL_COOP_QA

void qa_bot_Arm      ( const char* pWhy );          // the level has started: start the script clock
void qa_bot_Disarm   ( const char* pWhy );          // back to the menus: pause the script clock, press A again
void qa_bot_Tick     ( float DeltaSeconds );        // once per game frame, advances the script clock
int  qa_bot_ScriptMs ( void );                      // script time in ms since arming, -1 before that
unsigned qa_bot_WallMs ( void );                    // wall-clock ms since the game started
void qa_bot_Log      ( const char* pFormat, ... );  // appends a line to a51_qa_bot.log

// Script lines "cmd <ms> <name>" call this hook once, at that script time, with the name.
typedef void (*qa_bot_command_fn)( const char* pName );
void qa_bot_SetCommandHook( qa_bot_command_fn pHook );

#else

inline void qa_bot_Arm      ( const char* ) {}
inline void qa_bot_Disarm   ( const char* ) {}
inline void qa_bot_Tick     ( float )       {}
inline int  qa_bot_ScriptMs ( void )        { return -1; }
inline unsigned qa_bot_WallMs ( void )      { return 0; }
typedef void (*qa_bot_command_fn)( const char* pName );
inline void qa_bot_SetCommandHook( qa_bot_command_fn ) {}

#endif

#endif
