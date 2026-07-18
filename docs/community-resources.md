# Community resources — tools, docs, prior art, research backlog

Compiled 2026-07-18. Access notes: fabletlcmod.com has a broken HTTPS cert —
fetch over plain http (`curl -k`), sequentially (parallel requests get
dropped). Forum thread URL pattern:
`http://fabletlcmod.com/forum/index.php?t=msg&th=<id>&start=0&`.

## Tools

| Tool | Where | What it does |
|---|---|---|
| **FableExplorer (FE) Beta 4.1** | forum th=1330 (Hunter) | Canonical .big/.bin/def editor; def.xml schema; FMP mod packages; model/anim preview/export; "Save Mods and Run Fable". Closed source; Hunter ("TodX") offered source on request (th=9297). |
| **FE ShadowNet builds** | th=6075 (Silverback) | Enhanced FE: search, FMP merge, per-file backup/restore, script.bin FMPs. Sound build's code lost. |
| **ChocolateBox (CBox)** | th=5795 (BayStone) | FE + AlbionExplorer fusion + 3D region editor: place/move things, edit walk areas, create teleporters, names.bin viewer, fly mode. cbox.zip + x64 build attached. FE can't read CBox FMPs. |
| **AlbionExplorer** | th=740 | Older 2D region/TNG editor. |
| **Fable Window Mode Hook** | th=4130 (Wiccaan) | DLL forcing retail into a window — prior art for our WindowedMode/MouseUnlock (thread documents the same DI mouse-grab pain we hit). |
| **Freeroam** | App Suite | Extracts FinalAlbion.WAD to loose level files. |
| **Fable Application Suite** | th=6973 / th=10631 | Bundle installer (FE/AE/CBox…). |
| **CAppearanceDef editor, Met Edit, SavingIt** | th=1666 (Silverback) | Per-creature animation-slot tables (expression/combat anims → graphics.big anim IDs — relevant to anim sync!), .met sound metadata editor, read-only save explorer. |
| **Community def.xml** | th=10273 (Keshire 2014) | Most complete def schema (794KB attachment). |
| **FableMenu** | github.com/ermaccer/FableMenu | In-game trainer/menu, ImGui overlay, huge verified address DB (see engine-internals.md). **No license — reference only.** |
| **FableSDK** | th=9198 (EternalNoob 2012) | .NET plugin SDK injected into retail exe; FableSDK.zip still attached; thread contains the ~2662-class RTTI dump (goto=66825). |
| **Unified Debug Build + EGO_R** | (community; no public link in our sources) | The original in-engine world editor + region compiler — see file-formats.md. |
| Xporter (3ds Max) th=11088, DX9 Model Ripper th=4529, B-Morph th=4533 | | Model pipeline / morph tools. |

## Documentation hubs

- **fabletlcmod wiki**: `http://fabletlcmod.com/wiki/doku.php?id=documentation`
  — file_formats (see file-formats.md), def_editing, scripting
  (map_scripting_101, custom_bodyguard, d.i.y._teleporter…), basics, utilities.
- **"The Alright Library of Alexander(ia)"** (Alexander The Alright) — def
  editing bible, distilled into def-system.md:
  https://docs.google.com/document/d/1ODjpkoo_wSMFJuoPgCmICMmfDzXrgJNmMLeq5ylHg38
- **Map editing tutorial** (MakhnoBlazed) — Unified Debug Build workflow,
  distilled into file-formats.md:
  https://docs.google.com/document/d/e/2PACX-1vQBCoXbQfI8KPJ55HyM20m4DV8sJYTcwdnSZJ4DWaQWaMP2Bpw31ys9KumK0r8L-LoXBsL90vY2brxz/pub
- **Big Avarice tool-setup videos**: https://www.youtube.com/playlist?list=PLPOVgJGt6qPeVEStXZ1pK52Q9USEFNUZu
- Forum guides: Oldboy "New to Modding Fable?" th=7218, Nesdude's guide
  th=3978, jwc2200 hero appearance th=7600, .fmp inspection th=7235,
  vegetation meshes th=11084, weapon editing video
  https://www.youtube.com/watch?v=ZDAaHStB_XI
