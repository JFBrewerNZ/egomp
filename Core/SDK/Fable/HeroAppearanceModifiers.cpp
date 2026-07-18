#include "HeroAppearanceModifiers.h"

#include <cstring>
#include <windows.h>
#include <MinHook/include/MinHook.h>

#include "Thing.h"
#include "3DVector.h"
#include "CharString.h"
#include "DefString.h"
#include "DefStringTable.h"
#include "DefinitionManager.h"

namespace
{
    const size_t TC_LIST_OFFSET = 0x44;
    const int TC_ID_HERO_ATTACHABLE_APPEARANCE = 0x5E;
    const int TC_ID_HERO_MORPH = 0x03;
    const int TC_ID_HERO_STATS = 0x04;
    const int TC_ID_INVENTORY_WEAPONS = 0x13;

    // Carried-weapon def-holders (CIntelligentPointer) + accessors. The
    // ranged holder sits at the UNALIGNED +0x141 — both RestoreCarried-
    // Weapons (0x5C8101) and the wield reconciler (0x5CA68E, `add edi,
    // 0x141`) read it there; the earlier +0x148 guess read garbage.
    const size_t WEAPON_MELEE_HOLDER_OFFSET = 0x134;
    const size_t WEAPON_RANGED_HOLDER_OFFSET = 0x141;
    const uintptr_t FN_INTELLIGENT_POINTER_GET = 0xA01B50;
    const uintptr_t FN_INTELLIGENT_POINTER_SET = 0xA01B90;

    // CTCInventoryWeapons::RegenerateCarriedWeapons — rebuilds the on-back
    // weapon visuals from the carried holders (tail-calls RestoreCarried-
    // Weapons 0x5C8101 or the clear path 0x5C552C by sheathed mode).
    const uintptr_t FN_REGENERATE_CARRIED_WEAPONS = 0x5C9962;

    // CTCInventoryWeapons::CreateCarriedWeapon(defIndex) — the menu-equip
    // creation path (factory hook traced it: mode 0 for the hero, from
    // creature+0x90) — and its inventory gate.
    const uintptr_t FN_CREATE_CARRIED_WEAPON = 0x5BE8F3;
    const uintptr_t FN_INVENTORY_WEAPON_GATE = 0x5BDF08;

    // RestoreCarriedWeapons — the build branch RegenerateCarriedWeapons
    // takes when the CTCHero +0xC weapons-visible byte is set.
    const uintptr_t FN_RESTORE_CARRIED_WEAPONS = 0x5C8101;
    const int TC_ID_HERO = 0x29;
    const size_t HERO_TC_WEAPONS_VISIBLE_OFFSET = 0xC;

    // The inventory-record lookup inside CreateCarriedWeapon. On an EMPTY
    // record container (puppets) it returns a non-null end-sentinel, and
    // the augmentation copies then read through garbage — the fault at
    // 0x5BE9A5 (`mov edx,[record+0xC]`) when the garbage is unmapped, or
    // worse, silent garbage copies into the weapon's combat data when it
    // is readable (crashed the observer when that weapon was unsheathed).
    const uintptr_t FN_INVENTORY_RECORD_LOOKUP = 0x5BE8D3;

    // Object factory + pickup action (see RE-NOTES.md / EquipmentProbe).
    const uintptr_t FN_THING_OBJECT_CREATE = 0x703210;
    const uintptr_t FN_ACTION_ADD_REAL_OBJECT_CTOR = 0x7EB2D0;
    const uintptr_t FN_DO_CREATURE_ACTION = 0x6644F0;

    // Combat action builders (from each move's trigger function). Actions
    // are built in a stack buffer then posted via DoCreatureAction.
    //   0x858030 = base strafe-jump ctor(this, creature, mode, dir2f*);
    //              the roll builder (0x85BB60) calls it with a ZERO dir (a
    //              neutral jump) then stamps the derived vtable. We call it
    //              directly with the real direction so the roll is oriented.
    const uintptr_t FN_STRAFE_JUMP_CTOR = 0x858030;
    const uintptr_t VT_CONTROLLED_STRAFE_JUMP = 0x12766A4;
    const int STRAFE_JUMP_MODE_ROLL = 2;

