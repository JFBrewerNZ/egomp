#pragma once

// Gives each client on the same machine its own Fable data folders.
//
// Fable locates per-user data in TWO places, both resolved through
// SHGetFolderPathW and both identical for every instance of the same Windows
// user:
//     CSIDL_PERSONAL + "\My Games\Fable\..."   saves / tattoos / profiles
//     CSIDL_APPDATA  + "\Microsoft\Fable\..."  comfront/comback.dat cache,
//                                              opened read/write with NO
//                                              sharing during world load
// Two clients therefore collide: on the Documents side they overwrite each
// other's files (e.g. a hero's Tattoos\<id>\*.bmp); on the AppData side the
// second client's world load throws an unhandled CBBBFileException the moment
// the first client is holding comfront.dat open.
//
// Each launch claims a client number (1, 2, 3, ... by order, reusing freed
// numbers) and has CSIDL_PERSONAL redirected to "<root>\Client<N>" and
// CSIDL_APPDATA to "<root>\Client<N>\AppData", so both trees live in a
// private per-client directory.
namespace SaveRedirect
{
    // Call once, after MinHook is initialised, while the main thread is still
    // suspended (before the game resolves any folder). No-op when disabled or if
    // the target path can't be resolved.
    void Install();
}
