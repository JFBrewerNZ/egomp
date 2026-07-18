# File formats — what's documented where

Fable TLC's data files, with the state of community documentation for each.
Primary spec source: the fabletlcmod.com wiki
(`http://fabletlcmod.com/wiki/doku.php?id=file_formats:<name>`; pages: bba,
bbm, big, bin, effect, gamesave, lev, lut, met, stb, stb_lev, wad, plus the
`bin_entries` sub-namespace with per-def binary layouts). Site quirk: HTTPS has
a broken cert chain — fetch over plain http (curl -k).

General: game data compression is **lzo1x** almost everywhere, plus **zlib**
inside .bin tables and saves.

## Archives & compiled data

- **.big** (graphics.big, textures.big, text.big, frontend.big…) — FULLY
  SPECIFIED (wiki `file_formats:big`): 'BIGB' header → bank index → file index
  (magic/ID/type/size/start) → dev-info block per entry (symbol name, CRC,
  source file list). Texture entries: sub-header, first mipmap lzo1x-compressed,
  remaining mips raw, DXT formats. Mesh entries: sub-header with physics mesh,
  LODs, texture IDs referencing textures.big. Banks zero-padded to 0x800.
- **.bin + names.bin** (game.bin, script.bin, frontend.bin = CompiledDefs) —
  SPECIFIED (wiki `file_formats:bin`): names.bin is the shared string library;
  table 1 = 12-byte rows (name offset, filename offset/enumerator, counter);
  table 2 = zlib-compressed chunks holding the def payloads. PC↔Xbox
  differences documented. Per-def payload layouts: wiki `bin_entries`
  namespace (deep-read backlog). See also [def-system.md](def-system.md) for
  the semantic layer.
- **.wad** (FinalAlbion.wad = level file container) — no prose spec, but
  WORKING C++ reader/writer classes by BigFreak & Fatum:
  `http://fabletlcmod.com/wiki/lib/exe/fetch.php?media=file_formats:fable-wad-classes.zip`.
  The Freeroam tool extracts FinalAlbion.wad into loose level files (the wad
  must then be absent from data\Levels for the game to use them).

## World / levels

- **FinalAlbion.wld** — plain text registry of maps and regions. Maps listed
  after the line `NewMap 398`; regions at end of file. Editable by hand.
- **.lev** (per-map landscape) — FULLY SPECIFIED (wiki `file_formats:lev`):
  main header (version byte 0x25), map header, 33792-byte ground-theme and
  sound-theme palettes, heightmap cells at (W+1)×(H+1) — each cell: height
  (float/2048), 3 theme indices + 2 strengths, **walkable bool**, camera-pass
  bool, shore bool — sound map, and complete **navigation data**: 7-layer
  quadtree (32×32 down to 0.5×0.5), NAV_STANDARD_NODE / NAV_NODE / EXIT_NODE
  structs with adjacency lists and TNG UIDs (exit-node UID = stored value +
  18446741874686296064).
- **.tng** (per-map thing placement) — plain text; entries grouped into
  **Sections** (see def-system.md for quest-gated sections). Things carry def
  name, UID, position/rotation, ScriptName/ScriptData, persistence flags, and
  CTC* component blocks (e.g. CTCCreatureGenerator → CreatureFamilies,
  CTCActionUseScriptedHook → teleporter marker UID). Edited by
  ChocolateBox's Region Editor or by hand.
- **.gtg** — map sections registry (a "map section in the GTG" is needed when
  adding maps); .bwd — compiled world data, bypassable (below); .qst
  (FinalAlbion.qst, GlobalQuests.qst) — quest registry; .stb ↓.
- **.stb** (compiled static map data) — PARTIALLY CRACKED ONLY (wiki
  `file_formats:stb` + forum th=809): archive header 'BBBB', dev listings,
  STATIC_MAP_COMMON_HEADER entry, 36-byte entry headers, DDS blocks; Hunter
  decoded the landscape vertex buffers (zig-zag strip order). Nobody has a
  full round-trip editor.
- **Bypassing the STB entirely** (forum th=10765 "Ditching STB & Levels"):
  custom maps without compiled data — copy .lev+.tng, add NewMap/NewRegion to
  FinalAlbion.wld (MapUID > 1,000,000, coords divisible by 32), add map
  section to the GTG, set `UseCompiledWorldFiles FALSE` in userst.ini (game
  then ignores .bwd and uses the .wld), rename FinalAlbion_RT.stb away.
  Remaining blockers at the time: landscape mesh + navdata editing.
- **Official pipeline** (map-editing tutorial, Google Doc by MakhnoBlazed):
  the **Unified Debug Build** of the game contains the original in-engine
  world editor (terrain painting, Thing Mode placement, Survey Mode
  passability/villager-preferability painting, region transitions via
  entrance/exit markers + OBJECT_REGION_TRANSITION_GATE); `dbugst.ini`
  (AllowDataGeneration) configures it; **EGO_R.exe** compiles a region to its
  `.stb`. Region integration chain: editor → FinalAlbion.wld + Region.def
  entry (Data/Defs/, matched by region name, references TXT id) → text.big
  entry (display name, via ChocolateBox) → text.h (Data/Defs/RetailHeaders/,
  sequential numeric text IDs). Debug build has a `~` console
  (SetTimeOfDay(n), EnableScreenEffectGlowRenderer FALSE…) and crashes if
  FinalAlbion_RT.stb is present when the editor opens.

## Saves & per-user data

- **Save game ("FableSave!")** — FULLY SPECIFIED (wiki
  `file_formats:gamesave`): magic + custom 32-bit ones-complement CRC;
  chunk 1 = zlib world state, keyed by CRC-of-string control bytes (world
  frame, save/teleport enables, marker pos, guild recall pos, region…);
  chunk 2 = TimeOfDay + ENTITIES/SAVED_ENTITIES per-map zlib blocks keyed by
  MapUID (from FinalAlbion.wld) + SAVED_NPC_NAMES + **PLAYER block (CRC key
  27C8AD96): PlayerCharacterUID + 8-byte player UID + current-map string** +
  QUESTS (lzo1x_999) + REGIONS + FACTIONS (lzo). Directly relevant to hero
  replication and to our per-client save sandboxing.
- **Profile data** — loaders at sub_40D350/sub_40BCA0 (see
  engine-internals.md); save parser sub_4A21F0.
- **Per-user directories the game resolves via SHGetFolderPathW** (both
  redirected per client by EgoMP's SaveRedirect):
  - `CSIDL_PERSONAL` → `My Games\Fable\` — Saves\<hero>\, Tattoos\, profiles
  - `CSIDL_APPDATA` → `Microsoft\Fable\` — comfront.dat / comback.dat /
    comback.rec: a double-buffered cache rotated during world load, opened
    R/W with **share mode 0** (this exclusive open is what crashed the second
    client before the redirect; not documented anywhere in the community —
    discovered by our CrashDiag 2026-07-18).

## Sound

- **.lug / .met** (Data/Sound) — .met documented on the wiki
  (`file_formats:met`); Met Edit tool exists (forum th=1666). The game opens
  every region's .lug/.met sequentially during world load (read-only,
  share-read — multi-client safe).

## Text

- **text.big** entries keyed by numeric IDs; `Data/Defs/RetailHeaders/text.h`
  maps TXT_* identifiers to sequential integers (last retail entry
  NarratorList = 28913).
