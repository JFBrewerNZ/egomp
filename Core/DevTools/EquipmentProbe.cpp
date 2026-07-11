#include "EquipmentProbe.h"
#include "ObjectInspector.h"

#include <windows.h>
#include <cstring>
#include <iostream>
#include <string>

#include "../SDK/Fable/Thing.h"
#include "../SDK/Fable/ThingPlayerCreature.h"
#include "../SDK/Fable/DefString.h"
#include "../SDK/Fable/DefStringTable.h"
#include "../SDK/Fable/CharString.h"
#include "../SDK/Fable/DefinitionManager.h"
#include "../SDK/Fable/HeroAppearanceModifiers.h"

namespace
{
    // {int typeId, CTC*} pair array (discovered via NUMPAD5/6 dumps).
    const size_t TC_LIST_OFFSET = 0x44;
    const int TC_ID_INVENTORY_CLOTHING = 0x12;
    const int TC_ID_INVENTORY_WEAPONS = 0x13;
    const int TC_ID_HERO_ATTACHABLE_APPEARANCE = 0x5E;
    const int TC_ID_HERO_MORPH = 0x03;
    const int TC_ID_HERO_STATS = 0x04;

    // CTCHeroMorph body-shape input blob (+0x40..+0x63). Empirically:
    // +0x40 = muscle (1.0 maxed hero, 0.048 apprentice), +0x48 = will glow,
    // +0x50 = age, +0x54 = morality (1.0 -> halo).
    const size_t MORPH_BLOB_OFFSET = 0x40;

    // CTCHeroAttachableAppearanceModifiers members, derived from its
    // serializer @0x706F40 and the appearance-reset vfunc @0x7079E0:
    //   +0x30/+0x34 = vector begin/end of 8-byte entries, first dword =
    //                 appearance-modifier defGlobalIndex
    //   0x706880    = AddModifier(int defGlobalIndex)   (__thiscall)
    //   vfunc slot 4 @0x7079E0 = re-apply def modifiers + add
    //                 OBJECT_HERO_HORNS + rebuild + refresh (no args)
    const size_t MODIFIER_VECTOR_OFFSET = 0x30;
    const uintptr_t FN_APPEARANCE_RESET_WITH_HORNS = 0x7079E0;
    const uintptr_t FN_ADD_MODIFIER = 0x706880;

    // Horns turned out to be alignment-scaled (invisible on a good hero) —
    // a large hat is unconditional and unmistakable.
    const char* const PROBE_MODIFIER_DEF = "OBJECT_HERO_HAT_PIMP";

    // CTCInventoryWeapons carried-weapon members (serializer @0x5C3A95):
    // CIntelligentPointer<CThing>, 0x10 bytes each.
    const size_t MELEE_CARRIED_OFFSET = 0x140;
    const size_t RANGED_CARRIED_OFFSET = 0x150;

    // Per-category inventory record, 0x2C bytes, discovered by raw-dumping
    // the pools behind the clothing TC (five records, categoryId
    // 0xA4C..0xA50, immediately recognisable by the CInventoryCategoryDef*
    // at +0x0C):
    //   +0x00 itemVector.begin   +0x04 itemVector.end   +0x08 itemVector.cap
    //   +0x0C CInventoryCategoryDef*   +0x10 owner subobject*
    //   +0x14 categoryId   +0x18 selectedIndex(-1 = none)   +0x1C pad/0
    // The pool pointers held by the TC move between sessions, so records are
    // found by scanning every pool the TC references for CategoryDef anchors.
    struct CategoryRecord
    {
        void* const* itemsBegin;
        void* const* itemsEnd;
        const void* categoryDef;
        int categoryId;
        int selectedIndex;
    };
    const size_t CATEGORY_MAX = 12;
    const size_t TC_MEMBER_SCAN_BYTES = 0x80;
    const size_t POOL_SCAN_DWORDS = 0x180;
    const size_t VECTOR_DUMP_MAX_DWORDS = 48;

    void* FindTC(void* creature, int typeId)
    {
        const char* list = *(const char* const*)((const char*)creature + TC_LIST_OFFSET);

        for (size_t i = 0; i < 96; i++)
        {
            int id = ((const int*)list)[i * 2];
            void* tc = ((void* const*)list)[i * 2 + 1];

            if (id < 0 || id > 0x200 || !ObjectInspector::GetRttiName(tc))
                break;

            if (id == typeId)
                return tc;
        }

        return nullptr;
    }

