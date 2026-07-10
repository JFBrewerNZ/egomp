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

namespace
{
    // {int typeId, CTC*} pair array (discovered via NUMPAD5/6 dumps).
    const size_t TC_LIST_OFFSET = 0x44;
    const int TC_ID_INVENTORY_CLOTHING = 0x12;
    const int TC_ID_INVENTORY_WEAPONS = 0x13;
    const int TC_ID_HERO_ATTACHABLE_APPEARANCE = 0x5E;

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

        // The horns experiment: the appearance-reset vfunc re-applies the
        // creature def's modifiers, adds OBJECT_HERO_HORNS via
        // AddModifier(defIndex), and rebuilds the visible appearance. If the
        // hero visibly sprouts horns, the whole apply pipeline for
        // appearance sync is confirmed in one call.
        void* appearance = FindTC(creature, TC_ID_HERO_ATTACHABLE_APPEARANCE);
        if (!appearance)
        {
            Emit(report, "[Equip] probe: no appearance TC found");
            ObjectInspector::AppendToLogFile(report);
            return;
        }

        CCharString defName(PROBE_MODIFIER_DEF);
        int defIndex = CDefinitionManager::Get()->GetDefGlobalIndexFromName(&defName);

        sprintf_s(buf, "[Equip] probe: %s -> defIndex %d; AddModifier @ 0x%X on TC %p,"
            " then rebuild - watch the hero's head!",
            PROBE_MODIFIER_DEF, defIndex, (unsigned)FN_ADD_MODIFIER, appearance);
        Emit(report, buf);
        ObjectInspector::AppendToLogFile(report);
        report.clear();

        if (defIndex < 0)
        {
            Emit(report, "[Equip] probe: def name did not resolve, aborting");
            ObjectInspector::AppendToLogFile(report);
            return;
        }

        unsigned long exceptionCode = 0;
        GuardedThiscall1(FN_ADD_MODIFIER, appearance, (void*)(uintptr_t)defIndex, &exceptionCode);

        unsigned long rebuildException = 0;
        if (!exceptionCode)
            GuardedThiscall0(FN_APPEARANCE_RESET_WITH_HORNS, appearance, &rebuildException);

        if (exceptionCode || rebuildException)
            sprintf_s(buf, "[Equip] probe: EXCEPTION add=0x%X rebuild=0x%X"
                " (caught; game state may be unstable)",
                (unsigned)exceptionCode, (unsigned)rebuildException);
        else
            sprintf_s(buf, "[Equip] probe: calls returned - is the hero wearing a big hat?");
        Emit(report, buf);
        ObjectInspector::AppendToLogFile(report);
    }
}
