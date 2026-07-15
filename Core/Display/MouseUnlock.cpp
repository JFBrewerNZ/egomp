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
// Everything that is not WM_SETCURSOR is forwarded to the game untouched. The
// earlier, backed-out experiment ran a move loop from a subclass while the
// mouse was still exclusive -- the exclusivity is what broke the cursor, not
// the subclassing.

namespace
{
    bool     active     = false;
    WNDPROC  gameProc   = nullptr;
    volatile LONG cursorCountFixed = 0;

    void Log(const char* msg)
    {
        std::cout << "[EgoMP][mouse] " << msg << std::endl;
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
        if (msg == WM_SETCURSOR)
        {
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
                SetCursor(nullptr); // the game draws its own cursor here
                return TRUE;
            }
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

        gameProc = (WNDPROC)SetWindowLongPtrW(gameWindow, GWLP_WNDPROC,
                                              (LONG_PTR)&HookProc);
        Log(gameProc ? "cursor fix installed (arrow on frame, hidden in game)"
                     : "failed to subclass the game window");
    }
}
