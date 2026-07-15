#pragma once

#include <windows.h>

// Makes the game window draggable by its title bar: Fable acquires its
// DirectInput mouse exclusively, which suppresses all normal Windows mouse
// handling for the window (so a click on the title bar never starts a move).
// This forces the mouse to non-exclusive and tidies up the cursor. See the
// .cpp for the full story. Gated on [display] mouse_unlock in EgoMP.ini.
namespace MouseUnlock
{
    // Patch the game's mouse cooperative level. Must run before the game
    // initialises input, i.e. at DLL attach while the main thread is still
    // suspended. Safe to call unconditionally; it gates itself on config.
    void Install();

    // True when Install() applied the patch (config on + bytes matched).
    bool Active();

    // Install the cursor cosmetics (WndProc subclass) on the game window.
    // Called by WindowedMode's deferred-reshape timer, well after the game's
    // fragile init window. No-op unless Active().
    void OnWindowReady(HWND gameWindow);
}
