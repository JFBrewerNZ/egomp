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
    // Remote creatures must not outlive the region they were spawned in:
    // tear them down just before a region unloads, respawn matching ones
    // once the new region has loaded (and tell the session where we are).
    CWorld::AddSetAsLoadingRegionCallback("NetPlayerManager", [this]() {
        DespawnNetPlayers();
    });
    CWorldMap::AddPostRegionLoadCallback("NetPlayerManager", [this](long regionIndex) {
        HandleLocalRegionLoad(regionIndex);
    });
}

NetPlayerManager::~NetPlayerManager()
{
    // The callback maps this manager registers into are static and outlive it;
    // anything left behind would fire with a dangling `this`.
    CWorld::RemoveSetAsLoadingRegionCallback("NetPlayerManager");
    CWorldMap::RemovePostRegionLoadCallback("NetPlayerManager");
    world->RemoveUpdateRegionLoadCallback("BroadcastCreateLocalNetPlayer");

    DestroyLocalNetPlayer();

    while (!netPlayers.empty())
        DestroyNetPlayer(netPlayers.front()->GetNetworkId());

    network = nullptr;
}
