#pragma once

// Gives each client on the same machine its own Fable data folder.
//
// Fable locates its saves/tattoos/config under
//     SHGetFolderPathW(CSIDL_PERSONAL)  +  "\My Games\Fable\..."
// which resolves to the same Documents folder for every instance of the same
// Windows user. Two clients therefore write the same files (e.g. a hero's
// Tattoos\<id>\*.bmp) and the second one dies on the collision.
//
// Each launch claims a client number (1, 2, 3, ... by order, reusing freed
// numbers) and has its CSIDL_PERSONAL redirected to "<root>\Client<N>", so its
// "My Games\Fable" lives in a private per-client directory.
namespace SaveRedirect
{
    // Call once, after MinHook is initialised, while the main thread is still
    // suspended (before the game resolves any folder). No-op when disabled or if
    // the target path can't be resolved.
    void Install();
}