    // StartBlocking builder: __thiscall(actionBuffer, creature); creature-
    // only (ret 4), builds and stamps the block action. From the block
    // trigger @0x62D14B.
    const uintptr_t FN_BUILD_BLOCK = 0x855BE0;

    // Unsheathe/sheathe builders. Unsheathe args confirmed from the wield
    // reconciler (0x5CA84B): (creature, weaponThing, 0, 0x96). Sheathe is
    // 3 args; (creature, weaponThing, 0x96) by symmetry — SEH-guarded.
    const uintptr_t FN_BUILD_UNSHEATHE = 0x6A0150;
    const uintptr_t FN_BUILD_SHEATHE = 0x69FFD0;
    const int SHEATHE_FLAGS = 0x96;

    const size_t MORPH_BLOB_OFFSET = 0x40;
    const size_t MORPH_DIRTY_FLAG_OFFSET = 0x3D;
    const uintptr_t FN_MORPH_UPDATE_PUMP = 0x71E130;
    const size_t STATS_EXP_SPENT_ON_OFFSET = 0x18; // ptr to 12 ints

    const uintptr_t FN_ADD_MODIFIER = 0x706880;
    const uintptr_t FN_RESET_AND_REBUILD = 0x7079E0;

    const size_t MODIFIER_SETS_OFFSET = 0x30;
    const size_t MODIFIER_SET_COUNT = 3;
    const size_t MODIFIER_SET_STRIDE = 0xC; // vector: begin, end, capacity

#pragma pack(push, 1)
    struct ModifierEntry
    {
        int defGlobalIndex;
        unsigned char flags;
        char pad[3];
    };
#pragma pack(pop)
}

namespace
{
    void* FindThingComponent(CThingPlayerCreature* creature, int typeId)
    {
        if (!creature)
            return nullptr;

        const char* list = *(const char* const*)((const char*)creature + TC_LIST_OFFSET);

        if (!list)
            return nullptr;

        // {int typeId, CTC*} pairs, typeIds ascending; a non-ascending id
        // means we ran off the end of the list.
        int previousId = -1;

        for (size_t i = 0; i < 96; i++)
        {
            int id = ((const int*)list)[i * 2];
            void* tc = ((void* const*)list)[i * 2 + 1];

            if (id <= previousId || id > 0x200 || !tc)
                break;

            if (id == typeId)
                return tc;

            previousId = id;
        }

        return nullptr;
    }
}

CTCHeroAttachableAppearanceModifiers* CTCHeroAttachableAppearanceModifiers::FromCreature(CThingPlayerCreature* creature)
{
    return (CTCHeroAttachableAppearanceModifiers*)FindThingComponent(creature, TC_ID_HERO_ATTACHABLE_APPEARANCE);
}

void CTCHeroAttachableAppearanceModifiers::AddModifier(int defGlobalIndex)
{
    ((void(__thiscall*)(void*, int))FN_ADD_MODIFIER)(this, defGlobalIndex);
}

void CTCHeroAttachableAppearanceModifiers::ResetAndRebuild()
{
    ((void(__thiscall*)(void*))FN_RESET_AND_REBUILD)(this);
}

void CTCHeroAttachableAppearanceModifiers::ClearModifiers()
{
    char* sets = *(char**)((char*)this + MODIFIER_SETS_OFFSET);

    if (!sets)
        return;

    for (size_t s = 0; s < MODIFIER_SET_COUNT; s++)
    {
        char* vec = sets + s * MODIFIER_SET_STRIDE;
        *(void**)(vec + 4) = *(void**)vec; // end = begin
    }
}

