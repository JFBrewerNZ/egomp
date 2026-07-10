#pragma once

#include <iostream>
#include <vector>

#include "../../../SDK/Fable/SDK.h"

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
};
