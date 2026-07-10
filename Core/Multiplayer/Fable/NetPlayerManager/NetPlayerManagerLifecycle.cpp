#include "NetPlayerManager.h"

void NetPlayerManager::CreateLocalNetPlayer(int networkId, C3DVector position, bool hostIsPlayer)
{
    int localId = GetFreeLocalId();
    CThingPlayerCreature* creature = GetCreatureFromLocalId(localId);

    if (!creature)
    {
        std::cout << "[NetPlayerManager::CreateLocalNetPlayer]: !creature" << std::endl;
        return;
    }

    if (!localNetPlayer)
        localNetPlayer = std::make_unique<LocalNetPlayer>();

    localNetPlayer->SetLocalId(localId);
    localNetPlayer->SetNetworkId(networkId);

    BroadcastLocalNetPlayerMovement(networkId);
    BroadcastLocalNetPlayerRotation(networkId);

    if (networkId == 0)
        return;

    if (hostIsPlayer)
        TeleportLocalNetPlayerToHost(networkId, position);
    else
        AnnounceLocalNetPlayer(networkId);
}

void NetPlayerManager::CreateNetPlayer(int networkId, C3DVector position, int defGlobalIndex)
{
    int localId = GetFreeLocalId();
    playerManager->CreatePlayer(localId);
    CPlayer* player = playerManager->GetPlayer(localId);

    if (!player)
    {
        std::cout << "[NetPlayerManager::CreateNetPlayer]: !player: " << networkId << std::endl;
        return;
    }

    std::unique_ptr<NetPlayer> netPlayer = std::make_unique<NetPlayer>();
    netPlayer->SetNetworkId(networkId);
    netPlayer->SetLocalId(localId);
    netPlayers.push_back(std::move(netPlayer));

    CThingPlayerCreatureInit init = {};
    CThingPlayerCreature* creature = CThingPlayerCreature::Create(defGlobalIndex, position, localId, init);
    player->SetControlledCreature(creature);

    ApplyNetPlayerMovement(networkId);
    ApplyNetPlayerRotation(networkId);

    if (localNetPlayer && localNetPlayer->GetNetworkId() == 0)
    {
        BroadcastCreateNetPlayer(networkId, defGlobalIndex, position);
        BroadcastCreateNetPlayers(networkId);
    }
}

void NetPlayerManager::CreateNetPlayers(int networkId, C3DVector position, int defGlobalIndex)
{
    if (!localNetPlayer)
        return;

    if (networkId != localNetPlayer->GetNetworkId() && GetLocalIdFromNetworkId(networkId) == -1)
    {
        CreateNetPlayer(networkId, position, defGlobalIndex);
    }
}

void NetPlayerManager::DestroyLocalNetPlayer()
{
    if (!localNetPlayer)
        return;

    int networkId = localNetPlayer->GetNetworkId();

    CThingPlayerCreature::RemoveResolveMovementAccelerationCallback("ResolveMovementAcceleration" + std::to_string(networkId));
    CThingPlayerCreature::RemoveResolveFacingDirectionCallback("ResolveFacingDirection" + std::to_string(networkId));

    localNetPlayer.reset();
}

void NetPlayerManager::DestroyNetPlayer(int networkId)
{
    CThingPlayerCreature::RemoveResolveMovementAccelerationCallback("ResolveMovementAcceleration" + std::to_string(networkId));
    CThingPlayerCreature::RemoveResolveFacingDirectionCallback("ResolveFacingDirection" + std::to_string(networkId));

    int localId = GetLocalIdFromNetworkId(networkId);
    CPlayer* player = localId != -1 ? playerManager->GetPlayer(localId) : nullptr;

    if (player)
    {
        CThingPlayerCreature* creature = player->GetPControlledCreature();

        if (creature)
        {
            player->UninitCharacter();
            player->Uninitialise();
        }
        else
            std::cout << "[NetPlayerManager::DestroyNetPlayer]: !creature: " << networkId << std::endl;
    }
    else
        std::cout << "[NetPlayerManager::DestroyNetPlayer]: !player: " << networkId << std::endl;

    // Always remove the entry and notify clients, even when the game-side
    // lookups fail — otherwise DestroyNetPlayers loops forever and other
    // clients keep a ghost player.
    for (size_t i = 0; i < netPlayers.size(); ++i)
    {
        if (netPlayers[i] && netPlayers[i]->GetNetworkId() == networkId)
        {
            netPlayers.erase(netPlayers.begin() + i);
            break;
        }
    }

    if (localNetPlayer && localNetPlayer->GetNetworkId() == 0)
        BroadcastDestroyNetPlayer(networkId);
}

