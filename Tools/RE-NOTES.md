# Reverse-engineering notes — equipment/clothing sync

Target: sync equipped clothing/weapons of remote heroes.
Binary: Steam Fable TLC `Fable.exe`, image base 0x400000 (no ASLR-relevant
relocation observed — the existing mod hooks hardcoded VAs successfully).

Tooling: `rtti_scan.py` (MSVC RTTI walk: TypeDescriptor -> COL -> vtable, plus
string-xref scan), `callgraph.py` (capstone: function boundary + call targets
around an address). Run with any Python 3 + `pip install pefile capstone`.

## Findings (2026-07-10)

Hero inventory is composed of Thing Components (RTTI class names in binary):
`CTCInventoryClothing`, `CTCInventoryWeapons`, `CTCInventoryItem`, plus
Abilities/Magic/Stats/etc. Wielding/sheathing is performed via creature
actions: `CCreatureAction_SheatheWeapons`, `CCreatureAction_SheatheItemToInventory`.

Key addresses:

| What | VA |
|---|---|
| CTCInventoryClothing vtable (87 vfuncs) | `0x124356C` |
| CTCInventoryWeapons vtable (88 vfuncs) | `0x1253FF4` |
| CTCInventoryItem vtable (28 vfuncs) | `0x1240004` |
| CTCInventoryWeapons save/load serializer | `0x5C3A95` (refs "MeleeWeaponCarried", "RangedWeaponCarried", "PreviouslyWielded*DefName") |
| Hero save fields serializer (weapon equipped) | `0x6CA120` (refs "LastWeaponEquippedID") |
| CCreatureAction_SheatheWeapons vtable (81 vfuncs) | `0x1282CEC` |
| CCreatureAction_SheatheItemToInventory vtable (70 vfuncs) | `0x125C83C` |

Save-field names confirm the state we need to replicate per player:
`MeleeWeaponCarried`, `RangedWeaponCarried` (visible on back),
`LastWeaponEquippedID` (wielded), clothing worn per slot
(`INVENTORY_CATEGORY_CLOTHES_{HEADWEAR,TORSO_WEAR,LEGWEAR,GLOVES,FOOTWEAR,SUITS}`).

## Live-probe findings (2026-07-10, NUMPAD5/6/7 on a late-game hero)

**Thing-component list** at `CThing+0x44` -> array of `{int typeId, CTC* tc}`
pairs. Hero's relevant entries: CTCHeroMorph=0x03, CTCInventory=0x11,
**CTCInventoryClothing=0x12**, **CTCInventoryWeapons=0x13**, CTCHero=0x29,
CTCCarrying=0x46, CTCGraphicAppearance=0x5B (also at `CThing+0x64`),
CTCHeroAttachableAppearanceModifiers=0x5E. `CThing+0x70` = CThingCreatureDef.

**CTCInventoryClothing** (per NUMPAD7 dump): `+0x004` owner creature,
`+0x068` CInventoryDef. ~~`+0x14C` worn-armour slot array~~ — **wrong**:
that pool tracks armoured *world objects* too (guild bookcases matched as
"worn clothing"); do not use it.

