#include <windows.h>

#include <algorithm>

#include "NetPlayerManager.h"
#include "../../../SDK/Fable/HeroAppearanceModifiers.h"

// How often the local hero's modifier set is compared against the last
// broadcast one. Appearance changes are rare; a coarse poll is plenty.
static const unsigned long long APPEARANCE_CHECK_INTERVAL_MS = 1000;

void NetPlayerManager::UpdateAppearanceSync()
{
    if (!localNetPlayer)
        return;

    unsigned long long now = GetTickCount64();
    if (now < nextAppearanceCheckMs)
        return;
    nextAppearanceCheckMs = now + APPEARANCE_CHECK_INTERVAL_MS;

    CThingPlayerCreature* creature = GetCreatureFromLocalId(localNetPlayer->GetLocalId());
    CTCHeroAttachableAppearanceModifiers* appearance =
        CTCHeroAttachableAppearanceModifiers::FromCreature(creature);

    if (!appearance)
        return;

    std::vector<int> current = appearance->GetModifierDefIndexes();

    if (current.empty() || current == lastSentAppearance)
        return;

    BroadcastLocalNetPlayerAppearance(current);
    lastSentAppearance = std::move(current);
}

void NetPlayerManager::BroadcastLocalNetPlayerAppearance(const std::vector<int>& modifierDefIndexes)
{
    int networkId = localNetPlayer->GetNetworkId();

    SLNet::BitStream bs;
    bs.Write((SLNet::MessageID)ID_PLAYER_APPEARANCE);
    bs.Write(networkId);
    bs.Write((int)modifierDefIndexes.size());
    for (int defIndex : modifierDefIndexes)
        bs.Write(defIndex);

    if (networkId == 0)
        network->SendToAllClients((const char*)bs.GetData(), bs.GetNumberOfBytesUsed());
    else
        network->SendToHost((const char*)bs.GetData(), bs.GetNumberOfBytesUsed());

    std::cout << "[NetPlayerManager] Broadcast appearance ("
        << modifierDefIndexes.size() << " modifiers)" << std::endl;
}

void NetPlayerManager::ReceiveNetPlayerAppearance(int networkId, std::vector<int> modifierDefIndexes)
{
    if (localNetPlayer && localNetPlayer->GetNetworkId() == 0)
    {
        SLNet::BitStream bs;
        bs.Write((SLNet::MessageID)ID_PLAYER_APPEARANCE);
        bs.Write(networkId);
        bs.Write((int)modifierDefIndexes.size());
        for (int defIndex : modifierDefIndexes)
            bs.Write(defIndex);

        network->SendToAllClientsExcept(networkId, (const char*)bs.GetData(), bs.GetNumberOfBytesUsed());
    }

    NetPlayer* netPlayer = FindNetPlayer(networkId);

    if (!netPlayer)
        return;

    netPlayer->SetAppearance(std::move(modifierDefIndexes));

    if (netPlayer->IsSpawned())
        ApplyNetPlayerAppearance(*netPlayer);
}

void NetPlayerManager::ApplyNetPlayerAppearance(NetPlayer& netPlayer)
{
    const std::vector<int>& wanted = netPlayer.GetAppearance();

    if (wanted.empty())
        return;

    CThingPlayerCreature* creature = GetCreatureFromNetworkId(netPlayer.GetNetworkId());
    CTCHeroAttachableAppearanceModifiers* appearance =
        CTCHeroAttachableAppearanceModifiers::FromCreature(creature);

    if (!appearance)
    {
        std::cout << "[NetPlayerManager::ApplyNetPlayerAppearance]: !appearance: "
            << netPlayer.GetNetworkId() << std::endl;
        return;
    }

    std::vector<int> current = appearance->GetModifierDefIndexes();
    bool changed = false;

    for (int defIndex : wanted)
    {
        if (std::find(current.begin(), current.end(), defIndex) == current.end())
        {
            appearance->AddModifier(defIndex);
            changed = true;
        }
    }

    // No RemoveModifier is known yet, so stale modifiers persist until the
    // creature respawns (which region changes do frequently).
    if (changed)
        appearance->ResetAndRebuild();
}

void NetPlayerManager::SendNetPlayerAppearancesTo(int networkId)
{
    // Host seeding a newcomer: our own appearance plus every stored one.
    if (!lastSentAppearance.empty())
    {
        SLNet::BitStream bs;
        bs.Write((SLNet::MessageID)ID_PLAYER_APPEARANCE);
        bs.Write(localNetPlayer->GetNetworkId());
        bs.Write((int)lastSentAppearance.size());
        for (int defIndex : lastSentAppearance)
            bs.Write(defIndex);

        network->SendToClient(networkId, (const char*)bs.GetData(), bs.GetNumberOfBytesUsed());
    }

    for (const auto& netPlayer : netPlayers)
    {
        if (!netPlayer || netPlayer->GetNetworkId() == networkId
            || netPlayer->GetAppearance().empty())
            continue;

        SLNet::BitStream bs;
        bs.Write((SLNet::MessageID)ID_PLAYER_APPEARANCE);
        bs.Write(netPlayer->GetNetworkId());
        bs.Write((int)netPlayer->GetAppearance().size());
        for (int defIndex : netPlayer->GetAppearance())
            bs.Write(defIndex);

        network->SendToClient(networkId, (const char*)bs.GetData(), bs.GetNumberOfBytesUsed());
    }
}
