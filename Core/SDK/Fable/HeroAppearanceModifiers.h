#pragma once

#include <vector>

#include "ThingPlayerCreature.h"

class CThing;

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

    // Empties the three modifier-set vectors (entries are POD {defIndex,
    // flags}; capacity is left allocated). Combined with
    // RebuildAttachments this removes modifiers — including the creature
    // def's defaults — without needing the game's remove function.
    void ClearModifiers();

    // The rebuild tail of the reset vfunc, without its re-add-defaults
    // head: 0x706040 (mark), 0x706510 (clear the attachment map and
    // rebuild it from the modifier sets), 0x7070D0 (graphics-side
    // rebuild), then refresh the owner creature's graphic appearance.
    void RebuildAttachments();

    // Def indexes of all currently attached modifiers, across all three
    // modifier sets.
    std::vector<int> GetModifierDefIndexes();
};

// CTCHeroMorph body-shape input block at +0x40..+0x63: strength (muscle;
// 1.0 on a maxed hero, ~0.05 on the apprentice), will (1.0 -> glowing
// hands), skill, age, morality (1.0 -> halo), fatness, tan, teenager flag.
// Synced as a raw blob — the exact per-field labels cost us three wrong
// iterations, the engine only cares about the bytes. The dirty flag at
// +0x3D plus the update pump (vtable slot 28 @0x71E130: posts
// CMessageOnMorphChanged, recomputes, refreshes graphics, clears the
// flag) make changes visible.
const size_t HERO_MORPH_BLOB_DWORDS = 9; // +0x40 .. +0x63

struct HeroMorphValues
{
    unsigned int raw[HERO_MORPH_BLOB_DWORDS] = {};

    bool operator==(const HeroMorphValues& o) const
    {
        for (size_t i = 0; i < HERO_MORPH_BLOB_DWORDS; i++)
            if (raw[i] != o.raw[i])
                return false;
        return true;
    }
    bool operator!=(const HeroMorphValues& o) const { return !(*this == o); }
};

class CTCHeroMorph
{
public:
    static CTCHeroMorph* FromCreature(CThingPlayerCreature* creature);

    HeroMorphValues GetValues();

    // Writes the blob; when it differs from the current one, also sets the
    // dirty flag and runs the update pump so the change becomes visible.
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

// CTCInventoryWeapons — the hero's weapons component (thing-component id
// 0x13). Carried-weapon def-holders are CIntelligentPointers at +0x134
// (melee) / +0x148 (ranged); accessor 0xA01B50 dereferences them,
// 0xA01B90 assigns with correct refcounting.
class CTCInventoryWeapons
{
public:
    static CTCInventoryWeapons* FromCreature(CThingPlayerCreature* creature);

    // Global def index of the carried melee / ranged weapon, or -1.
    int GetCarriedMeleeDefIndex();
    int GetCarriedRangedDefIndex();

    // The carried melee weapon CThing itself (holder deref), or nullptr.
    CThing* GetCarriedMeleeThing();

    // The game's own carried-weapon creator (0x5BE8F3), traced from the
    // inventory-menu equip: validates the def, factory-creates the weapon
    // with the CREATURE's mode byte (+0x90) at the creature's position,
    // and copies the inventory record's augmentation data onto the new
    // weapon's components — the wiring hand-made factory objects lacked.
    // Returns the carried-ready weapon, or nullptr. SEH-guarded.
    CThing* CreateCarriedWeapon(int defGlobalIndex);

    // Writes the carried-weapon holder (CIntelligentPointer::Assign,
    // 0xA01B90). Pass nullptr to clear. Call RegenerateCarriedWeapons
    // afterwards to rebuild the on-back visuals.
    void SetCarriedMeleeWeapon(CThing* weapon);
    void SetCarriedRangedWeapon(CThing* weapon);

    // 0x5C9962: rebuilds (or clears, depending on the creature's sheathed
    // mode) the carried-weapon back visuals from the holders — the save-
    // load restore path.
    void RegenerateCarriedWeapons();

    // Creates a weapon object from a def index next to the creature and
    // posts a pickup action so the creature acquires it. Returns the
    // created world object (before the pickup completes), or nullptr.
    static CThing* GiveWeapon(CThingPlayerCreature* creature, int defGlobalIndex);
};

// Replicated combat actions (see the tracer capture in RE-NOTES.md). Most
// hero actions take only the creature, so each is built with a
// build(buffer, creature) wrapper and posted through DoCreatureAction.
enum class CombatActionType
{
    Roll = 0,   // CCombatAction_ControlledStrafeJump
    Block = 1,  // CCreatureAction_StartBlocking
    Count
};

namespace CombatActions
{
    // Reconstructs the given action on the creature and posts it via
    // DoCreatureAction. `direction` is the sender's horizontal movement
    // direction at action time (world space), used by directional moves
    // like the roll. SEH-guarded; unknown types are a no-op.
    void Perform(CThingPlayerCreature* creature, CombatActionType type, const C3DVector& direction);
}
