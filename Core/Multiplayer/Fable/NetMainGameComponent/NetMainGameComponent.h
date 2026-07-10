#pragma once

#include <iostream>

#include "../../../SDK/Fable/SDK.h"

#include "../../Network/Network.h"
#include "../NetPlayerManager/NetPlayerManager.h"

class NetMainGameComponent
{
public:
	static NetMainGameComponent& GetInstance();

	NetMainGameComponent();
	~NetMainGameComponent();

private:
	CMainGameComponent* mainGameComponent;

	std::unique_ptr<Network> network;
	std::unique_ptr<NetPlayerManager> netPlayerManager;

	void SetupCallbacks();
	void ClearCallbacks();

	void SetupNetworkCallbacks();
	void ClearNetworkCallbacks();

	void Selection();
	void HandleDebugKeys();
	void Options();
	void Host();
	void Connect();
	void Disconnect();

	void HandleMainGameComponentPostInit();
	void HandleMainGameComponentUpdate();
	void HandleMainGameComponentShutdown();

	bool worldReady = false;
	bool autoConnectEnabled = false;
	unsigned long long nextConnectAttemptMs = 0;
};
