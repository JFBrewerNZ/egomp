#pragma once

#include "../../SDK/Fable/SDK.h"

#include "./NetMainGameComponent/NetMainGameComponent.h"

class Multiplayer
{
public:
	static Multiplayer& GetInstance();
	Multiplayer();

private:
	SDK& sdk;
	NetMainGameComponent& net;
};
