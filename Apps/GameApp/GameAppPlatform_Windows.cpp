//==============================================================================
//
//  GameAppPlatform_Windows.cpp
//
//==============================================================================

//==============================================================================
//  INCLUDES
//==============================================================================

#include "GameAppPlatform.hpp"
#include "A51Resources.hpp"

#include <windows.h>
#include <cstdlib>
#include <cstring>

#include "Entropy/SDLEngine/sdleng_window.hpp"

//==============================================================================
//  HELPER FUNCTIONS
//==============================================================================

static 
xbool CopyPath( char* pBuffer, s32 BufferSize, const char* pPath )
{
    if( (pBuffer == NULL) || (BufferSize <= 0) || (pPath == NULL) )
        return( FALSE );

    size_t Length = std::strlen( pPath );
    if( Length >= (size_t)BufferSize )
    {
        pBuffer[0] = '\0';
        return( FALSE );
    }

    std::memcpy( pBuffer, pPath, Length + 1 );
    return( TRUE );
}

//==============================================================================
//  IMPLEMENTATION
//==============================================================================

xbool GameAppGetExecutableDirectory( char* pBuffer, s32 BufferSize )
{
    if( (pBuffer == NULL) || (BufferSize <= 0) )
        return( FALSE );

    DWORD Length = GetModuleFileNameA( NULL, pBuffer, (DWORD)BufferSize );
    if( (Length == 0) || (Length >= (DWORD)BufferSize) )
    {
        pBuffer[0] = '\0';
        return( FALSE );
    }

    pBuffer[Length] = '\0';
    char* pLastSlash = std::strrchr( pBuffer, '\\' );
    char* pLastForwardSlash = std::strrchr( pBuffer, '/' );
    if( pLastForwardSlash && (!pLastSlash || (pLastForwardSlash > pLastSlash)) )
        pLastSlash = pLastForwardSlash;

    if( pLastSlash )
        *pLastSlash = '\0';

    return( pBuffer[0] != '\0' );
}

//=============================================================================

xbool GameAppGetDataDirectory( char* pBuffer, s32 BufferSize )
{
    const char* pDataRoot = std::getenv( "A51_DATA_ROOT" );
    if( pDataRoot && pDataRoot[0] )
        return( CopyPath( pBuffer, BufferSize, pDataRoot ) );

    return( GameAppGetExecutableDirectory( pBuffer, BufferSize ) );
}

//=============================================================================

xbool GameAppGetSaveDirectory( char* pBuffer, s32 BufferSize )
{
    return( GameAppGetDataDirectory( pBuffer, BufferSize ) );
}

//=============================================================================

void GameAppSetWindowIcon( void )
{
    HWND const hWindow = sdleng_WindowGetHandle();
    if( !hWindow )
    {
        return;
    }

    HINSTANCE const hInstance = GetModuleHandleW( NULL );
    HICON const hIcon = LoadIconW( hInstance, MAKEINTRESOURCEW( A51_ICON_RESOURCE_ID ) );
    if( !hIcon )
    {
        return;
    }

    SendMessageW( hWindow, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>( hIcon ) );
    SendMessageW( hWindow, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>( hIcon ) );
}

//==============================================================================
//  Local co-op test builds: crash report.
//
//  If the game crashes, a51_crash.txt (exception, faulting address and call
//  stack as module + offset) and a51_crash.dmp (a small minidump) are written
//  to the working directory, so a crash on a player's PC can be traced back
//  with the A51.pdb published next to the build.  Nothing is sent anywhere.
//==============================================================================

#ifdef A51_LOCAL_COOP

#include <stdio.h>

namespace
{
    void CoopCrash_WriteAddress( FILE* pFile, int Index, DWORD64 Address )
    {
        HMODULE hModule = NULL;
        char    Name[MAX_PATH] = "?";

        if( GetModuleHandleExA( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                (LPCSTR)(ULONG_PTR)Address, &hModule ) && hModule )
        {
            char Path[MAX_PATH];
            if( GetModuleFileNameA( hModule, Path, MAX_PATH ) )
            {
                const char* pSlash = strrchr( Path, '\\' );
                strncpy( Name, pSlash ? pSlash + 1 : Path, MAX_PATH - 1 );
                Name[MAX_PATH - 1] = 0;
            }
            fprintf( pFile, "#%02d %s+0x%llx\n", Index, Name,
                     (unsigned long long)(Address - (DWORD64)(ULONG_PTR)hModule) );
        }
        else
        {
            fprintf( pFile, "#%02d 0x%llx\n", Index, (unsigned long long)Address );
        }
    }

