#define NOMINMAX

#include "windows.h"
#include <string>

#include "NetMainGameComponent.h"
#include "../../../Config/Config.h"
#include "../../../DevTools/ObjectInspector.h"
#include "../../../DevTools/EquipmentProbe.h"
#include "../../../DevTools/ActionTracer.h"

// Delay before the first automatic connect after entering the world, and
// between retries while unconnected.
static const unsigned long long FIRST_CONNECT_DELAY_MS = 2000;
static const unsigned long long CONNECT_RETRY_DELAY_MS = 10000;

NetMainGameComponent& NetMainGameComponent::GetInstance()
{
    static NetMainGameComponent instance;
    return instance;
}

NetMainGameComponent::NetMainGameComponent() : mainGameComponent(CMainGameComponent::Get())
{
    SetupCallbacks();
}

NetMainGameComponent::~NetMainGameComponent()
{
    ClearCallbacks();
}

void NetMainGameComponent::SetupCallbacks()
{
    mainGameComponent->AddPostInitCallback("PostInit", [this]() { this->HandleMainGameComponentPostInit(); });
    mainGameComponent->AddUpdateCallback("Update", [this]() { this->HandleMainGameComponentUpdate(); });
    mainGameComponent->AddShutdownCallback("Shutdown", [this]() { this->HandleMainGameComponentShutdown(); });
}

void NetMainGameComponent::ClearCallbacks()
{
    mainGameComponent->RemovePostInitCallback("PostInit");
    mainGameComponent->RemoveUpdateCallback("Update");
    mainGameComponent->RemoveShutdownCallback("Shutdown");
}

void NetMainGameComponent::HandleMainGameComponentShutdown()
{
    worldReady = false;

    if (!network)
        return;

    if (network->IsActive())
        network->Disconnect();

    if (!network->IsActive())
        Disconnect();
}

void NetMainGameComponent::HandleMainGameComponentPostInit() {
    worldReady = true;
    autoConnectEnabled = Config::Get().autoConnect;
    nextConnectAttemptMs = GetTickCount64() + FIRST_CONNECT_DELAY_MS;

    Options();
}

void NetMainGameComponent::HandleMainGameComponentUpdate()
{
    Selection();

    if (network)
    {
        network->Update();

        if (network && netPlayerManager)
        {
            netPlayerManager->UpdateAppearanceSync();
            netPlayerManager->UpdateCombat();
        }

        if (network && !network->IsActive())
        {
            Disconnect();

            if (autoConnectEnabled)
            {
                nextConnectAttemptMs = GetTickCount64() + CONNECT_RETRY_DELAY_MS;
                std::cout << "[EgoMP] Disconnected. Retrying in "
                    << (CONNECT_RETRY_DELAY_MS / 1000) << "s..." << std::endl;
            }
            else
                Options();
        }

        return;
    }

    if (worldReady && autoConnectEnabled && GetTickCount64() >= nextConnectAttemptMs)
        Connect();
}

void NetMainGameComponent::Options()
{
    const Config& config = Config::Get();

    std::cout << "[EgoMP] Server: " << config.serverIp << ":" << config.serverPort
        << " (auto-connect " << (config.autoConnect ? "on" : "off") << ")" << std::endl;
    std::cout << "[EgoMP] NUMPAD1: host P2P session (port " << config.hostPort << ")" << std::endl;
    std::cout << "[EgoMP] NUMPAD2: connect to server" << std::endl;
    std::cout << "[EgoMP] NUMPAD3: disconnect" << std::endl;
    std::cout << "[EgoMP] Edit EgoMP.ini to change these settings." << std::endl;

    if (config.debugKeys)
    {
        std::cout << "[EgoMP] NUMPAD5/6/7: inspect hero / raw TC list / inventory TCs (debug)" << std::endl;
        std::cout << "[EgoMP] NUMPAD8: log worn equipment (debug)" << std::endl;
        std::cout << "[EgoMP] NUMPAD9: replay last captured animation on the hero (debug)" << std::endl;
        std::cout << "[EgoMP] NUMPAD0: toggle live action/combat tracer (debug)" << std::endl;
    }
}

// Offset of the thing-component list pointer inside CThing[PlayerCreature],
// discovered empirically with the NUMPAD5 dump (entries alternate
// id/pointer; validated by PhysicsTC at +0x60 matching list entry [1]).
static const size_t TC_LIST_OFFSET = 0x44;
static const size_t TC_LIST_DWORDS = 0x100;

