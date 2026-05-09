#pragma once

#include <iostream>

#include "../../../SDK/Fable/SDK.h"

class NetPlayer
{
private:
    int networkId;
    int localId;

    C3DVector movementAcceleration;
    C3DVector position;
    CRightHandedSet rhSet;

public:
    NetPlayer();
    ~NetPlayer() = default;

    void SetNetworkId(int id);
    int GetNetworkId() const;

    void SetLocalId(int id);
    int GetLocalId() const;

    void SetMovementAcceleration(C3DVector movementAcceleration);
    C3DVector GetMovementAcceleration() const;

    void SetPosition(C3DVector position);
    C3DVector GetPosition() const;

    void SetRHSet(CRightHandedSet rhSet);
    CRightHandedSet GetRHSet() const;
};
