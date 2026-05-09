#include "Network.h"

void Network::RemoveSelf()
{
	for (const auto& pair : destroyLocalNetPlayerCallbacks)
	{
		if (pair.second)
			pair.second();
	}

	self = Connection{};
}

void Network::RemoveConnection(int networkId)
{
	for (const auto& pair : destroyNetPlayerCallbacks)
	{
		if (pair.second)
			pair.second(networkId);
	}

	for (auto connection = connections.begin(); connection != connections.end(); ++connection)
	{
		if (connection->networkId == networkId)
		{
			connections.erase(connection);
			break;
		}
	}
}

void Network::RemoveConnections()
{
	for (const auto& pair : destroyNetPlayersCallbacks)
	{
		if (pair.second)
			pair.second();
	}

	connections.clear();
}

void Network::ClearSession()
{
	settings.ip.clear();
	settings.port = 0;
}

int Network::GetFreeNetworkId()
{
	int networkId = 0;

	while (true)
	{
		bool used = false;
		for (auto& connection : connections)
		{
			if (connection.networkId == networkId)
			{
				used = true;
				break;
			}
		}

		if (!used)
			return networkId;

		++networkId;
	}
}

int Network::GetNetworkIdFromAddress(const SystemAddress& address) const
{
	SystemAddress hostAddress(this->settings.ip.c_str(), this->settings.port);
	if (address == hostAddress)
	{
		return 0;
	}

	for (const auto& connection : connections)
	{
		if (connection.address == address)
			return connection.networkId;
	}

	return -1;
}

SystemAddress Network::GetAddressFromNetworkId(int networkId) const
{
	for (const auto& connection : connections)
	{
		if (connection.networkId == networkId)
			return connection.address;
	}

	return SystemAddress();
}
