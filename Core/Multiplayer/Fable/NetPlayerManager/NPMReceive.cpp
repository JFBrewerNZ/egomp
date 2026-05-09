#include "NetPlayerManager.h"

void NetPlayerManager::ReceiveNetPlayerMovement(int networkId, C3DVector remotePosition, C3DVector movementAcceleration)
{
    if (localNetPlayer && localNetPlayer->GetNetworkId() == 0)
    {
        SLNet::BitStream bs;
        bs.Write((SLNet::MessageID)ID_PLAYER_MOVEMENT);
        bs.Write(networkId);
        bs.Write(remotePosition);
        bs.Write(movementAcceleration);

        network->SendToAllClientsExcept(networkId, (const char*)bs.GetData(), bs.GetNumberOfBytesUsed(), HIGH_PRIORITY, UNRELIABLE_SEQUENCED);
    }

    for (auto& netPlayer : netPlayers)
    {
        if (netPlayer && netPlayer->GetNetworkId() == networkId)
        {
            netPlayer->SetMovementAcceleration(movementAcceleration);
            netPlayer->SetPosition(remotePosition);

            return;
        }
    }
}

void NetPlayerManager::ReceiveNetPlayerRotation(int networkId, C3DVector up, C3DVector forward)
{
    if (localNetPlayer && localNetPlayer->GetNetworkId() == 0)
    {
        SLNet::BitStream bs;
        bs.Write((SLNet::MessageID)ID_PLAYER_ROTATION);
        bs.Write(networkId);
        bs.Write(up);
        bs.Write(forward);

        network->SendToAllClientsExcept(networkId, (const char*)bs.GetData(), bs.GetNumberOfBytesUsed(), HIGH_PRIORITY, UNRELIABLE_SEQUENCED);
    }

    for (auto& netPlayer : netPlayers)
    {
        if (netPlayer && netPlayer->GetNetworkId() == networkId)
        {
            CRightHandedSet rhSet;
            rhSet.Up = up;
            rhSet.Forward = forward;

            netPlayer->SetRHSet(rhSet);

            return;
        }
    }
}
