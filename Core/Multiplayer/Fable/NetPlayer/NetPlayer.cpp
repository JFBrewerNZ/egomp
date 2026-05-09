#include "NetPlayer.h"

NetPlayer::NetPlayer() :
    networkId(-1),
    localId(-1)
{
    movementAcceleration = C3DVector();
    position = C3DVector();
    rhSet = CRightHandedSet();
}

void NetPlayer::SetNetworkId(int id)
{
    networkId = id;
}

int NetPlayer::GetNetworkId() const
{
    return networkId;
}

void NetPlayer::SetLocalId(int id)
{
    localId = id;
}

int NetPlayer::GetLocalId() const
{
    return localId;
}

void NetPlayer::SetMovementAcceleration(C3DVector movementAcceleration)
{
    this->movementAcceleration = movementAcceleration;
}

C3DVector NetPlayer::GetMovementAcceleration() const
{
    return movementAcceleration;
}

void NetPlayer::SetPosition(C3DVector position)
{
    this->position = position;
}

C3DVector NetPlayer::GetPosition() const
{
    return this->position;
}

void NetPlayer::SetRHSet(CRightHandedSet rhSet)
{
    this->rhSet = rhSet;
}

CRightHandedSet NetPlayer::GetRHSet() const
{
    return rhSet;
}
