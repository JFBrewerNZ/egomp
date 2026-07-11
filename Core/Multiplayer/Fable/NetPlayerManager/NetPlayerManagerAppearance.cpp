#include <windows.h>

#include <algorithm>

#include "NetPlayerManager.h"
#include "../../../SDK/Fable/HeroAppearanceModifiers.h"

// How often the local hero's look is compared against the last broadcast
// one. Appearance changes are rare; a coarse poll is plenty.
static const unsigned long long APPEARANCE_CHECK_INTERVAL_MS = 1000;

static void WriteAppearance(SLNet::BitStream& bs, int networkId,
    const HeroMorphValues& morph, const HeroStatsExperience& exp,
    int meleeWeaponDef, int rangedWeaponDef,
    const std::vector<int>& modifierDefIndexes)
{
    bs.Write((SLNet::MessageID)ID_PLAYER_APPEARANCE);
    bs.Write(networkId);
    for (unsigned int value : morph.raw)
        bs.Write(value);
    for (int value : exp.spentOn)
        bs.Write(value);
    bs.Write(meleeWeaponDef);
    bs.Write(rangedWeaponDef);
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
        if (CTCHeroStats* remoteStats = CTCHeroStats::FromCreature(remote))
            remoteStats->SetExperienceSpentOn(netPlayer->GetStatsExperience());
    }

    CThingPlayerCreature* creature = GetCreatureFromLocalId(localNetPlayer->GetLocalId());
    CTCHeroAttachableAppearanceModifiers* appearance =
        CTCHeroAttachableAppearanceModifiers::FromCreature(creature);
    CTCHeroMorph* morph = CTCHeroMorph::FromCreature(creature);
    CTCHeroStats* stats = CTCHeroStats::FromCreature(creature);

    if (!appearance || !morph || !stats)
        return;

    std::vector<int> currentModifiers = appearance->GetModifierDefIndexes();
    HeroMorphValues currentMorph = morph->GetValues();
    HeroStatsExperience currentExp;

    if (currentModifiers.empty() || !stats->GetExperienceSpentOn(currentExp))
        return;

    int meleeDef = -1;
    int rangedDef = -1;
    if (CTCInventoryWeapons* weapons = CTCInventoryWeapons::FromCreature(creature))
    {
        meleeDef = weapons->GetCarriedMeleeDefIndex();
        rangedDef = weapons->GetCarriedRangedDefIndex();
    }

    if (currentModifiers == lastSentAppearance && currentMorph == lastSentMorph
        && currentExp == lastSentExperience
        && meleeDef == lastSentMeleeWeaponDef && rangedDef == lastSentRangedWeaponDef)
        return;

    BroadcastLocalNetPlayerAppearance(currentMorph, currentExp, meleeDef, rangedDef, currentModifiers);
    lastSentAppearance = std::move(currentModifiers);
    lastSentMorph = currentMorph;
    lastSentExperience = currentExp;
    lastSentMeleeWeaponDef = meleeDef;
    lastSentRangedWeaponDef = rangedDef;
}

void NetPlayerManager::BroadcastLocalNetPlayerAppearance(const HeroMorphValues& morph,
    const HeroStatsExperience& exp, int meleeWeaponDef, int rangedWeaponDef,
    const std::vector<int>& modifierDefIndexes)
{
    int networkId = localNetPlayer->GetNetworkId();

    SLNet::BitStream bs;
    WriteAppearance(bs, networkId, morph, exp, meleeWeaponDef, rangedWeaponDef, modifierDefIndexes);

    if (networkId == 0)
        network->SendToAllClients((const char*)bs.GetData(), bs.GetNumberOfBytesUsed());
    else
        network->SendToHost((const char*)bs.GetData(), bs.GetNumberOfBytesUsed());

    std::cout << "[NetPlayerManager] Broadcast appearance ("
        << modifierDefIndexes.size() << " modifiers)" << std::endl;
}

void NetPlayerManager::ReceiveNetPlayerAppearance(int networkId,
    HeroMorphValues morph, HeroStatsExperience exp,
    int meleeWeaponDef, int rangedWeaponDef, std::vector<int> modifierDefIndexes)
{
    if (localNetPlayer && localNetPlayer->GetNetworkId() == 0)
    {
        SLNet::BitStream bs;
        WriteAppearance(bs, networkId, morph, exp, meleeWeaponDef, rangedWeaponDef, modifierDefIndexes);

        network->SendToAllClientsExcept(networkId, (const char*)bs.GetData(), bs.GetNumberOfBytesUsed());
    }

    NetPlayer* netPlayer = FindNetPlayer(networkId);

    if (!netPlayer)
        return;

    netPlayer->SetAppearance(std::move(modifierDefIndexes));
    netPlayer->SetMorphValues(morph);
    netPlayer->SetStatsExperience(exp);
    netPlayer->SetWeaponDefs(meleeWeaponDef, rangedWeaponDef);

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
        if (CTCHeroStats* stats = CTCHeroStats::FromCreature(creature))
            stats->SetExperienceSpentOn(netPlayer.GetStatsExperience());
    }

    // Weapons before the modifier early-returns: a weapons-only change must
    // still apply when the clothing set is unchanged.
    ApplyNetPlayerWeapons(netPlayer);

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

    // Exact-set replacement: clear everything (including the creature
    // def's default hood/clothes) and apply precisely the sender's set.
    std::vector<int> sortedCurrent = appearance->GetModifierDefIndexes();
    std::vector<int> sortedWanted = wanted;
    std::sort(sortedCurrent.begin(), sortedCurrent.end());
    std::sort(sortedWanted.begin(), sortedWanted.end());

    if (sortedCurrent == sortedWanted)
        return;

    appearance->ClearModifiers();
    for (int defIndex : wanted)
        appearance->AddModifier(defIndex);
    appearance->RebuildAttachments();
}

