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
