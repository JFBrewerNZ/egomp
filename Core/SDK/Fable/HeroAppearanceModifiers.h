#pragma once

#include <vector>

#include "ThingPlayerCreature.h"

// CTCHeroAttachableAppearanceModifiers — the thing component that attaches
// visual appearance modifiers (hair, beards, hats, horns, clothing visuals)
// to a hero creature. Layout and functions recovered from the component's
// serializer (0x706F40) and appearance-reset vfunc (0x7079E0); see
// Tools/RE-NOTES.md.
//
// [this+0x30] -> struct of three vectors (0xC bytes apart) of 8-byte
// entries { int defGlobalIndex, unsigned char flags }.
class CTCHeroAttachableAppearanceModifiers
{
public:
    // Walks the creature's thing-component list (CThing+0x44, {typeId, TC*}
    // pairs, typeId 0x5E). Returns nullptr if absent.
    static CTCHeroAttachableAppearanceModifiers* FromCreature(CThingPlayerCreature* creature);

    // Fable.exe 0x706880. Instantiates the modifier def and attaches it.
    void AddModifier(int defGlobalIndex);

    // Fable.exe 0x7079E0 (vtable slot 4): re-applies the creature def's
    // modifiers, re-adds OBJECT_HERO_HORNS (alignment-scaled), rebuilds the
    // attachment state and refreshes the creature's graphic appearance.
    // Called after AddModifier to make changes visible.
    void ResetAndRebuild();

    // Def indexes of all currently attached modifiers, across all three
    // modifier sets.
    std::vector<int> GetModifierDefIndexes();
};

// CTCHeroMorph body-shape inputs — normalized 0..1 floats the engine
// continuously morphs the hero model toward. Member offsets from the
// component's serializer (0x71D020).
struct HeroMorphValues
{
    float strength = 0.0f;
    float will = 0.0f;
    float skill = 0.0f;
    float age = 0.0f;
    float morality = 0.5f;
    float fatness = 0.5f;
    float tan = 0.0f;

    bool operator==(const HeroMorphValues& o) const
    {
        return strength == o.strength && will == o.will && skill == o.skill
            && age == o.age && morality == o.morality && fatness == o.fatness
            && tan == o.tan;
    }
    bool operator!=(const HeroMorphValues& o) const { return !(*this == o); }
};

class CTCHeroMorph
{
public:
    static CTCHeroMorph* FromCreature(CThingPlayerCreature* creature);

    HeroMorphValues GetValues();
    void SetValues(const HeroMorphValues& values);
};

// CTCHeroStats "ExperienceSpentOn" ledger (member map from the serializer
// @0x57E2F1): +0x18 points at 12 ints of experience spent per stat — the
// values body physique derives from (their sum equals TotalSpentExperience).
const size_t HERO_STAT_EXPERIENCE_COUNT = 12;

struct HeroStatsExperience
{
    int spentOn[HERO_STAT_EXPERIENCE_COUNT] = {};

    bool operator==(const HeroStatsExperience& o) const
    {
        for (size_t i = 0; i < HERO_STAT_EXPERIENCE_COUNT; i++)
            if (spentOn[i] != o.spentOn[i])
                return false;
        return true;
    }
    bool operator!=(const HeroStatsExperience& o) const { return !(*this == o); }
};

class CTCHeroStats
{
public:
    static CTCHeroStats* FromCreature(CThingPlayerCreature* creature);

    // false if the ledger buffer is absent.
    bool GetExperienceSpentOn(HeroStatsExperience& out);
    void SetExperienceSpentOn(const HeroStatsExperience& values);
};
