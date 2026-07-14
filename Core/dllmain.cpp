#include "pch.h"
#include <iostream>

#include "./Config/Config.h"
#include "./Multiplayer/Fable/Multiplayer.h"

// Fable's WinMain (0x403480) silently exits if OpenMutexW finds
// "Global\Fable: The Lost Chapters" held by another instance. NOP the
// jne at 0x4034AA so a second copy can run for same-machine multiplayer
// testing. We are injected while the main thread is still suspended, so
// this always runs before the check.
static bool PatchSingleInstanceGuard() {
    BYTE* site = (BYTE*)0x004034AA;
    if (site[0] != 0x75 || site[1] != 0x4A)
        return false; // not the expected image; leave it alone

    DWORD oldProtect;
    if (!VirtualProtect(site, 2, PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;
    site[0] = 0x90;
    site[1] = 0x90;
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

            Multiplayer::GetInstance();
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
