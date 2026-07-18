# Machine-readable reference data

Extracted from community sources for direct use in EgoMP code and tooling.

## `rtti-classes.tsv` — engine class RTTI (2090 classes)

Every polymorphic engine class with its **vtable address**, RTTI flags,
inheritance chain, and inheritance kind (SI/MI/VI). Scraped from the
fabletlcmod.com FableSDK thread (th=9198, EternalNoob 2012) and validated
against independently-published VFTs — `CThingPlayerCreature 0x012457FC` and
`CTCHeroStats 0x0124F70C` match to the byte.

Columns (tab-separated): `VFT  flags  class  bases  rtti_kind`.

Useful queries:
```sh
# vtable address of a class
awk -F'\t' '$3=="CPlayer"{print $1}' rtti-classes.tsv
# everything deriving (directly) from CTCBase
awk -F'\t' '$4 ~ /(^|, )CTCBase(,|$)/ {print $3}' rtti-classes.tsv
```

Dormant online/co-op classes worth investigating for Fable Online:
`CNetworkClient 0x0122EDAC`, `CNetworkServer 0x0122ED30`,
`CTCCoopSpirit 0x0125B264`, `CCoopSpiritDef 0x01232214`,
`CTCHeroOnlineScoreboard 0x0124DB34`.

## Def-name tables (from FableMenu `plugin/Database.cpp`)

Plain name lists, one per line. Resolve any name to its runtime def global
index via `CGameDefinitionManager::GetDefGlobalIndexFromName` (`0x9AD410`);
reverse via `0x9ACCC0`. These are game data (not FableMenu code).

| File | Entries | Contents |
|---|---|---|
| `objects.txt` | 2822 | OBJECT def names (items, weapons, props, markers, templates) |
| `creatures.txt` | 700 | CREATURE def names |
| `particles.txt` | 1165 | particle-effect names |
| `creature-animations.txt` | 232 | creature animation names |
| `quests.txt` | 189 | quest script names |
| `brains.txt` | 113 | BRAIN (AI) def names |
| `creature-modes.txt` | 96 | creature-mode names |
| `holy-sites.txt` | 95 | holy-site names (targets for `TeleportHeroToHSP` `0x4A0940`) |
| `attack-styles.txt` | 89 | attack-style names |
| `creature-weapons.txt` | 86 | creature weapon OBJECT names |
| `player-modes.txt` | 48 | `EPlayerMode` names — **line index = enum value** (0 NULL, 1 CONTROL_CREATURE, 3 DEAD, 17 FREEZE_CONTROLS, 24 CONTROL_SPIRIT) |
| `expressions.txt` | 38 | expression names |
| `factions.txt` | 30 | FACTION def names |

Example — the crossbow family (relevant to the weapon-replication "items on
the floor" bug) is in `objects.txt`: `OBJECT_YEW_CROSSBOW`,
`OBJECT_OAK_CROSSBOW`, `OBJECT_EBONY_CROSSBOW`, `OBJECT_CRYSTAL_CROSSBOW`
(+ `_PUMPCROSSBOW` variants), plus `OBJECT_PROJECTILE_WEAPON_CROSSBOW_TEMPLATE`.
