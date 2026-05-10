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
3. Run the `EgoMP.exe` launcher to start the game with multiplayer support.
4. Both the game and the console will launch separately.
5. You must start or continue to enter the game world first — the mod will not work while you are still in the main menu.
6. Once the game is running:
   - Press **NUMPAD 1** to host a session.
   - Press **NUMPAD 2** to connect to a session.
   - Press **NUMPAD 3** to disconnect from the current session.
7. If you are hosting:
   - Alt + Tab to the console window.
   - Enter a port number, or leave it empty to use the default port.
8. If you are connecting:
   - Alt + Tab to the console window.
   - Enter the host IP (or leave empty for default).
   - Then enter the port (or leave empty for default).
9. Return to the game and *voilà*, everything should work!

## 📜 Legal & Licensing

This project is licensed under the [GPLv3](https://www.gnu.org/licenses/gpl-3.0.html) license.

You are free to modify and redistribute the code, provided that derivative works remain open source under the same license.

Fable: The Lost Chapters and all related assets are property of their respective owners, including [Microsoft](https://en.wikipedia.org/wiki/Microsoft) and [Lionhead Studios](https://en.wikipedia.org/wiki/Lionhead_Studios).

This repository does not distribute original game assets. A legitimate copy of the game is required to use this project.