    std::string DefNameOf(CThing* thing)
    {
        CDefString def{};
        CCharString name("");

        thing->GetDefName(&def);
        CDefStringTable::Get()->GetString(&name, def.TablePos);

        const char* chars = name.GetAsCharArray();
        return chars ? chars : "<null>";
    }

    // A CThing-derived object, or nullptr.
    CThing* AsThing(void* candidate)
    {
        const char* rtti = ObjectInspector::GetRttiName(candidate);
        return rtti && strstr(rtti, "Thing") ? (CThing*)candidate : nullptr;
    }

    // First CThing found inside a CIntelligentPointer<CThing> (exact member
    // layout unknown; scan its four dwords).
    CThing* ThingFromIntelligentPointer(const void* pointerObject)
    {
        for (size_t i = 0; i < 4; i++)
        {
            if (CThing* thing = AsThing(((void* const*)pointerObject)[i]))
                return thing;
        }
        return nullptr;
    }

    bool RttiContains(const void* object, const char* substring)
    {
        const char* rtti = ObjectInspector::GetRttiName(object);
        return rtti && strstr(rtti, substring);
    }

    size_t FindCategoryRecords(void* inventoryTC, CategoryRecord* out, size_t max)
    {
        size_t found = 0;

        for (size_t m = 0; m < TC_MEMBER_SCAN_BYTES / sizeof(void*) && found < max; m++)
        {
            const char* pool = ((const char* const*)inventoryTC)[m];

            // Raw heap buffers only — polymorphic objects are not pools.
            if (!pool || !ObjectInspector::IsReadableMemory(pool, sizeof(void*))
                || ObjectInspector::GetRttiName(pool))
                continue;

            for (size_t i = 3; i < POOL_SCAN_DWORDS && found < max; i++)
            {
                const char* slot = pool + i * sizeof(void*);
                if (!ObjectInspector::IsReadableMemory(slot, sizeof(void*)))
                    break;

                void* anchor = *(void* const*)slot;
                if (!RttiContains(anchor, "CInventoryCategoryDef"))
                    continue;

                void* const* rec = (void* const*)(slot - 0x0C);
                void* const* begin = (void* const*)rec[0];
                void* const* end = (void* const*)rec[1];
                int categoryId = (int)(uintptr_t)rec[5];
                int selectedIndex = (int)(uintptr_t)rec[6];

                if ((uintptr_t)end < (uintptr_t)begin
                    || (uintptr_t)end - (uintptr_t)begin > 0x1000)
                    continue;
                if (begin != end && !ObjectInspector::IsReadableMemory(begin, sizeof(void*)))
                    continue;

                bool duplicate = false;
                for (size_t k = 0; k < found; k++)
                    duplicate |= out[k].categoryDef == anchor;
                if (duplicate)
                    continue;

                out[found++] = { begin, end, anchor, categoryId, selectedIndex };
            }
        }

        return found;
    }

    void Emit(std::string& report, const std::string& line)
    {
        std::cout << line << std::endl;
        report += line + "\n";
    }

    // Active appearance-modifier def indexes, read exactly the way the
    // game's own serializer does.
    size_t ReadModifierDefIndexes(void* appearanceTC, unsigned* out, size_t max)
    {
        const char* base = (const char*)appearanceTC + MODIFIER_VECTOR_OFFSET;
        const char* begin = ((const char* const*)base)[0];
        const char* end = ((const char* const*)base)[1];

        if (!begin || end < begin || (size_t)(end - begin) > 0x800
            || !ObjectInspector::IsReadableMemory(begin, end - begin))
            return 0;

        size_t count = (end - begin) / 8;
        if (count > max)
            count = max;

        for (size_t i = 0; i < count; i++)
            out[i] = *(const unsigned*)(begin + i * 8);

        return count;
    }

    // Item-vector entry: 5 dwords {defIndex, count, ptrA, ptrB, flags}.
    // ptrA/ptrB are non-null only on a handful of entries (suspected: items
    // instantiated in the world, i.e. currently worn/equipped).
    struct ItemEntry
    {
        unsigned defIndex;
        unsigned count;
        void* ptrA;
        void* ptrB;
        unsigned flags;
    };

