#include "MouseUnlock.h"

#include "../Config/Config.h"

#include <iostream>

// Why the title bar can't be dragged: Fable's input init (0xAB5710) creates
// its DirectInput mouse and calls SetCooperativeLevel(hwnd, 5) at 0xAB578A --
// DISCL_EXCLUSIVE | DISCL_FOREGROUND. While a mouse is exclusively acquired,
// Windows suppresses its own mouse handling for the desktop: the click on the
// caption is never seen, so the modal move loop never starts. The window
// itself is fine -- Fable's WndProc (0x9A5B60) passes WM_NCLBUTTONDOWN and
// friends straight to DefWindowProcW, and its WM_SYSCOMMAND handler only
// swallows SC_SCREENSAVE/SC_MONITORPOWER -- so ordinary dragging works the
// moment the mouse stops being exclusive.
//
// The fix is one byte: the `push 5` immediate becomes 6 (DISCL_NONEXCLUSIVE |
// DISCL_FOREGROUND). Nothing else about input changes:
//  - Relative DirectInput data -- what drives the in-game cursor and camera
//    today -- is identical under a non-exclusive acquire, and this is the
//    game's ONLY mouse SetCooperativeLevel call.
//  - Mouse buttons/wheel already arrive via window messages (the WndProc sets
//    state bytes for 0x201..0x20D); unaffected.
//  - The keyboard was already non-exclusive (flags 6 at 0xAB6484), and the
//    joystick setup (flags 5 at 0xAB6B78) is deliberately left alone.
//  - The engine does have its own non-exclusive mode (init's bool argument
//    picks flags 5 or 0xA and sets a mode byte at +0x343C that switches the
//    cursor to GetCursorPos-based code). We deliberately do NOT flip that
//    bool: it would move the game onto a read-the-OS-cursor path retail never
//    exercised. With just the flag byte patched, the game keeps believing
//    it is exclusive and keeps its proven DirectInput-delta cursor.
//
// Cursor cosmetics, handled by a minimal WndProc subclass:
//  - Fable's input constructor calls ShowCursor(FALSE) once when built for
//    exclusive mode (0xAB5D8F), leaving the cursor display count at -1: the
//    Windows arrow would stay invisible even over our title bar. On the first
//    WM_SETCURSOR -- which runs on the window's own thread, the thread that
//    count belongs to -- we raise the count back to 0.
//  - The game window's class registers the standard arrow as its class cursor
//    (LoadCursorW(0, IDC_ARROW) at 0x9A64F5), so once visible, the arrow
//    would also paint over the CLIENT area on top of Fable's own rendered
//    cursor. WM_SETCURSOR with HTCLIENT therefore hides it (SetCursor(NULL));
//    every other hit zone keeps standard frame cursors.
//
// Cursor CONTAINMENT (the second half of the feature): a non-exclusive mouse
// means the real Windows cursor moves again -- with pointer acceleration, so
// it drifts apart from the game's DirectInput-driven cursor -- and once it
// wanders outside the window, the next click lands elsewhere and the game
// loses focus mid-fight. So while a game window is the active window, the OS
// cursor is confined to its CLIENT area with ClipCursor:
//  - Clicks can then never land outside the game, and the invisible cursor
//    can never be seen gliding across the desktop.
//  - The client area specifically, NOT the whole window: mouse-look presses
//    the cursor against the clip boundary, and if the boundary were the title
//    bar, an attack click with the cursor resting there would start a window
//    drag instead.
//  - HOLD ALT to release the lock for window management: the arrow becomes
//    visible over the game so it can be steered onto the title bar (drag),
//    the frame (resize), or another client's window (focus it). The lock
//    re-engages when the cursor next rests on the client area with Alt up.
//    Fable's WndProc already swallows VK_MENU syskeys (no menu loop), so Alt
//    reaches the game exactly as before.
//  - The lock also releases during DefWindowProc's move/size loop (otherwise
//    the window could only be dragged as far as the old clip rect), when the
//    window deactivates (alt-tab), and re-asserts on every client-area
//    WM_SETCURSOR, which also heals races between two clients handing the
//    (global) clip to each other.
// Everything else is forwarded to the game untouched. The earlier, backed-out
// experiment ran a move loop from a subclass while the mouse was still
// exclusive -- the exclusivity is what broke the cursor, not the subclassing.

namespace
{
    bool     active     = false;
    WNDPROC  gameProc   = nullptr;
    volatile LONG cursorCountFixed = 0;

    // Cursor-lock state. All of it is touched only on the window's thread
    // (inside the subclassed WndProc), except the initial values set by
    // OnWindowReady before messages start flowing through the hook.
    bool windowActive = false; // game window is the active window
    bool inSizeMove   = false; // inside DefWindowProc's move/size modal loop
    bool weClipped    = false; // the current (global) ClipCursor rect is ours

    void Log(const char* msg)
    {
        std::cout << "[EgoMP][mouse] " << msg << std::endl;
    }

    bool AltHeld()
    {
        return (GetKeyState(VK_MENU) & 0x8000) != 0;
    }