**Per-category inventory records** (the real item storage; found in pools
referenced by the TC, but the pool pointers move between sessions — locate
records by scanning TC-referenced pools for the CInventoryCategoryDef
anchor). Record = 0x2C bytes:
`{+0x00 itemVec.begin, +0x04 itemVec.end, +0x08 itemVec.cap,
+0x0C CInventoryCategoryDef*, +0x10 owner*, +0x14 categoryId
(clothing: 0xA4C..0xA50), +0x18 selectedIndex (-1 = none), +0x1C 0}`.
Item-vector entry layout TBD from next NUMPAD8 run (expected ~3 dwords per
entry: item CThingObject*, count, flags — worn state presumably per-entry
flag or the record's selectedIndex).
Trivia: the pools contain leftover Lionhead dev strings
("C:\Documents and Settings\rshaw\Desktop\TestOutputDir\...wav").

**CTCInventoryWeapons** (serializer disassembly @0x5C3A95): `+0x140`
MeleeWeaponCarried, `+0x150` RangedWeaponCarried (both look like
CIntelligentPointer&lt;CThing&gt;, 0x10 bytes), `+0x13C` area =
ActiveMelee/RangedWeaponDefIndex2, `+0x160/164/168/16C` =
Confiscated/PreviouslyWielded def-name ints.
CTCInventoryClothing's own serializer (@0x5B369A) only writes "InGameSave"
and delegates — the clothing list is serialized by the base
CTCInventory::Serialize (@0x591A66), not yet mapped.

## Creature-action API (2026-07-11) — the "make a creature do X" mechanism

Universal pattern (seen at every action call site):
```
sub esp, ~0xB0..0xBC        ; action constructed in a STACK buffer
push <args>                 ; action-specific
lea ecx, [esp+X]
call <action ctor>          ; returns eax = action*
push eax
mov ecx, <creature>         ; CThingCreatureBase*
call 0x6644F0               ; DoCreatureAction(action*)  <- THE entry point
lea ecx, [esp+X]
call 0x693EF0               ; action dtor (some sites use a specific dtor)
```

| Action | vtable | ctor | notes |
|---|---|---|---|
| CCreatureAction_GiveItemToThing | `0x125919C` | `0x62D9E0` | 3 args (two CIntelligentPointers at +0xA8/+0xB0: item, recipient); transfers an EXISTING item Thing |
| CCreatureAction_AddRealObjectToInventory | `0x1270D5C` | `0x7EB2D0` | args (thing, creature-ish); pickup of an existing world object |
| CCreatureAction_UnsheatheItemFromInventory | `0x125CCBC` | `0x69F0F0` | callers 0x69F213/0x7A7C5F/0x8B970E/0x8B97AE — draw weapon |
| CCreatureAction_SheatheItemToInventory | `0x125C83C` | `0x69EF80` | callers 0x69F079/0A9/0D9, 0x72CC96 |

Wield-state members (weapons serializer @0x5C3A95 load path):
`ActiveMeleeWeaponDefIndex2` @ weapons TC **+0x13C**,
`ActiveRangedWeaponDefIndex2` @ **+0x14C**. Carried IPs at +0x140/+0x150
deserialize generically (0x4107C0) and re-link by UID — the weapon Things
belong to the world/save, NOT created by this component. So giving a
remote creature a weapon still needs an **object-from-def factory**
(CThingObject creation; next RE target — trace CThingObject ctor callers
or chest/spawner code).

## Next steps

1. ~~Locate the hero's TC components~~ → **probe built** (2026-07-10):
   `Core/Debug/ObjectInspector` identifies every pointer member of a live
   object by its RTTI class name at runtime (vptr -> COL -> TypeDescriptor),
   one indirection deep, so component lists show up too. Set
   `debug_keys=1` under `[general]` in EgoMP.ini, load into the world, and
   press **NUMPAD5**: the local hero `CThingPlayerCreature`'s first 0x600
   bytes are mapped to the console and appended to `EgoMP-inspect.log`
   (untracked, next to the DLL). Expected output: member offsets of
   CTCInventoryClothing / CTCInventoryWeapons / CTCHeroMorph etc. — those
   offsets become SDK struct members.
2. ~~Read the worn-item state~~ → **solved**, see "Live-probe findings".
   **NUMPAD8** (Core/DevTools/EquipmentProbe) logs the def names of all worn
   clothing and carried weapons — the send-side of equipment sync, working.
3. Identify apply functions — **probe built**: **NUMPAD9** calls the next
   of six unidentified 1-arg CTCInventoryClothing virtuals (slots
   12/26/36/11/0/77, chosen by arity scan + worn-array-usage ranking) with
   worn piece 0 as the argument, SEH-guarded, logging slot+address before
   the call. Protocol: press NUMPAD9, watch the hero and console, note the
   slot number of anything interesting (clothes falling off = found the
   unequip/toggle path). Use a throwaway save. Slot 77 references
   HERO_SUIT_NAKED / OBJECT_HERO_NO_HAT and reads the worn array — suspected
   "rebuild visible clothing from worn set".
4. Wire protocol: extend announce/create payloads with an equipment blob
   (n slots x def index), new ID_PLAYER_EQUIPMENT message on change
   (hook the inventory-changed path once found).
