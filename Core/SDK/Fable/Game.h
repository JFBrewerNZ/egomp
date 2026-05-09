#pragma once

#include <map>
#include <functional>
#include <iostream>

#include "../Utils/Hook.h"

class CGame
{
public:
    static CGame* GetInstance();

    static void Hook();

private:
    static void(__thiscall* OInitialise)(CGame*);
    static void __fastcall HInitialise(CGame* _this, void* _EDX);
};

