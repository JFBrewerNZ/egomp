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

void NetPlayerManager::CreateNetPlayer(int networkId, C3DVector position, int defGlobalIndex, int regionIndex)
{
    NetPlayer* netPlayer = FindNetPlayer(networkId);

    if (!netPlayer)
    {
        std::unique_ptr<NetPlayer> created = std::make_unique<NetPlayer>();
        created->SetNetworkId(networkId);
        netPlayers.push_back(std::move(created));
        netPlayer = netPlayers.back().get();
    }

    netPlayer->SetDefGlobalIndex(defGlobalIndex);
    netPlayer->SetPosition(position);
    netPlayer->SetRegionIndex(regionIndex);

    UpdateNetPlayerSpawn(*netPlayer);

    if (localNetPlayer && localNetPlayer->GetNetworkId() == 0)
    {
        BroadcastCreateNetPlayer(networkId, defGlobalIndex, position, regionIndex);
        BroadcastCreateNetPlayers(networkId);
    }
}

void NetPlayerManager::CreateNetPlayers(int networkId, C3DVector position, int defGlobalIndex, int regionIndex)
{
    if (!localNetPlayer)
        return;

    if (networkId != localNetPlayer->GetNetworkId() && !FindNetPlayer(networkId))
    {
        CreateNetPlayer(networkId, position, defGlobalIndex, regionIndex);
    }
}

NetPlayer* NetPlayerManager::FindNetPlayer(int networkId)
{
    for (auto& netPlayer : netPlayers)
    {
        if (netPlayer && netPlayer->GetNetworkId() == networkId)
            return netPlayer.get();
    }

    return nullptr;
}

void NetPlayerManager::SpawnNetPlayer(NetPlayer& netPlayer)
{
    if (netPlayer.IsSpawned())
        return;

    int localId = GetFreeLocalId();
    playerManager->CreatePlayer(localId);
    CPlayer* player = playerManager->GetPlayer(localId);

    if (!player)
    {
        std::cout << "[NetPlayerManager::SpawnNetPlayer]: !player: " << netPlayer.GetNetworkId() << std::endl;
        return;
    }

    netPlayer.SetLocalId(localId);

    CThingPlayerCreatureInit init = {};
    CThingPlayerCreature* creature = CThingPlayerCreature::Create(netPlayer.GetDefGlobalIndex(), netPlayer.GetPosition(), localId, init);
    player->SetControlledCreature(creature);

    ApplyNetPlayerMovement(netPlayer.GetNetworkId());
    ApplyNetPlayerRotation(netPlayer.GetNetworkId());
}

void NetPlayerManager::DespawnNetPlayer(NetPlayer& netPlayer)
{
    if (!netPlayer.IsSpawned())
        return;

    int networkId = netPlayer.GetNetworkId();

    CThingPlayerCreature::RemoveResolveMovementAccelerationCallback("ResolveMovementAcceleration" + std::to_string(networkId));
    CThingPlayerCreature::RemoveResolveFacingDirectionCallback("ResolveFacingDirection" + std::to_string(networkId));

    CPlayer* player = playerManager->GetPlayer(netPlayer.GetLocalId());

    if (player)
    {
        CThingPlayerCreature* creature = player->GetPControlledCreature();

        if (creature)
        {
            player->UninitCharacter();
            player->Uninitialise();
        }
        else
            std::cout << "[NetPlayerManager::DespawnNetPlayer]: !creature: " << networkId << std::endl;
    }
    else
        std::cout << "[NetPlayerManager::DespawnNetPlayer]: !player: " << networkId << std::endl;

    netPlayer.SetLocalId(-1);
}

void NetPlayerManager::DespawnNetPlayers()
{
    for (auto& netPlayer : netPlayers)
    {
        if (netPlayer)
            DespawnNetPlayer(*netPlayer);
    }
}

void NetPlayerManager::UpdateNetPlayerSpawn(NetPlayer& netPlayer)
{
    bool sameRegion = netPlayer.GetRegionIndex() == (int)CWorldMap::GetCurrentRegionIndex();

    if (sameRegion && !netPlayer.IsSpawned())
        SpawnNetPlayer(netPlayer);
    else if (!sameRegion && netPlayer.IsSpawned())
        DespawnNetPlayer(netPlayer);
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
    // Always remove the entry and notify clients, even when the game-side
    // lookups inside DespawnNetPlayer fail — otherwise DestroyNetPlayers
    // loops forever and other clients keep a ghost player.
    for (size_t i = 0; i < netPlayers.size(); ++i)
    {
        if (netPlayers[i] && netPlayers[i]->GetNetworkId() == networkId)
        {
            DespawnNetPlayer(*netPlayers[i]);
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
    bs.Write((int)CWorldMap::GetCurrentRegionIndex());

    network->SendToHost((const char*)bs.GetData(), bs.GetNumberOfBytesUsed());
}

void NetPlayerManager::HandleLocalRegionLoad(long regionIndex)
{
    if (!localNetPlayer)
        return;

    CThingPlayerCreature* creature = GetCreatureFromLocalId(localNetPlayer->GetLocalId());
    C3DVector position = creature ? *((CThing*)creature)->GetPos() : C3DVector{};

    SLNet::BitStream bs;
    bs.Write((SLNet::MessageID)ID_PLAYER_REGION);
    bs.Write(localNetPlayer->GetNetworkId());
    bs.Write((int)regionIndex);
    bs.Write(position);

    if (localNetPlayer->GetNetworkId() == 0)
        network->SendToAllClients((const char*)bs.GetData(), bs.GetNumberOfBytesUsed());
    else
        network->SendToHost((const char*)bs.GetData(), bs.GetNumberOfBytesUsed());

    for (auto& netPlayer : netPlayers)
    {
        if (netPlayer)
            UpdateNetPlayerSpawn(*netPlayer);
    }
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
        bs.Write((int)CWorldMap::GetCurrentRegionIndex());

        network->SendToHost((const char*)bs.GetData(), bs.GetNumberOfBytesUsed());
        });
}

void NetPlayerManager::BroadcastCreateNetPlayer(int networkId, int defGlobalIndex, C3DVector position, int regionIndex)
{
    SLNet::BitStream bsOut;
    bsOut.Write((SLNet::MessageID)ID_CREATE_NET_PLAYER);
    bsOut.Write(networkId);
    bsOut.Write(defGlobalIndex);
    bsOut.Write(position);
    bsOut.Write(regionIndex);

    network->SendToAllClientsExcept(networkId, (const char*)bsOut.GetData(), bsOut.GetNumberOfBytesUsed());
}

void NetPlayerManager::BroadcastCreateNetPlayers(int networkId)
{
    // Entries are collected first: the count must reflect only the entries
    // actually written, and the local creature lookup can fail. Remote
    // entries come from stored state — their creatures only exist here when
    // they share the host's region.
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
            entries.Write((int)CWorldMap::GetCurrentRegionIndex());
            ++count;
        }
        else
            std::cout << "[NetPlayerManager::BroadcastCreateNetPlayers]: !creature" << std::endl;
    }

    for (const auto& netPlayer : netPlayers)
    {
        if (!netPlayer || netPlayer->GetNetworkId() == networkId)
            continue;

        entries.Write(netPlayer->GetNetworkId());
        entries.Write(netPlayer->GetDefGlobalIndex());
        entries.Write(netPlayer->GetPosition());
        entries.Write(netPlayer->GetRegionIndex());
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
