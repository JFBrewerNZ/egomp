#include "HeroAppearanceModifiers.h"

namespace
{
    const size_t TC_LIST_OFFSET = 0x44;
    const int TC_ID_HERO_ATTACHABLE_APPEARANCE = 0x5E;

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

CTCHeroAttachableAppearanceModifiers* CTCHeroAttachableAppearanceModifiers::FromCreature(CThingPlayerCreature* creature)
{
    if (!creature)
        return nullptr;

    const char* list = *(const char* const*)((const char*)creature + TC_LIST_OFFSET);

    if (!list)
        return nullptr;

    // {int typeId, CTC*} pairs, typeIds ascending; a non-ascending id means
    // we ran off the end of the list.
    int previousId = -1;

    for (size_t i = 0; i < 96; i++)
    {
        int id = ((const int*)list)[i * 2];
        void* tc = ((void* const*)list)[i * 2 + 1];

        if (id <= previousId || id > 0x200 || !tc)
            break;

        if (id == TC_ID_HERO_ATTACHABLE_APPEARANCE)
            return (CTCHeroAttachableAppearanceModifiers*)tc;

        previousId = id;
    }

    return nullptr;
}

void CTCHeroAttachableAppearanceModifiers::AddModifier(int defGlobalIndex)
{
    ((void(__thiscall*)(void*, int))FN_ADD_MODIFIER)(this, defGlobalIndex);
}

void CTCHeroAttachableAppearanceModifiers::ResetAndRebuild()
{
    ((void(__thiscall*)(void*))FN_RESET_AND_REBUILD)(this);
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
