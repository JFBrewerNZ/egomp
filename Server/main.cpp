// EgoMP dedicated server.
//
// The server is a session authority and relay: it assigns network ids,
// remembers each player's spawn state, and forwards player updates. It holds
// no game world of its own; every client simulates its own world and the
// server replicates player presence between them.
//
// Clients treat networkId 0 as "the host", so the server reserves id 0 for
// itself and assigns ids from 1 upward. Unlike a player host, the server
// announces itself with hostIsPlayer=0 so clients neither teleport to it nor
// expect a creature for it.

#include <iostream>
#include <map>

#include <SLikeNet/RakPeerInterface.h>
#include <SLikeNet/RakNetTypes.h>
#include <SLikeNet/BitStream.h>
#include <SLikeNet/RakSleep.h>

#include "../Core/Multiplayer/Network/NetworkMessages.h"

using namespace SLNet;

// Mirrors the game's C3DVector wire layout (three floats).
struct Vec3
{
    float X = 0.0f, Y = 0.0f, Z = 0.0f;
};

struct PlayerState
{
    SystemAddress address;
    int defGlobalIndex = -1;
    int regionIndex = -1;
    Vec3 position;
    Vec3 up;
    Vec3 forward;
    bool announced = false; // true once the client has sent ID_CREATE_NET_PLAYER
};

class Server
{
public:
    bool Start(unsigned short port, int slots)
    {
        peer = RakPeerInterface::GetInstance();

        SocketDescriptor sd(port, 0);

        if (peer->Startup(slots, &sd, 1) != RAKNET_STARTED)
        {
            std::cout << "[Server::Start] Failed to start on port " << port << std::endl;
            return false;
        }

        peer->SetMaximumIncomingConnections(slots);

        std::cout << "[Server::Start] Listening on port " << port
            << " (" << slots << " slots)" << std::endl;

        return true;
    }

    void Run()
    {
        while (true)
        {
            Update();
            RakSleep(10);
        }
    }

private:
    RakPeerInterface* peer = nullptr;
    std::map<int, PlayerState> players; // networkId -> state; id 0 is the server itself

    void Update()
    {
        for (Packet* packet = peer->Receive(); packet; peer->DeallocatePacket(packet), packet = peer->Receive())
        {
            switch (packet->data[0])
            {
            case ID_NEW_INCOMING_CONNECTION:
                std::cout << "[Server] ID_NEW_INCOMING_CONNECTION: "
                    << packet->systemAddress.ToString() << std::endl;
                break;

            case ID_CONNECTION_NOTIFICATION:
                HandleConnectionNotification(packet);
                break;

            case ID_CREATE_NET_PLAYER:
                HandleCreateNetPlayer(packet);
                break;

            case ID_PLAYER_MOVEMENT:
                HandlePlayerMovement(packet);
                break;

            case ID_PLAYER_ROTATION:
                HandlePlayerRotation(packet);
                break;

            case ID_PLAYER_REGION:
                HandlePlayerRegion(packet);
                break;

            case ID_DISCONNECTION_NOTIFICATION:
            case ID_CONNECTION_LOST:
                HandleDisconnect(packet);
                break;

            default:
                std::cout << "[Server] Unhandled message ID: "
                    << (int)packet->data[0] << " from "
                    << packet->systemAddress.ToString() << std::endl;
                break;
            }
        }
    }

    void HandleConnectionNotification(Packet* packet)
    {
        int protocolVersion = -1;

        BitStream versionBs(packet->data, packet->length, false);
        versionBs.IgnoreBytes(sizeof(MessageID));
        versionBs.Read(protocolVersion);

        if (protocolVersion != EGOMP_PROTOCOL_VERSION)
        {
            std::cout << "[Server] Rejecting " << packet->systemAddress.ToString()
                << ": protocol version " << protocolVersion
                << " (expected " << EGOMP_PROTOCOL_VERSION << ")" << std::endl;

            peer->CloseConnection(packet->systemAddress, true);
            return;
        }

        int networkId = GetFreeNetworkId();

        PlayerState state;
        state.address = packet->systemAddress;
        players[networkId] = state;

        BitStream bs;
        bs.Write((MessageID)ID_CREATE_LOCAL_NET_PLAYER);
        bs.Write(networkId);
        bs.Write((unsigned char)0); // hostIsPlayer: dedicated server, client stays at its own position
        bs.Write(Vec3{});

        peer->Send(&bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, packet->systemAddress, false);

        std::cout << "[Server] Player connected: "
            << packet->systemAddress.ToString() << " - id " << networkId << std::endl;
    }

