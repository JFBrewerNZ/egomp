#include "HeroAppearanceModifiers.h"

namespace
{
    const size_t TC_LIST_OFFSET = 0x44;
    const int TC_ID_HERO_ATTACHABLE_APPEARANCE = 0x5E;
    const int TC_ID_HERO_MORPH = 0x03;
    const int TC_ID_HERO_STATS = 0x04;

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