void CTCHeroAttachableAppearanceModifiers::RebuildAttachments()
{
    ((void(__thiscall*)(void*))0x706040)(this);
    ((void(__thiscall*)(void*))0x706510)(this);
    ((void(__thiscall*)(void*))0x7070D0)(this);

    // Owner creature at +0x04, its CTCGraphicAppearance at creature+0x64;
    // 0x4BF9E0(0xF) is the refresh the reset vfunc performs.
    void* creature = *(void**)((char*)this + 0x04);
    if (creature)
    {
        void* graphicAppearance = *(void**)((char*)creature + 0x64);
        if (graphicAppearance)
            ((void(__thiscall*)(void*, int))0x4BF9E0)(graphicAppearance, 0xF);
    }
}

std::vector<int> CTCHeroAttachableAppearanceModifiers::GetModifierDefIndexes()
{
    std::vector<int> result;

    const char* sets = *(const char* const*)((const char*)this + MODIFIER_SETS_OFFSET);

    if (!sets)
        return result;

    for (size_t s = 0; s < MODIFIER_SET_COUNT; s++)
    {
        const ModifierEntry* begin = ((const ModifierEntry* const*)(sets + s * MODIFIER_SET_STRIDE))[0];
        const ModifierEntry* end = ((const ModifierEntry* const*)(sets + s * MODIFIER_SET_STRIDE))[1];

        if (!begin || end < begin || (size_t)((const char*)end - (const char*)begin) > 0x800)
            continue;

        for (const ModifierEntry* e = begin; e < end; e++)
            result.push_back(e->defGlobalIndex);
    }

    return result;
}

CTCHeroMorph* CTCHeroMorph::FromCreature(CThingPlayerCreature* creature)
{
    return (CTCHeroMorph*)FindThingComponent(creature, TC_ID_HERO_MORPH);
}

HeroMorphValues CTCHeroMorph::GetValues()
{
    HeroMorphValues values;
    const unsigned int* blob = (const unsigned int*)((const char*)this + MORPH_BLOB_OFFSET);

    for (size_t i = 0; i < HERO_MORPH_BLOB_DWORDS; i++)
        values.raw[i] = blob[i];

    return values;
}

void CTCHeroMorph::SetValues(const HeroMorphValues& values)
{
    if (GetValues() == values)
        return;

    unsigned int* blob = (unsigned int*)((char*)this + MORPH_BLOB_OFFSET);
    for (size_t i = 0; i < HERO_MORPH_BLOB_DWORDS; i++)
        blob[i] = values.raw[i];

    // Two passes: empirically the first pump re-seats attachments (sheathed
    // weapons move) and only the second applies the skeletal bulk.
    for (int pass = 0; pass < 2; pass++)
    {
        *((unsigned char*)this + MORPH_DIRTY_FLAG_OFFSET) = 1;
        ((void(__thiscall*)(void*))FN_MORPH_UPDATE_PUMP)(this);
    }
}

CTCHeroStats* CTCHeroStats::FromCreature(CThingPlayerCreature* creature)
{
    return (CTCHeroStats*)FindThingComponent(creature, TC_ID_HERO_STATS);
}

bool CTCHeroStats::GetExperienceSpentOn(HeroStatsExperience& out)
{
    const int* buffer = *(const int* const*)((const char*)this + STATS_EXP_SPENT_ON_OFFSET);

    if (!buffer)
        return false;

    for (size_t i = 0; i < HERO_STAT_EXPERIENCE_COUNT; i++)
        out.spentOn[i] = buffer[i];

    return true;
}

void CTCHeroStats::SetExperienceSpentOn(const HeroStatsExperience& values)
{
    int* buffer = *(int* const*)((char*)this + STATS_EXP_SPENT_ON_OFFSET);

    if (!buffer)
        return;

    for (size_t i = 0; i < HERO_STAT_EXPERIENCE_COUNT; i++)
        buffer[i] = values.spentOn[i];
}

