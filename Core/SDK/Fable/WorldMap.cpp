#include "WorldMap.h"


void(__thiscall* CWorldMap::OPostRegionLoad)(CWorldMap*, long) = nullptr;
void __fastcall CWorldMap::HPostRegionLoad(CWorldMap* _this, void* _EDX, long region_index)
{
    OPostRegionLoad(_this, region_index);
}

void CWorldMap::Hook()
{
    ADD_HOOK(0x005064C0, HPostRegionLoad, OPostRegionLoad);
}
