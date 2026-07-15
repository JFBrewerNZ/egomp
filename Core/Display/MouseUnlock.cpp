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
// re-grabs.
//
// CRITICAL threading rule (learned from a crash): the coop-level change is
// done on a DEDICATED WORKER THREAD, never from the window procedure.
// IDirectInputDevice8::SetCooperativeLevel with DISCL_FOREGROUND synchronises
// through the cooperative window's message pump. Calling it from inside that
// window's WndProc blocks the pump it is waiting on -> the game goes Not
// Responding (deadlock) and then faults inside combase during teardown (an
// access violation with EgoMP frames directly above combase in the crash
// dump). Fable also pumps messages on a different thread from where this ran,
// making the window-thread call doubly unsafe. Running the toggle on its own
// thread lets the game keep pumping, so DirectInput's internal messaging
// completes normally. Every DirectInput call is additionally SEH-guarded, so
// even an unexpected fault can only fail the toggle, never crash the game.
//
// Earlier attempts, for the record:
//  1. Permanent non-exclusive (one-byte patch 5 -> 6) made dragging work but
//     freed the real Windows cursor during play: it moved with acceleration,
//     drifted off the game cursor, and a click outside stole focus mid-fight.
//     Kept as mouse_lock=0 for anyone who prefers drag-without-Alt.
//  2. Containing that free cursor with ClipCursor killed the in-game cursor:
//     a NON-exclusive DirectInput system mouse derives its axis data from
//     Windows cursor MOVEMENT, so a cursor pinned against the clip boundary
//     produces no deltas at all. (Fable's own unused windowed-mouse mode
//     recentres the cursor every frame to dodge exactly this; +0x343E.)
//
// Cursor cosmetics, still handled by the WndProc subclass (safe there -- no
// DirectInput calls): Fable's input ctor does one ShowCursor(FALSE) (0xAB5D8F)
// leaving the display count at -1; the first WM_SETCURSOR raises it to 0 so
// the arrow can show while the grab is released. Over the client area the
// arrow is shown only while released (window management) and hidden while
// grabbed (the game draws its own cursor). Alt reaches the game unchanged
// (its WndProc swallows VK_MENU syskeys).

namespace
{
    bool     active   = false;
    bool     grabMode = false; // [display] mouse_lock: Alt-toggled grab
    HWND     gameWnd  = nullptr;
    WNDPROC  gameProc = nullptr;

    // Posted by the worker to the window thread on every grab/release
    // transition so the cursor's visibility flips deterministically, without
    // waiting for a WM_SETCURSOR (which only fires when the mouse moves).
    // wParam: 1 = grabbed (hide the arrow), 0 = released (show it).
    constexpr UINT WM_EGOMP_CURSOR = WM_APP + 0x51;

    // Written by the WndProc (window thread), read by the worker thread.
    volatile bool inSizeMove = false; // inside DefWindowProc's move/size loop

    // The game's DirectInput mouse (IDirectInputDevice8*), captured by the
    // CreateDevice hook during game init -- long before the worker starts, so
    // no capture/use race. mouseExclusive mirrors the coop level we last set;
    // it is only ever changed by the worker thread.
    void* mouseDevice     = nullptr;
    volatile bool mouseExclusive = true; // read by the WndProc for the cursor
    HANDLE worker         = nullptr;

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
        return (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
    }

