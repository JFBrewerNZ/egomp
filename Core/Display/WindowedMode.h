#pragma once

// Forces Fable into a windowed Direct3D 9 device so two clients can run on
// one machine for local multiplayer testing. Two fullscreen instances fight
// over the exclusive display: the second one's CreateDevice fails and the
// process exits (the "EgoMP <pid>" console flashes and vanishes). A windowed
// device takes no exclusive ownership, so both instances coexist.
//
// The game's own SetFullscreen(false) config command is a no-op on this build
// (the command string is absent from every shipped module), so we override the
// decision at the source instead: hook IDirect3D9::CreateDevice and flip
// D3DPRESENT_PARAMETERS.Windowed just before the driver sees it.
namespace WindowedMode
{
    // Installs the Direct3D 9 hooks. Call once, AFTER MinHook is initialised
    // (i.e. after the SDK hooks are set up) and while the game's main thread is
    // still suspended, so the hook is armed before the game builds its device.
    // Safe to call when d3d9.dll is missing or already hooked: it just logs and
    // returns.
    void Install();
}