namespace
{
    bool IsReadable(const void* p, size_t bytes)
    {
        if (!p)
            return false;
        MEMORY_BASIC_INFORMATION mbi = {};
        if (!VirtualQuery(p, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT)
            return false;
        if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))
            return false;
        return (uintptr_t)p + bytes <= (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
    }

    int DefIndexOfThing(CThing* thing)
    {
        if (!IsReadable(thing, 0x94))
            return -1;

        CDefString def{};
        CCharString name("");
        thing->GetDefName(&def);
        CDefStringTable::Get()->GetString(&name, def.TablePos);

        const char* chars = name.GetAsCharArray();
        if (!chars || !*chars)
            return -1;

        return CDefinitionManager::Get()->GetDefGlobalIndexFromName(&name);
    }

    int CarriedWeaponDefIndex(void* weaponsTC, size_t holderOffset)
    {
        void* holder = (char*)weaponsTC + holderOffset;
        CThing* weapon = ((CThing*(__thiscall*)(void*))FN_INTELLIGENT_POINTER_GET)(holder);
        return DefIndexOfThing(weapon);
    }
}

CTCInventoryWeapons* CTCInventoryWeapons::FromCreature(CThingPlayerCreature* creature)
{
    return (CTCInventoryWeapons*)FindThingComponent(creature, TC_ID_INVENTORY_WEAPONS);
}

int CTCInventoryWeapons::GetCarriedMeleeDefIndex()
{
    return CarriedWeaponDefIndex(this, WEAPON_MELEE_HOLDER_OFFSET);
}

int CTCInventoryWeapons::GetCarriedRangedDefIndex()
{
    return CarriedWeaponDefIndex(this, WEAPON_RANGED_HOLDER_OFFSET);
}

// weapon-def + 0x38 = the def index of the on-back visual (0 = no back
// visual). See RE-NOTES "Carried-weapon visuals".
static const size_t WEAPON_DEF_CARRIED_VISUAL_OFFSET = 0x38;

int CTCInventoryWeapons::GetCarriedVisualDefIndex(int weaponDefGlobalIndex)
{
    CDefinitionManager* defMgr = CDefinitionManager::Get();
    if (!defMgr)
        return -1;
    void* def = defMgr->GetDefObjectByIndex(weaponDefGlobalIndex);
    if (!def)
        return -1;
    __try
    {
        return *(int*)((char*)def + WEAPON_DEF_CARRIED_VISUAL_OFFSET);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return -1;
    }
}

CThing* CTCInventoryWeapons::GetCarriedMeleeThing()
{
    void* holder = (char*)this + WEAPON_MELEE_HOLDER_OFFSET;
    return ((CThing*(__thiscall*)(void*))FN_INTELLIGENT_POINTER_GET)(holder);
}

int CTCInventoryWeapons::GetInventoryWeaponGate(int defGlobalIndex)
{
    __try
    {
        return ((int(__thiscall*)(void*, int))FN_INVENTORY_WEAPON_GATE)(
            this, defGlobalIndex);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return -999;
    }
}

namespace
{
    // Bypass hooks: while g_gateBypass is set, the inventory gate
    // (0x5BDF08) reports ownership for any def, and the record lookup
    // (0x5BE8D3) returns NULL — CreateCarriedWeapon's own null check
    // (0x5BE96E) then skips every record copy and the weapon keeps its
    // def-default combat data, like an NPC's weapon. (Inventory-less
    // puppets otherwise get a garbage end-sentinel: unmapped -> the
    // 0x5BE9A5 faults; readable -> poisoned combat data. A ZEROED dummy
    // record was no better: the copied fields are treated as POINTERS
    // during unsheathe — nulls crashed the observer.)
    volatile bool g_gateBypass = false;