    typedef BOOL (WINAPI *mini_dump_fn)( HANDLE, DWORD, HANDLE, int, void*, void*, void* );

    // Same layout as MINIDUMP_EXCEPTION_INFORMATION, which dbghelp.h packs to 4 bytes.
#pragma pack( push, 4 )
    struct coop_dump_exception_info
    {
        DWORD               ThreadId;
        EXCEPTION_POINTERS* ExceptionPointers;
        BOOL                ClientPointers;
    };
#pragma pack( pop )

    LONG WINAPI CoopCrash_Filter( EXCEPTION_POINTERS* pInfo )
    {
        static LONG s_Entered = 0;
        if( InterlockedExchange( &s_Entered, 1 ) != 0 )
            return EXCEPTION_CONTINUE_SEARCH;

        FILE* pFile = fopen( "a51_crash.txt", "w" );
        if( pFile )
        {
            const HMODULE hExe   = GetModuleHandleA( NULL );
            const IMAGE_DOS_HEADER* pDos = (const IMAGE_DOS_HEADER*)hExe;
            const IMAGE_NT_HEADERS* pNt  = (const IMAGE_NT_HEADERS*)((const char*)hExe + pDos->e_lfanew);

            fprintf( pFile, "Area 51 co-op build: crash report\n" );
            fprintf( pFile, "exe timestamp: %08lx\n", (unsigned long)pNt->FileHeader.TimeDateStamp );
            fprintf( pFile, "exception: %08lx at ", (unsigned long)pInfo->ExceptionRecord->ExceptionCode );
            CoopCrash_WriteAddress( pFile, 0, (DWORD64)(ULONG_PTR)pInfo->ExceptionRecord->ExceptionAddress );
            if( (pInfo->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) &&
                (pInfo->ExceptionRecord->NumberParameters >= 2) )
            {
                fprintf( pFile, "access violation: %s address 0x%llx\n",
                         pInfo->ExceptionRecord->ExceptionInformation[0] ? "writing" : "reading",
                         (unsigned long long)pInfo->ExceptionRecord->ExceptionInformation[1] );
            }

            fprintf( pFile, "call stack:\n" );
#if defined( _M_X64 )
            CONTEXT Context = *pInfo->ContextRecord;
            for( int i = 0; i < 48; i++ )
            {
                if( Context.Rip == 0 )
                    break;
                CoopCrash_WriteAddress( pFile, i, Context.Rip );

                DWORD64 ImageBase = 0;
                PRUNTIME_FUNCTION pFunction = RtlLookupFunctionEntry( Context.Rip, &ImageBase, NULL );
                if( pFunction )
                {
                    void*   pHandlerData = NULL;
                    DWORD64 EstablisherFrame = 0;
                    RtlVirtualUnwind( UNW_FLAG_NHANDLER, ImageBase, Context.Rip, pFunction,
                                      &Context, &pHandlerData, &EstablisherFrame, NULL );
                }
                else
                {
                    // Leaf function: the return address is on top of the stack.
                    Context.Rip  = *(DWORD64*)(ULONG_PTR)Context.Rsp;
                    Context.Rsp += 8;
                }
            }
#endif
            fprintf( pFile, "\nPlease send this file (and a51_crash.dmp) with a note of what you were doing.\n" );
            fclose( pFile );
        }

        HMODULE hDbgHelp = LoadLibraryA( "dbghelp.dll" );
        if( hDbgHelp )
        {
            mini_dump_fn pWrite = (mini_dump_fn)GetProcAddress( hDbgHelp, "MiniDumpWriteDump" );
            HANDLE hDump = CreateFileA( "a51_crash.dmp", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );
            if( pWrite && (hDump != INVALID_HANDLE_VALUE) )
            {
                coop_dump_exception_info Info;
                Info.ThreadId          = GetCurrentThreadId();
                Info.ExceptionPointers = pInfo;
                Info.ClientPointers    = FALSE;
                pWrite( GetCurrentProcess(), GetCurrentProcessId(), hDump, 0 /* MiniDumpNormal */, &Info, NULL, NULL );
            }
            if( hDump != INVALID_HANDLE_VALUE )
                CloseHandle( hDump );
        }

        return EXCEPTION_CONTINUE_SEARCH;
    }

    struct coop_crash_install
    {
        coop_crash_install( void ) { SetUnhandledExceptionFilter( CoopCrash_Filter ); }
    };

    coop_crash_install s_CoopCrashInstall;
}

#endif // A51_LOCAL_COOP
