#pragma once

#include <map>
#include <string>
#include <functional>
#include <iostream>

#include "../Utils/Hook.h"

class CWorldMap
{
public:
    static void Hook();

    // Index of the most recently loaded region, -1 before the first load.
    static long GetCurrentRegionIndex() { return currentRegionIndex; }

    static void AddPostRegionLoadCallback(const std::string& id, std::function<void(long)> callback) { postRegionLoadCallbacks[id] = callback; }
    static void RemovePostRegionLoadCallback(const std::string& id) { postRegionLoadCallbacks.erase(id); }

private:
    static long currentRegionIndex;
    static std::map<std::string, std::function<void(long)>> postRegionLoadCallbacks;

    static void(__thiscall* OPostRegionLoad)(CWorldMap*, long);
    static void __fastcall HPostRegionLoad(CWorldMap* _this, void* _EDX, long region_index);
};