    void DescribeInstancePointer(std::string& report, const char* tag, void* p)
    {
        if (!p || !ObjectInspector::IsReadableMemory(p, 4 * sizeof(void*)))
            return;

        char buf[256];
        for (size_t i = 0; i < 4; i++)
        {
            void* inner = ((void* const*)p)[i];
            const char* rtti = ObjectInspector::GetRttiName(inner);
            if (!rtti)
                continue;

            if (CThing* thing = AsThing(inner))
                sprintf_s(buf, "[Equip]      %s[%u] -> %s (%s)", tag, (unsigned)i,
                    rtti, DefNameOf(thing).c_str());
            else
                sprintf_s(buf, "[Equip]      %s[%u] -> %s", tag, (unsigned)i, rtti);
            Emit(report, buf);
        }
    }

    void DumpCategories(std::string& report, const char* label, void* inventoryTC)
    {
        char buf[256];
        CategoryRecord records[CATEGORY_MAX] = {};
        size_t count = FindCategoryRecords(inventoryTC, records, CATEGORY_MAX);

        sprintf_s(buf, "[Equip] %s TC @ %p: %u category records",
            label, inventoryTC, (unsigned)count);
        Emit(report, buf);

        for (size_t c = 0; c < count; c++)
        {
            const CategoryRecord& rec = records[c];
            size_t entries = (rec.itemsEnd - rec.itemsBegin) / 5;

            sprintf_s(buf, "[Equip]  category id=0x%X selected=%d entries=%u:",
                rec.categoryId, rec.selectedIndex, (unsigned)entries);
            Emit(report, buf);

            for (size_t i = 0; i < entries; i++)
            {
                const ItemEntry* e = (const ItemEntry*)(rec.itemsBegin + i * 5);
                if (!ObjectInspector::IsReadableMemory(e, sizeof(ItemEntry)))
                    break;
                if (!e->defIndex && !e->count && !e->ptrA)
                    continue;

                sprintf_s(buf, "[Equip]   [%2u] def=0x%04X count=%-3u ptrA=%08X ptrB=%08X flags=%08X%s",
                    (unsigned)i, e->defIndex, e->count,
                    (unsigned)(uintptr_t)e->ptrA, (unsigned)(uintptr_t)e->ptrB, e->flags,
                    e->ptrA ? "  <-- INSTANTIATED" : "");
                Emit(report, buf);

                if (e->ptrA)
                {
                    DescribeInstancePointer(report, "ptrA", e->ptrA);
                    DescribeInstancePointer(report, "ptrB", e->ptrB);
                }
            }
        }
    }

    // CThingObject factory + pickup action (see RE-NOTES.md):
    //   0x703210 = CThingObject::Create(__fastcall: defIndex, pos*, name*)
    //   0x7EB2D0 = CCreatureAction_AddRealObjectToInventory ctor
    //              (stack action buffer, args: creature, object)
    //   0x6644F0 = CThingCreatureBase::DoCreatureAction(action*)
    const uintptr_t FN_THING_OBJECT_CREATE = 0x703210;
    const uintptr_t FN_ACTION_ADD_REAL_OBJECT_CTOR = 0x7EB2D0;
    const uintptr_t FN_DO_CREATURE_ACTION = 0x6644F0;
    const char* const PROBE_WEAPON_DEF = "OBJECT_LEGENDARY_BROADSWORD";

