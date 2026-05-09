#include "LocalNetPlayer.h"

LocalNetPlayer::LocalNetPlayer() :
    networkId(-1),
    localId(-1)
{
}

void LocalNetPlayer::SetNetworkId(int id)
{
    networkId = id;
}

int LocalNetPlayer::GetNetworkId() const
{
    return networkId;
}

void LocalNetPlayer::SetLocalId(int id)
{
    localId = id;
}

int LocalNetPlayer::GetLocalId() const
{
    return localId;
}