    void HandleCreateNetPlayer(Packet* packet)
    {
        BitStream in(packet->data, packet->length, false);
        in.IgnoreBytes(sizeof(MessageID));

        int networkId = -1;
        int defGlobalIndex = -1;
        Vec3 position;
        int regionIndex = -1;

        in.Read(networkId);
        in.Read(defGlobalIndex);
        in.Read(position);
        in.Read(regionIndex);

        int senderId = GetNetworkIdFromAddress(packet->systemAddress);

        if (senderId == -1 || senderId != networkId)
        {
            std::cout << "[Server] Dropping ID_CREATE_NET_PLAYER with id " << networkId
                << " from " << packet->systemAddress.ToString()
                << " (assigned id " << senderId << ")" << std::endl;
            return;
        }

        PlayerState& state = players[senderId];
        state.defGlobalIndex = defGlobalIndex;
        state.position = position;
        state.regionIndex = regionIndex;
        state.announced = true;

        // Introduce the newcomer to everyone else...
        SendToAnnouncedExcept(senderId, packet, HIGH_PRIORITY, RELIABLE_ORDERED);

        // ...and everyone else to the newcomer.
        int count = 0;
        for (const auto& pair : players)
        {
            if (pair.first != senderId && pair.second.announced)
                ++count;
        }

        if (count > 0)
        {
            BitStream seed;
            seed.Write((MessageID)ID_CREATE_NET_PLAYERS);
            seed.Write(count);

            for (const auto& pair : players)
            {
                if (pair.first == senderId || !pair.second.announced)
                    continue;

                seed.Write(pair.first);
                seed.Write(pair.second.defGlobalIndex);
                seed.Write(pair.second.position);
                seed.Write(pair.second.regionIndex);
            }

            peer->Send(&seed, HIGH_PRIORITY, RELIABLE_ORDERED, 0, state.address, false);
        }

        std::cout << "[Server] Player announced: id " << senderId
            << ", def " << defGlobalIndex
            << ", region " << regionIndex
            << ", pos (" << position.X << ", " << position.Y << ", " << position.Z << ")"
            << std::endl;
    }

    void HandlePlayerRegion(Packet* packet)
    {
        BitStream in(packet->data, packet->length, false);
        in.IgnoreBytes(sizeof(MessageID));

        int networkId = -1;
        int regionIndex = -1;
        Vec3 position;

        in.Read(networkId);
        in.Read(regionIndex);
        in.Read(position);

        int senderId = GetNetworkIdFromAddress(packet->systemAddress);

        if (senderId == -1 || senderId != networkId)
            return;

        players[senderId].regionIndex = regionIndex;
        players[senderId].position = position;

        SendToAnnouncedExcept(senderId, packet, HIGH_PRIORITY, RELIABLE_ORDERED);

        std::cout << "[Server] Player " << senderId << " entered region " << regionIndex << std::endl;
    }

    void HandlePlayerMovement(Packet* packet)
    {
        BitStream in(packet->data, packet->length, false);
        in.IgnoreBytes(sizeof(MessageID));

        int networkId = -1;
        Vec3 position;

        in.Read(networkId);
        in.Read(position);

        int senderId = GetNetworkIdFromAddress(packet->systemAddress);

        if (senderId == -1 || senderId != networkId)
            return;

        players[senderId].position = position;

        SendToAnnouncedExcept(senderId, packet, HIGH_PRIORITY, UNRELIABLE_SEQUENCED);
    }

    void HandlePlayerRotation(Packet* packet)
    {
        BitStream in(packet->data, packet->length, false);
        in.IgnoreBytes(sizeof(MessageID));

        int networkId = -1;
        Vec3 up;
        Vec3 forward;

        in.Read(networkId);
        in.Read(up);
        in.Read(forward);

        int senderId = GetNetworkIdFromAddress(packet->systemAddress);

        if (senderId == -1 || senderId != networkId)
            return;

        players[senderId].up = up;
        players[senderId].forward = forward;

        SendToAnnouncedExcept(senderId, packet, HIGH_PRIORITY, UNRELIABLE_SEQUENCED);
    }

    void HandleDisconnect(Packet* packet)
    {
        int networkId = GetNetworkIdFromAddress(packet->systemAddress);

        std::cout << "[Server] Player disconnected: "
            << packet->systemAddress.ToString() << " - id " << networkId << std::endl;

        if (networkId == -1)
            return;

        bool wasAnnounced = players[networkId].announced;
        players.erase(networkId);

        if (wasAnnounced)
        {
            BitStream bs;
            bs.Write((MessageID)ID_DESTROY_NET_PLAYER);
            bs.Write(networkId);

            SendToAnnounced(bs, HIGH_PRIORITY, RELIABLE_ORDERED);
        }
    }

    // Forwards a client packet verbatim to every announced player except one.
    void SendToAnnouncedExcept(int exceptId, Packet* packet, PacketPriority priority, PacketReliability reliability)
    {
        for (const auto& pair : players)
        {
            if (pair.first == exceptId || !pair.second.announced)
                continue;

            peer->Send((const char*)packet->data, packet->length, priority, reliability, 0, pair.second.address, false);
        }
    }

    void SendToAnnounced(BitStream& bs, PacketPriority priority, PacketReliability reliability)
    {
        for (const auto& pair : players)
        {
            if (!pair.second.announced)
                continue;

            peer->Send(&bs, priority, reliability, 0, pair.second.address, false);
        }
    }

    int GetFreeNetworkId() const
    {
        int networkId = 1;

        while (players.count(networkId))
            ++networkId;

        return networkId;
    }

    int GetNetworkIdFromAddress(const SystemAddress& address) const
    {
        for (const auto& pair : players)
        {
            if (pair.second.address == address)
                return pair.first;
        }

        return -1;
    }
};

int main(int argc, char** argv)
{
    unsigned short port = 60000;
    int slots = 8;

    if (argc > 1)
        port = (unsigned short)atoi(argv[1]);

    if (argc > 2)
        slots = atoi(argv[2]);

    Server server;

    if (!server.Start(port, slots))
        return 1;

    server.Run();

    return 0;
}
