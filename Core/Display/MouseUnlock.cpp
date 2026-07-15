#include "MouseUnlock.h"

#include "../Config/Config.h"

#include <iostream>

#include <MinHook/include/MinHook.h>

// Why the title bar can't be dragged in retail: Fable acquires its DirectInput
// mouse EXCLUSIVE | FOREGROUND (SetCooperativeLevel(hwnd, 5) at 0xAB578A, its
// only mouse coop call). While a mouse is exclusively acquired, Windows
// suppresses its own mouse handling for the desktop: a click on the caption is
// never seen, so the modal move loop never starts. The window itself is fine
// -- Fable's WndProc (0x9A5B60) passes WM_NCLBUTTONDOWN and friends straight
// to DefWindowProcW, and its WM_SYSCOMMAND handler only swallows
// SC_SCREENSAVE/SC_MONITORPOWER -- so ordinary dragging works whenever the
// mouse is not exclusively acquired.
//
// Approach (mouse_lock=1, the default): TOGGLE the exclusivity at runtime.
// Stay EXCLUSIVE while playing -- byte-identical retail behaviour: true device
// deltas, cursor grabbed and hidden, nothing can drift out or click away the
// focus -- and switch the device to NON-exclusive only while ALT is held,
// which hands the mouse back to Windows for window management: drag the title
// bar, resize an edge, or click another client. Releasing Alt over the game
// (or clicking back into it) re-grabs.
//
// Two earlier attempts inform this design:
//  1. Forcing the mouse permanently non-exclusive (one-byte patch 5 -> 6) made
//     dragging work, but freed the REAL Windows cursor during play: it moves
//     with pointer acceleration, drifts away from the game's cursor, glides
//     invisibly onto the desktop, and the next click steals focus mid-fight.
//     (Still available as mouse_lock=0 for anyone who prefers it.)
//  2. Containing that free cursor with ClipCursor killed the in-game cursor:
//     a NON-exclusive DirectInput system mouse derives its axis data from
//     Windows cursor MOVEMENT, so once the cursor rests against the clip
//     boundary no deltas flow at all. (Fable's engine knows: its own unused
//     windowed-mouse mode has a recenter-every-frame branch at +0x343E.)
//
// Mechanics of the toggle:
//  - The device pointer is captured by hooking DirectInput8Create ->
//    IDirectInput8::CreateDevice and matching GUID_SysMouse (same pattern as
//    WindowedMode's Direct3DCreate9 hook). No game bytes are patched.
//  - A coop-level change requires Unacquire -> SetCooperativeLevel ->
//    Acquire. All toggles run on the window's own thread (inside the
//    subclassed WndProc), the thread that also runs the game's input reads,
//    so nothing races the per-frame GetDeviceData. A failed re-Acquire is
//    benign -- the game's input loop re-acquires every frame (retail's
//    alt-tab path).
//  - Fable's input constructor calls ShowCursor(FALSE) once (0xAB5D8F),
//    leaving the cursor display count at -1; the first WM_SETCURSOR raises it
//    back to 0 so the arrow can actually show while the grab is released.
//    While exclusive, the acquisition itself keeps the cursor hidden.
//  - Alt reaches the game exactly as before: Fable's WndProc swallows VK_MENU
//    syskeys, and a lone-Alt release can't open DefWindowProc's menu loop
//    because DefWindowProc never sees the keydown.
//  - WA_CLICKACTIVE deliberately does NOT re-grab: a click on the caption of
//    an unfocused client must be able to start a drag. The re-grab happens on
//    the first client-area WM_SETCURSOR (cursor over the game, Alt up).

namespace
{
    bool     active   = false;
    bool     grabMode = false; // [display] mouse_lock: toggle exclusivity
    HWND     gameWnd  = nullptr;
    WNDPROC  gameProc = nullptr;
    volatile LONG cursorCountFixed = 0;