void NetMainGameComponent::HandleDebugKeys()
{
    if (!Config::Get().debugKeys || !worldReady)
        return;

    if (GetAsyncKeyState(VK_NUMPAD0) & 1)
    {
        ActionTracer::Install();
        ActionTracer::SetEnabled(!ActionTracer::IsEnabled());
        std::cout << "[EgoMP] Action tracer " << (ActionTracer::IsEnabled() ? "ON" : "OFF")
            << " - play to log creature actions (wield/attack/roll/block/cast)" << std::endl;
    }

    bool inspectCreature = (GetAsyncKeyState(VK_NUMPAD5) & 1) != 0;
    bool inspectTcList = (GetAsyncKeyState(VK_NUMPAD6) & 1) != 0;
    bool inspectInventories = (GetAsyncKeyState(VK_NUMPAD7) & 1) != 0;
    bool dumpEquipment = (GetAsyncKeyState(VK_NUMPAD8) & 1) != 0;
    bool replayAnim = (GetAsyncKeyState(VK_NUMPAD9) & 1) != 0;

    if (!inspectCreature && !inspectTcList && !inspectInventories && !dumpEquipment && !replayAnim)
        return;

    CPlayerManager* playerManager = mainGameComponent->GetPlayerManager();
    CPlayer* player = playerManager ? playerManager->GetPlayer(0) : nullptr;
    CThingPlayerCreature* creature = player ? player->GetPControlledCreature() : nullptr;

    if (!creature)
    {
        std::cout << "[EgoMP] Inspect: no local hero creature" << std::endl;
        return;
    }

    if (inspectCreature)
        ObjectInspector::Dump("local hero creature", creature, 0x600);

    const void* tcList = *(const void* const*)((const char*)creature + TC_LIST_OFFSET);

    if (inspectTcList)
        ObjectInspector::DumpRaw("hero TC list", tcList, TC_LIST_DWORDS);

    if (inspectInventories)
    {
        static const char* const interesting[] = {
            "Inventory", "Morph", "Carrying", "Carryable", "Weapon",
            "GraphicAppearance", "AppearanceModifiers",
        };
        ObjectInspector::DumpMatchingObjects("hero TC list", tcList, TC_LIST_DWORDS,
            interesting, sizeof(interesting) / sizeof(interesting[0]), 0x200);
    }

    if (dumpEquipment)
        EquipmentProbe::DumpEquipment(creature);

    // Local end-to-end test of the animation-sync apply path, no LAN
    // needed: NUMPAD9 replays the newest captured anim action on the local
    // hero by resolving a FRESH context from the anim name — exactly what a
    // remote peer does with an incoming ID_PLAYER_ANIM. (Never replays a
    // captured context pointer: those are per-action transients and doing
    // so crashed the game.)
    if (replayAnim)
    {
        AnimActionFields fields;
        if (!ActionTracer::GetLastAnimCapture(fields))
        {
            std::cout << "[EgoMP] Anim replay: nothing captured yet — stand near NPCs first"
                << std::endl;
        }
        else if (!fields.ctxName[0] && !fields.name[0])
        {
            std::cout << "[EgoMP] Anim replay: last capture has no resolved name"
                " (resolver hook saw nothing for ctxId=" << std::hex
                << fields.ctxId0 << "/" << fields.ctxId1 << std::dec << ")" << std::endl;
        }
        else
        {
            const char* resolveName = fields.ctxName[0] ? fields.ctxName : fields.name;
            void* context = AnimAction::ResolveContext(creature, resolveName, fields.ctxFlag);

            bool ok = context && AnimAction::Play(creature, fields, context);
            std::cout << "[EgoMP] Anim replay '" << resolveName
                << "' (d24=" << fields.d24 << ", loops=" << fields.loops
                << ", ctx=" << (context ? "resolved" : "RESOLVE FAILED") << "): "
                << (ok ? "posted" : "FAILED") << std::endl;
        }
    }
}

void NetMainGameComponent::Selection() {
    HandleDebugKeys();

    if (network)
    {
        if (GetAsyncKeyState(VK_NUMPAD3) & 1)
        {
            // Explicit disconnect also opts out of auto-reconnect until the
            // player asks to connect again (NUMPAD2) or reloads the world.
            autoConnectEnabled = false;
            network->Disconnect();
        }

        return;
    }

    if (GetAsyncKeyState(VK_NUMPAD1) & 1)
    {
        autoConnectEnabled = false;
        Host();
    }
    else if (GetAsyncKeyState(VK_NUMPAD2) & 1)
    {
        autoConnectEnabled = Config::Get().autoConnect;
        Connect();
    }
}

void NetMainGameComponent::Host()
{
    mainGameComponent = CMainGameComponent::Get();
    network = std::make_unique<Network>();
    netPlayerManager = std::make_unique<NetPlayerManager>(
        network.get(),
        mainGameComponent->GetPlayerManager(),
        mainGameComponent->GetWorld()
    );

    SetupNetworkCallbacks();

    unsigned short port = Config::Get().hostPort;
    std::cout << "[EgoMP] Hosting on port " << port << "..." << std::endl;

    if (!network->Host(port))
        Disconnect();
}