void NetPlayerManager::DestroyNetPlayers()
{
    while (!netPlayers.empty())
    {
        int networkId = netPlayers.front()->GetNetworkId();
        DestroyNetPlayer(networkId);
    }
}

void NetPlayerManager::TeleportLocalNetPlayerToHost(int networkId, C3DVector position)
{
    CThingPlayerCreature* creature = GetCreatureFromNetworkId(networkId);

    if (!creature) {
        std::cout << "[NetPlayerManager::TeleportLocalNetPlayerToHost]: !creature" << std::endl;
        return;
    }

    int defGlobalIndex = GetDefGlobalIndex(creature);

    CTCPhysicsBase* physicsTC = ((CThing*)creature)->PhysicsTC;
    float facingAngleXY = ((CTCPhysicsStandard*)physicsTC)->GetFacingAngleXY();

    BroadcastCreateLocalNetPlayer(networkId, defGlobalIndex, position);
    world->SetAsLoadingRegion(position, facingAngleXY, false, false, false);
}

void NetPlayerManager::AnnounceLocalNetPlayer(int networkId)
{
    CThingPlayerCreature* creature = GetCreatureFromNetworkId(networkId);

    if (!creature) {
        std::cout << "[NetPlayerManager::AnnounceLocalNetPlayer]: !creature" << std::endl;
        return;
    }

    int defGlobalIndex = GetDefGlobalIndex(creature);
    C3DVector position = *((CThing*)creature)->GetPos();

    SLNet::BitStream bs;
    bs.Write((SLNet::MessageID)ID_CREATE_NET_PLAYER);
    bs.Write(networkId);
    bs.Write(defGlobalIndex);
    bs.Write(position);

    network->SendToHost((const char*)bs.GetData(), bs.GetNumberOfBytesUsed());
}

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
    // Entries are collected first: the count must reflect only the entries
    // actually written, and creature lookups can fail.
    SLNet::BitStream entries;
    int count = 0;

    if (localNetPlayer)
    {
        int localNetPlayerNetworkId = localNetPlayer->GetNetworkId();
        CThingPlayerCreature* creature = GetCreatureFromNetworkId(localNetPlayerNetworkId);

        if (creature)
        {
            entries.Write(localNetPlayerNetworkId);
            entries.Write(GetDefGlobalIndex(creature));
            entries.Write(*((CThing*)creature)->GetPos());
            ++count;
        }
        else
            std::cout << "[NetPlayerManager::BroadcastCreateNetPlayers]: !creature" << std::endl;
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

        entries.Write(netPlayerNetworkId);
        entries.Write(GetDefGlobalIndex(creature));
        entries.Write(*((CThing*)creature)->GetPos());
        ++count;
    }

    if (count == 0)
        return;

    SLNet::BitStream bsOut;
    bsOut.Write((SLNet::MessageID)ID_CREATE_NET_PLAYERS);
    bsOut.Write(count);
    bsOut.Write(&entries);

    network->SendToClient(networkId, (const char*)bsOut.GetData(), bsOut.GetNumberOfBytesUsed());
}

void NetPlayerManager::BroadcastDestroyNetPlayer(int networkId)
{
    SLNet::BitStream bsOut;
    bsOut.Write((SLNet::MessageID)ID_DESTROY_NET_PLAYER);
    bsOut.Write(networkId);

    network->SendToAllClientsExcept(networkId, (const char*)bsOut.GetData(), bsOut.GetNumberOfBytesUsed());
}