    bool windowActive = false; // game window is the active window
    bool inSizeMove   = false; // inside DefWindowProc's move/size modal loop

    // The game's DirectInput mouse (IDirectInputDevice8*), captured by the
    // CreateDevice hook. mouseExclusive mirrors the coop level the device
    // holds; the game sets exclusive at init and never changes it itself.
    void* mouseDevice    = nullptr;
    bool  mouseExclusive = true;

    // IDirectInput8 / IDirectInputDevice8 vtable slots.
    constexpr int kCreateDeviceSlot = 3;
    constexpr int kAcquireSlot      = 7;
    constexpr int kUnacquireSlot    = 8;
    constexpr int kSetCoopSlot      = 13;

    constexpr DWORD kExclusiveFlags    = 0x05; // DISCL_EXCLUSIVE | DISCL_FOREGROUND
    constexpr DWORD kNonExclusiveFlags = 0x06; // DISCL_NONEXCLUSIVE | DISCL_FOREGROUND

    const GUID kGuidSysMouse =
        { 0x6F1D2B60, 0xD5A0, 0x11CF, { 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 } };

    using DirectInput8Create_t = HRESULT(WINAPI*)(HINSTANCE, DWORD, const GUID*, void**, void*);
    using CreateDevice_t       = HRESULT(STDMETHODCALLTYPE*)(void*, const GUID*, void**, void*);
    using DeviceCall_t         = HRESULT(STDMETHODCALLTYPE*)(void*);
    using SetCoopLevel_t       = HRESULT(STDMETHODCALLTYPE*)(void*, HWND, DWORD);

    DirectInput8Create_t oDirectInput8Create = nullptr;
    CreateDevice_t       oCreateDevice       = nullptr;
    bool                 createDeviceHooked  = false;

    void Log(const char* msg)
    {
        std::cout << "[EgoMP][mouse] " << msg << std::endl;
    }

    bool AltHeld()
    {
        return (GetKeyState(VK_MENU) & 0x8000) != 0;
    }

    bool CursorInClient(HWND hwnd)
    {
        POINT p = {};
        RECT  r = {};
        if (!GetCursorPos(&p) || !GetClientRect(hwnd, &r))
            return false;
        POINT tl = { r.left, r.top }, br = { r.right, r.bottom };
        ClientToScreen(hwnd, &tl);
        ClientToScreen(hwnd, &br);
        RECT s = { tl.x, tl.y, br.x, br.y };
        return PtInRect(&s, p) != FALSE;
    }

    void SetMouseExclusive(bool exclusive)
    {
        if (!mouseDevice || !gameWnd || exclusive == mouseExclusive)
            return;

        void** vtbl = *reinterpret_cast<void***>(mouseDevice);
        auto unacquire = reinterpret_cast<DeviceCall_t>(vtbl[kUnacquireSlot]);
        auto setCoop   = reinterpret_cast<SetCoopLevel_t>(vtbl[kSetCoopSlot]);
        auto acquire   = reinterpret_cast<DeviceCall_t>(vtbl[kAcquireSlot]);

        unacquire(mouseDevice);
        HRESULT hr = setCoop(mouseDevice, gameWnd,
                             exclusive ? kExclusiveFlags : kNonExclusiveFlags);
        acquire(mouseDevice);

        if (SUCCEEDED(hr))
        {
            mouseExclusive = exclusive;
            Log(exclusive ? "mouse grabbed (retail exclusive)"
                          : "mouse released (drag/resize/switch windows)");
        }
        else
        {
            Log("SetCooperativeLevel failed; keeping previous grab state");
        }
    }

    // Park the freshly released cursor mid-window so the arrow appears
    // somewhere predictable instead of wherever it was left long ago.
    void CenterCursor(HWND hwnd)
    {
        RECT r = {};
        if (!GetClientRect(hwnd, &r))
            return;
        POINT c = { (r.left + r.right) / 2, (r.top + r.bottom) / 2 };
        ClientToScreen(hwnd, &c);
        SetCursorPos(c.x, c.y);
    }

