#include "Multiplayer.h"

Multiplayer& Multiplayer::GetInstance()
{
    static Multiplayer instance;
    return instance;
}

Multiplayer::Multiplayer()
    : sdk(SDK::GetInstance()),
    net(NetMainGameComponent::GetInstance())
{
}
