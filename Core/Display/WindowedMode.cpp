#include "WindowedMode.h"

#include "MouseUnlock.h"
#include "../Config/Config.h"
#include "../Platform/ClientSlot.h"

#include <windows.h>
#include <d3d9.h>
#include <iostream>

#include <MinHook/include/MinHook.h>

// Force Fable's Direct3D 9 device to windowed so two clients can share the
// display. Two fullscreen instances fight over exclusive ownership; a windowed
// device takes none, so both coexist.
//
// Window shaping (a titled, movable, resizable window placed side by side) is
// OPT-IN and applied LATE, on a timer. Restyling the window *during* device
// creation makes Fable's own window/DirectX init think it failed and relaunch
// into a second, un-modded process. So we wait a few seconds -- well past that
// init check -- and only then reshape. We use a timer rather than a rendered-
// frame count because a second, unfocused client barely renders any frames.
//
// The game statically imports d3d9.dll, so Direct3DCreate9 is resolvable the
// moment we attach. We hook the export (safe under the loader lock) and only
// when the game calls it -- on its own thread, no loader lock -- reach into the
// returned object's vtable to hook CreateDevice.
//
// Alternative (not used): Fable derives Windowed from a global fullscreen byte
// at 0x137544A (default 1 = fullscreen; the chain is 0x137544A -> device config
// -> [esi+0x8e] -> D3DPP.Windowed at 0x9BF89F). Forcing it to 0 -- or patching
// 0x9A667C (8A 47 10 -> 32 C0 90) -- makes Fable build a windowed device through
// its own code. We deliberately avoid that path: retail Fable has no windowed
// mode, so that code is untested by the developers. Overriding only the present
// parameters lets the engine keep its normal (fullscreen) code path and just
// renders it into a window, which is what has tested cleanly here.

namespace
{
    // IDirect3D9 / IDirect3DDevice9 vtable slots (after the 3 IUnknown ones).
    constexpr int kCreateDeviceSlot = 16; // IDirect3D9::CreateDevice
    constexpr int kResetSlot        = 16; // IDirect3DDevice9::Reset

    // How long to wait after device creation before reshaping the window. Must
    // be well past Fable's post-CreateDevice init check (reshaping during it
    // makes Fable relaunch), but is otherwise just a short cosmetic delay.
    constexpr DWORD kReshapeDelayMs = 4000;

