#include "NetPlayerManager.h"

void NetPlayerManager::BroadcastCreateLocalNetPlayer(int networkId, int defGlobalIndex, C3DVector position)
{
    world->AddUpdateRegionLoadCallback("BroadcastCreateLocalNetPlayer", [this, networkId, defGlobalIndex, position]() {
        if (world->RegionLoadStatus != CWorld::NOT_LOADING_REGION)
            return;

        world->RemoveUpdateRegionLoadCallback("BroadcastCreateLocalNetPlayer");

        SLNet::BitStream bs;
        bs.Write((SLNet::MessageID)ID_CREATE_NET_PLAYER);
        bs.Write(networkId);
        bs.Write(defGlobalIndex);
        bs.Write(position);

        network->SendToHost((const char*)bs.GetData(), bs.GetNumberOfBytesUsed());
    });
}

void NetPlayerManager::BroadcastCreateNetPlayer(int networkId, int defGlobalIndex, C3DVector position)
{
    SLNet::BitStream bsOut;
    bsOut.Write((SLNet::MessageID)ID_CREATE_NET_PLAYER);
    bsOut.Write(networkId);
    bsOut.Write(defGlobalIndex);
    bsOut.Write(position);

    network->SendToAllClientsExcept(networkId, (const char*)bsOut.GetData(), bsOut.GetNumberOfBytesUsed());
}

void NetPlayerManager::BroadcastCreateNetPlayers(int networkId)
{
    SLNet::BitStream bsOut;
    bsOut.Write((SLNet::MessageID)ID_CREATE_NET_PLAYERS);

    int count = (int)netPlayers.size() + (localNetPlayer ? 1 : 0);
    bsOut.Write(count);

    CDefinitionManager* definitionManager = CDefinitionManager::Get();

    if (localNetPlayer)
    {
        int localNetPlayerNetworkId = localNetPlayer->GetNetworkId();
        CThingPlayerCreature* creature = GetCreatureFromNetworkId(localNetPlayerNetworkId);

        if (!creature)
        {
            std::cout << "[NetPlayerManager::BroadcastCreateNetPlayers]: !creature" << std::endl;
            return;
        }

        C3DVector position = *((CThing*)creature)->GetPos();

        CDefString def;
        CCharString defName("");

        ((CThing*)creature)->GetDefName(&def);
        CDefStringTable::Get()->GetString(&defName, def.TablePos);

        int defGlobalIndex = definitionManager->GetDefGlobalIndexFromName(&defName);

        bsOut.Write(localNetPlayerNetworkId);
        bsOut.Write(defGlobalIndex);
        bsOut.Write(position);
    }

    for (const auto& netPlayer : netPlayers)
    {
        int netPlayerNetworkId = netPlayer->GetNetworkId();
        CThingPlayerCreature* creature = GetCreatureFromNetworkId(netPlayerNetworkId);

        if (!creature)
        {
            std::cout << "[NetPlayerManager::BroadcastCreateNetPlayers]: !creature" << std::endl;
            continue;
        }

        C3DVector position = *((CThing*)creature)->GetPos();

        CDefString def;
        CCharString defName("");

        ((CThing*)creature)->GetDefName(&def);
        CDefStringTable::Get()->GetString(&defName, def.TablePos);

        int defGlobalIndex = definitionManager->GetDefGlobalIndexFromName(&defName);

        bsOut.Write(netPlayerNetworkId);
        bsOut.Write(defGlobalIndex);
        bsOut.Write(position);
    }

    network->SendToClient(networkId, (const char*)bsOut.GetData(), bsOut.GetNumberOfBytesUsed());
}

void NetPlayerManager::BroadcastLocalNetPlayerMovement(int networkId)
{
    CThingPlayerCreature* creature = GetCreatureFromNetworkId(networkId);

    if (!creature) {
        std::cout << "[NetPlayerManager::BroadcastLocalNetPlayerMovement]: !creature" << std::endl;
        return;
    }

    creature->AddResolveMovementAccelerationCallback("ResolveMovementAcceleration" + std::to_string(networkId), [this, networkId, creature]() {
        C3DVector position = *((CThing*)creature)->GetPos();
        C3DVector movementAcceleration = creature->MovementAcceleration;

        SLNet::BitStream bs;
        bs.Write((SLNet::MessageID)ID_PLAYER_MOVEMENT);
        bs.Write(networkId);
        bs.Write(position);
        bs.Write(movementAcceleration);

        if (localNetPlayer->GetNetworkId() == 0) {
            network->SendToAllClientsExcept(networkId, (const char*)bs.GetData(), bs.GetNumberOfBytesUsed(), HIGH_PRIORITY, UNRELIABLE_SEQUENCED);
        }
        else {
            network->SendToHost((const char*)bs.GetData(), bs.GetNumberOfBytesUsed(), HIGH_PRIORITY, UNRELIABLE_SEQUENCED);
        }
    });
}
void NetPlayerManager::BroadcastLocalNetPlayerRotation(int networkId)
{
    CThingPlayerCreature* creature = GetCreatureFromNetworkId(networkId);

    if (!creature) {
        std::cout << "[NetPlayerManager::BroadcastLocalNetPlayerRotation]: !creature" << std::endl;
        return;
    }

    creature->AddResolveFacingDirectionCallback("ResolveFacingDirection" + std::to_string(networkId), [this, networkId, creature]() {
        CTCPhysicsBase* physicsTC = ((CThing*)creature)->PhysicsTC;
        CRightHandedSet* rhSet = ((CTCPhysicsStandard*)physicsTC)->GetRHSet();

        SLNet::BitStream bs;
        bs.Write((SLNet::MessageID)ID_PLAYER_ROTATION);
        bs.Write(networkId);
        bs.Write(rhSet->Up);
        bs.Write(rhSet->Forward);

        if (localNetPlayer->GetNetworkId() == 0) {
            network->SendToAllClientsExcept(networkId, (const char*)bs.GetData(), bs.GetNumberOfBytesUsed(), HIGH_PRIORITY, UNRELIABLE_SEQUENCED);
        }
        else
        {
            network->SendToHost((const char*)bs.GetData(), bs.GetNumberOfBytesUsed(), HIGH_PRIORITY, UNRELIABLE_SEQUENCED);
        }
        });
}

void NetPlayerManager::BroadcastDestroyNetPlayer(int networkId)
{
    SLNet::BitStream bsOut;
    bsOut.Write((SLNet::MessageID)ID_DESTROY_NET_PLAYER);
    bsOut.Write(networkId);

    network->SendToAllClientsExcept(networkId, (const char*)bsOut.GetData(), bsOut.GetNumberOfBytesUsed());
}
