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

## Animation sync RE (2026-07-11, anim_scan.py / anim_disasm.py / anim_vfuncs.py)

Goal: mirror the local hero's animations on remote puppets via the generic
`CCreatureAction_PlayAnimation` (NPCs are driven by it constantly).

**Animation identity = NAME.** Anim-name key ctor `0x99EBF0(this, name, idx)`
has **25,448 call sites** — names ("ANIM_HERO_WILL_FIREBALL_ARMED_RELEASE",
"ANIM_..." strings all over .rdata) are the game's own anim currency. The key
is 8 bytes; dword0 = refcounted 0x11-byte node allocated by
`0x99EA60(name, idx)` (node+0x0 = name ptr (probable, verify live), +0xD =
dword refcount, global count @0x13BD800). Key dtor/release `0x99EAE0`.

**PlayAnimation-family classes** (RTTI): PlayAnimation (vtable `0x1273A0C`),
PlayAnimationFromIndex (`0x127507C`), PlayAnimationWithLookTurning,
PlayIntoLoopOutOfAnimation (`0x1273B2C`), PlayConversationAnimation — all
share base ctor `0x693B30` (7 args, ret 0x1C). PlayCombatAnimation
(`0x127A604`) is DIFFERENT: shares the combat-action base ctor `0x858030`
(same one the roll uses, mode 0x32) via ctors `0x8B7F80`/`0x8B7FF0`; its anim
identity offset is unmapped (tracer raw-dumps it live).

**Action layout** (base ctor 0x693B30 + full derived ctor):
`+0x08` creature CIntelligentPointer, `+0x20/+0x24` caller dwords,
`+0x34/+0x38` the 8-byte anim key (name handle + extra), `+0x4C/+0x50` = -1,
`+0x74` optional context CThing* (via `0x693030`), `+0x90` heap string node,
`+0xA8..+0xAB/+0xB0` flag bytes, `+0xAC` loop count.

**Full ctor `0x8425E0`** (11 args, ret 0x2C, __thiscall on a ~0xB8 stack
buffer): `(creature, context, bAA, bAB, loops, animKey*, bA8, bA9, bB0,
d20, d24)`. Decoded from the repost vfunc `0x843B40` (slot 4), which
re-posts a follow-up PlayAnimation from a finished one's fields and is a
perfect replay template: ctor -> `DoCreatureAction 0x6644F0` -> dtor
`0x693EF0`. Simpler ctors: `0x8424F0` (121 callers; resolves an extra arg
via `0x6924B0` -> `0x662FA0(creature, x, 1)`), `0x842570` (12 callers,
pre-resolved).

Other landmarks: base slot 12 `0x694D10` = completion (restores creature
modes 0x24/0x25/0x1F/0x23 via `0x70D8C0`); slot-4 base `0x694430` =
interrupt/cleanup (auto-unsheathe etc.); `CTCShapeManager` vtable
`0x126A2A4` (29 vfuncs, ctor refs 0x7620A1/0x76211E) — not needed for v1.

Implemented (protocol v10): `Core/SDK/Fable/AnimAction` (Extract/Play),
tracer v2 logs `[AnimAction]` lines w/ names + raw-dumps each family class
once, `ID_PLAYER_ANIM` [netId][d20][d24][keyExtra][loops][5 flag bytes]
[nameLen][name], NUMPAD9 = replay last capture on local hero (alternates
context=null / captured). Open questions for the live session: does the
HERO emit PlayAnimation-family actions for attacks (or only NPCs)? Which
name-recovery interpretation wins? Are d20/d24 floats or pointers?

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

## Input / window system RE (2026-07-15) — mouse grab & window dragging

Static analysis (pefile+capstone probes; no debugger). Addresses are file =
runtime (no ASLR, base 0x400000).

- **DirectInput**: single import `DirectInput8Create`, called once at
  0xA60093 (wrapper obj stores IDirectInput8* at +0x80). Mouse init fn =
  0xAB5710, `__thiscall(inputObj; bool exclusive)`:
  - CreateDevice(GUID_SysMouse @0x12ACAA4) at 0xAB574B.
  - exclusive!=0 branch: `SetCooperativeLevel(hwnd, 5)` (EXCLUSIVE|FOREGROUND)
    via `push 5` at **0xAB5786** — the game's ONLY mouse coop call, and the
    thing that blocks title-bar dragging. Sets mode byte `inputObj+0x343C=1`.
  - exclusive==0 branch (retail-unused): flags 0xA (NONEXCLUSIVE|BACKGROUND)
    at 0xAB583D, clears +0x343C.
  - hwnd source everywhere: `0x9A4EC0()` (engine window obj) `+0x94`.
  - Keyboard: GUID @0x12ACA94, coop flags **6** at 0xAB6484 (already
    non-exclusive). Joystick setup fn 0xAB6B10 uses flags 5 at 0xAB6B78 +
    DIPROP range/deadzone — leave alone (joystick exclusivity is harmless).
- **Mode byte +0x343C** gates the cursor strategy in the mouse update fn
  (0xAB58E0, runs per frame): 1 = read buffered DI events (0xAB4910), sync
  renderer cursor from accumulated floats +0x3414/+0x3418; 0 = read the real
  OS cursor (GetCursorPos+ScreenToClient, 0xAB4BB0/0xAB5030). Byte
  **+0x343E** (ctor-init 0) gates a SetCursorPos recenter-to-window-center
  block (0xAB5BEB). Byte +0x343D=1 ctor-set. The input ctor (~0xAB5D05) also
  calls `ShowCursor(exclusive ? FALSE : TRUE)` once at 0xAB5D8F (display
  count -1 in retail mode).
