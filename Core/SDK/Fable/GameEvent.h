#pragma once

#include <vector>
#include <functional>
#include <iostream>

#include "../Utils/Hook.h"

class CGameEvent
{
public:
    int Type;
    char Player;
    unsigned __int8 Data[32];
    unsigned __int8 EndPos;
    bool Valid;
    bool Replacement;

    static void Hook();
};