namespace
{
    // SEH-isolated holster + visual build (no unwindable locals).
    bool GuardedHolsterAndRestore(CTCInventoryWeapons* weapons,
        CThing* melee, CThing* ranged, unsigned long* exceptionCode)
    {
        *exceptionCode = 0;
        __try
        {
            if (melee)
                weapons->SetCarriedMeleeWeapon(melee);
            if (ranged)
                weapons->SetCarriedRangedWeapon(ranged);

            // The build branch directly: RegenerateCarriedWeapons routes to
            // the CLEAR path on puppets (their CTCHero weapons-visible byte
            // starts unset — first LAN test applied weapons invisibly).
            weapons->RestoreCarriedVisuals();
            return true;
        }
        __except (*exceptionCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }
}

void NetPlayerManager::ApplyNetPlayerWeapons(NetPlayer& netPlayer)
{
    // The game's own equip path, hero-verified 2026-07-11: create a wired
    // carried weapon from the def (inventory gate forced open — puppets
    // have no inventory records; that path just skips the augmentation
    // copies), holster it, build the back visuals. Silent — no popups,
    // no pickups.
    if (!netPlayer.IsSpawned())
        return;

    int melee = netPlayer.GetMeleeWeaponDef();
    int ranged = netPlayer.GetRangedWeaponDef();

    if (melee == netPlayer.GetAppliedMeleeWeaponDef()
        && ranged == netPlayer.GetAppliedRangedWeaponDef())
        return;

    CThingPlayerCreature* creature = GetCreatureFromNetworkId(netPlayer.GetNetworkId());
    if (!creature)
        return;

    CTCInventoryWeapons* weapons = CTCInventoryWeapons::FromCreature(creature);
    if (!weapons)
        return;

    // Keep the game's own regenerations (sheathe/unsheathe reconciler) on
    // the build path for this puppet from now on.
    bool flagSet = CTCInventoryWeapons::SetCarriedWeaponsVisibleFlag(creature);

    CThing* meleeWeapon = nullptr;
    CThing* rangedWeapon = nullptr;

    if (melee > 0 && melee != netPlayer.GetAppliedMeleeWeaponDef())
        meleeWeapon = weapons->CreateCarriedWeaponUnchecked(melee);

    if (ranged > 0 && ranged != netPlayer.GetAppliedRangedWeaponDef())
        rangedWeapon = weapons->CreateCarriedWeaponUnchecked(ranged);

    unsigned long exceptionCode = 0;
    bool ok = (meleeWeapon || rangedWeapon)
        && GuardedHolsterAndRestore(weapons, meleeWeapon, rangedWeapon, &exceptionCode);

    // Marked applied even on failure so change-driven re-broadcasts don't
    // recreate weapon objects endlessly; a respawn resets and retries.
    netPlayer.SetAppliedWeaponDefs(melee, ranged);

    std::cout << "[NetPlayerManager] Player " << netPlayer.GetNetworkId()
        << " weapons: melee def " << melee << " (" << (meleeWeapon ? "created" : "-")
        << "), ranged def " << ranged << " (" << (rangedWeapon ? "created" : "-")
        << "), heroFlag " << (flagSet ? "set" : "MISSING")
        << ", visuals " << (ok ? "restored" : "FAILED");
    if (exceptionCode)
        std::cout << " (exception " << std::hex << exceptionCode << std::dec << ")";
    std::cout << std::endl;
}

void NetPlayerManager::SendNetPlayerAppearancesTo(int networkId)
{
    // Host seeding a newcomer: our own appearance plus every stored one.
    if (!lastSentAppearance.empty())
    {
        SLNet::BitStream bs;
        WriteAppearance(bs, localNetPlayer->GetNetworkId(), lastSentMorph,
            lastSentExperience, lastSentMeleeWeaponDef, lastSentRangedWeaponDef, lastSentAppearance);

        network->SendToClient(networkId, (const char*)bs.GetData(), bs.GetNumberOfBytesUsed());
    }

    for (const auto& netPlayer : netPlayers)
    {
        if (!netPlayer || netPlayer->GetNetworkId() == networkId
            || netPlayer->GetAppearance().empty())
            continue;

        SLNet::BitStream bs;
        WriteAppearance(bs, netPlayer->GetNetworkId(), netPlayer->GetMorphValues(),
            netPlayer->GetStatsExperience(), netPlayer->GetMeleeWeaponDef(),
            netPlayer->GetRangedWeaponDef(), netPlayer->GetAppearance());

        network->SendToClient(networkId, (const char*)bs.GetData(), bs.GetNumberOfBytesUsed());
    }
}