void NetMainGameComponent::Connect()
{
    mainGameComponent = CMainGameComponent::Get();
    network = std::make_unique<Network>();
    netPlayerManager = std::make_unique<NetPlayerManager>(
        network.get(),
        mainGameComponent->GetPlayerManager(),
        mainGameComponent->GetWorld()
    );

    SetupNetworkCallbacks();

    network->AddDisconnectionNotificationCallback("DisconnectionNotification", [this](int networkId) { if (networkId == 0) network->Disconnect(); });
    network->AddConnectionLostCallback("ConnectionLost", [this](int networkId) { if (networkId == 0) network->Disconnect(); });
    network->AddConnectionAttemptFailedCallback("ConnectionAttemptFailed", [this]() { network->Disconnect(); });

    const Config& config = Config::Get();
    std::cout << "[EgoMP] Connecting to " << config.serverIp << ":" << config.serverPort << "..." << std::endl;

    if (!network->Connect(config.serverIp.c_str(), config.serverPort))
    {
        Disconnect();

        if (autoConnectEnabled)
            nextConnectAttemptMs = GetTickCount64() + CONNECT_RETRY_DELAY_MS;
    }
}

void NetMainGameComponent::Disconnect()
{
    ClearNetworkCallbacks();

    netPlayerManager.reset();
    network.reset();
}

void NetMainGameComponent::SetupNetworkCallbacks()
{
    // Route every creature action through the combat-sync send path. The
    // hook is installed once and left in place; the observer is cleared on
    // disconnect so it never dereferences a destroyed NetPlayerManager.
    ActionTracer::Install();
    ActionTracer::SetObserver([this](void* creature, void* action, const char* actionClass) {
        if (netPlayerManager)
            netPlayerManager->HandleLocalCreatureAction(creature, action, actionClass);
    });

    network->AddConnectionNotificationCallback("ConnectionNotification", [this](int networkId, SystemAddress systemAddress) {
        netPlayerManager->ConnectionNotification(networkId, systemAddress);
    });
    network->AddNetPlayerActionCallback("NetPlayerAction", [this](BitStream& bs) {
        int networkId = -1;
        int actionType = -1;
        C3DVector direction = {};
        bs.Read(networkId);
        bs.Read(actionType);
        bs.Read(direction);
        netPlayerManager->ReceiveNetPlayerAction(networkId, actionType, direction);
    });
    network->AddNetPlayerAnimCallback("NetPlayerAnim", [this](BitStream& bs) {
        int networkId = -1;
        AnimActionFields fields;
        unsigned char nameLen = 0;

        bs.Read(networkId);
        bs.Read(fields.d20);
        bs.Read(fields.d24);
        bs.Read(fields.keyExtra);
        bs.Read(fields.ctxId0);
        bs.Read(fields.ctxId1);
        bs.Read(fields.ctxFlag);
        bs.Read(fields.loops);
        bs.Read(fields.a8);
        bs.Read(fields.a9);
        bs.Read(fields.aa);
        bs.Read(fields.ab);
        bs.Read(fields.b0);
        bs.Read(nameLen);

        // nameLen == 0 is a nameless anim (NPC-ambient style) — valid.
        if (nameLen >= AnimActionFields::NAME_MAX)
            return;
        if (nameLen > 0 && !bs.ReadAlignedBytes((unsigned char*)fields.name, nameLen))
            return;
        fields.name[nameLen] = '\0';

        unsigned char ctxNameLen = 0;
        bs.Read(ctxNameLen);
        if (ctxNameLen >= AnimActionFields::NAME_MAX)
            return;
        if (ctxNameLen > 0 && !bs.ReadAlignedBytes((unsigned char*)fields.ctxName, ctxNameLen))
            return;
        fields.ctxName[ctxNameLen] = '\0';

        netPlayerManager->ReceiveNetPlayerAnim(networkId, fields);
    });
    network->AddCreateLocalNetPlayerCallback("CreateLocalNetPlayer", [this](BitStream& bs) {
        int networkId = -1;
        unsigned char hostIsPlayer = 1;
        C3DVector position = {};

        bs.Read(networkId);
        bs.Read(hostIsPlayer);
        bs.Read(position);

        netPlayerManager->CreateLocalNetPlayer(networkId, position, hostIsPlayer != 0);
    });
    network->AddCreateNetPlayerCallback("CreateNetPlayer", [this](BitStream& bs) {
        int networkId = -1;
        C3DVector position = {};
        int defGlobalIndex = -1;
        int regionIndex = -1;

        bs.Read(networkId);
        bs.Read(defGlobalIndex);
        bs.Read(position);
        bs.Read(regionIndex);

        netPlayerManager->CreateNetPlayer(networkId, position, defGlobalIndex, regionIndex);
    });
    network->AddCreateNetPlayersCallback("CreateNetPlayers", [this](BitStream& bs) {
        int count = 0;
        bs.Read(count);

        for (int i = 0; i < count; i++)
        {
            int networkId = -1;
            C3DVector position = {};
            int defGlobalIndex = -1;
            int regionIndex = -1;

            bs.Read(networkId);
            bs.Read(defGlobalIndex);
            bs.Read(position);
            bs.Read(regionIndex);

            netPlayerManager->CreateNetPlayers(networkId, position, defGlobalIndex, regionIndex);
        }
    });
    network->AddNetPlayerMovementCallback("NetPlayerMovement", [this](BitStream& bs) {
        int networkId = -1;
        C3DVector remotePosition = {};
        C3DVector movementAcceleration = {};

        bs.Read(networkId);
        bs.Read(remotePosition);
        bs.Read(movementAcceleration);

        netPlayerManager->ReceiveNetPlayerMovement(networkId, remotePosition, movementAcceleration);
    });
    network->AddNetPlayerRotationCallback("NetPlayerRotation", [this](BitStream& bs) {
        int networkId = -1;
        C3DVector up = {};
        C3DVector forward = {};

        bs.Read(networkId);
        bs.Read(up);
        bs.Read(forward);

        netPlayerManager->ReceiveNetPlayerRotation(networkId, up, forward);
    });
    network->AddNetPlayerRegionCallback("NetPlayerRegion", [this](BitStream& bs) {
        int networkId = -1;
        int regionIndex = -1;
        C3DVector position = {};

        bs.Read(networkId);
        bs.Read(regionIndex);
        bs.Read(position);

        netPlayerManager->ReceiveNetPlayerRegion(networkId, regionIndex, position);
    });
    network->AddNetPlayerAppearanceCallback("NetPlayerAppearance", [this](BitStream& bs) {
        int networkId = -1;
        HeroMorphValues morph;
        int count = 0;

        bs.Read(networkId);
        for (unsigned int& value : morph.raw)
            bs.Read(value);

        HeroStatsExperience exp;
        for (int& value : exp.spentOn)
            bs.Read(value);

        int meleeWeaponDef = -1;
        int rangedWeaponDef = -1;
        bs.Read(meleeWeaponDef);
        bs.Read(rangedWeaponDef);

        bs.Read(count);

        if (count < 0 || count > 256)
            return;

        std::vector<int> modifierDefIndexes;
        modifierDefIndexes.reserve(count);

        for (int i = 0; i < count; i++)
        {
            int defIndex = -1;
            bs.Read(defIndex);
            modifierDefIndexes.push_back(defIndex);
        }

        netPlayerManager->ReceiveNetPlayerAppearance(networkId, morph, exp, meleeWeaponDef, rangedWeaponDef, std::move(modifierDefIndexes));
    });
    network->AddDestroyLocalNetPlayerCallback("DestroyLocalNetPlayer", [this]() {
        netPlayerManager->DestroyLocalNetPlayer();
    });
    network->AddDestroyNetPlayerCallback("DestroyNetPlayer", [this](int networkId) {
        netPlayerManager->DestroyNetPlayer(networkId);
    });
    network->AddDestroyNetPlayersCallback("DestroyNetPlayers", [this]() {
        netPlayerManager->DestroyNetPlayers();
    });
}

