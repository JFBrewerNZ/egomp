#pragma once

#include <vector>
#include <functional>
#include <iostream>

#include "../Utils/Hook.h"

#include "Thing.h"

class CTCCoopSpirit
{
public:
    CTCCoopSpirit(CThing& thing);
    static void Hook();

private:
    static void(__thiscall* OCTCCoopSpirit)(CTCCoopSpirit*, CThing &);
    static void __fastcall HCTCCoopSpirit(CTCCoopSpirit* _this, void* _EDX, CThing &thing);

    static int(__thiscall* OGetScore)(CTCCoopSpirit*);
    static int __fastcall HGetScore(CTCCoopSpirit* _this, void* _EDX);
};
