#include <cstring>
#include <windows.h>

#include "NetPlayerManager.h"
#include "../../../SDK/Fable/HeroAppearanceModifiers.h"

// While a remote player holds block, they keep sending Block actions; each
// refreshes the puppet's block for this long, and the puppet re-posts the
// block action at this interval to sustain the pose without flickering.
static const unsigned long long BLOCK_HOLD_WINDOW_MS = 400;
static const unsigned long long BLOCK_REPOST_INTERVAL_MS = 250;

// Maps an action's demangled RTTI class to a synced CombatActionType, or
// -1 if the action is not replicated. Kept deliberately small — each entry
// is a move confirmed to reconstruct cleanly on a puppet.
static int CombatActionTypeForClass(const char* actionClass)
{
    if (std::strstr(actionClass, "ControlledStrafeJump"))
        return (int)CombatActionType::Roll;
    if (std::strstr(actionClass, "StartBlocking"))
        return (int)CombatActionType::Block;

    return -1;
}

void NetPlayerManager::HandleLocalCreatureAction(void* creature, const char* actionClass)
{
    // Only the local hero's own actions are broadcast. Puppet actions (which
    // include the ones we reconstruct from the network) are ignored here, so
    // there is no replication feedback loop.
    if (!localNetPlayer || !actionClass)
        return;

    CThingPlayerCreature* localHero = GetCreatureFromLocalId(localNetPlayer->GetLocalId());
    if (!localHero || creature != localHero)
        return;

    int actionType = CombatActionTypeForClass(actionClass);
    if (actionType < 0)
        return;

    // Direction candidates at action time. MovementAcceleration falls to ~0
    // at steady speed, so also grab the facing forward vector; diagnostic
    // log picks the field that actually reflects the roll direction.
    C3DVector accel = ((CThingPlayerCreature*)localHero)->MovementAcceleration;

    C3DVector forward = {};
    CTCPhysicsBase* physicsTC = ((CThing*)localHero)->PhysicsTC;
    if (physicsTC)
    {
        CRightHandedSet* rh = ((CTCPhysicsStandard*)physicsTC)->GetRHSet();
        if (rh)
            forward = rh->Forward;
    }

    // forward (facing) is always a clean unit vector; accel is ~0 at steady
    // speed. Use the facing direction as the roll direction.
    C3DVector direction = forward;
    (void)accel;

    int networkId = localNetPlayer->GetNetworkId();

    SLNet::BitStream bs;
    bs.Write((SLNet::MessageID)ID_PLAYER_ACTION);
    bs.Write(networkId);
    bs.Write(actionType);
    bs.Write(direction);

    if (networkId == 0)
        network->SendToAllClients((const char*)bs.GetData(), bs.GetNumberOfBytesUsed(), HIGH_PRIORITY, UNRELIABLE);
    else
        network->SendToHost((const char*)bs.GetData(), bs.GetNumberOfBytesUsed(), HIGH_PRIORITY, UNRELIABLE);
}

void NetPlayerManager::ReceiveNetPlayerAction(int networkId, int actionType, C3DVector direction)
{
    // Host relays to the other clients.
    if (localNetPlayer && localNetPlayer->GetNetworkId() == 0)
    {
        SLNet::BitStream bs;
        bs.Write((SLNet::MessageID)ID_PLAYER_ACTION);
        bs.Write(networkId);
        bs.Write(actionType);
        bs.Write(direction);

        network->SendToAllClientsExcept(networkId, (const char*)bs.GetData(), bs.GetNumberOfBytesUsed(), HIGH_PRIORITY, UNRELIABLE);
    }

    if (actionType < 0 || actionType >= (int)CombatActionType::Count)
        return;

    // Block is a held state: refresh the puppet's block window instead of
    // firing once. UpdateCombat sustains it. Other actions fire immediately.
    if (actionType == (int)CombatActionType::Block)
    {
        if (NetPlayer* netPlayer = FindNetPlayer(networkId))
            netPlayer->RefreshBlock(GetTickCount64() + BLOCK_HOLD_WINDOW_MS);
        return;
    }

    CThingPlayerCreature* creature = GetCreatureFromNetworkId(networkId);
    if (!creature)
        return;

    CombatActions::Perform(creature, (CombatActionType)actionType, direction);
}

void NetPlayerManager::UpdateCombat()
{
    unsigned long long now = GetTickCount64();

    for (auto& netPlayer : netPlayers)
    {
        if (!netPlayer || !netPlayer->IsSpawned())
            continue;

        if (now >= netPlayer->GetBlockActiveUntilMs())
            continue;
        if (now - netPlayer->GetLastBlockPostMs() < BLOCK_REPOST_INTERVAL_MS)
            continue;

        CThingPlayerCreature* creature = GetCreatureFromNetworkId(netPlayer->GetNetworkId());
        if (!creature)
            continue;

        CombatActions::Perform(creature, CombatActionType::Block, C3DVector{});
        netPlayer->SetLastBlockPostMs(now);
    }
}
