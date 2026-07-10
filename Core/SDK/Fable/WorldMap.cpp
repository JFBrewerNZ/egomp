#include "WorldMap.h"

long CWorldMap::currentRegionIndex = -1;
std::map<std::string, std::function<void(long)>> CWorldMap::postRegionLoadCallbacks;

void(__thiscall* CWorldMap::OPostRegionLoad)(CWorldMap*, long) = nullptr;
void __fastcall CWorldMap::HPostRegionLoad(CWorldMap* _this, void* _EDX, long region_index)
{
    OPostRegionLoad(_this, region_index);

    currentRegionIndex = region_index;

    // Iterate a snapshot: callbacks may unregister themselves during dispatch.
    auto callbacks = postRegionLoadCallbacks;
    for (const auto& pair : callbacks)
    {
        if (pair.second)
            pair.second(region_index);
    }
}

void CWorldMap::Hook()
{
    ADD_HOOK(0x005064C0, HPostRegionLoad, OPostRegionLoad);
}
