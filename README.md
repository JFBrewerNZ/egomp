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
