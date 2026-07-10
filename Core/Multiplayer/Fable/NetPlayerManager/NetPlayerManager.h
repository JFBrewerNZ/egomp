#pragma once

#include <sstream>

#include "../../../SDK/Fable/SDK.h"
#include "../../Network/Network.h"

#include "../LocalNetPlayer/LocalNetPlayer.h"
#include "../NetPlayer/NetPlayer.h"

class NetPlayerManager
{
public:
    NetPlayerManager(Network* network, CPlayerManager* playerManager, CWorld* world);
    ~NetPlayerManager();

    void ConnectionNotification(int networkId, SystemAddress systemAddress);

    void CreateLocalNetPlayer(int networkId, C3DVector position, bool hostIsPlayer);
    void CreateNetPlayer(int networkId, C3DVector position, int defGlobalIndex);
    void CreateNetPlayers(int networkId, C3DVector position, int defGlobalIndex);

    void ReceiveNetPlayerMovement(int networkId, C3DVector remotePosition, C3DVector movementAcceleration);
    void ReceiveNetPlayerRotation(int networkId, C3DVector up, C3DVector forward);

    void DestroyLocalNetPlayer();
    void DestroyNetPlayer(int networkId);
    void DestroyNetPlayers();

private:
    Network* network;

    CPlayerManager* playerManager;
    CWorld* world;

    std::unique_ptr<LocalNetPlayer> localNetPlayer;
    std::vector<std::unique_ptr<NetPlayer>> netPlayers;

    void TeleportLocalNetPlayerToHost(int networkId, C3DVector position);
    void AnnounceLocalNetPlayer(int networkId);

    void ApplyNetPlayerMovement(int networkId);
    void ApplyNetPlayerRotation(int networkId);

    void BroadcastCreateLocalNetPlayer(int networkId, int defGlobalIndex, C3DVector position);
    void BroadcastCreateNetPlayer(int networkId, int defGlobalIndex, C3DVector position);
    void BroadcastCreateNetPlayers(int networkId);

    void BroadcastLocalNetPlayerMovement(int networkId);
    void BroadcastLocalNetPlayerRotation(int networkId);

    void BroadcastDestroyNetPlayer(int networkId);

    CThingPlayerCreature* GetCreatureFromNetworkId(int networkId) const;
    CThingPlayerCreature* GetCreatureFromLocalId(int localId) const;

    int GetDefGlobalIndex(CThingPlayerCreature* creature) const;

    int GetFreeLocalId();
    int GetLocalIdFromNetworkId(int networkId) const;
    int GetNetworkIdFromLocalId(int localId) const;
};
