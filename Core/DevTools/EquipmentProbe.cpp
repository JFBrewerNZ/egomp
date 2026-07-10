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

namespace
{
    // {int typeId, CTC*} pair array (discovered via NUMPAD5/6 dumps).
    const size_t TC_LIST_OFFSET = 0x44;
    const int TC_ID_INVENTORY_CLOTHING = 0x12;
    const int TC_ID_INVENTORY_WEAPONS = 0x13;

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
            size_t dwords = rec.itemsEnd - rec.itemsBegin;

            sprintf_s(buf, "[Equip]  category id=0x%X selected=%d items-vector=%u dwords:",
                rec.categoryId, rec.selectedIndex, (unsigned)dwords);
            Emit(report, buf);

            for (size_t i = 0; i < dwords && i < VECTOR_DUMP_MAX_DWORDS; i++)
            {
                if (!ObjectInspector::IsReadableMemory(rec.itemsBegin + i, sizeof(void*)))
                    break;

                void* value = rec.itemsBegin[i];
                if (CThing* thing = AsThing(value))
                    sprintf_s(buf, "[Equip]   [%2u] %08X  %s", (unsigned)i,
                        (unsigned)(uintptr_t)value, DefNameOf(thing).c_str());
                else
                    sprintf_s(buf, "[Equip]   [%2u] %08X", (unsigned)i,
                        (unsigned)(uintptr_t)value);
                Emit(report, buf);
            }
        }
    }

    // First non-building item Thing in any clothing category — a real owned
    // clothing item, safe to hand to probe candidates.
    CThing* FirstClothingItem(void* clothingTC, std::string* defNameOut)
    {
        CategoryRecord records[CATEGORY_MAX] = {};
        size_t count = FindCategoryRecords(clothingTC, records, CATEGORY_MAX);

        for (size_t c = 0; c < count; c++)
        {
            size_t dwords = records[c].itemsEnd - records[c].itemsBegin;
            for (size_t i = 0; i < dwords; i++)
            {
                if (!ObjectInspector::IsReadableMemory(records[c].itemsBegin + i, sizeof(void*)))
                    break;

                CThing* thing = AsThing(records[c].itemsBegin[i]);
                if (!thing || !RttiContains(thing, "ThingObject"))
                    continue;

                std::string defName = DefNameOf(thing);
                if (defName.find("BUILDING") != std::string::npos
                    || defName.find("CREATURE") != std::string::npos)
                    continue;

                *defNameOut = defName;
                return thing;
            }
        }

        return nullptr;
    }

    // Unidentified 1-argument CTCInventoryClothing virtuals (vtable
    // 0x124356C), ranked by static analysis; see RE-NOTES.md. Slot 0 is
    // excluded: MSVC puts the scalar deleting destructor there.
    const struct { int slot; uintptr_t va; } CANDIDATES[] = {
        { 12, 0x5BFB96 },
        { 26, 0x5B3E4E },
        { 36, 0x5B5BE6 },
        { 11, 0x5B9641 },
        { 77, 0x5B76D1 },
    };
    const size_t CANDIDATE_COUNT = sizeof(CANDIDATES) / sizeof(CANDIDATES[0]);

    // Plain function: __try cannot share a frame with unwindable objects.
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

            DumpCategories(report, "weapons", weapons);
        }
        else
            Emit(report, "[Equip] no CTCInventoryWeapons found");

        Emit(report, "[Equip] --- end ---");
        ObjectInspector::AppendToLogFile(report);
    }

    void ProbeNextCandidate(CThingPlayerCreature* creature)
    {
        static size_t next = 0;
        std::string report;
        char buf[256];

        void* clothing = FindTC(creature, TC_ID_INVENTORY_CLOTHING);
        std::string defName;
        CThing* item = clothing ? FirstClothingItem(clothing, &defName) : nullptr;

        if (!item)
        {
            Emit(report, "[Equip] probe: no owned clothing item found"
                " (check NUMPAD8 output first)");
            ObjectInspector::AppendToLogFile(report);
            return;
        }

        if (next >= CANDIDATE_COUNT)
        {
            Emit(report, "[Equip] probe: all candidates tried; restarting cycle");
            next = 0;
        }

        const auto& candidate = CANDIDATES[next++];

        // Logged (and flushed) before the call so a hard crash still tells
        // us which candidate died.
        sprintf_s(buf, "[Equip] probe %u/%u: vtbl slot %d @ 0x%X (this=clothingTC, arg=%s)",
            (unsigned)next, (unsigned)CANDIDATE_COUNT, candidate.slot,
            (unsigned)candidate.va, defName.c_str());
        Emit(report, buf);
        ObjectInspector::AppendToLogFile(report);
        report.clear();

        unsigned long exceptionCode = 0;
        int result = GuardedThiscall1(candidate.va, clothing, item, &exceptionCode);

        if (exceptionCode)
            sprintf_s(buf, "[Equip] probe: EXCEPTION 0x%X (caught; game state may be unstable)",
                (unsigned)exceptionCode);
        else
            sprintf_s(buf, "[Equip] probe: returned %d - did anything change on the hero?", result);
        Emit(report, buf);
        ObjectInspector::AppendToLogFile(report);
    }
}
