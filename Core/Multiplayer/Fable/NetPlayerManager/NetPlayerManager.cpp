#include "NetPlayerManager.h";

NetPlayerManager::NetPlayerManager(
    Network* network,
    CPlayerManager* playerManager,
    CWorld* world
)
    : network(network),
    playerManager(playerManager),
    world(world)
{
}

NetPlayerManager::~NetPlayerManager()
{
	network = nullptr;
}

void NetPlayerManager::CreateLocalNetPlayer(int networkId, C3DVector position)
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

    if (networkId != 0)
        TeleportLocalNetPlayerToHost(networkId, position);
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

    if (localNetPlayer->GetNetworkId() == 0)
    {
        BroadcastCreateNetPlayer(networkId, defGlobalIndex, position);
        BroadcastCreateNetPlayers(networkId);
    }
}

void NetPlayerManager::CreateNetPlayers(int networkId, C3DVector position, int defGlobalIndex)
{
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
    int localId = GetLocalIdFromNetworkId(networkId);
    CPlayer* localPlayer = playerManager->GetPlayer(localId);

    if (!localPlayer)
    {
        std::cout << "[NetPlayerManager::DestroyLocalNetPlayer]: !localPlayer" << std::endl;
        return;
    }

    CThingPlayerCreature* creature = localPlayer->GetPControlledCreature();

    if (!creature)
    {
        std::cout << "[NetPlayerManager::DestroyLocalNetPlayer]: !creature" << std::endl;
        return;
    }

    creature->RemoveResolveMovementAccelerationCallback("ResolveMovementAcceleration" + std::to_string(networkId));
    creature->RemoveResolveFacingDirectionCallback("ResolveFacingDirection" + std::to_string(networkId));

    localNetPlayer.reset();
}

void NetPlayerManager::DestroyNetPlayer(int networkId)
{
    int localId = GetLocalIdFromNetworkId(networkId);
    CPlayer* player = playerManager->GetPlayer(localId);

    if (!player)
    {
        std::cout << "[NetPlayerManager::DestroyNetPlayer]: !player: " << networkId << std::endl;
        return;
    }

    CThingPlayerCreature* creature = player->GetPControlledCreature();

    if (!creature)
    {
        std::cout << "[NetPlayerManager::DestroyNetPlayer]: !creature: " << networkId << std::endl;
        return;
    }

    creature->RemoveResolveMovementAccelerationCallback("ResolveMovementAcceleration" + std::to_string(networkId));
    creature->RemoveResolveFacingDirectionCallback("ResolveFacingDirection" + std::to_string(networkId));

    player->UninitCharacter();
    player->Uninitialise();

    for (size_t i = 0; i < netPlayers.size(); ++i)
    {
        if (netPlayers[i] && netPlayers[i]->GetNetworkId() == networkId)
        {
            netPlayers.erase(netPlayers.begin() + i);
            break;
        }
    }

    if (localNetPlayer->GetNetworkId() == 0)
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

    CDefString def;
    CCharString defName("");

    ((CThing*)creature)->GetDefName(&def);
    CDefStringTable::Get()->GetString(&defName, def.TablePos);

    CDefinitionManager* definitionManager = CDefinitionManager::Get();
    int defGlobalIndex = definitionManager->GetDefGlobalIndexFromName(&defName);

    CTCPhysicsBase* physicsTC = ((CThing*)creature)->PhysicsTC;
    float facingAngleXY = ((CTCPhysicsStandard*)physicsTC)->GetFacingAngleXY();

    BroadcastCreateLocalNetPlayer(networkId, defGlobalIndex, position);
    world->SetAsLoadingRegion(position, facingAngleXY, false, false, false);
}

void NetPlayerManager::ApplyNetPlayerMovement(int networkId)
{
    CThingPlayerCreature* creature = GetCreatureFromNetworkId(networkId);

    if (!creature) {
        std::cout << "[NetPlayerManager::ApplyNetPlayerMovement]: !creature" << std::endl;
        return;
    }

    creature->AddResolveMovementAccelerationCallback("ResolveMovementAcceleration" + std::to_string(networkId), [this, networkId, creature]() {
        for (auto& netPlayer : netPlayers)
        {
            if (netPlayer && netPlayer->GetNetworkId() == networkId)
            {
                creature->MovementAcceleration = netPlayer->GetMovementAcceleration();

                C3DVector remotePosition = netPlayer->GetPosition();
                C3DVector position = *((CThing*)creature)->GetPos();

                float dx = remotePosition.X - position.X;
                float dy = remotePosition.Y - position.Y;
                float dz = remotePosition.Z - position.Z;

                float driftSq = (dx * dx) + (dy * dy) + (dz * dz);

                if (driftSq > 1)
                    ((CThing*)creature)->PhysicsTC->SetPosition(remotePosition);

                return;
            }
        }
        });
}

void NetPlayerManager::ApplyNetPlayerRotation(int networkId)
{
    CThingPlayerCreature* creature = GetCreatureFromNetworkId(networkId);

    if (!creature) {
        std::cout << "[NetPlayerManager::ApplyNetPlayerRotation]: !creature" << std::endl;
        return;
    }

    creature->AddResolveFacingDirectionCallback("ResolveFacingDirection" + std::to_string(networkId), [this, networkId, creature]() {
        for (auto& netPlayer : netPlayers)
        {
            if (netPlayer && netPlayer->GetNetworkId() == networkId)
            {
                CRightHandedSet rhSet = netPlayer->GetRHSet();

                CTCPhysicsBase* physicsTC = ((CThing*)creature)->PhysicsTC;
                ((CTCPhysicsStandard*)physicsTC)->SetRHSet(rhSet);

                return;
            }
        }
        });
}