    using Direct3DCreate9_t = IDirect3D9*(WINAPI*)(UINT);
    using CreateDevice_t    = HRESULT(STDMETHODCALLTYPE*)(IDirect3D9*, UINT, D3DDEVTYPE, HWND,
                                                          DWORD, D3DPRESENT_PARAMETERS*,
                                                          IDirect3DDevice9**);
    using Reset_t           = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);

    Direct3DCreate9_t oDirect3DCreate9 = nullptr;
    CreateDevice_t    oCreateDevice    = nullptr;
    Reset_t           oReset           = nullptr;

    bool createDeviceHooked = false;
    bool resetHooked        = false;

    // Desktop pixel format, captured at device creation; reused when the game
    // resets the device so windowed Reset stays format-compatible.
    D3DFORMAT desktopFormat = D3DFMT_X8R8G8B8;

    // Window reshaping (opt-in via [display] reshape).
    bool          reshapeEnabled = false;
    int           cfgWidth       = 0; // configured client size; 0 = use game res
    int           cfgHeight      = 0;
    int           bbWidth        = 0; // the game's backbuffer (native) resolution
    int           bbHeight       = 0;
    HWND          gameWindow     = nullptr;
    volatile LONG reshapeApplied = 0;

    void Log(const char* msg)
    {
        std::cout << "[EgoMP][windowed] " << msg << std::endl;
    }

    // Overrides that turn any present-parameters block into a valid windowed
    // request without disturbing the game's chosen resolution.
    void ForceWindowed(D3DPRESENT_PARAMETERS* pp)
    {
        pp->Windowed = TRUE;
        pp->FullScreen_RefreshRateInHz = 0; // must be 0 for windowed
        pp->BackBufferFormat = desktopFormat;
    }

    // Give the game's window a normal title bar so it can be moved and resized.
    // We keep its top-left position (no tiling) but DO size it to fit on screen:
    // a screen-sized window plus a title bar spills off the bottom, and Fable's
    // cursor then breaks. Default size is the game's own resolution; the launcher
    // can override it via window_width/window_height. D3D scales the backbuffer
    // to whatever size the window ends up.
    void ReshapeWindow()
    {
        if (!gameWindow || !IsWindow(gameWindow))
            return;

        const int screenW = GetSystemMetrics(SM_CXSCREEN);
        const int screenH = GetSystemMetrics(SM_CYSCREEN);
        const DWORD style = WS_OVERLAPPEDWINDOW | WS_VISIBLE;

        int clientW = cfgWidth  > 0 ? cfgWidth  : bbWidth;
        int clientH = cfgHeight > 0 ? cfgHeight : bbHeight;
        if (clientW <= 0) clientW = 1024;
        if (clientH <= 0) clientH = 768;

        // Shrink to fit the screen (leave room for the title bar and taskbar).
        RECT fr = { 0, 0, 1000, 1000 };
        AdjustWindowRect(&fr, style, FALSE);
        const int borderW = (fr.right - fr.left) - 1000;
        const int borderH = (fr.bottom - fr.top) - 1000;
        if (clientW > screenW - borderW)        clientW = screenW - borderW;
        if (clientH > screenH - borderH - 48)   clientH = screenH - borderH - 48;

        RECT r = { 0, 0, clientW, clientH };
        AdjustWindowRect(&r, style, FALSE);
        const int winW = r.right - r.left;
        const int winH = r.bottom - r.top;

        SetWindowLongW(gameWindow, GWL_STYLE, style);
        SetWindowLongW(gameWindow, GWL_EXSTYLE,
                       GetWindowLongW(gameWindow, GWL_EXSTYLE) & ~WS_EX_TOPMOST);
        SetWindowPos(gameWindow, HWND_NOTOPMOST, 0, 0, winW, winH,
                     SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOZORDER);

        int n = ClientSlot::Number();
        if (n < 1) n = 1;
        wchar_t title[64];
        swprintf_s(title, L"Fable - EgoMP Client %d", n);
        SetWindowTextW(gameWindow, title);

        char buf[96];
        sprintf_s(buf, "client %d: titled window, %dx%d client", n, clientW, clientH);
        Log(buf);
    }

    void ApplyReshapeOnce()
    {
        if (InterlockedExchange(&reshapeApplied, 1) == 0)
            ReshapeWindow();
    }

    // Deferred window work: the reshape, plus MouseUnlock's cursor fix, both
    // of which must land well after Fable's fragile post-CreateDevice init.
    DWORD WINAPI ReshapeThread(LPVOID)
    {
        Sleep(kReshapeDelayMs);
        if (reshapeEnabled)
            ApplyReshapeOnce();
        MouseUnlock::OnWindowReady(gameWindow);
        return 0;
    }

    HRESULT STDMETHODCALLTYPE HReset(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* pp)
    {
        if (pp)
            ForceWindowed(pp);
        return oReset(device, pp);
    }

    void InstallResetHook(IDirect3DDevice9* device)
    {
        if (resetHooked || !device)
            return;
        void** vtbl = *reinterpret_cast<void***>(device);
        void* target = vtbl[kResetSlot];
        if (MH_CreateHook(target, reinterpret_cast<void*>(&HReset),
                          reinterpret_cast<void**>(&oReset)) == MH_OK &&
            MH_EnableHook(target) == MH_OK)
        {
            resetHooked = true;
            Log("Reset hooked (stays windowed across device resets)");
        }
    }

    HRESULT STDMETHODCALLTYPE HCreateDevice(IDirect3D9* d3d, UINT adapter, D3DDEVTYPE type,
                                            HWND focusWindow, DWORD behaviorFlags,
                                            D3DPRESENT_PARAMETERS* pp,
                                            IDirect3DDevice9** returnedDevice)
    {
        if (!pp)
            return oCreateDevice(d3d, adapter, type, focusWindow, behaviorFlags, pp, returnedDevice);

        const D3DPRESENT_PARAMETERS original = *pp;

        D3DDISPLAYMODE dm = {};
        if (SUCCEEDED(d3d->GetAdapterDisplayMode(adapter, &dm)))
            desktopFormat = dm.Format;

        gameWindow = pp->hDeviceWindow ? pp->hDeviceWindow : focusWindow;
        bbWidth    = (int)pp->BackBufferWidth;
        bbHeight   = (int)pp->BackBufferHeight;

        // Attempt 1: windowed, keeping the game's other parameters.
        ForceWindowed(pp);
        {
            char buf[128];
            sprintf_s(buf, "forcing windowed %ux%u (was %s)",
                      pp->BackBufferWidth, pp->BackBufferHeight,
                      original.Windowed ? "windowed" : "fullscreen");
            Log(buf);
        }
        HRESULT hr = oCreateDevice(d3d, adapter, type, focusWindow, behaviorFlags, pp, returnedDevice);

        // Attempt 2: a multisample setup valid for fullscreen can be invalid
        // windowed against the desktop format; drop it and try once more.
        if (FAILED(hr) && pp->MultiSampleType != D3DMULTISAMPLE_NONE)
        {
            char buf[96];
            sprintf_s(buf, "windowed create failed (0x%08lX); retrying without multisampling",
                      (unsigned long)hr);
            Log(buf);
            pp->MultiSampleType = D3DMULTISAMPLE_NONE;
            pp->MultiSampleQuality = 0;
            hr = oCreateDevice(d3d, adapter, type, focusWindow, behaviorFlags, pp, returnedDevice);
        }

        // Last resort: never do worse than the stock game -- give it exactly
        // what it asked for.
        if (FAILED(hr))
        {
            char buf[96];
            sprintf_s(buf, "windowed create failed (0x%08lX); falling back to the game's params",
                      (unsigned long)hr);
            Log(buf);
            *pp = original;
            hr = oCreateDevice(d3d, adapter, type, focusWindow, behaviorFlags, pp, returnedDevice);
        }

        if (SUCCEEDED(hr) && returnedDevice && *returnedDevice)
        {
            InstallResetHook(*returnedDevice);
            if (reshapeEnabled || MouseUnlock::Active())
            {
                // Reshape (and/or install the cursor fix) shortly, off-thread,
                // so it lands well after Fable's init check regardless of
                // whether this (possibly unfocused) client is rendering.
                HANDLE t = CreateThread(nullptr, 0, &ReshapeThread, nullptr, 0, nullptr);
                if (t)
                    CloseHandle(t);
            }
        }

        return hr;
    }

    IDirect3D9* WINAPI HDirect3DCreate9(UINT sdkVersion)
    {
        IDirect3D9* d3d = oDirect3DCreate9(sdkVersion);
        if (d3d && !createDeviceHooked)
        {
            void** vtbl = *reinterpret_cast<void***>(d3d);
            void* target = vtbl[kCreateDeviceSlot];
            if (MH_CreateHook(target, reinterpret_cast<void*>(&HCreateDevice),
                              reinterpret_cast<void**>(&oCreateDevice)) == MH_OK &&
                MH_EnableHook(target) == MH_OK)
            {
                createDeviceHooked = true;
                Log("CreateDevice hooked");
            }
            else
            {
                Log("failed to hook CreateDevice");
            }
        }
        return d3d;
    }
}

namespace WindowedMode
{
    void Install()
    {
        reshapeEnabled = Config::Get().reshapeWindow;
        cfgWidth       = Config::Get().windowWidth;
        cfgHeight      = Config::Get().windowHeight;

        HMODULE d3d9 = GetModuleHandleW(L"d3d9.dll");
        if (!d3d9)
            d3d9 = LoadLibraryW(L"d3d9.dll");
        if (!d3d9)
        {
            Log("d3d9.dll not present; leaving display mode untouched");
            return;
        }

        void* create9 = reinterpret_cast<void*>(GetProcAddress(d3d9, "Direct3DCreate9"));
        if (!create9)
        {
            Log("Direct3DCreate9 not found; leaving display mode untouched");
            return;
        }

        if (MH_CreateHook(create9, reinterpret_cast<void*>(&HDirect3DCreate9),
                          reinterpret_cast<void**>(&oDirect3DCreate9)) != MH_OK ||
            MH_EnableHook(create9) != MH_OK)
        {
            Log("failed to hook Direct3DCreate9");
            return;
        }

        Log(reshapeEnabled ? "armed (windowed + reshape)" : "armed (windowed)");
    }
}
