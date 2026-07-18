# Engine internals — addresses, layouts, functions

Target: Steam Fable TLC `Fable.exe`, x86, image base `0x400000`, no ASLR.

Sources, in citation shorthand:
- **[FM]** FableMenu v0.6 source (ermaccer, github.com/ermaccer/FableMenu — "tested
  only with Steam version"; NO license, reference only)
- **[EN]** EternalNoob's statics + layouts, fabletlcmod "Executable Functions"
  thread (th=4125) and Fable SDK thread (th=9198), 2012
- **[KE]** Keshire's function list, same th=4125, 2007
- **[BL]** blastedt, th=4125 + th=11080, 2019
- **[RE]** our own `../Tools/RE-NOTES.md`

Cross-confirmations between independent sources are marked ✓✓. Verify a couple
of addresses against the live binary before trusting a source wholesale
(FableMenu was verified compatible by inspection, not execution).

## Global singletons / statics

| Address | What | Source |
|---|---|---|
| `0x13B86A0` | `CMainGameComponent*` (+28 `CPlayerManager*`, +36 `CWorld*`) | [FM][EN] ✓✓ |
| `0x13B879C` | `CGameDefinitionManager*` | [FM][EN] ✓✓ |
| `0x13B89FC` | `CQuestManager*` | [FM][EN] ✓✓ |
| `0x13B8A1C` | `CThingManager*` (+0x1C CMainGameComponent, +0x20 CGameDefinitionManager, +0x24 CWorld, +0x28 CWorldMap, +0x30 CPlayerManager) | [FM][EN] ✓✓ |
| `0x13B8790` | `CHUD*` | [FM] |
| `0x13B7D4C` | `CUserProfileManager*` | [EN] |
| `0x13B83D0` | `CGame*` | [EN] |
| `0x13B878C` | `CPlayerDef*` | [EN] |
| `0x13BA854` | `CEngineManager*` | [EN] |
| `0x13B8384/88/90/94` | CRenderManager / CInputManager / CDisplayManager / CSoundManager | [EN] |
| `0x13BCA10` | GameDirectory string | [EN] |
| `0x13B89BC` | `int CWorld::ms_curFrame` | [FM] |
| `0x143E910` | script-info manager instance (method `0xCD39B1` = get CScriptInfo by name) | [FM] |
| `0x13CAA38` | `CAProgressDisplay* PProgressDisplay` (zero → region load without progress screen; re-init `0x413120`, alloc size 179) | [FM] |
| `0x138306C` | `int` summon-spell creature def index | [FM] |
| `0x13833D4` | `float CTCAIScratchPad::TradingPriceMult` | [FM] |
| `0x13B8650` | `CVector GOverridePlayerStartPos` (+`0x13B8647` bool FromConsole) | [FM][RE] ✓✓ (already in our SDK) |

FGlobals: `0x13B89D1` GDoNotCallStartAutoSaveProgress, `0x13B89C1`
GDisplaySavingGameState, `0x13B85F3` GUsePassiveAggressiveMode, `0x13756EA`
GUseRubbishMovementMethod. [FM]

**Retail debug-console globals (left in retail!):** `0x13B86C7` HeroGodMode,
`0x13B86C8` EnemyGodMode, `0x13B86E5` EnableHeroJump, `0x13B86C5`
EnableHeroSprint, `0x1375756` EnableUpdateAI, `0x1375759` EnableUpdateObjects,
`0x1375754` EnableParticles, `0x13756E9` EnableHeroThingCollision, `0x13B86CA`/
`0x1375710` ForcePrimitiveFadeDistance/PrimitiveFadeDistance, `0x13B86D5`
GCombatStressTestDebug, `0x13BAF18` ConsoleOverrideMultiplier (float),
`0x1375741` GEnableRegionLockingSaveSystem (1→0 = leave quest zones freely
[BL]). [FM]

**Display/window statics:** `0x137544A` engine's built-in windowed-mode flag,
`0x137545C`/`0x1375460` default X/Y resolution. [FM] (We currently force
windowed via a CreateDevice hook; the built-in flag is a candidate
simplification — needs testing against the init-failure relaunch behaviour.)

## Structure layouts

- **CThing**: `+0xB0 float m_fMaxHealth`, `+0xB4 float m_fHealth` [FM][EN] ✓✓.
  Thing-component list at `+0x44` (array of `{int typeId, CTC* tc}` pairs)
  [RE]; FableMenu says "component map at +68/+72" — +68 decimal == +0x44 ✓✓
  (tree-lookup helper fn `0x40F020` [FM]).
- **CTCBase**: `{int unk; CThing* m_pParentThing; bool m_bToBeRemoved; bool m_bIsInGlobalUpdateTCs;}` [FM]
- **CPlayer**: `+0x9 bool m_bPlayerIsCharacter; +0x28 int m_dNumber; +0x34
  CIntelligentPointer → +0x4 CThingPlayerCreature` [EN]; `+0x208 bool m_bLocal;
  +0x210 std::list<EPlayerMode>; +0x21B bool killMode` [FM]. Full 48-value
  `EPlayerMode` enum in FableMenu `Player.h` — notable:
  `PLAYER_MODE_CONTROL_CREATURE=1`, `PLAYER_MODE_DEAD=3`,
  `PLAYER_MODE_FREEZE_CONTROLS=17`, `PLAYER_MODE_CONTROL_SPIRIT=24`.
- **CThingPlayerCreature** VFT `0x012457FC` [EN].
- **CWorld**: `+28 timeObject*` (→ `+8 float curTime` 0..1 day fraction, `+16
  float timeStep`), `+32 CThingSearchTools*`, `+56 CGameScriptInterface*`, `+88
  CScriptInfoManager*`, `+104 CBulletTimeManager*` (`+4 bool active` = slowmo),
  `+221 bool minimap`, `+0xF8 bool isLoadSave`, `+0x104 bool isLoadRegion` [FM]
- **CCamera** (via CGameCameraManager: `*(int*)(this+64) + 4`):
  `{CVector pos; CVector up; CVector forward; bool[2]; int; float FOV; ...}` [FM]
- **CTCHeroStats** VFT `0x0124F70C`: `+0x28 int morality; +0x30 float age,
  sunTan, fatness; +0x3C int money` ✓✓ [FM][EN], maxMoney, `+0x58 will/stamina`,
  `+0x5C` max will, `+0x70` renown; full block in FableMenu `HeroStats.h`.
- **CTCHero : CTCBase**: `bool canUseWeapons, canUseWill, weaponsUsable; EHeroTitle title` [FM]
- **CTCHeroExperience : CTCBase**: `+8 int general; int* perStat` (STRENGTH/SKILL/WILL) [FM]
- **CTCHeroMorph**: `+0x3D bool m_bUpdate; floats strength, berserk, will,
  skill, age, align, fat; bool kid` — write values then `m_bUpdate=true` [FM]
- **CTCHaste**: `+0xC float multiplier` [FM]
- **FCore**: `CVector{X,Y,Z}`, `CRGBAColour{B,G,R,A}`, `CCharString{int; char*}`,
  `CDefString{int tablePos}`, `RHSet{CVector Up, Forward}` [FM][RE] ✓✓
- **ETCInterfaceType**: complete 296-entry enum in FableMenu `Base.h`
  (`TCI_HERO_STATS=4`, …, incl. `TCI_COOP_SPIRIT`,
  `TCI_HERO_ONLINE_SCOREBOARD`). Our empirically-mapped typeIds in RE-NOTES
  (CTCHeroMorph=0x03, CTCInventory=0x11, Clothing=0x12, Weapons=0x13,
  CTCHero=0x29, CTCCarrying=0x46, GraphicAppearance=0x5B) should be
  cross-checked against it.
- **Input**: full `EGameAction` (121 actions) + `EInputKey` enums +
  `CActionInputControl` struct in FableMenu `GameInputProcess.h`.

## Engine functions (all `__thiscall` unless noted)

The full FableMenu-derived list (≈150 functions) — spawning, teleport, quest
manager, script interface, TC methods — is in the staging notes; the ones most
load-bearing for multiplayer:

```
CPlayerManager::GetMainPlayer            0x449970
CWorld::GetPlayer(int id)                0x4498C0   // engine supports player ids!
CPlayer::GetCharacterThing               0x487B70
CPlayer::AddMode/HasMode/RemoveMode      0x6345C0 / 0x634160 / 0x634190
CPlayer::SetControlledCreature(CThing*)  0x487CF0   // possess arbitrary creature
CPlayer::InitCharacterAs(CCharString*)   0x48A070   // (0x48A0A6 jz patch allows UninitCharacter 0x487BD0)
CreateThing   (fastcall)                 0x703210   // id, CVector*, player, 0, 0, CCharString*
CreateCreature(fastcall)                 0x833800   // id, CVector*, player, CreatureAI*{IsPlayer,Draw,...}
CGameDefinitionManager::GetDefGlobalIndexFromName 0x9AD410
CGameDefinitionManager::GetDefNameFromGlobalIndex 0x9ACCC0
CThing::AddTC 0x4C9D60 | HasTC 0x4118C0 | RemoveTC 0x4C9840 | Kill(bool) 0x4C9B80
CThing::GetPosition 0x4C73D0 | GetDefName 0x4C7CC0 | SetNewBrain(int) 0x833010
CThing::SetCurrentAction 0x6644F0 | ClearQueuedActions 0x663600
CWorldMap::LoadRegion(idx,ELoadType,nav) 0x500540 | IsLoadingAnyMap 0x4FB4A0
CWorldMap::SetPlayerPos(player,CVector*,ELoadType) 0x5063E0  // cross-region teleport
CTCPhysicsStandard::SetPosition 0x726750 | EnablePhysics 0x723630
CTCScriptedControl::AddAction 0x7137D0 | CActionPlayAnimation ctor 0x9034F0
  // ^ scripted-action puppeteering: anim by name, loop, priority, queue flags
CTCEnemy::SetFaction 0x76C810 | CTCRegionFollower::AddFollower 0x6AEDC0
CTCInventoryBase::AddItemToInventory 0x5BF654
CCharString ctor 0x99EBF0 [FM][BL] ✓✓ | CDefString::GetString 0x9D49B0
GameMalloc 0xBFEA1A [FM][RE] ✓✓ (already hooked in our SDK)
CGameScriptInterface (this = *(CWorld+56)): Pause 0x88E4F0, FadeScreenOut/In
  0x890820/0x88E4C0, ShowOnScreenMessage 0x892780/B0/0x892810,
  AddScreenTitleMessage 0x89E4C0, GiveHeroYesNoQuestion 0x89E540,
  camera moves 0x892410/0x892530/0x892C20, CameraShake 0x88ECE0,
  SheatheHeroWeapons 0x8916A0, ResetToFrontEnd 0x88F970, ...
CQuestManager::ActivateQuest 0x4B4A10 | SetQuestAsCompleted 0x4B1D30 | IsQuestActive 0x4AF610
```

Hook points used by FableMenu (call-site InjectHook, i.e. rel32 rewrites):
`0x4162E3` per-frame update (runs during load/save too), `0x4A5DFB` world
update tick, `0x69B7F4` camera-manager update, `0x69EAEE` HUD display,
`0x41677B` = pointer to the fixed logic-rate FPS constant (default 15 — the
engine's fixed update rate!).

## Script/loader internals [KE][BL]

```
sub_CBFB7D  main scripts.bin parser/engine (~80KB single function)
sub_CD52D0  compiled-script registering — "best place to add our own stuff into"
sub_4A21F0  fablesav parser/loader
sub_40D350  main profile loader; sub_40BCA0 profile parser
sub_99AD80 / sub_99A6A0  open-file handlers
sub_9F1D20  boot.ini loader
sub_C05FD0  zlib crc32
ScriptMain 0xCDE2F0 | Script_Global 0xCE19A0
0x9ED190  CConsole::Initialise(CConsole*, char, EInputKey, CFontBank*)
          — dev console code intact in retail, never enabled
Fable.exe+0x7030  CUserProfileManager::IsDebugProfile — patch 32 C0 C3 → B0 01 C3
          to force debug profile (world-save anywhere; saves land in "Save04")
0x57C2E6  refresh player visuals (call after poking alignment/morality) [EN]
```

## Useful byte patches (FableMenu-verified toggles)

```
0xAB5786+1  DirectInput mouse SetCooperativeLevel flags byte.
            Retail 0x05 = EXCLUSIVE|FOREGROUND. Our mouse_lock=0 writes 0x06
            (NONEXCLUSIVE|FOREGROUND). FableMenu writes 0x0A =
            NONEXCLUSIVE|BACKGROUND — device keeps delivering when unfocused;
            candidate fix for background-client/menu-cursor issues.
0xA0BEE2   "89 41 2C" FOV write — NOP for custom camera FOV
0x6BBA58   "D9 56 08" world-time write — NOP to freeze/control time of day
0x4A411E   jz↔jnz (+ bool 0x4A415D) — skip region-load fadeout
0x4C9920   PATCH_JUMP → return 1 — slow-motion affects everything
0x766D88   "7D 38" NOP — unlimited augmentation slots
0x6AB514/0x6AB5BC  hero movement type consts (walk=2, run=3, jog=4)
0x81F3F6   region-locking save system on/off
0x4A073B   autosave disable byte
0xE60689+2 bodyguard count limit (2 → up to 0xFF)
```

## Multiplayer-relevant leftovers in the retail engine

- RTTI dump (fabletlcmod th=9198, ~2662 classes) includes **CNetworkClient
  (VFT 0x0122EDAC)** and **CNetworkServer (VFT 0x0122ED30)** — dormant netcode.
- `CWorld::GetPlayer(int)`, `CPlayer::m_dNumber`, `TCI_COOP_SPIRIT` (we already
  wrap CoopSpirit in the SDK), `TCI_HERO_ONLINE_SCOREBOARD`, `FACTION_CTF_*`:
  Xbox co-op/Live remnants. Worth a dedicated RE pass — the engine may already
  have per-player plumbing we're reimplementing.
- Keshire (2007): the player is "a special NPC" with a special script type and
  movement type, both hookable — the NPC-puppet replication model EgoMP uses.

### Investigated 2026-07-18 (disassembled the live Steam exe)

**The transport classes are hollow — do NOT try to reuse them.**
`CNetworkClient` (vtable `0x0122EDAC`) and `CNetworkServer` (`0x0122ED30`) each
have only a **2-slot vtable**: slot 0 is a standard MSVC scalar-deleting
destructor, slot 1 is the shared `return 0` stub at `0x0099A340`. No virtual
methods, and no code anywhere in them calls Ws2_32/winsock. Only the RTTI, a
destructor, and a few member fields (the CNetworkServer ctor at `0x00415E20`
constructs a handful of string members) survived into retail; the actual
send/recv/connect implementation was compiled out. **Conclusion: EgoMP's own
SLikeNet transport is the correct approach — there is no dormant engine
networking to hook.** (Ctor/ref sites: CNetworkClient `0x00419333`,
CNetworkServer `0x00415E20` / `0x00415E5A`.)

**The gameplay-side co-op/online Thing Components ARE live and functional**
(full 28-slot CTCBase vtables with real, non-stub methods; none call winsock —
they are local logic with nothing to talk to):
- `CTCCoopSpirit` (vtable `0x0125B264`, ctor near `0x00670063`): real methods at
  slots 4 (`0x006700F0`, ~53 ins) and 9 (`0x00670A10`, ~48 ins) plus dtor. This
  is the retail co-op "spirit" helper — a real, working second-entity primitive.
  EgoMP already wraps it (`SDK/Fable/CoopSpirit`); this confirms it is genuine
  engine functionality, not a stub, and a natural building block for a
  controllable second pawn.
- `CTCHeroOnlineScoreboard` (vtable `0x0124DB34`, ctor near `0x00562ED0`): a
  substantial method at slot 1 (`0x00560720`, **~120 instructions**) plus slot
  26 (`0x00564570`, ~29 ins). Real leaderboard/stat-serialization logic that
  exists but has no service to submit to. Worth a closer read only if EgoMP
  ever wants a stat-sync / scoreboard surface — not for transport.

Net: the engine was clearly built with multiplayer/co-op in mind (the RTTI in
`data/rtti-classes.tsv` proves it), but the retail build kept the *gameplay*
scaffolding and stripped the *transport*. Reimplementing the wire protocol (as
EgoMP does) is unavoidable; the co-op-spirit TC is reusable gameplay plumbing.

## ImGui overlay recipe (for a future in-game EgoMP UI)

FableMenu's proven approach on this exact game (gui/dx9hook.cpp, ~350 lines):
create a NULLREF D3D9 device on the desktop window purely to read the device
vtable → MinHook `vtable[42]` (EndScene) + `vtable[16]` (Reset) → on first
EndScene get the real window from `GetCreationParameters().hFocusWindow`, init
ImGui Win32+DX9 backends, subclass the WndProc → while menu open, feed ImGui
and return 1 to swallow game input, and freeze the game side with
`CPlayer::AddMode(PLAYER_MODE_FREEZE_CONTROLS)` → Reset hook
invalidates/recreates ImGui device objects. Write our own implementation (no
license on FableMenu); the technique is standard.
