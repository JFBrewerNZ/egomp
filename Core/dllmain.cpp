#include "pch.h"
#include <iostream>

#include "./Config/Config.h"
#include "./Multiplayer/Fable/Multiplayer.h"

static void CreateConsole() {
    if (AllocConsole()) {
        FILE* f;
        freopen_s(&f, "CONOUT$", "w", stdout);
        freopen_s(&f, "CONOUT$", "w", stderr);
        freopen_s(&f, "CONIN$", "r", stdin);

        SetConsoleTitleW(L"EgoMP");

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

            if (Config::Get().showConsole)
                CreateConsole();

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
