#define NOMINMAX

#include "windows.h"
#include <string>

#include "NetMainGameComponent.h"
#include "../../../Config/Config.h"

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
}

void NetMainGameComponent::Selection() {
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
    network->AddConnectionNotificationCallback("ConnectionNotification", [this](int networkId, SystemAddress systemAddress) {
        netPlayerManager->ConnectionNotification(networkId, systemAddress);
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
        int defGlobalIndex;

        bs.Read(networkId);
        bs.Read(defGlobalIndex);
        bs.Read(position);

        netPlayerManager->CreateNetPlayer(networkId, position, defGlobalIndex);
    });
    network->AddCreateNetPlayersCallback("CreateNetPlayers", [this](BitStream& bs) {
        int count = 0;
        bs.Read(count);

        for (int i = 0; i < count; i++)
        {
            int networkId = -1;
            C3DVector position = {};
            int defGlobalIndex;

            bs.Read(networkId);
            bs.Read(defGlobalIndex);
            bs.Read(position);

            netPlayerManager->CreateNetPlayers(networkId, position, defGlobalIndex);
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
    if (network) {
        network->RemoveConnectionNotificationCallback("ConnectionNotification");
        network->RemoveDisconnectionNotificationCallback("DisconnectionNotification");
        network->RemoveConnectionLostCallback("ConnectionLost");
        network->RemoveConnectionAttemptFailedCallback("ConnectionAttemptFailed");

        network->RemoveCreateLocalNetPlayerCallback("CreateLocalNetPlayer");

        network->RemoveCreateNetPlayerCallback("CreateNetPlayer");
        network->RemoveCreateNetPlayersCallback("CreateNetPlayers");

        network->RemoveNetPlayerMovementCallback("NetPlayerMovement");
        network->RemoveNetPlayerRotationCallback("NetPlayerRotation");

        network->RemoveDestroyNetPlayerCallback("DestroyNetPlayer");
        network->RemoveDestroyNetPlayersCallback("DestroyNetPlayers");
    }
}
