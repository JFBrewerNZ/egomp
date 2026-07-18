#include "pch.h"
#include <iostream>

#include "./Config/Config.h"
#include "./DevTools/CrashDiag.h"
#include "./Display/MouseUnlock.h"
#include "./Display/WindowedMode.h"
#include "./Platform/CpuAffinity.h"
#include "./Platform/SaveRedirect.h"
#include "./Multiplayer/Fable/Multiplayer.h"

// Let a second copy of the game run for same-machine multiplayer testing.
//
// Fable's WinMain (0x403480) guards against a second instance like this:
//     0x4034A2  call OpenMutexW            ; eax = handle, or NULL if none
//     0x4034A8  test eax, eax
//     0x4034AA  jne  0x4034F6              ; another instance -> bail
//     0x4034AC  push 0x122DC28             ; lpName
//     0x4034B1  push eax                   ; bInitialOwner
//     0x4034B2  push eax                   ; lpMutexAttributes
//     0x4034B3  call CreateMutexW
// The compiler knows eax is 0 once the jne is not taken, so it reuses eax as
// the constant NULL for CreateMutexW's attributes/owner arguments.
//
// The obvious patch -- NOP the jne -- is subtly broken: a second instance
// reaches 0x4034AC with eax still holding the *existing* mutex handle, so that
// handle is passed as lpMutexAttributes and CreateMutexW faults dereferencing
// it inside KERNELBASE!BaseFormatObjectAttributes. That is the crash that kills
// the second client right after startup.
//
// Instead, overwrite the two-byte jne (75 4A) with `xor eax, eax` (31 C0):
// same length, it falls through AND restores eax == 0 so CreateMutexW gets
// valid (NULL, FALSE) arguments. The already-open handle from OpenMutexW leaks,
// which is harmless for a one-shot startup path. We are injected while the main
// thread is still suspended, so this always runs before the check.
static bool PatchSingleInstanceGuard() {
    BYTE* site = (BYTE*)0x004034AA;
    if (site[0] != 0x75 || site[1] != 0x4A)
        return false; // not the expected image; leave it alone

    DWORD oldProtect;
    if (!VirtualProtect(site, 2, PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;
    site[0] = 0x31; // xor eax, eax
    site[1] = 0xC0;
    VirtualProtect(site, 2, oldProtect, &oldProtect);
    return true;
}

static void CreateConsole() {
    if (AllocConsole()) {
        FILE* f;
        freopen_s(&f, "CONOUT$", "w", stdout);
        freopen_s(&f, "CONOUT$", "w", stderr);
        freopen_s(&f, "CONIN$", "r", stdin);

        // PID in the title tells two instances' consoles apart.
        wchar_t title[32];
        swprintf_s(title, L"EgoMP %u", GetCurrentProcessId());
        SetConsoleTitleW(title);

        // Start minimized without taking focus: stealing focus from the
        // fullscreen game wedges its renderer.
        HWND console = GetConsoleWindow();
        if (console)
            ShowWindow(console, SW_SHOWMINNOACTIVE);
    }
}

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
        case DLL_PROCESS_ATTACH:
        {
            DisableThreadLibraryCalls(hModule);

            bool multiInstance = PatchSingleInstanceGuard();

            if (Config::Get().showConsole)
                CreateConsole();

            std::cout << "[EgoMP] single-instance guard "
                      << (multiInstance ? "patched" : "NOT patched (byte mismatch)") << std::endl;

            // Brings up SDK/game hooks and, importantly, initialises MinHook,
            // which WindowedMode::Install() below relies on.
            Multiplayer::GetInstance();

            // Force windowed mode so a second client can share the display.
            // Must follow Multiplayer::GetInstance() (MinHook init) and runs
            // while the main thread is still suspended, before the game builds
            // its Direct3D device.
            if (Config::Get().windowed)
                WindowedMode::Install();

            // Let the title bar be dragged: one-byte patch that makes Fable
            // acquire its DirectInput mouse non-exclusively. Gates itself on
            // config; must beat the game's input init, like the patch above.
            MouseUnlock::Install();

            // Give a second client its own save folder so the two clients don't
            // collide on Documents\My Games\Fable. Also before the game runs.
            SaveRedirect::Install();

            // Crash diagnostics (EgoMP-crash-<pid>.log/.dmp): names every C++
            // exception the game throws and keeps a ring of recent file opens.
            // After SaveRedirect so the header reports the claimed slot; needs
            // MinHook, initialised by Multiplayer::GetInstance() above.
            CrashDiag::Install();

            // Pin this client to its own CPU core (Fable's multi-core timing
            // bugs; two clients amplify them). After ClientSlot is claimable so
            // each client picks a distinct core.
            CpuAffinity::Install();
            break;
        }
        case DLL_THREAD_ATTACH:
        {
            break;
        }
        case DLL_THREAD_DETACH:
        {
            break;
        }
        case DLL_PROCESS_DETACH:
        {
            break;
        }
    }
    return TRUE;
}