    RECT ClientRectOnScreen(HWND hwnd)
    {
        RECT r = {};
        GetClientRect(hwnd, &r);
        POINT tl = { r.left, r.top }, br = { r.right, r.bottom };
        ClientToScreen(hwnd, &tl);
        ClientToScreen(hwnd, &br);
        RECT s = { tl.x, tl.y, br.x, br.y };
        return s;
    }

    bool WantLock()
    {
        return windowActive && !inSizeMove && !AltHeld();
    }

    void Lock(HWND hwnd)
    {
        RECT c = ClientRectOnScreen(hwnd);
        if (ClipCursor(&c))
            weClipped = true;
    }

    void Unlock()
    {
        if (weClipped)
        {
            ClipCursor(nullptr);
            weClipped = false;
        }
    }

    bool CursorInClient(HWND hwnd)
    {
        POINT p = {};
        if (!GetCursorPos(&p))
            return false;
        RECT c = ClientRectOnScreen(hwnd);
        return PtInRect(&c, p) != FALSE;
    }

    // mov edx,[eax] / push 5 / push ecx / push eax / call [edx+0x34]
    // at 0xAB5784 -- the SetCooperativeLevel(hwnd, DISCL_EXCLUSIVE |
    // DISCL_FOREGROUND) call in the mouse init. Flip the pushed 5 to 6.
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
                // on the window frame; never leave the count above 0.
                int count = ShowCursor(TRUE);
                for (int i = 0; count < 0 && i < 16; ++i)
                    count = ShowCursor(TRUE);
                if (count > 0)
                    ShowCursor(FALSE);
            }
            if (LOWORD(lParam) == HTCLIENT)
            {
                if (WantLock())
                {
                    // (Re)assert the lock on every client-area mouse move.
                    // This is also the deferred lock after a click-activate
                    // or after Alt-release/drag-end outside the client.
                    Lock(hwnd);
                    SetCursor(nullptr); // the game draws its own cursor here
                }
                else
                {
                    // Window-management mode (Alt held / mid-drag): show the
                    // real arrow so it can be steered onto a title bar.
                    static HCURSOR arrow =
                        LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)); // IDC_ARROW
                    SetCursor(arrow);
                }
                return TRUE;
            }
            break;

        case WM_ACTIVATE:
            windowActive = LOWORD(wParam) != WA_INACTIVE;
            if (!windowActive)
                Unlock();
            else if (LOWORD(wParam) == WA_ACTIVE && WantLock())
                Lock(hwnd); // alt-tab back: recapture immediately. A
                            // WA_CLICKACTIVE instead waits for the first
                            // client WM_SETCURSOR, so a click on the caption
                            // can start its drag without the cursor being
                            // yanked into the client area first.
            break;

        case WM_ACTIVATEAPP:
            if (!wParam)
            {
                windowActive = false;
                Unlock();
            }
            break;

        case WM_ENTERSIZEMOVE:
            // Free the cursor for DefWindowProc's modal loop, or the window
            // could only be dragged as far as the old clip rect allows.
            inSizeMove = true;
            Unlock();
            break;

        case WM_EXITSIZEMOVE:
            inSizeMove = false; // relock on the next client-area hover
            break;

        case WM_WINDOWPOSCHANGED:
            // Moved/resized programmatically (e.g. by the launcher): follow
            // with the clip rect.
            if (weClipped && WantLock())
                Lock(hwnd);
            break;

        case WM_SYSKEYDOWN:
        case WM_KEYDOWN:
            if (wParam == VK_MENU)
                Unlock();
            break;

        case WM_SYSKEYUP:
        case WM_KEYUP:
            // Alt released alone arrives as WM_KEYUP, after combos as
            // WM_SYSKEYUP -- handle both. Only relock when the cursor is
            // already back over the game, so releasing Alt out on the
            // desktop doesn't yank it home.
            if (wParam == VK_MENU && WantLock() && CursorInClient(hwnd))
                Lock(hwnd);
            break;

        case WM_DESTROY:
            Unlock();
            break;
        }
        return CallWindowProcW(gameProc, hwnd, msg, wParam, lParam);
    }
}

namespace MouseUnlock
{
    void Install()
    {
        if (!Config::Get().windowed || !Config::Get().mouseUnlock)
            return;

        active = PatchCooperativeLevel();
        Log(active ? "mouse forced non-exclusive (title bar is draggable)"
                   : "coop-level bytes unexpected; mouse left exclusive");
    }

    bool Active()
    {
        return active;
    }

    void OnWindowReady(HWND gameWindow)
    {
        if (!active || !gameWindow || gameProc)
            return;

        windowActive = (GetForegroundWindow() == gameWindow);
        gameProc = (WNDPROC)SetWindowLongPtrW(gameWindow, GWLP_WNDPROC,
                                              (LONG_PTR)&HookProc);
        if (!gameProc)
        {
            Log("failed to subclass the game window");
            return;
        }
        if (WantLock())
            Lock(gameWindow);
        Log("cursor locked to the game (hold ALT to release it)");
    }
}
