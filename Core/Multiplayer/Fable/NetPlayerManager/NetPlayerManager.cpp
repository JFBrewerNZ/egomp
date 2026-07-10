#include "NetPlayerManager.h"

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
    // The callback maps this manager registers into are static and outlive it;
    // anything left behind would fire with a dangling `this`.
    world->RemoveUpdateRegionLoadCallback("BroadcastCreateLocalNetPlayer");

    DestroyLocalNetPlayer();

    while (!netPlayers.empty())
        DestroyNetPlayer(netPlayers.front()->GetNetworkId());

    network = nullptr;
}