    int(__fastcall* OInventoryGate)(void* weaponsTC, void* edx, int defIndex) = nullptr;
    void*(__fastcall* OInventoryRecordLookup)(void* weaponsTC, void* edx, int defIndex) = nullptr;

    int __fastcall HInventoryGate(void* weaponsTC, void* edx, int defIndex)
    {
        int result = OInventoryGate(weaponsTC, edx, defIndex);
        if (g_gateBypass && result <= 0)
            result = 1;
        return result;
    }

    void* __fastcall HInventoryRecordLookup(void* weaponsTC, void* edx, int defIndex)
    {
        if (g_gateBypass)
            return nullptr;

        return OInventoryRecordLookup(weaponsTC, edx, defIndex);
    }

    bool EnsureGateHook()
    {
        static bool attempted = false;
        static bool installed = false;

        if (!attempted)
        {
            attempted = true;
            installed = MH_CreateHook(reinterpret_cast<void*>(FN_INVENTORY_WEAPON_GATE),
                    reinterpret_cast<void*>(&HInventoryGate),
                    reinterpret_cast<void**>(&OInventoryGate)) == MH_OK
                && MH_EnableHook(reinterpret_cast<void*>(FN_INVENTORY_WEAPON_GATE)) == MH_OK
                && MH_CreateHook(reinterpret_cast<void*>(FN_INVENTORY_RECORD_LOOKUP),
                    reinterpret_cast<void*>(&HInventoryRecordLookup),
                    reinterpret_cast<void**>(&OInventoryRecordLookup)) == MH_OK
                && MH_EnableHook(reinterpret_cast<void*>(FN_INVENTORY_RECORD_LOOKUP)) == MH_OK;
        }

        return installed;
    }
}

namespace
{
    int CaptureFault(EXCEPTION_POINTERS* pointers,
        unsigned long* exceptionCode, void** faultAddress)
    {
        if (exceptionCode)
            *exceptionCode = pointers->ExceptionRecord->ExceptionCode;
        if (faultAddress)
            *faultAddress = pointers->ExceptionRecord->ExceptionAddress;
        return EXCEPTION_EXECUTE_HANDLER;
    }
}

CThing* CTCInventoryWeapons::CreateCarriedWeaponUnchecked(int defGlobalIndex,
    unsigned long* exceptionCode, void** faultAddress)
{
    if (exceptionCode)
        *exceptionCode = 0;
    if (faultAddress)
        *faultAddress = nullptr;
    if (!EnsureGateHook())
        return nullptr;

    g_gateBypass = true;
    CThing* weapon = CreateCarriedWeapon(defGlobalIndex, exceptionCode, faultAddress);
    g_gateBypass = false;

    return weapon;
}

CThing* CTCInventoryWeapons::CreateCarriedWeapon(int defGlobalIndex,
    unsigned long* exceptionCode, void** faultAddress)
{
    if (exceptionCode)
        *exceptionCode = 0;
    if (faultAddress)
        *faultAddress = nullptr;
    if (defGlobalIndex <= 0)
        return nullptr;

    __try
    {
        return ((CThing*(__thiscall*)(void*, int))FN_CREATE_CARRIED_WEAPON)(
            this, defGlobalIndex);
    }
    __except (CaptureFault(GetExceptionInformation(), exceptionCode, faultAddress))
    {
        return nullptr;
    }
}

void CTCInventoryWeapons::SetCarriedMeleeWeapon(CThing* weapon)
{
    void* holder = (char*)this + WEAPON_MELEE_HOLDER_OFFSET;
    ((void(__thiscall*)(void*, CThing*))FN_INTELLIGENT_POINTER_SET)(holder, weapon);
}

void CTCInventoryWeapons::SetCarriedRangedWeapon(CThing* weapon)
{
    void* holder = (char*)this + WEAPON_RANGED_HOLDER_OFFSET;
    ((void(__thiscall*)(void*, CThing*))FN_INTELLIGENT_POINTER_SET)(holder, weapon);
}

