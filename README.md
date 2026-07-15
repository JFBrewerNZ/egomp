# EgoMP

Experimental multiplayer mod for [Fable: The Lost Chapters](https://en.wikipedia.org/wiki/Fable:_The_Lost_Chapters).

**EgoMP** is an ongoing project aimed at bringing multiplayer gameplay through a robust peer-to-peer networking layer for player synchronization, world state replication, and shared game logic.

> [!WARNING]
> This project is currently in an experimental state. Expect bugs, crashes, desyncs, and incomplete features.
>
> It is primarily intended for developers, reverse engineers, and early testers.

## 🛠 Technical Stack

- **Language:** C++
- **Networking:** SLNet (RakNet fork)
- **Hooks / Injection:** Custom DLL injection and memory manipulation

## 🚀 Getting Started

1. Download the latest version from the [Releases](https://github.com/98thrxse/EgoMP/releases) page.
2. Extract the archive into your game directory.
3. Edit `EgoMP.ini` and set `server_ip`/`server_port` to the server you want to play on (server operators usually distribute a preconfigured copy).
4. Run the `EgoMP.exe` launcher to start the game with multiplayer support.
5. Start or continue a save to enter the game world — the mod is inactive at the main menu.
6. That's it: the mod connects to the configured server automatically and keeps retrying every 10 seconds while unconnected.

Manual controls (all settings come from `EgoMP.ini`; nothing is ever typed into the console):

- **NUMPAD 1** — host a peer-to-peer session on the configured port (set `auto_connect=0` first).
- **NUMPAD 2** — connect to the configured server.
- **NUMPAD 3** — disconnect (also pauses auto-reconnect until you press NUMPAD 2 or reload the world).

The EgoMP console window is only a log — it starts minimized and can be hidden entirely with `console=0`.

## 🧪 Running two clients on one machine

For local multiplayer testing you can run the game twice on the same PC: just launch `EgoMP.exe` a second time. Two things make this work, both handled automatically:

- **Single-instance bypass.** Fable's `WinMain` bails when it finds its `Global\Fable: The Lost Chapters` mutex already held. The mod rewrites that check (the `jne` at `0x4034AA`) to `xor eax, eax`, which both lets the second instance continue *and* preserves the `eax == 0` the following `CreateMutexW` call relies on. (A plain `NOP` there leaves the existing mutex handle in `eax`, which `CreateMutexW` then dereferences as its attributes pointer — an instant crash in `KERNELBASE!BaseFormatObjectAttributes`. That was the "second client closes itself" bug.)
- **Windowed mode** (`[display] windowed=1`, on by default). Two *fullscreen* instances fight over the exclusive display, so the mod forces Direct3D to create a windowed device. With `reshape=1` (default) the mod then gives each client a normal **title bar** (titled `Fable - EgoMP Client <N>`) so you can move and resize the window yourself — it does **not** move it or pick a size for you. This is done a few seconds after startup, off-thread (doing it during init makes Fable's own window/DirectX init "fail" and relaunch, so it's deliberately deferred). Set `window_width`/`window_height` to force a fixed client size (0 = leave the game's size; the future launcher writes these to size each client). `reshape=0` leaves the game's borderless screen-sized window; `windowed=0` is normal fullscreen for solo play.
- **Movable windows: hold Alt to release the mouse** (`[display] mouse_unlock`, on whenever `reshape` is on). Fable acquires its DirectInput mouse *exclusively*, which makes Windows ignore clicks on the window frame — the title bar could never start a drag. While you play, the mod leaves that retail grab completely alone (in-game cursor and camera are exactly stock, the cursor can't wander off or click away the game's focus). **Holding Alt** switches the game's mouse device to shared mode: the Windows arrow appears mid-window, and you can steer it onto the title bar (drag to move — the game pauses while you hold it, which is Windows' modal move loop and normal for windowed games), onto an edge (resize), or onto the other client (click to switch). Release Alt over the game, or click back into it, and the retail grab returns. Set `mouse_lock=0` for a permanently shared mouse instead (drag without Alt, but the cursor roams invisibly during play and a click outside the window steals focus).
- **A separate save folder per client** (`[storage] separate_saves=1`, on by default). Fable resolves its data folder with `SHGetFolderPathW(CSIDL_PERSONAL)` → `Documents\My Games\Fable`, which is identical for every client of the same Windows user. Two clients then write the same files — notably a shared hero's `Tattoos\<id>\*.bmp` — and the second one crashes on world load. The mod gives each launch its own number and redirects its Documents to `<data_root>\Client<N>` (default `data_root` = `%LOCALAPPDATA%\EgoMP`). The first running client is `Client1`, the next `Client2`, and so on; closing a client frees its number for reuse.

A brand-new `Client<N>` folder is **seeded** from a template so it isn't empty (`[storage] seed_new_clients=1`, on by default). Put the heroes you want every new client to start with in `%LOCALAPPDATA%\EgoMP\Template\My Games\Fable` (or point `seed_from` at any `My Games\Fable`); existing client folders are never overwritten. Set `seed_new_clients=0` to have new clients start empty and populate them by hand instead.

## 🖥 Dedicated Server (experimental)

`EgoMPServer.exe` is a standalone session server — it needs no copy of the game and can run on any Windows machine or VPS. It assigns player ids, remembers who is where, and relays player updates; each connected client keeps simulating its own world.

```
EgoMPServer.exe [port] [maxPlayers]   # defaults: 60000, 8
```

Clients join a dedicated server using the address configured in `EgoMP.ini`. Unlike joining a player host, you are **not** teleported: you appear in your own world at your own position. Player visibility is region-scoped — you see the players who are in the same region as you, and they appear/disappear as either of you moves between regions.

## 📜 Legal & Licensing

This project is licensed under the [GPLv3](https://www.gnu.org/licenses/gpl-3.0.html) license.

You are free to modify and redistribute the code, provided that derivative works remain open source under the same license.

Fable: The Lost Chapters and all related assets are property of their respective owners, including [Microsoft](https://en.wikipedia.org/wiki/Microsoft) and [Lionhead Studios](https://en.wikipedia.org/wiki/Lionhead_Studios).

This repository does not distribute original game assets. A legitimate copy of the game is required to use this project.