    bool WantGrab()
    {
        return grabMode && windowActive && !inSizeMove && !AltHeld();
    }

    // mov edx,[eax] / push 5 / push ecx / push eax / call [edx+0x34]
    // at 0xAB5784 -- the SetCooperativeLevel(hwnd, DISCL_EXCLUSIVE |
    // DISCL_FOREGROUND) call in the mouse init. Flip the pushed 5 to 6.
    // Only used for mouse_lock=0 (permanently shared mouse).
    bool PatchCooperativeLevel()
    {
        static const BYTE expect[9] = { 0x8B, 0x10, 0x6A, 0x05, 0x51,
                                        0x50, 0xFF, 0x52, 0x34 };
        BYTE* site = (BYTE*)0x00AB5784;
        if (memcmp(site, expect, sizeof(expect)) != 0)
            return false; // not the expected image; leave it alone

        DWORD oldProtect;
        if (!VirtualProtect(site + 3, 1, PAGE_EXECUTE_READWRITE, &oldProtect))
            return false;
        site[3] = 0x06; // DISCL_NONEXCLUSIVE | DISCL_FOREGROUND
        VirtualProtect(site + 3, 1, oldProtect, &oldProtect);
        return true;
    }

    LRESULT CALLBACK HookProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg)
        {
        case WM_SETCURSOR:
            if (InterlockedExchange(&cursorCountFixed, 1) == 0)
            {
                // Undo the game's one ShowCursor(FALSE) so the arrow can show
                // while the grab is released; never leave the count above 0.
                int count = ShowCursor(TRUE);
                for (int i = 0; count < 0 && i < 16; ++i)
                    count = ShowCursor(TRUE);
                if (count > 0)
                    ShowCursor(FALSE);
            }
            if (LOWORD(lParam) == HTCLIENT)
            {
                if (WantGrab())
                {
                    // Cursor over the game with Alt up: (re)grab. This is the
                    // deferred grab after a click-activate, an Alt release
                    // outside the window, or the end of a drag.
                    SetMouseExclusive(true);
                    SetCursor(nullptr); // the game draws its own cursor
                }
                else if (grabMode)
                {
                    // Grab released: show the real arrow so it can be steered
                    // onto a title bar or another client.
                    static HCURSOR arrow =
                        LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)); // IDC_ARROW
                    SetCursor(arrow);
                }
                else
                {
                    SetCursor(nullptr); // mouse_lock=0: game cursor only
                }
                return TRUE;
            }
            break;

        case WM_ACTIVATE:
            windowActive = LOWORD(wParam) != WA_INACTIVE;
            // Losing activation needs no action: exclusive+foreground
            // auto-unacquires (retail behaviour) and the coop level persists.
            // Alt-tab back (WA_ACTIVE) re-grabs immediately; WA_CLICKACTIVE
            // waits for the first client WM_SETCURSOR so a caption click can
            // start its drag without the grab eating it.
            if (windowActive && LOWORD(wParam) == WA_ACTIVE && WantGrab())
                SetMouseExclusive(true);
            break;

        case WM_ENTERSIZEMOVE:
            inSizeMove = true; // grab is already off (Alt) -- keep it off
            break;

        case WM_EXITSIZEMOVE:
            inSizeMove = false; // re-grab on the next client-area hover
            break;

        case WM_SYSKEYDOWN:
        case WM_KEYDOWN:
            if (wParam == VK_MENU && grabMode && mouseExclusive)
            {
                SetMouseExclusive(false);
                CenterCursor(hwnd);
            }
            break;

        case WM_SYSKEYUP:
        case WM_KEYUP:
            // Alt released alone arrives as WM_KEYUP, after combos as
            // WM_SYSKEYUP -- handle both. Only re-grab when the cursor is
            // still over the game; released elsewhere, the grab waits until
            // the cursor comes back (or the window is re-activated).
            if (wParam == VK_MENU && WantGrab() && CursorInClient(hwnd))
                SetMouseExclusive(true);
            break;
        }
        return CallWindowProcW(gameProc, hwnd, msg, wParam, lParam);
    }

    HRESULT STDMETHODCALLTYPE HCreateDevice(void* self, const GUID* rguid,
                                            void** device, void* outer)
    {
        HRESULT hr = oCreateDevice(self, rguid, device, outer);
        if (SUCCEEDED(hr) && rguid && device && *device && !mouseDevice &&
            memcmp(rguid, &kGuidSysMouse, sizeof(GUID)) == 0)
        {
            mouseDevice = *device;
            Log("mouse device captured");
        }
        return hr;
    }

    HRESULT WINAPI HDirectInput8Create(HINSTANCE inst, DWORD version, const GUID* iid,
                                       void** out, void* outer)
    {
        HRESULT hr = oDirectInput8Create(inst, version, iid, out, outer);
        if (SUCCEEDED(hr) && out && *out && !createDeviceHooked)
        {
            void** vtbl = *reinterpret_cast<void***>(*out);
            void* target = vtbl[kCreateDeviceSlot];
            if (MH_CreateHook(target, reinterpret_cast<void*>(&HCreateDevice),
                              reinterpret_cast<void**>(&oCreateDevice)) == MH_OK &&
                MH_EnableHook(target) == MH_OK)
            {
                createDeviceHooked = true;
            }
            else
            {
                Log("failed to hook CreateDevice");
            }
        }
        return hr;
    }

    bool InstallDeviceCaptureHook()
    {
        HMODULE dinput = GetModuleHandleW(L"dinput8.dll");
        if (!dinput)
            dinput = LoadLibraryW(L"dinput8.dll");
        if (!dinput)
            return false;

        void* create = reinterpret_cast<void*>(GetProcAddress(dinput, "DirectInput8Create"));
        if (!create)
            return false;

        return MH_CreateHook(create, reinterpret_cast<void*>(&HDirectInput8Create),
                             reinterpret_cast<void**>(&oDirectInput8Create)) == MH_OK &&
               MH_EnableHook(create) == MH_OK;
    }
}