void CTCInventoryWeapons::RegenerateCarriedWeapons()
{
    ((void(__thiscall*)(void*))FN_REGENERATE_CARRIED_WEAPONS)(this);
}

void CTCInventoryWeapons::RestoreCarriedVisuals()
{
    ((void(__thiscall*)(void*))FN_RESTORE_CARRIED_WEAPONS)(this);
}

bool CTCInventoryWeapons::SetCarriedWeaponsVisibleFlag(CThingPlayerCreature* creature)
{
    char* heroTC = (char*)FindThingComponent(creature, TC_ID_HERO);
    if (!heroTC)
        return false;

    heroTC[HERO_TC_WEAPONS_VISIBLE_OFFSET] = 1;
    return true;
}

namespace CombatActions
{
    void Perform(CThingPlayerCreature* creature, CombatActionType type, const C3DVector& direction)
    {
        if (!creature)
            return;

        __try
        {
            // Action objects are constructed in a stack buffer (~0xB8 bytes
            // at real call sites); 0x140 is a safe margin.
            char actionBuffer[0x140];
            void* action = nullptr;

            switch (type)
            {
            case CombatActionType::Roll:
            {
                // Horizontal world-space direction (2 floats) the roll goes.
                float dir2f[2] = { direction.X, direction.Y };
                ((void(__thiscall*)(void*, void*, int, void*))FN_STRAFE_JUMP_CTOR)(
                    actionBuffer, creature, STRAFE_JUMP_MODE_ROLL, dir2f);
                *(void**)actionBuffer = (void*)VT_CONTROLLED_STRAFE_JUMP;
                action = actionBuffer;
                break;
            }
            case CombatActionType::Block:
                action = ((void*(__thiscall*)(void*, void*))FN_BUILD_BLOCK)(actionBuffer, creature);
                break;
            case CombatActionType::Unsheathe:
            case CombatActionType::Sheathe:
            {
                // Reconstructed with the puppet's OWN carried weapon — the
                // real action attaches/detaches the weapon model to the
                // hand, which the anim mirror alone never did.
                CTCInventoryWeapons* weapons = CTCInventoryWeapons::FromCreature(creature);
                CThing* weapon = weapons ? weapons->GetCarriedMeleeThing() : nullptr;
                if (!weapon)
                    return;

                if (type == CombatActionType::Unsheathe)
                    action = ((void*(__thiscall*)(void*, void*, void*, int, int))FN_BUILD_UNSHEATHE)(
                        actionBuffer, creature, weapon, 0, SHEATHE_FLAGS);
                else
                    action = ((void*(__thiscall*)(void*, void*, void*, int))FN_BUILD_SHEATHE)(
                        actionBuffer, creature, weapon, SHEATHE_FLAGS);
                break;
            }
            default:
                return;
            }

            if (action)
                ((int(__thiscall*)(void*, void*))FN_DO_CREATURE_ACTION)(creature, action);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }
}

CThing* CTCInventoryWeapons::GiveWeapon(CThingPlayerCreature* creature, int defGlobalIndex)
{
    if (defGlobalIndex < 0 || !creature)
        return nullptr;

    __try
    {
        C3DVector pos = *((CThing*)creature)->GetPos();
        pos.X += 1.0f;

        CCharString emptyName("");
        void* object = ((void*(__fastcall*)(int, void*, int, int, int, void*))FN_THING_OBJECT_CREATE)(
            defGlobalIndex, &pos, 4, 0, 0, &emptyName);

        if (!object)
            return nullptr;

        static char actionBuffer[0x200];
        void* action = ((void*(__thiscall*)(void*, void*, void*))FN_ACTION_ADD_REAL_OBJECT_CTOR)(
            actionBuffer, creature, object);
        ((int(__thiscall*)(void*, void*))FN_DO_CREATURE_ACTION)(creature, action);

        return (CThing*)object;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return nullptr;
    }
}
