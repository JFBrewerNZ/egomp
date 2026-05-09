#include "Network.h"

void Network::SendTo(const char* data, int length, PacketPriority priority, PacketReliability reliability, SystemAddress systemAddress)
{
	peer->Send(data, length, priority, reliability, 0, systemAddress, false);
}

void Network::SendToHost(const char* data, int length, PacketPriority priority, PacketReliability reliability)
{
	peer->Send(data, length, priority, reliability, 0, SystemAddress(this->settings.ip.c_str(), this->settings.port), false);
}

void Network::SendToClient(int networkId, const char* data, int length, PacketPriority priority, PacketReliability reliability)
{
	for (auto& connection : connections)
	{
		if (connection.networkId == networkId && connection.address != UNASSIGNED_SYSTEM_ADDRESS)
		{
			peer->Send(data, length, priority, reliability, 0, connection.address, false);
			return;
		}
	}
}

void Network::SendToAll(const char* data, int length, PacketPriority priority, PacketReliability reliability)
{
	for (auto& connection : connections)
	{
		if (connection.address != UNASSIGNED_SYSTEM_ADDRESS)
		{
			peer->Send(data, length, priority, reliability, 0, connection.address, false);
		}
	}
}

void Network::SendToAllClients(const char* data, int length, PacketPriority priority, PacketReliability reliability)
{
	for (auto& connection : connections)
	{
		if (connection.address != UNASSIGNED_SYSTEM_ADDRESS && connection.networkId != 0)
		{
			peer->Send(data, length, priority, reliability, 0, connection.address, false);
		}
	}
}

void Network::SendToAllClientsExcept(int networkId, const char* data, int length,
	PacketPriority priority, PacketReliability reliability)
{
	for (auto& connection : connections)
	{
		if (connection.networkId != networkId &&
			connection.networkId != 0 &&
			connection.address != UNASSIGNED_SYSTEM_ADDRESS)
		{
			peer->Send(data, length, priority, reliability, 0, connection.address, false);
		}
	}
}

void Network::SendToAllExcept(int networkId, const char* data, int length, PacketPriority priority, PacketReliability reliability)
{
	for (auto& connection : connections)
	{
		if (connection.networkId != networkId && connection.address != UNASSIGNED_SYSTEM_ADDRESS)
		{
			peer->Send(data, length, priority, reliability, 0, connection.address, false);
		}
	}
}
