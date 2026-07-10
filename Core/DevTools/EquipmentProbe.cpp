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

    // CTCInventoryClothing worn-armour slots: array of
    // {CThingObject* piece, CHitLocationDef*, CArmourDef*, ...} entries,
    // stride 0x28, first entry 0x28 into the buffer.
    const size_t WORN_ARRAY_OFFSET = 0x14C;
    const size_t WORN_FIRST_ENTRY = 0x28;
    const size_t WORN_STRIDE = 0x28;
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

    CThing* WornPiece(void* clothingTC, size_t slot)
    {
        const char* array = *(const char* const*)((const char*)clothingTC + WORN_ARRAY_OFFSET);
        if (!array)
            return nullptr;
        return AsThing(*(void* const*)(array + WORN_FIRST_ENTRY + slot * WORN_STRIDE));
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
    void DumpEquipment(CThingPlayerCreature* creature)
    {
        std::cout << "[Equip] --- equipment of hero @ " << creature << " ---" << std::endl;

        if (void* clothing = FindTC(creature, TC_ID_INVENTORY_CLOTHING))
        {
            for (size_t slot = 0; slot < WORN_MAX_SLOTS; slot++)
            {
                if (CThing* piece = WornPiece(clothing, slot))
                    std::cout << "[Equip] worn[" << slot << "] = " << DefNameOf(piece) << std::endl;
            }
        }
        else
            std::cout << "[Equip] no CTCInventoryClothing found" << std::endl;

        if (void* weapons = FindTC(creature, TC_ID_INVENTORY_WEAPONS))
        {
            const char* base = (const char*)weapons;
            if (CThing* melee = ThingFromIntelligentPointer(base + MELEE_CARRIED_OFFSET))
                std::cout << "[Equip] melee carried = " << DefNameOf(melee) << std::endl;
            if (CThing* ranged = ThingFromIntelligentPointer(base + RANGED_CARRIED_OFFSET))
                std::cout << "[Equip] ranged carried = " << DefNameOf(ranged) << std::endl;
        }
        else
            std::cout << "[Equip] no CTCInventoryWeapons found" << std::endl;

        std::cout << "[Equip] --- end ---" << std::endl;
    }

    void ProbeNextCandidate(CThingPlayerCreature* creature)
    {
        static size_t next = 0;

        void* clothing = FindTC(creature, TC_ID_INVENTORY_CLOTHING);
        CThing* piece = clothing ? WornPiece(clothing, 0) : nullptr;

        if (!piece)
        {
            std::cout << "[Equip] probe: need CTCInventoryClothing with a worn slot 0" << std::endl;
            return;
        }

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