void NetMainGameComponent::ClearNetworkCallbacks()
{
    // Stop routing actions before the NetPlayerManager is destroyed.
    ActionTracer::SetObserver(nullptr);

    if (network) {
        network->RemoveNetPlayerActionCallback("NetPlayerAction");
        network->RemoveNetPlayerAnimCallback("NetPlayerAnim");
        network->RemoveConnectionNotificationCallback("ConnectionNotification");
        network->RemoveDisconnectionNotificationCallback("DisconnectionNotification");
        network->RemoveConnectionLostCallback("ConnectionLost");
        network->RemoveConnectionAttemptFailedCallback("ConnectionAttemptFailed");

        network->RemoveCreateLocalNetPlayerCallback("CreateLocalNetPlayer");

        network->RemoveCreateNetPlayerCallback("CreateNetPlayer");
        network->RemoveCreateNetPlayersCallback("CreateNetPlayers");

        network->RemoveNetPlayerMovementCallback("NetPlayerMovement");
        network->RemoveNetPlayerRotationCallback("NetPlayerRotation");
        network->RemoveNetPlayerRegionCallback("NetPlayerRegion");
        network->RemoveNetPlayerAppearanceCallback("NetPlayerAppearance");

        network->RemoveDestroyNetPlayerCallback("DestroyNetPlayer");
        network->RemoveDestroyNetPlayersCallback("DestroyNetPlayers");
    }
}
