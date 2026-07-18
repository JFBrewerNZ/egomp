# EgoMP knowledgebase

Distilled community + reverse-engineering knowledge about Fable: The Lost
Chapters (Steam PC, x86, image base 0x400000, no ASLR), collected to speed up
Fable Online development. Compiled 2026-07-18 from the sources below; each
document cites where its facts came from.

| Document | Contents |
|---|---|
| [engine-internals.md](engine-internals.md) | Memory addresses: singletons, class/struct layouts, engine function addresses, byte patches, debug leftovers. Merged from FableMenu source + fabletlcmod.com RE threads, cross-checked against our own findings in `../Tools/RE-NOTES.md`. |
| [file-formats.md](file-formats.md) | On-disk formats: .big, .bin/defs, .lev, .stb, .wad, .tng/.wld/.gtg, saves — what is fully specified where, and what is still uncracked. |
| [def-system.md](def-system.md) | The CompiledDefs definition system: def types, CRC-hashed field IDs, entry-ID lists, hidden in-game scaling constants, quest-gated TNG sections. |
| [community-resources.md](community-resources.md) | Tool catalog with links, modding-community docs, multiplayer prior art, and the high-value research backlog (Mac debug symbols, RTTI dump, Fable SDK). |
| [data/](data/README.md) | Machine-readable reference data: the full RTTI class→vtable table (2090 classes) and 13 def-name lists (objects, creatures, brains, holy sites, player modes…) for use in code. |
| [platform-fixes.md](platform-fixes.md) | Community fixes for the platform-level problems under EgoMP — AMD load crash, windowed mode, cursor, multi-core timing — and how EgoMP integrates with them (dgVoodoo2 coexistence, built-in CPU affinity). |

Our own first-party reverse engineering lives in
[`../Tools/RE-NOTES.md`](../Tools/RE-NOTES.md) (equipment/inventory/animation/
input systems, discovered with the scripts in `../Tools/`). This knowledgebase
is the third-party complement.

## Headline facts for Fable Online

- **The Mac port of Fable TLC (Feral) shipped with full debug symbols** — every
  engine function named. Porting those names onto the PC exe is the single
  biggest reverse-engineering accelerator available. (Sourcing the Mac binary is
  on the backlog.)
- **The retail exe contains dormant network code**: RTTI proves `CNetworkClient`
  (VFT `0x0122EDAC`) and `CNetworkServer` (VFT `0x0122ED30`) exist, alongside
  co-op leftovers (`TCI_COOP_SPIRIT`, `TCI_HERO_ONLINE_SCOREBOARD`,
  `CWorld::GetPlayer(int)`, per-player numbers, `FACTION_CTF_*`). The engine was
  built with multiple players in mind; nobody ever wired it up.
- **FableMenu (ermaccer) is a verified address goldmine** for this exact Steam
  binary: hundreds of engine function addresses including
  `CreateThing`/`CreateCreature`, cross-region `SetPlayerPos`,
  `CPlayer::SetControlledCreature` (possession!), a scripted-action API, and a
  proven ImGui D3D9 overlay recipe. **No license on that repo** — use it as
  reference/cross-check, do not vendor its code.
- **No one has ever shipped Fable multiplayer.** The community's best minds
  scoped it in 2007 (hook player spawn/position/animation/input, proxy remote
  players as NPC puppets — exactly EgoMP's architecture) and declared it
  impractical. EgoMP is already past that line.
