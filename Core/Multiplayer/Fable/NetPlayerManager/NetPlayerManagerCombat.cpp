#include <cstring>
#include <windows.h>

#include "NetPlayerManager.h"
#include "../../../SDK/Fable/HeroAppearanceModifiers.h"

// While a remote player holds block, they keep sending Block actions; each
// refreshes the puppet's block for this long, and the puppet re-posts the
// block action at this interval to sustain the pose without flickering.
static const unsigned long long BLOCK_HOLD_WINDOW_MS = 400;
static const unsigned long long BLOCK_REPOST_INTERVAL_MS = 250;

// Floor between two anim broadcasts, in case the hero turns out to emit
// PlayAnimation actions every frame (e.g. for locomotion).
static const unsigned long long ANIM_SEND_MIN_INTERVAL_MS = 50;

// [netId is written by the caller] — serializes the wire fields of
// ID_PLAYER_ANIM after it.
static void WriteAnimFields(SLNet::BitStream& bs, const AnimActionFields& fields)
{
    bs.Write(fields.d20);
    bs.Write(fields.d24);
    bs.Write(fields.keyExtra);
    bs.Write(fields.ctxId0);
    bs.Write(fields.ctxId1);
    bs.Write(fields.ctxFlag);
    bs.Write(fields.loops);
    bs.Write(fields.a8);
    bs.Write(fields.a9);
    bs.Write(fields.aa);
    bs.Write(fields.ab);
    bs.Write(fields.b0);

    unsigned char nameLen = (unsigned char)std::strlen(fields.name);
    bs.Write(nameLen);
    bs.WriteAlignedBytes((const unsigned char*)fields.name, nameLen);

    unsigned char ctxNameLen = (unsigned char)std::strlen(fields.ctxName);
    bs.Write(ctxNameLen);
    bs.WriteAlignedBytes((const unsigned char*)fields.ctxName, ctxNameLen);
}

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

void NetPlayerManager::HandleLocalCreatureAction(void* creature, void* action, const char* actionClass)
{
    // Only the local hero's own actions are broadcast. Puppet actions (which
    // include the ones we reconstruct from the network) are ignored here, so
    // there is no replication feedback loop.
    if (!localNetPlayer || !actionClass)
        return;

    CThingPlayerCreature* localHero = GetCreatureFromLocalId(localNetPlayer->GetLocalId());
    if (!localHero || creature != localHero)
        return;

    // PlayAnimation-family actions are mirrored by animation name — the
    // catch-all that shows combat/gesture motion without reconstructing
    // each action's machine-specific arguments.
    if (AnimAction::ClassHasAnimLayout(actionClass))
    {
        AnimActionFields fields;
        // Without a key name or a resolver-learned context name the
        // receiver has nothing to replay from — skip those.
        if (AnimAction::Extract(action, fields)
            && (fields.name[0] || fields.ctxName[0]))
            SendLocalAnimAction(fields);
    }

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

// Same anim resolving twice within this window = the game re-querying a
// held state (bow-load resolves every frame while drawn), not a new move.
static const unsigned long long RESOLVE_RESEND_WINDOW_MS = 400;

void NetPlayerManager::HandleLocalAnimResolve(void* creature, const char* animName, int flag)
{
    if (!localNetPlayer || !animName || !animName[0])
        return;

    // Only purposeful one-shot anims (flag=1: unsheathe/sheathe/bow-load/
    // fire). flag=0 covers idles/blinks/locomotion, which puppets already
    // produce on their own.
    if (flag != 1)
        return;

    CThingPlayerCreature* localHero = GetCreatureFromLocalId(localNetPlayer->GetLocalId());
    if (!localHero || creature != localHero)
        return;

    unsigned long long now = GetTickCount64();
    if (std::strcmp(lastResolveSentName, animName) == 0
        && now - lastResolveSentMs < RESOLVE_RESEND_WINDOW_MS)
        return;

    strcpy_s(lastResolveSentName, animName);
    lastResolveSentMs = now;

    // A resolver event has no captured action fields — send defaults
    // matching the game's own one-shot PlayAnimation constructions.
    AnimActionFields fields;
    strcpy_s(fields.ctxName, animName);
    fields.ctxFlag = flag;
    fields.loops = 0;
    fields.a9 = 1;

    SendLocalAnimAction(fields);
}

void NetPlayerManager::SendLocalAnimAction(const AnimActionFields& fields)
{
    unsigned long long now = GetTickCount64();
    if (now - lastAnimSendMs < ANIM_SEND_MIN_INTERVAL_MS)
        return;
    lastAnimSendMs = now;

    int networkId = localNetPlayer->GetNetworkId();

    SLNet::BitStream bs;
    bs.Write((SLNet::MessageID)ID_PLAYER_ANIM);
    bs.Write(networkId);
    WriteAnimFields(bs, fields);

    if (networkId == 0)
        network->SendToAllClients((const char*)bs.GetData(), bs.GetNumberOfBytesUsed(), HIGH_PRIORITY, UNRELIABLE);
    else
        network->SendToHost((const char*)bs.GetData(), bs.GetNumberOfBytesUsed(), HIGH_PRIORITY, UNRELIABLE);
}

void NetPlayerManager::ReceiveNetPlayerAnim(int networkId, const AnimActionFields& fields)
{
    // Host relays to the other clients.
    if (localNetPlayer && localNetPlayer->GetNetworkId() == 0)
    {
        SLNet::BitStream bs;
        bs.Write((SLNet::MessageID)ID_PLAYER_ANIM);
        bs.Write(networkId);
        WriteAnimFields(bs, fields);

        network->SendToAllClientsExcept(networkId, (const char*)bs.GetData(), bs.GetNumberOfBytesUsed(), HIGH_PRIORITY, UNRELIABLE);
    }

    CThingPlayerCreature* creature = GetCreatureFromNetworkId(networkId);
    if (!creature)
        return;

    // Mint a FRESH context on the puppet via the game's own resolver —
    // captured context pointers are per-action transients and replaying
    // one is use-after-free (crashed the game before this approach). The
    // PlayAnimation ctor faults on a null context, so no context = skip.
    const char* resolveName = fields.ctxName[0] ? fields.ctxName : fields.name;
    void* context = AnimAction::ResolveContext(creature, resolveName, fields.ctxFlag);
    if (!context)
        return;

    AnimAction::Play(creature, fields, context);
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
