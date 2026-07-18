# The def system — CompiledDefs semantics

Source: "The Alright Library of Alexander(ia)" by Alexander The Alright
(https://docs.google.com/document/d/1ODjpkoo_wSMFJuoPgCmICMmfDzXrgJNmMLeq5ylHg38),
distilled 2026-07-18. That doc is the def-editing bible (FableExplorer /
ChocolateBox workflow). Binary container layout: see
[file-formats.md](file-formats.md) (.bin); runtime access: our SDK's
CDefinitionManager + `GetDefGlobalIndexFromName` (engine-internals.md).

## Model

- Game data = typed def records (OBJECT, CREATURE, THING, VILLAGE, BUILDING,
  BRAIN, FACTION, ARMOUR, HIT_LOCATION, EXPRESSION, HERO_ABILITY,
  OBJECT_FAMILY, CREATURE_GENERATION_FAMILY, GLOBAL, plus C*Def sub-records
  attached to them: CInventoryItemDef, CStockItemDef, CBonusItemDef,
  CAugmentationDef, CObjectAugmentationsDef, CAppearanceModifierDef,
  CCreatureDef, CAppearanceDef, CSummonableCreatureDef, CTurncoatDef,
  CHitLocationDef, CPerceivedThingDef, CEnemyDef, CContainerRewardHeroDef,
  CWeaponDef, CShopDef, CWifeDef, CHeroMarriageDef, CHeroMorphDef,
  CHeroSuitDef, CBuyableHouseDef, CVillagePeopleDef, CScriptDef,
  SPECIAL_ABILITIES_*_DEF/_INSTANCE, …).
- Records are referenced by **integer entry ID** (global index) or by **symbol
  name** (`OBJECT_…`, `CREATURE_…`). Fields are identified by **CRC hashes**
  (the 8-hex-digit codes below) — display names differ between tools, the CRC
  is canonical. Voices reference **names.bin offsets**; icons/meshes reference
  **texture/mesh IDs in the .big archives**.
- EgoMP already replicates appearance-modifier def indexes + weapon def
  indexes over the wire; this table system is how everything else (items,
  spells, creature stats) is looked up.

## Hidden in-game scaling constants (important when syncing/displaying values)

| Value | Scale |
|---|---|
| CREATURE Health / Damage | def × **10** in-game |
| Agreeableness / Attractiveness / Scariness changes | def × **~50** |
| Armour displayed | ArmourPercentage × ArmourTypeValue (Naked 384.5, Dress 416, Villager 500, Bandit 526, Guard 588, Leather 666, Chainmail 833.3, Platemail 1000, Platinum 1110) |
| Consumable exp | def × current combat multiplier (+ Experience augments) |
| Marriage gift chance | def × 10 |

## Frequently useful CRC field IDs

Items (CInventoryItemDef): `C8636B2E` icon/model graphic (also CREATURE model),
`FA2A946A` title, `619E4A2F` description, `51FF3A2D` InventoryCategory,
`A73045D5` stack size, `242F0BB6` IsSellable, `E7670E10` IsBuyable (also
BUILDING), `C96EBF35` IsConfiscatable. CStockItemDef: `575B3970` DefaultPrice,
`782B1D3` DefaultIsStealable, `140193AF` CanBeDisplayedInShop.

Consumables (CBonusItemDef): `8EC6F7A7` HealthModifier, `73B1FB91`
StaminaModifier (Will), `E501B4CC` SecondsToApplyHealth, `383E7115`/`EA65B60B`/
`4814B110` Will/Skill/Strength exp, `745D231A` MoralityChange, `7657161D`/
`C0E30E2B` MaxHealth/MaxStamina increase, `1C68E63E` SpeedMultiplier (anim
speed; **0 can freeze the character or game**), `56873C64` ParticleEffect.

Augments (CAugmentationDef): `B27A80D` Type (**bitflag, powers of 2**;
1 Sharpening, 2 Silver, 4 Fire, 8 Lightning, 16 Piercing), `DFA5CC9C`
DamageMultiplier, `5356DDC7` ExperienceMultiplier, HP/Will regen pairs
`6FBD733`/`12C34935` and `ABEECD0A`/`71A14B21`. Entry IDs: 2877 Sharpening,
2878 Piercing, 2879 Silver, 2880 Fire, 2882 Lightning, 2883 Experience,
2884 Health, 2885 Mana.

Armour: CAppearanceModifierDef (edit in ChocolateBox — FableExplorer throws
Control-Bytes-Mismatch on it): worn mesh ("Appearance Clothing Model"),
clipping-prevention body-part hiding, body-placement bitmask (Feet=1 Legs=2
Hips=4 Torso=8 Head=16 Arms=32 Hands=64 LowerFace=128 MiddleFace=256
UpperFace=512), Removable, HeroSuit/SuitPart (1 head…5 boots),
ArmourDefIndex + ArmourPercentage (0 = invincible). ARMOUR entry: `4F1221A7`
AugmentationResponse (per-augment-type multiplier array; 0 immune, 2 = double
weak), `E8DCE48C` DamageTypeResponse keyed by damage type (0 melee, 1 unarmed,
2 lightning, 3 fire, 4 projectile, 5 explosion, 6 drain life, 8 force push,
9/10 Infernal Wrath/Divine Fury), `C8D6664E` AllHitsNegated, `DF96499`
AllHitsKnockdown, `B33E7C99` AllHitsCauseRecoil. Enemies/bosses have ARMOUR
entries too.

Creatures (CREATURE): `8687B478` Health, `F9F0A6A7` unarmed Damage,
`E8590FBA`/`94329232`/`2A2015C5` primary-melee/secondary-melee/ranged weapon
def IDs, `6F56ABB8` PBrain (BRAIN entry). Attached: CCreatureDef (`237DCBE3`
RandomAppearanceMorph body-part pools, `B3BFA450` Stats = morality/exp/renown
rewards, `D84DE818` Pickpocketable), CSummonableCreatureDef (CDefDataEntry
8912 = unsummonable), CTurncoatDef (8913 = immune), CPerceivedThingDef (FOV,
sight/sound radius, chase radius), CEnemyDef (FACTION),
CContainerRewardHeroDef → OBJECT_FAMILY loot tables (RewardObjectID +
RewardObjectOccuranceRate weights; NULLDEF = no-drop). CWeaponDef on a
creature does nothing. Speed/Dexterity/StrengthDamageMultiplier fields do
nothing.

Spawns: CREATURE_GENERATION_FAMILY = array of CREATURE IDs + `873A4F8E`
Difficulty (hidden tier vs hero's total exp, max 10); placed via
MARKER_CREATURE_GENERATOR things whose CTCCreatureGenerator names the family.

Spells: HERO_ABILITY (`7563DA0F` StaminaCost per level, `1B8AFCFA` upgrade
costs, `6613D645` MoralityCostFactor, `DF58D974` Aggressive); mechanics in
SPECIAL_ABILITIES_<NAME>_DEF_INSTANCE (per-spell schemas).

Shops: CShopDef `EE2FC7F1` DefaultStock (item ID, qty, price mult, max stock,
restock days, sales/day…), `C881A257`/`3F9C4FFC` buy/sell multipliers,
`5C38C8A8`/`DBA1D20B` attitude-based price arrays (12 OPINION_ATTITUDE slots:
1 Renown, 3 Goodness, 5 Evilness…). Known shop entry IDs list is in the source
doc (e.g. Bowerstone South General Store 9420, Oakvale Blacksmith 9570).

Misc: EXPRESSION `1740FCAE` SheatheWeapons; GLOBAL→DEFAULT_GLOBAL_DEFS
`463C44A1` AgeIncreasePerLevelUp (0.7); Demon Doors in script.bin
(DemonDoor_* fields; **expand CScriptDef with the plus — clicking the root
crashes FableExplorer**); CBuyableHouseDef price/rent arrays; CVillagePeopleDef
spawn weights + guard respawn tiers; CWifeDef/CHeroMarriageDef marriage
mechanics.

## TNG sections & quest gating (relevant to shared-world design)

Things in a .tng belong to a **Section**; sections named after quests
(`Q_HobbeCave`, `Q_WaspBoss`…) spawn only in the matching quest state. Any
map can gain a section:

```
XXXSectionStart Q_WaspBoss;
XXXSectionEnd;
```

Useful gating states: `DummyQuestForScarletRoseStatue` (post-Human-Jack),
`Hook_Fresco_12_KilledDragon` (post-Dragon-Jack), `Hook_Fresco_01/02_*Epilogue`
(evil/good ending), `Q_BowerstoneTownLifeIntro` (pre-Arena only),
`Hook_Fresco_13_KilledScorpion` (post-Arena), `Hook_Fresco_06_Prison`
(post-Bargate), `V_SickChild_Activate`. For multiplayer this is the vanilla
mechanism by which two clients on different quest states see **different
world content** — a sync-design consideration.

## Known crash pitfalls (from the community, worth remembering)

- Loading `OBJECT_PIE_STRAWBERRY_01` crashes the game (unused item).
- SpeedMultiplier 0 can freeze the character or game.
- Tooling: clicking script.bin's CScriptDef root in FableExplorer crashes it;
  CAppearanceModifierDef needs ChocolateBox.
