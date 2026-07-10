#include <windows.h>

#include <algorithm>

#include "NetPlayerManager.h"
#include "../../../SDK/Fable/HeroAppearanceModifiers.h"

// How often the local hero's look is compared against the last broadcast
// one. Appearance changes are rare; a coarse poll is plenty.
static const unsigned long long APPEARANCE_CHECK_INTERVAL_MS = 1000;

static void WriteAppearance(SLNet::BitStream& bs, int networkId,
    const HeroMorphValues& morph, const std::vector<int>& modifierDefIndexes)
{
    bs.Write((SLNet::MessageID)ID_PLAYER_APPEARANCE);
    bs.Write(networkId);
    bs.Write(morph.strength);
    bs.Write(morph.will);
    bs.Write(morph.skill);
    bs.Write(morph.age);
    bs.Write(morph.morality);
    bs.Write(morph.fatness);
    bs.Write(morph.tan);
    bs.Write((int)modifierDefIndexes.size());
    for (int defIndex : modifierDefIndexes)
        bs.Write(defIndex);
}

void NetPlayerManager::UpdateAppearanceSync()
{
    if (!localNetPlayer)
        return;

    unsigned long long now = GetTickCount64();
    if (now < nextAppearanceCheckMs)
        return;
    nextAppearanceCheckMs = now + APPEARANCE_CHECK_INTERVAL_MS;

    // The game recomputes stat-driven morphs (muscle) from the creature's
    // own stats, clobbering one-time writes — remote creatures have default
    // stats, so keep re-asserting the synced values.
    for (auto& netPlayer : netPlayers)
    {
        if (!netPlayer || !netPlayer->IsSpawned() || !netPlayer->HasMorphValues())
            continue;

        CThingPlayerCreature* remote = GetCreatureFromNetworkId(netPlayer->GetNetworkId());
        if (CTCHeroMorph* remoteMorph = CTCHeroMorph::FromCreature(remote))
            remoteMorph->SetValues(netPlayer->GetMorphValues());
    }

    CThingPlayerCreature* creature = GetCreatureFromLocalId(localNetPlayer->GetLocalId());
    CTCHeroAttachableAppearanceModifiers* appearance =
        CTCHeroAttachableAppearanceModifiers::FromCreature(creature);
    CTCHeroMorph* morph = CTCHeroMorph::FromCreature(creature);

    if (!appearance || !morph)
        return;

    std::vector<int> currentModifiers = appearance->GetModifierDefIndexes();
    HeroMorphValues currentMorph = morph->GetValues();

    if (currentModifiers.empty())
        return;

    if (currentModifiers == lastSentAppearance && currentMorph == lastSentMorph)
        return;

    BroadcastLocalNetPlayerAppearance(currentMorph, currentModifiers);
    lastSentAppearance = std::move(currentModifiers);
    lastSentMorph = currentMorph;
}

void NetPlayerManager::BroadcastLocalNetPlayerAppearance(const HeroMorphValues& morph,
    const std::vector<int>& modifierDefIndexes)
{
    int networkId = localNetPlayer->GetNetworkId();

    SLNet::BitStream bs;
    WriteAppearance(bs, networkId, morph, modifierDefIndexes);

    if (networkId == 0)
        network->SendToAllClients((const char*)bs.GetData(), bs.GetNumberOfBytesUsed());
    else
        network->SendToHost((const char*)bs.GetData(), bs.GetNumberOfBytesUsed());

    std::cout << "[NetPlayerManager] Broadcast appearance ("
        << modifierDefIndexes.size() << " modifiers, strength "
        << morph.strength << ")" << std::endl;
}

void NetPlayerManager::ReceiveNetPlayerAppearance(int networkId,
    HeroMorphValues morph, std::vector<int> modifierDefIndexes)
{
    if (localNetPlayer && localNetPlayer->GetNetworkId() == 0)
    {
        SLNet::BitStream bs;
        WriteAppearance(bs, networkId, morph, modifierDefIndexes);

        network->SendToAllClientsExcept(networkId, (const char*)bs.GetData(), bs.GetNumberOfBytesUsed());
    }

    NetPlayer* netPlayer = FindNetPlayer(networkId);

    if (!netPlayer)
        return;

    netPlayer->SetAppearance(std::move(modifierDefIndexes));
    netPlayer->SetMorphValues(morph);

    if (netPlayer->IsSpawned())
        ApplyNetPlayerAppearance(*netPlayer);
}

void NetPlayerManager::ApplyNetPlayerAppearance(NetPlayer& netPlayer)
{
    CThingPlayerCreature* creature = GetCreatureFromNetworkId(netPlayer.GetNetworkId());

    if (netPlayer.HasMorphValues())
    {
        if (CTCHeroMorph* morph = CTCHeroMorph::FromCreature(creature))
            morph->SetValues(netPlayer.GetMorphValues());
    }

    const std::vector<int>& wanted = netPlayer.GetAppearance();

    if (wanted.empty())
        return;

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
        WriteAppearance(bs, localNetPlayer->GetNetworkId(), lastSentMorph, lastSentAppearance);

        network->SendToClient(networkId, (const char*)bs.GetData(), bs.GetNumberOfBytesUsed());
    }

    for (const auto& netPlayer : netPlayers)
    {
        if (!netPlayer || netPlayer->GetNetworkId() == networkId
            || netPlayer->GetAppearance().empty())
            continue;

        SLNet::BitStream bs;
        WriteAppearance(bs, netPlayer->GetNetworkId(),
            netPlayer->GetMorphValues(), netPlayer->GetAppearance());

        network->SendToClient(networkId, (const char*)bs.GetData(), bs.GetNumberOfBytesUsed());
    }
}