namespace MouseUnlock
{
    void Install()
    {
        if (!Config::Get().windowed || !Config::Get().mouseUnlock)
            return;

        grabMode = Config::Get().mouseLock;
        if (grabMode)
        {
            // Default: leave the game fully retail (exclusive grab) and only
            // toggle the captured device while Alt is held.
            active = InstallDeviceCaptureHook();
            Log(active ? "armed (retail grab; hold ALT to release the mouse)"
                       : "failed to hook DirectInput8Create; mouse left retail");
        }
        else
        {
            // mouse_lock=0: permanently shared mouse. Free cursor at all
            // times; dragging works without Alt, but clicks outside the
            // window steal the game's focus.
            active = PatchCooperativeLevel();
            Log(active ? "mouse forced non-exclusive (mouse_lock=0)"
                       : "coop-level bytes unexpected; mouse left exclusive");
        }
    }

    bool Active()
    {
        return active;
    }

    void OnWindowReady(HWND gameWindow)
    {
        if (!active || !gameWindow || gameProc)
            return;

        gameWnd = gameWindow;
        windowActive = (GetForegroundWindow() == gameWindow);
        if (grabMode && !mouseDevice)
            Log("mouse device was never captured; ALT release unavailable");

        gameProc = (WNDPROC)SetWindowLongPtrW(gameWindow, GWLP_WNDPROC,
                                              (LONG_PTR)&HookProc);
        if (!gameProc)
            Log("failed to subclass the game window");
        else if (grabMode)
            Log("installed (hold ALT to release the mouse)");
        else
            Log("installed (shared mouse, no grab)");
    }
}
