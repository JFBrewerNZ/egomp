#pragma once

#include <iostream>
#include <vector>

#include "../../../SDK/Fable/SDK.h"
#include "../../../SDK/Fable/HeroAppearanceModifiers.h"

// State for one remote player. The entry always exists while the player is in
// the session; its in-world creature exists only while both players are in
// the same region (localId is -1 while despawned).
class NetPlayer
{
private:
    int networkId;
    int localId;

    int defGlobalIndex = -1;
    int regionIndex = -1;

    C3DVector movementAcceleration;
    C3DVector position;
    CRightHandedSet rhSet;

    std::vector<int> appearanceDefIndexes;
    HeroMorphValues morphValues;
    HeroStatsExperience statsExperience;
    bool hasMorphValues = false;

    int meleeWeaponDef = -1;
    int rangedWeaponDef = -1;
    int appliedMeleeWeaponDef = -1;  // last def actually given to the creature
    int appliedRangedWeaponDef = -1;

public:
    NetPlayer();
    ~NetPlayer() = default;

    void SetNetworkId(int id);
    int GetNetworkId() const;

    void SetLocalId(int id);
    int GetLocalId() const;

    void SetDefGlobalIndex(int index) { defGlobalIndex = index; }
    int GetDefGlobalIndex() const { return defGlobalIndex; }

    void SetRegionIndex(int index) { regionIndex = index; }
    int GetRegionIndex() const { return regionIndex; }

    bool IsSpawned() const { return localId != -1; }

    void SetMovementAcceleration(C3DVector movementAcceleration);
    C3DVector GetMovementAcceleration() const;

    void SetPosition(C3DVector position);
    C3DVector GetPosition() const;

    void SetRHSet(CRightHandedSet rhSet);
    CRightHandedSet GetRHSet() const;

    void SetAppearance(std::vector<int> modifierDefIndexes) { appearanceDefIndexes = std::move(modifierDefIndexes); }
    const std::vector<int>& GetAppearance() const { return appearanceDefIndexes; }

    void SetMorphValues(const HeroMorphValues& values) { morphValues = values; hasMorphValues = true; }
    bool HasMorphValues() const { return hasMorphValues; }
    const HeroMorphValues& GetMorphValues() const { return morphValues; }

    void SetStatsExperience(const HeroStatsExperience& values) { statsExperience = values; }
    const HeroStatsExperience& GetStatsExperience() const { return statsExperience; }

    void SetWeaponDefs(int melee, int ranged) { meleeWeaponDef = melee; rangedWeaponDef = ranged; }
    int GetMeleeWeaponDef() const { return meleeWeaponDef; }
    int GetRangedWeaponDef() const { return rangedWeaponDef; }

    int GetAppliedMeleeWeaponDef() const { return appliedMeleeWeaponDef; }
    int GetAppliedRangedWeaponDef() const { return appliedRangedWeaponDef; }
    void SetAppliedWeaponDefs(int melee, int ranged) { appliedMeleeWeaponDef = melee; appliedRangedWeaponDef = ranged; }
};
