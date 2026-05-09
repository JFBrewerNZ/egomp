#pragma once

#include <iostream>

#include "../../../SDK/Fable/SDK.h"

class LocalNetPlayer
{
private:
    int networkId;
    int localId;

public:
    LocalNetPlayer();
    ~LocalNetPlayer() = default;

    void SetNetworkId(int id);
    int GetNetworkId() const;

    void SetLocalId(int id);
    int GetLocalId() const;
};
