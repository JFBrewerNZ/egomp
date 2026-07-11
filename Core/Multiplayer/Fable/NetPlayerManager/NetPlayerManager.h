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
    void CreateNetPlayer(int networkId, C3DVector position, int defGlobalIndex, int regionIndex);
    void CreateNetPlayers(int networkId, C3DVector position, int defGlobalIndex, int regionIndex);

    void ReceiveNetPlayerMovement(int networkId, C3DVector remotePosition, C3DVector movementAcceleration);
    void ReceiveNetPlayerRotation(int networkId, C3DVector up, C3DVector forward);
    void ReceiveNetPlayerRegion(int networkId, int regionIndex, C3DVector position);
    void ReceiveNetPlayerAppearance(int networkId, HeroMorphValues morph, HeroStatsExperience exp, int meleeWeaponDef, int rangedWeaponDef, std::vector<int> modifierDefIndexes);

    // Fires (game thread) for every creature action. Broadcasts the local
    // hero's whitelisted combat actions so remote puppets mirror them.
    void HandleLocalCreatureAction(void* creature, const char* actionClass);
    void ReceiveNetPlayerAction(int networkId, int actionType);

    // Called every game update: broadcasts the local hero's appearance
    // whenever the modifier set changes (throttled).
    void UpdateAppearanceSync();

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

    NetPlayer* FindNetPlayer(int networkId);
    void SpawnNetPlayer(NetPlayer& netPlayer);
    void DespawnNetPlayer(NetPlayer& netPlayer);
    void DespawnNetPlayers();
    void UpdateNetPlayerSpawn(NetPlayer& netPlayer);
    void HandleLocalRegionLoad(long regionIndex);

    void ApplyNetPlayerMovement(int networkId);
    void ApplyNetPlayerRotation(int networkId);
    void ApplyNetPlayerAppearance(NetPlayer& netPlayer);
    void ApplyNetPlayerWeapons(NetPlayer& netPlayer);

    void BroadcastLocalNetPlayerAppearance(const HeroMorphValues& morph, const HeroStatsExperience& exp, int meleeWeaponDef, int rangedWeaponDef, const std::vector<int>& modifierDefIndexes);
    void SendNetPlayerAppearancesTo(int networkId);

    std::vector<int> lastSentAppearance;
    HeroMorphValues lastSentMorph;
    HeroStatsExperience lastSentExperience;
    int lastSentMeleeWeaponDef = -1;
    int lastSentRangedWeaponDef = -1;
    unsigned long long nextAppearanceCheckMs = 0;

    void BroadcastCreateLocalNetPlayer(int networkId, int defGlobalIndex, C3DVector position);
    void BroadcastCreateNetPlayer(int networkId, int defGlobalIndex, C3DVector position, int regionIndex);
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
