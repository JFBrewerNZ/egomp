#include <cstring>

#include "NetPlayerManager.h"
#include "../../../SDK/Fable/HeroAppearanceModifiers.h"

// Maps an action's demangled RTTI class to a synced CombatActionType, or
// -1 if the action is not replicated. Kept deliberately small — each entry
// is a move confirmed to reconstruct cleanly on a puppet.
static int CombatActionTypeForClass(const char* actionClass)
{
    if (std::strstr(actionClass, "ControlledStrafeJump"))
        return (int)CombatActionType::Roll;

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

    int networkId = localNetPlayer->GetNetworkId();

    SLNet::BitStream bs;
    bs.Write((SLNet::MessageID)ID_PLAYER_ACTION);
    bs.Write(networkId);
    bs.Write(actionType);

    if (networkId == 0)
        network->SendToAllClients((const char*)bs.GetData(), bs.GetNumberOfBytesUsed(), HIGH_PRIORITY, UNRELIABLE);
    else
        network->SendToHost((const char*)bs.GetData(), bs.GetNumberOfBytesUsed(), HIGH_PRIORITY, UNRELIABLE);
}

void NetPlayerManager::ReceiveNetPlayerAction(int networkId, int actionType)
{
    // Host relays to the other clients.
    if (localNetPlayer && localNetPlayer->GetNetworkId() == 0)
    {
        SLNet::BitStream bs;
        bs.Write((SLNet::MessageID)ID_PLAYER_ACTION);
        bs.Write(networkId);
        bs.Write(actionType);

        network->SendToAllClientsExcept(networkId, (const char*)bs.GetData(), bs.GetNumberOfBytesUsed(), HIGH_PRIORITY, UNRELIABLE);
    }

    if (actionType < 0 || actionType >= (int)CombatActionType::Count)
        return;

    CThingPlayerCreature* creature = GetCreatureFromNetworkId(networkId);
    if (!creature)
        return;

    CombatActions::Perform(creature, (CombatActionType)actionType);
}
