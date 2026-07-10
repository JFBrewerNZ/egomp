#include "NetPlayerManager.h"

void NetPlayerManager::ConnectionNotification(int networkId, SystemAddress systemAddress)
{
    if (!localNetPlayer)
    {
        std::cout << "[NetPlayerManager::ConnectionNotification]: !localNetPlayer" << std::endl;
        return;
    }

    int localId = localNetPlayer->GetLocalId();
    CThingPlayerCreature* creature = GetCreatureFromLocalId(localId);

    if (!creature) {
        std::cout << "[NetPlayerManager::ConnectionNotification]: !creature" << std::endl;
        return;
    }

    C3DVector position = *((CThing*)creature)->GetPos();

    SLNet::BitStream bs;
    bs.Write((SLNet::MessageID)ID_CREATE_LOCAL_NET_PLAYER);
    bs.Write(networkId);
    bs.Write(position);

    network->SendTo((const char*)bs.GetData(), bs.GetNumberOfBytesUsed(), HIGH_PRIORITY, RELIABLE_ORDERED, systemAddress);
}