- **EgoMP mouse_unlock patch**: flip the `push 5` immediate at 0xAB5787 to 6
  (NONEXCLUSIVE|FOREGROUND). DI relative data is identical non-exclusive, and
  +0x343C stays 1 so the game keeps its retail cursor path; only Windows'
  own frame handling comes back. Deliberately NOT flipping the init bool —
  that would switch the game onto the untested GetCursorPos path. A
  WM_SETCURSOR-only subclass restores the ShowCursor count (arrow visible on
  frame) and hides the arrow over the client area (Core/Display/MouseUnlock).
- **Game window**: class registered at 0x9A6551 (RegisterClassExW), class
  cursor = IDC_ARROW, WndProc = **0x9A5B60**. WndProc dispatch:
  - optional pre-filter callback at `[0x13CA798]+0x178` gets every message
    first (returns handled flag) — installable engine hook, none seen.
  - msgs 1..8 jump table 0x9A5F7C: CREATE stores obj, DESTROY sets quit flag
    +8, ACTIVATE→0x9A5A50(bool), SETFOCUS/KILLFOCUS→0x9A5A30(bool) + sound
    volume ramp, SIZE handles min/restore like activate.
  - msgs 0x1C..0x105 byte-table 0x9A5FAC → 4 handlers @0x9A5F9C: only
    ACTIVATEAPP (→0x9A5270(bool)), WM_CHAR (text input), SYSKEYDOWN/UP
    (swallow VK_MENU/VK_F10). **Everything else → DefWindowProcW** (incl.
    WM_SETCURSOR, WM_NCLBUTTONDOWN, WM_CLOSE, WM_NCHITTEST).
  - WM_SYSCOMMAND (0x112) handler 0x9A5E36 swallows only SC_SCREENSAVE and
    SC_MONITORPOWER; SC_MOVE/SC_SIZE/SC_CLOSE reach DefWindowProc, so native
    dragging works once the mouse grab is gone.
  - Mouse buttons/wheel are WINDOW-MESSAGE driven: 0x201/0x202/0x204/0x205
    set state bytes obj+0xDD/+0xDF; 0x207..0x218 second table @0x9A60B4
    (MBUTTON +0xDE, XBUTTONs +0xE0/+0xE1, wheel accumulates obj+0xE4).
    Only mouse MOTION comes from DirectInput.
- `ClipCursor` is NOT imported. SetCursorPos sites: 0xAB47A0 (sync OS cursor
  to game cursor, only when +0x343C==0) and 0xAB5BEB (recenter, gated by
  +0x343E). Second window class @0x99E0BC (DefWindowProcA proc, own
  SetCapture/SetCursor at 0x99D6xx) is a separate dialog-ish window.
- Pause-on-focus-loss leads for later: WM_ACTIVATE→0x9A5A50,
  WM_ACTIVATEAPP→0x9A5270, focus→0x9A5A30 (volume + [+0x170] callback).

## Carried-weapon visuals & why crossbows drop to the floor (2026-07-18)

Investigated with Tools/disasm.py (annotated against docs/data/symbols.tsv)
after Jamon's observation that crossbows never show on a hero's back.

**RestoreCarriedWeapons `0x5C8101`** (= "build sheathed/back visuals"): clears
old visuals (`0x5C552C`), then for the melee holder (`weaponsTC+0x134`) and the
ranged holder (`weaponsTC+0x141`) — if the held CThing `isThingAlive`
(`0x4CC340`) — derefs the IntelligentPointer (`0xA01B50`) and calls the SAME
builder `0x5C36C2(creature, weaponThing)`. **No weapon-type branch here**; melee
and ranged are treated identically.

**Builder `0x5C36C2`** resolves the weapon's def (`0x42B1A6(weapon+0x70, &def)`)
and reads **`def+0x38`**. That value is used BOTH as a gate — `cmp/jle` at
`0x5C37AC`/`0x5C37B4` skips the whole create-visual block when `def+0x38 <= 0`
— AND, inside the block, as the **def-global-index passed to `CreateThing`**
(`0x5C37E0` `mov ecx,[def+0x38]` → `0x5C37E3` CreateThing) that spawns the
on-back mesh, which is then `SetInLimbo` + `AddThingInCarrySlot` (carry slot
0x47). So:

> **weapon def `+0x38` = the def index of the weapon's on-back/sheathed visual
> object; `0` = the weapon has no back visual.** Swords/longbows have one;
> crossbows (per Jamon, confirmed by this gate) have `+0x38 == 0`, so the game
> builds no back model for them — correct retail behaviour.

**CreateCarriedWeapon `0x5BE8F3`** (what our puppet apply calls directly via
CreateCarriedWeaponUnchecked): after the ownership gate (`0x5BDF08`), it
factory-creates the weapon at `GetPosition(creature)` with mode
`byte[creature+0x90]` (`0x5BE93C` CreateThing), then — record NULL for puppets
(our 34ebde1 bypass) → skips all the record/augment/section copies
(`je 0x5BEA8F`) — and returns the created CThing. **It never limbos or attaches
that object itself.**

Consequence for puppets: ApplyNetPlayerWeapons creates a carried weapon for the
ranged slot unconditionally. For a crossbow the game then builds no back visual
(def+0x38==0), so nothing consumes the factory-created weapon object → it is
the crossbow left at the puppet's feet. FIX DIRECTION: gate ranged-carried
creation on def+0x38 > 0 (skip crossbows); show crossbows only via the wield /
animation path. NEEDS a live probe to confirm def+0x38 on a real crossbow def
and the in-world state of the created object before shipping (weapon changes
here have a history of deferred crashes — verify, don't assume).
