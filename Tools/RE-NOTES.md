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
2. Read the worn-item state: with component pointers in hand, inspect the
   components themselves (extend the debug key or add a second one) to find
   where equipped def indexes live; cross-check against the save-field
   names above.
3. Identify apply functions: candidates are vfuncs on the two inventory
   vtables and the creature-action constructors. Verification plan: extend
   the debug key to invoke one candidate on the local hero with a known def
   index and log — iterate until the right function is confirmed
   (crash-safe: try in a throwaway save).
4. Wire protocol: extend announce/create payloads with an equipment blob
   (n slots x def index), new ID_PLAYER_EQUIPMENT message on change
   (hook the inventory-changed path once found).
