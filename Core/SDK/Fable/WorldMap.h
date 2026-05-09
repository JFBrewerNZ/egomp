#pragma once

#include <functional>
#include <iostream>

#include "../Utils/Hook.h"

class CWorldMap
{
public:
    static void Hook();

private:
    static void(__thiscall* OPostRegionLoad)(CWorldMap*, long);
    static void __fastcall HPostRegionLoad(CWorldMap* _this, void* _EDX, long region_index);
};