- Community hub: **Fable Modder's Guild Discord** (where Jamon found these).

## Multiplayer prior art (nobody shipped; we're past their line)

- **th=3391 "Fable Multiplayer" (2007)** — Keshire's scoping, still the best
  summary: the player is a special NPC (special script type + movement type,
  both hookable); sync spawning, position, animation, input; find spawn/pos
  code via holy-site coordinates, anim sync via graphics.big file-ID watches;
  SA:MP-style dummy-NPC proxies. This is EgoMP's architecture, predicted.
- **goto=56600 (2010)** — "the scripting system needs to be hijacked first and
  foremost"; consensus was effort > payoff. No code ever produced.
- **th=9198 FableSDK** — the closest infrastructure attempt; confirmed dormant
  CNetworkClient/CNetworkServer in the retail exe.
- 2016 split-screen thread (goto=71550) unanswered; 2018 "Project: The Lost
  Ego" (goto=71678) is a world-restoration mod, no netcode.

## High-value research backlog

1. **Mac debug symbols** — CONFIRMED real, tooling READY, blocked only on
   legitimately obtaining the binary (2026-07-18 investigation):
   - blastedt confirmed (fabletlcmod th=71721 / th=11080) the Feral Mac port
     (2008) is compiled with **full debug symbols — every function name with
     its demangled C++ signature**. His one public screenshot (imgur LrerHwK)
     is transcribed in [data/mac-symbols-sample.txt](data/mac-symbols-sample.txt)
     (31 symbols); nobody ever published a full dump. He also noted the Mac
     dev-console render function is empty too — corroborating that the
     console was stubbed engine-wide, not just on PC.
   - **Acquisition reality:** the Steam release (app 204030) is
     Windows-only — no Mac depot, so a Steam copy does NOT contain it. The
     Mac version was Feral's separate, now-delisted 2008 retail SKU (32-bit;
     won't even run on macOS Catalina+). No legitimate digital storefront
     sells it today; a used retail Mac disc is the realistic legal route.
     Abandonware mirrors host it but that is not a license — do not use them.
   - **When a legitimate binary is in hand:** run
     [`Tools/extract_mac_symbols.py`](../Tools/extract_mac_symbols.py)
     (`pip install lief`; runs on Windows, no macOS needed) on the Mach-O
     executable inside the .app bundle. It dumps every demangled symbol, an
     address→name TSV, and auto-cross-references class names against
     [data/rtti-classes.tsv](data/rtti-classes.tsv) to produce the
     high-confidence name↔PC-class map. From there, port names onto the PC
     exe by matching code/vtable shape (blastedt's method). Biggest single RE
     accelerator available; everything but the binary is ready.
2. ~~Scrape the full RTTI dump from th=9198~~ **DONE** →
   [data/rtti-classes.tsv](data/rtti-classes.tsv) (2090 classes with VFT +
   inheritance; validated against known VFTs). A diff against a fresh
   `Tools/rtti_scan.py` run of the live exe would confirm nothing drifted.
3. ~~Extract FableMenu Database.cpp name tables~~ **DONE** →
   [data/](data/README.md) (13 lists: objects, creatures, brains, holy sites,
   player modes, factions…).
4. **Deep-read threads**: th=3391 all pages (more addresses mid-thread),
   th=5795 pages 2–4 (newer CBox builds), wiki `bin_entries` namespace
   (per-def binary layouts), th=10273 page 2 (latest def.xml).
5. ~~Dormant netcode RE~~ **DONE 2026-07-18** (see engine-internals.md
   "Investigated"): CNetworkClient/CNetworkServer are hollow stubs (2-slot
   vtables, no winsock) — no reusable transport, so EgoMP's SLikeNet approach
   is correct. But CTCCoopSpirit and CTCHeroOnlineScoreboard are **live**
   Thing Components with real logic — the co-op spirit is a genuine
   second-entity primitive worth building on.
6. **Debug console resurrection**: CConsole::Initialise at 0x9ED190 is intact
   in retail — enabling it would give us a free in-game command surface
   (FableMenu's retail-debug-bool list proves the console-backed globals still
   work).