    void* GuardedObjectCreate(int defIndex, void* pos, void* name, unsigned long* exceptionCode)
    {
        // Call-site pattern (0x4B2018, 0x5C37E3): ecx=defIndex, edx=pos*,
        // stack = (mode, 0, 0, CCharString* name); generic spawns pass 4.
        *exceptionCode = 0;
        __try
        {
            return ((void*(__fastcall*)(int, void*, int, int, int, void*))FN_THING_OBJECT_CREATE)(
                defIndex, pos, 4, 0, 0, name);
        }
        __except (*exceptionCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    void* GuardedObjectCreateMode(int defIndex, void* pos, int mode, void* name,
        unsigned long* exceptionCode)
    {
        *exceptionCode = 0;
        __try
        {
            return ((void*(__fastcall*)(int, void*, int, int, int, void*))FN_THING_OBJECT_CREATE)(
                defIndex, pos, mode, 0, 0, name);
        }
        __except (*exceptionCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    int GuardedPickup(void* creature, void* object, unsigned long* exceptionCode)
    {
        // The action is constructed in a stack buffer at every game call
        // site (~0xB0 bytes); its dtor is skipped here — probe-only leak.
        static char actionBuffer[0x200];

        *exceptionCode = 0;
        __try
        {
            void* action = ((void*(__thiscall*)(void*, void*, void*))FN_ACTION_ADD_REAL_OBJECT_CTOR)(
                actionBuffer, creature, object);
            return ((int(__thiscall*)(void*, void*))FN_DO_CREATURE_ACTION)(creature, action);
        }
        __except (*exceptionCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    // 0x5C36C2: __fastcall(creature, weaponThing) — builds the carried
    // weapon visual on the creature's back (the save-load restore path,
    // no inventory/popup). Second arg is a weapon CThing (the factory
    // produces one).
    const uintptr_t FN_ATTACH_CARRIED_WEAPON = 0x5C36C2;

    void GuardedAttachCarried(void* creature, void* weaponObject, unsigned long* exceptionCode)
    {
        *exceptionCode = 0;
        __try
        {
            ((void(__fastcall*)(void*, void*))FN_ATTACH_CARRIED_WEAPON)(creature, weaponObject);
        }
        __except (*exceptionCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    // Plain functions: __try cannot share a frame with unwindable objects.
    int GuardedThiscall1(uintptr_t fn, void* self, void* arg, unsigned long* exceptionCode)
    {
        *exceptionCode = 0;
        __try
        {
            return ((int(__thiscall*)(void*, void*))fn)(self, arg);
        }
        __except (*exceptionCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    int GuardedThiscall0(uintptr_t fn, void* self, unsigned long* exceptionCode)
    {
        *exceptionCode = 0;
        __try
        {
            return ((int(__thiscall*)(void*))fn)(self);
        }
        __except (*exceptionCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }
}

namespace EquipmentProbe
{
    void DumpEquipment(CThingPlayerCreature* creature)
    {
        std::string report;
        char buf[256];

        sprintf_s(buf, "[Equip] --- equipment of hero @ %p (%s) ---",
            (void*)creature, DefNameOf((CThing*)creature).c_str());
        Emit(report, buf);

        // Active appearance modifiers (hair/hat/clothing visuals), read via
        // the member offsets the game's serializer uses.
        if (void* appearance = FindTC(creature, TC_ID_HERO_ATTACHABLE_APPEARANCE))
        {
            unsigned indexes[32] = {};
            size_t n = ReadModifierDefIndexes(appearance, indexes, 32);

            sprintf_s(buf, "[Equip] appearance TC @ %p, active modifiers: %u",
                appearance, (unsigned)n);
            Emit(report, buf);

            std::string line = "[Equip]  modifier def indexes:";
            for (size_t i = 0; i < n; i++)
            {
                char hex[16];
                sprintf_s(hex, " 0x%X", indexes[i]);
                line += hex;
            }
            if (n)
                Emit(report, line);
        }
        else
            Emit(report, "[Equip] no CTCHeroAttachableAppearanceModifiers found");

        // Body-shape morph input blob (synced raw over the network).
        if (void* morph = FindTC(creature, TC_ID_HERO_MORPH))
        {
            const float* f = (const float*)((const char*)morph + MORPH_BLOB_OFFSET);
            sprintf_s(buf, "[Equip] morph TC @ %p blob +0x40..+0x60: "
                "%.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f",
                morph, f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7], f[8]);
            Emit(report, buf);
        }
        else
            Emit(report, "[Equip] no CTCHeroMorph found");

        // Muscle bulk isn't driven by the morph floats (a buff hero reads
        // strength=0.354 locally); suspect experience spent per stat.
        // CTCHeroStats serializer: ExperienceSpentOn @+0x18,
        // HeroStatsExperience @+0x118, TotalSpentExperience @+0x13C.
        if (const char* stats = (const char*)FindTC(creature, TC_ID_HERO_STATS))
        {
            const void* spentOn = *(const void* const*)(stats + 0x18);
            std::string line = "[Equip] stats TC: expSpentOn buffer";
            if (ObjectInspector::IsReadableMemory(spentOn, 24 * 4))
            {
                const int* v = (const int*)spentOn;
                for (size_t i = 0; i < 24; i++)
                {
                    char num[16];
                    sprintf_s(num, " %d", v[i]);
                    line += num;
                }
            }
            else
                line += " <unreadable>";
            Emit(report, line);

            sprintf_s(buf, "[Equip] stats TC: heroStatsExp=%d totalSpentExp=%d raw+0x18=%08X",
                *(const int*)(stats + 0x118), *(const int*)(stats + 0x13C),
                (unsigned)(uintptr_t)spentOn);
            Emit(report, buf);
        }

        if (void* clothing = FindTC(creature, TC_ID_INVENTORY_CLOTHING))
            DumpCategories(report, "clothing", clothing);
        else
            Emit(report, "[Equip] no CTCInventoryClothing found");

        if (void* weapons = FindTC(creature, TC_ID_INVENTORY_WEAPONS))
        {
            const char* base = (const char*)weapons;
            CThing* melee = ThingFromIntelligentPointer(base + MELEE_CARRIED_OFFSET);
            CThing* ranged = ThingFromIntelligentPointer(base + RANGED_CARRIED_OFFSET);

            sprintf_s(buf, "[Equip] weapons TC @ %p, melee = %s, ranged = %s",
                weapons,
                melee ? DefNameOf(melee).c_str() : "<none>",
                ranged ? DefNameOf(ranged).c_str() : "<none>");
            Emit(report, buf);

            // The carried-slot members read <none> even with weapons on the
            // hero's back — decode the raw CIntelligentPointer contents
            // (handles rather than pointers?) and list every Thing reachable
            // from the +0x138 pool, where weapon CThingObjects were seen.
            sprintf_s(buf, "[Equip]  melee IP raw: %08X %08X %08X %08X  ranged IP raw: %08X %08X %08X %08X",
                ((unsigned*)(base + MELEE_CARRIED_OFFSET))[0], ((unsigned*)(base + MELEE_CARRIED_OFFSET))[1],
                ((unsigned*)(base + MELEE_CARRIED_OFFSET))[2], ((unsigned*)(base + MELEE_CARRIED_OFFSET))[3],
                ((unsigned*)(base + RANGED_CARRIED_OFFSET))[0], ((unsigned*)(base + RANGED_CARRIED_OFFSET))[1],
                ((unsigned*)(base + RANGED_CARRIED_OFFSET))[2], ((unsigned*)(base + RANGED_CARRIED_OFFSET))[3]);
            Emit(report, buf);

            const char* pool = *(const char* const*)(base + 0x138);
            if (ObjectInspector::IsReadableMemory(pool, sizeof(void*)))
            {
                size_t hits = 0;
                for (size_t i = 0; i < 0x140 && hits < 20; i++)
                {
                    const char* slot = pool + i * sizeof(void*);
                    if (!ObjectInspector::IsReadableMemory(slot, sizeof(void*)))
                        break;

                    CThing* thing = AsThing(*(void* const*)slot);
                    if (thing && RttiContains(thing, "ThingObject"))
                    {
                        sprintf_s(buf, "[Equip]  +0x138 pool [%3u] %s", (unsigned)i,
                            DefNameOf(thing).c_str());
                        Emit(report, buf);
                        hits++;
                    }
                }
            }

            DumpCategories(report, "weapons", weapons);
        }
        else
            Emit(report, "[Equip] no CTCInventoryWeapons found");

        Emit(report, "[Equip] --- end ---");
        ObjectInspector::AppendToLogFile(report);
    }

    void ProbeNextCandidate(CThingPlayerCreature* creature)
    {
        std::string report;
        char buf[256];

        // Validity test for the on-back restore function: feed it the
        // hero's OWN existing carried weapon (a known-good source) instead
        // of a fresh factory object. A duplicate on the back = the function
        // works and the earlier crash was just an under-initialised object.
        void* weapons = FindTC(creature, TC_ID_INVENTORY_WEAPONS);
        if (!weapons)
        {
            Emit(report, "[Equip] probe: no weapons TC");
            ObjectInspector::AppendToLogFile(report);
            return;
        }

        // First weapon Thing in the weapons +0x138 pool (pool[0] = the
        // carried melee weapon in every dump so far).
        const char* pool = *(const char* const*)((const char*)weapons + 0x138);
        CThing* sourceWeapon = nullptr;
        if (ObjectInspector::IsReadableMemory(pool, sizeof(void*)))
        {
            for (size_t i = 0; i < 0x140; i++)
            {
                const char* slot = pool + i * sizeof(void*);
                if (!ObjectInspector::IsReadableMemory(slot, sizeof(void*)))
                    break;
                CThing* t = AsThing(*(void* const*)slot);
                if (t && strstr(ObjectInspector::GetRttiName(t), "ThingObject"))
                {
                    sourceWeapon = t;
                    break;
                }
            }
        }

        if (!sourceWeapon)
        {
            Emit(report, "[Equip] probe: no carried weapon found to re-attach");
            ObjectInspector::AppendToLogFile(report);
            return;
        }

        // Non-crashing diagnostic: dump a FRESH factory weapon and the
        // hero's REAL carried weapon side by side. 0x5C36C2 faults reading
        // source+0x70 on a fresh object but not on a real one, so the diff
        // of these two dumps shows exactly which member the factory leaves
        // uninitialised.
        CCharString swordDefName(PROBE_WEAPON_DEF);
        int defIndex = CDefinitionManager::Get()->GetDefGlobalIndexFromName(&swordDefName);

        C3DVector spawnPos = *((CThing*)creature)->GetPos();
        spawnPos.X += 1.0f;

        CCharString emptyName("");
        unsigned long exceptionCode = 0;
        void* freshObject = GuardedObjectCreate(defIndex, &spawnPos, &emptyName, &exceptionCode);

        sprintf_s(buf, "[Equip] probe: fresh factory object %p, real carried weapon %s @ %p",
            freshObject, DefNameOf(sourceWeapon).c_str(), (void*)sourceWeapon);
        Emit(report, buf);
        ObjectInspector::AppendToLogFile(report);

        if (freshObject)
            ObjectInspector::Dump("FRESH factory weapon", freshObject, 0x100);
        ObjectInspector::Dump("REAL carried weapon", sourceWeapon, 0x100);

        // Raw +0x70 dword on each — the exact field 0x5C36C2 dereferences.
        std::string cmp;
        char line[128];
        void* freshP70 = freshObject && ObjectInspector::IsReadableMemory((char*)freshObject + 0x70, 4)
            ? *(void**)((char*)freshObject + 0x70) : (void*)~0u;
        void* realP70 = ObjectInspector::IsReadableMemory((char*)sourceWeapon + 0x70, 4)
            ? *(void**)((char*)sourceWeapon + 0x70) : (void*)~0u;
        sprintf_s(line, "[Equip] +0x70: fresh=%p (%s)  real=%p (%s)",
            freshP70, ObjectInspector::GetRttiName(freshP70) ? ObjectInspector::GetRttiName(freshP70) : "-",
            realP70, ObjectInspector::GetRttiName(realP70) ? ObjectInspector::GetRttiName(realP70) : "-");
        cmp = line;
        std::cout << cmp << std::endl;
        ObjectInspector::AppendToLogFile(cmp);
    }
}

namespace
{
    // Learned the hard way (2026-07-11): Fable's inventory stores DEF
    // entries — picking an object up kills the world CThing (+0x91 bit
    // set), so a picked-up pointer must never go into a carried holder,
    // and a faulted equip must be undone immediately (a corrupted carried
    // slot crashes the game on its next own regenerate).

    // SEH-isolated equip step (no unwindable locals allowed here).
    bool GuardedCarriedEquip(CTCInventoryWeapons* weapons, CThing* weapon,
        unsigned long* exceptionCode)
    {
        *exceptionCode = 0;
        __try
        {
            weapons->SetCarriedMeleeWeapon(weapon);
            weapons->RegenerateCarriedWeapons();
            return true;
        }
        __except (*exceptionCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }
}

namespace EquipmentProbe
{
    // The full equip-by-def chain, exactly as the inventory-menu equip does
    // it (traced 2026-07-11 via the factory/holster hooks): the game's own
    // CTCInventoryWeapons::CreateCarriedWeapon(defIndex) builds a properly
    // wired carried weapon (creature mode byte + inventory-record
    // augmentation copies), then holster + regenerate. On any fault the
    // original carried weapon is restored immediately.
    void WeaponEquipProbe(CThingPlayerCreature* creature)
    {
        CTCInventoryWeapons* weapons = CTCInventoryWeapons::FromCreature(creature);
        if (!weapons)
        {
            ObjectInspector::LogLine("[Equip] weapon probe: no CTCInventoryWeapons on hero");
            return;
        }

        char buf[320];

        CThing* original = weapons->GetCarriedMeleeThing();
        int carriedDef = weapons->GetCarriedMeleeDefIndex();

        // Candidate defs: the probe broadsword plus the two weapons seen in
        // this save's equip traces (stick 5545, katana 5475) and whatever
        // is currently carried. The gate (0x5BDF08) reveals which the
        // inventory actually holds; the first owned one that is NOT already
        // carried gets equipped so the change is visible.
        CCharString defName(PROBE_WEAPON_DEF);
        int candidates[4] = {
            CDefinitionManager::Get()->GetDefGlobalIndexFromName(&defName),
            5545, 5475, carriedDef,
        };

        int chosen = -1;
        for (int i = 0; i < 4; i++)
        {
            int gate = weapons->GetInventoryWeaponGate(candidates[i]);
            sprintf_s(buf, "[Equip] weapon probe: gate(def %d) = %d%s",
                candidates[i], gate,
                candidates[i] == carriedDef ? " (currently carried)" : "");
            ObjectInspector::LogLine(buf);

            if (gate > 0 && chosen < 0 && candidates[i] != carriedDef)
                chosen = candidates[i];
        }

        if (chosen < 0)
        {
            ObjectInspector::LogLine("[Equip] weapon probe: no owned, not-carried weapon def"
                " passed the gate — nothing to equip");
            return;
        }

        unsigned long createException = 0;
        CThing* fresh = weapons->CreateCarriedWeapon(chosen, &createException);

        if (!fresh)
        {
            sprintf_s(buf, "[Equip] weapon probe: CreateCarriedWeapon(def %d) FAILED"
                " (exception %08lX)", chosen, createException);
            ObjectInspector::LogLine(buf);
            return;
        }

        unsigned long equipException = 0;
        bool ok = GuardedCarriedEquip(weapons, fresh, &equipException);

        bool restored = false;
        if (!ok)
        {
            unsigned long restoreException = 0;
            restored = GuardedCarriedEquip(weapons, original, &restoreException);
        }

        sprintf_s(buf, "[Equip] weapon probe: created %p via game path, equip=%s"
            " (exception %08lX)%s — check the hero's back",
            (void*)fresh, ok ? "OK" : "FAULTED", equipException,
            ok ? "" : (restored ? ", original restored" : ", RESTORE FAILED"));
        ObjectInspector::LogLine(buf);

        // Local reproduction of the puppet RANGED-create fault: gate-
        // bypassed creation of ranged defs, preferably one NOT in the
        // inventory (that is the puppet situation). The captured fault
        // address pins the failing read; the created/leaked object is NOT
        // holstered.
        CCharString bowName("OBJECT_EBONY_LONGBOW");
        int rangedCandidates[2] = {
            CDefinitionManager::Get()->GetDefGlobalIndexFromName(&bowName),
            5530,
        };

        for (int i = 0; i < 2; i++)
        {
            int def = rangedCandidates[i];
            if (def <= 0)
                continue;

            int gate = weapons->GetInventoryWeaponGate(def);
            unsigned long exc = 0;
            void* faultAddr = nullptr;
            CThing* created = weapons->CreateCarriedWeaponUnchecked(def, &exc, &faultAddr);

            sprintf_s(buf, "[Equip] ranged repro: def %d gate %d -> %s"
                " (exc %08lX @%p)%s",
                def, gate, created ? "created" : "FAILED", exc, faultAddr,
                created ? " (probe object left un-holstered)" : "");
            ObjectInspector::LogLine(buf);
        }
    }
}
