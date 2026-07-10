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

    // CTCInventoryClothing+0x14C points into a pool holding worn-armour
    // entries of {CThingObject* piece, CHitLocationDef*, CArmourDef*} at
    // irregular positions (observed strides vary between sessions), so worn
    // pieces are found by scanning for that pointer-triple pattern rather
    // than by fixed offsets.
    const size_t WORN_ARRAY_OFFSET = 0x14C;
    const size_t WORN_SCAN_DWORDS = 0x140;
    const size_t WORN_MAX_SLOTS = 8;

    // CTCInventoryWeapons carried-weapon members (serializer @0x5C3A95):
    // CIntelligentPointer<CThing>, 0x10 bytes each.
    const size_t MELEE_CARRIED_OFFSET = 0x140;
    const size_t RANGED_CARRIED_OFFSET = 0x150;

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

    size_t FindWornPieces(void* clothingTC, CThing** out, size_t max)
    {
        const char* pool = *(const char* const*)((const char*)clothingTC + WORN_ARRAY_OFFSET);
        size_t found = 0;

        for (size_t i = 0; found < max && i + 2 < WORN_SCAN_DWORDS; i++)
        {
            const char* slot = pool + i * sizeof(void*);
            if (!ObjectInspector::IsReadableMemory(slot, 3 * sizeof(void*)))
                break;

            void* const* dwords = (void* const*)slot;
            if (RttiContains(dwords[1], "HitLocationDef")
                && RttiContains(dwords[2], "ArmourDef"))
            {
                if (CThing* piece = AsThing(dwords[0]))
                    out[found++] = piece;
            }
        }

        return found;
    }

    // Unidentified 1-argument CTCInventoryClothing virtuals (vtable
    // 0x124356C), ranked by static analysis; see RE-NOTES.md.
    const struct { int slot; uintptr_t va; } CANDIDATES[] = {
        { 12, 0x5BFB96 },
        { 26, 0x5B3E4E },
        { 36, 0x5B5BE6 },
        { 11, 0x5B9641 },
        {  0, 0x4E7D7B },
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
    void Emit(std::string& report, const std::string& line)
    {
        std::cout << line << std::endl;
        report += line + "\n";
    }

    void DumpEquipment(CThingPlayerCreature* creature)
    {
        std::string report;
        char buf[256];

        sprintf_s(buf, "[Equip] --- equipment of hero @ %p (%s) ---",
            (void*)creature, DefNameOf((CThing*)creature).c_str());
        Emit(report, buf);

        void* clothing = FindTC(creature, TC_ID_INVENTORY_CLOTHING);
        size_t wornCount = 0;

        if (clothing)
        {
            CThing* pieces[WORN_MAX_SLOTS] = {};
            wornCount = FindWornPieces(clothing, pieces, WORN_MAX_SLOTS);
            const void* pool = *(const void* const*)((const char*)clothing + WORN_ARRAY_OFFSET);

            sprintf_s(buf, "[Equip] clothing TC @ %p, +0x14C pool @ %p, worn pieces found: %u",
                clothing, pool, (unsigned)wornCount);
            Emit(report, buf);

            for (size_t i = 0; i < wornCount; i++)
            {
                sprintf_s(buf, "[Equip] worn[%u] = %s", (unsigned)i, DefNameOf(pieces[i]).c_str());
                Emit(report, buf);
            }
        }
        else
            Emit(report, "[Equip] no CTCInventoryClothing found");

        void* weapons = FindTC(creature, TC_ID_INVENTORY_WEAPONS);
        CThing* melee = nullptr;
        CThing* ranged = nullptr;

        if (weapons)
        {
            const char* base = (const char*)weapons;
            melee = ThingFromIntelligentPointer(base + MELEE_CARRIED_OFFSET);
            ranged = ThingFromIntelligentPointer(base + RANGED_CARRIED_OFFSET);

            sprintf_s(buf, "[Equip] weapons TC @ %p, melee = %s, ranged = %s",
                weapons,
                melee ? DefNameOf(melee).c_str() : "<none>",
                ranged ? DefNameOf(ranged).c_str() : "<none>");
            Emit(report, buf);
        }
        else
            Emit(report, "[Equip] no CTCInventoryWeapons found");

        Emit(report, "[Equip] --- end ---");
        ObjectInspector::AppendToLogFile(report);

        // A dressed hero with no findings means our layout assumptions are
        // off again — capture the actual component contents for diagnosis.
        if (clothing && wornCount == 0)
            ObjectInspector::Dump("clothing TC (auto-dump: 0 worn found)", clothing, 0x200);
        if (weapons && !melee && !ranged)
            ObjectInspector::Dump("weapons TC (auto-dump: no weapons found)", weapons, 0x200);
    }

    void ProbeNextCandidate(CThingPlayerCreature* creature)
    {
        static size_t next = 0;

        void* clothing = FindTC(creature, TC_ID_INVENTORY_CLOTHING);
        CThing* pieces[1] = {};

        if (!clothing || !FindWornPieces(clothing, pieces, 1))
        {
            std::cout << "[Equip] probe: no worn clothing found on this hero"
                " (check NUMPAD8 output first)" << std::endl;
            return;
        }

        CThing* piece = pieces[0];

        if (next >= CANDIDATE_COUNT)
        {
            std::cout << "[Equip] probe: all " << CANDIDATE_COUNT
                << " candidates tried; restarting cycle" << std::endl;
            next = 0;
        }

        const auto& candidate = CANDIDATES[next++];

        // Logged (and flushed) before the call so a hard crash still tells
        // us which candidate died.
        std::cout << "[Equip] probe " << next << "/" << CANDIDATE_COUNT
            << ": vtbl slot " << candidate.slot << " @ 0x" << std::hex << candidate.va << std::dec
            << " (this=clothingTC, arg=" << DefNameOf(piece) << ")" << std::endl;

        unsigned long exceptionCode = 0;
        int result = GuardedThiscall1(candidate.va, clothing, piece, &exceptionCode);

        if (exceptionCode)
            std::cout << "[Equip] probe: EXCEPTION 0x" << std::hex << exceptionCode << std::dec
                << " (caught; game state may be unstable)" << std::endl;
        else
            std::cout << "[Equip] probe: returned " << result
                << " - did anything change on the hero?" << std::endl;
    }
}
