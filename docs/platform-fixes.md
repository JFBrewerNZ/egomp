# Platform fixes — display, crashes, cursor (community + EgoMP)

Fable TLC on modern Windows/AMD has well-documented platform problems that sit
*underneath* EgoMP. Leaning on the community's proven fixes beats hand-rolling.
Compiled 2026-07-18 (Steam guide by McMuffin id 483177549, dgvoodoo2.com, DxWnd
SourceForge, Nexus mods #58/#444/#589, fabletlcmod forum).

## Crash triage first

Categorising your own crashes tells you which layer to fix. From WER (id 1000),
by faulting module (see `Tools/symbolize.py` to name Fable.exe frames):
- **KERNELBASE `0xe06d7363`** — a C++ exception the game threw. EgoMP-domain
  (file collisions, weapon/record logic). CrashDiag names the type.
- **Fable.exe `0xc0000005`** — access violation in game code. Often EgoMP RE
  probing, but also the AMD shadow path (below).
- **unknown module, offset `0x00000009`** — a wild jump to ~null, i.e. a call
  through a freed/garbage pointer. EgoMP dangling-pointer bug (use-after-free
  family). CrashDiag now catches these first-chance (commit 962ae52); frame #1
  in the log is the culprit — feed it to `symbolize.py`.
- **combase / d3d9 / driver modules** — platform (COM/DirectX/driver).

## AMD/ATI crash on New Game or Load

Documented: many AMD/ATI D3D9 drivers crash the instant you load, in the
**shadow-buffer** setup. Fix without any wrapper: **set all in-game Shadow
detail to minimum before loading**, then raise it once in-game; don't change
graphics options from the main menu afterward. This machine is AMD (AMDXN32 /
`AMD\DX9Cache` in the logs), so rule this in/out for any load crash. dgVoodoo2
(below) avoids the trigger entirely by replacing the AMD D3D9 runtime.

## dgVoodoo2 — the recommended display layer

A drop-in D3D9→D3D11/12 wrapper. Drop `D3D9.dll` (from dgVoodoo `MS/x86`) and
`dgVoodooCpl.exe` next to `Fable.exe`. It replaces AMD's native D3D9 path, so it
**(a)** sidesteps the AMD shadow crash, **(b)** presents into a real window with
a normal desktop cursor (very likely fixing the 2nd-client cursor without
EgoMP's DirectInput grab), and **(c)** forces windowed reliably.

Config (dgVoodooCpl): DirectX tab → **Windowed = on**, **"Disable application
controlled fullscreen/windowed state" = on**. Do NOT tick "Disable Alt-Enter…"
(it stops Fable booting). Optional: `dgVoodoo.conf` `FullscreenAttributes="fake"`
for borderless, `FPSLimit=` to cap.

**Coexistence with EgoMP (verified reasoning):** dgVoodoo *is* the `d3d9.dll`
file; EgoMP *injects* and *hooks* `Direct3DCreate9`/`CreateDevice` from inside
its DLL. Different mechanisms → compatible, because EgoMP does **not** ship its
own `d3d9.dll`, and its hook resolves the export from the *loaded* d3d9 module
(dgVoodoo's), forwarding into it. The one hard rule: never let EgoMP become the
`d3d9.dll` file — that collides with dgVoodoo.

**To hand display to dgVoodoo, set `windowed=0` in `EgoMP.ini`.** That skips
`WindowedMode::Install` and (since MouseUnlock gates on `windowed`) the mouse
grab too — EgoMP stays out of display entirely and does only game-logic hooks.

## CPU single-core affinity — now built into EgoMP

Fable's engine has timing/threading bugs across multiple cores (broken
animation framerate, missing sounds, giant hero model, timing glitches). The
community fix is `start /affinity <mask>`. Two EgoMP clients amplify it, and it
is a plausible contributor to the flaky cursor and timing-sensitive weapon/spawn
behaviour. **EgoMP now pins each client to its own core** (`[performance]
cpu_affinity=1`, default on; commit dcea59f) — verified two clients on cores 1
and 2. Manual equivalent if you ever disable it: launch via
`cmd /c start "" /affinity 2 EgoMP.exe` and `/affinity 4` for the second.

## Other tools (for reference)

- **FableHook** (Nexus #58) — a windowed DirectX hook, the closed-source
  ancestor of EgoMP's approach. No advantage over dgVoodoo for us.
- **DxWnd** — borderless-windowed alternative to dgVoodoo; per-exe profile, so
  clumsier for two instances. If used: disable "Hook DirectShow" (causes
  black-screen-on-load) and try "No SHIMs".
- **Unofficial Fable Patch** (Nexus #444, github Wyntilda/Unofficial-Fable-Patch)
  — data/quest bug fixes (weapon inventory slot count 55→68, re-encoded skill
  tutorial videos for Win10). Not a rendering/engine fix; won't fix invisible
  weapons.
- **Weapons not rendering** is more likely the vanilla "weapons disappear when
  sheathed" bug or an EgoMP D3D-hook side-effect than a data bug — check whether
  weapons render on an *unmodded single instance* to isolate.

## Access notes

Nexus and PCGamingWiki are Cloudflare-403 to automated fetch. Steam Community
guides, dgvoodoo2.com, the DxWnd SourceForge thread, and GitHub are reachable
(`curl -A` a browser UA for Steam). fabletlcmod.com needs `curl -k` (bad cert).