    // The three DirectInput calls, isolated in an SEH frame (no C++ objects,
    // so __try/__except is legal here). Returns true only on a clean toggle.
    bool ApplyCoopLevel(bool exclusive)
    {
        __try
        {
            void** vtbl = *reinterpret_cast<void***>(mouseDevice);
            auto unacquire = reinterpret_cast<DeviceCall_t>(vtbl[kUnacquireSlot]);
            auto setCoop   = reinterpret_cast<SetCoopLevel_t>(vtbl[kSetCoopSlot]);
            auto acquire   = reinterpret_cast<DeviceCall_t>(vtbl[kAcquireSlot]);

            unacquire(mouseDevice);
            HRESULT hr = setCoop(mouseDevice, gameWnd,
                                 exclusive ? kExclusiveFlags : kNonExclusiveFlags);
            acquire(mouseDevice); // may fail benignly; the game re-acquires
            return SUCCEEDED(hr);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    // Drive the cursor's display count to a definite state (window thread
    // only). ShowCursor keeps a counter; the cursor shows while it is >= 0.
    // Fable's init left it at -1 (hidden); we pin it to -1 when grabbed and 0
    // when released, so visibility never depends on a stray WM_SETCURSOR. The
    // iteration cap guards against a count that starts far from the target.
    void SetCursorHidden(bool hidden)
    {
        if (hidden)
        {
            for (int i = 0; i < 64 && ShowCursor(FALSE) >= 0; ++i) {}
        }
        else
        {
            for (int i = 0; i < 64 && ShowCursor(TRUE) < 0; ++i) {}
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

    // Watches Alt + focus and flips the mouse device's coop level to match,
    // off the window thread. Only ever acts on the focused game window, so a
    // second (background) client never touches its device.
    DWORD WINAPI ToggleWorker(LPVOID)
    {
        for (;;)
        {
            Sleep(15);
            if (!mouseDevice)
                continue;

            bool focused = (GetForegroundWindow() == gameWnd);
            if (!focused)
                continue; // background clients: leave the device to the game

            // Release the grab while Alt is held or a drag is in progress.
            bool wantExclusive = !(AltHeld() || inSizeMove);
            if (wantExclusive == mouseExclusive)
                continue;

            if (ApplyCoopLevel(wantExclusive))
            {
                mouseExclusive = wantExclusive;
                if (!wantExclusive)
                    CenterCursor(gameWnd);
                // Flip the cursor on the window thread, now, regardless of
                // whether the mouse is moving.
                PostMessageW(gameWnd, WM_EGOMP_CURSOR, wantExclusive ? 1 : 0, 0);
                Log(wantExclusive ? "mouse grabbed (retail exclusive)"
                                  : "mouse released (drag/resize/switch windows)");
            }
        }
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
        case WM_EGOMP_CURSOR:
            // Grab/release transition from the worker: set visibility now,
            // without waiting for a mouse-move WM_SETCURSOR. The exclusive
            // re-acquire does NOT hide the cursor on its own, so this explicit
            // hide is what removes the arrow when you return to the game.
            SetCursorHidden(wParam != 0);
            if (wParam == 0)
            {
                static HCURSOR arrow =
                    LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)); // IDC_ARROW
                SetCursor(arrow);
            }
            else
            {
                SetCursor(nullptr);
            }
            return 0;

        case WM_SETCURSOR:
            if (LOWORD(lParam) == HTCLIENT)
            {
                if (grabMode && !mouseExclusive)
                {
                    // Grab released (Alt): show the real arrow so it can be
                    // steered onto a title bar or another client.
                    static HCURSOR arrow =
                        LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)); // IDC_ARROW
                    SetCursor(arrow);
                }
                else
                {
                    SetCursor(nullptr); // the game draws its own cursor
                }
                return TRUE;
            }
            break;

        // The worker reads inSizeMove to keep the mouse released for the whole
        // drag/resize, so releasing Alt mid-drag doesn't re-grab under it.
        case WM_ENTERSIZEMOVE:
            inSizeMove = true;
            break;
        case WM_EXITSIZEMOVE:
            inSizeMove = false;
            break;
        }
        return CallWindowProcW(gameProc, hwnd, msg, wParam, lParam);
    }

    HRESULT STDMETHODCALLTYPE HCreateDevice(void* self, const GUID* rguid,
                                            void** device, void* outer)
    {
        HRESULT hr = oCreateDevice(self, rguid, device, outer);
        if (SUCCEEDED(hr) && rguid && device && *device &&
            memcmp(rguid, &kGuidSysMouse, sizeof(GUID)) == 0)
        {
            // Track the latest mouse device in case the game recreates it, so
            // we never toggle a released COM object.
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
            // toggle the captured device while Alt is held, from the worker.
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
        gameProc = (WNDPROC)SetWindowLongPtrW(gameWindow, GWLP_WNDPROC,
                                              (LONG_PTR)&HookProc);
        if (!gameProc)
        {
            Log("failed to subclass the game window");
            return;
        }

        if (grabMode)
        {
            if (!mouseDevice)
            {
                Log("mouse device was never captured; ALT release unavailable");
            }
            else if (!worker)
            {
                // Normalise the initial (grabbed) state to hidden, in case the
                // display count is not where Fable's init left it.
                PostMessageW(gameWindow, WM_EGOMP_CURSOR, 1, 0);
                worker = CreateThread(nullptr, 0, &ToggleWorker, nullptr, 0, nullptr);
                if (worker)
                    CloseHandle(worker); // fire-and-forget for process life
            }
            Log("installed (hold ALT to release the mouse)");
        }
        else
        {
            Log("installed (shared mouse, no grab)");
        }
    }
}
