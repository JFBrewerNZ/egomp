#include "World.h"

std::map<std::string, std::function<void()>> CWorld::updateRegionLoadCallbacks;
std::map<std::string, std::function<void()>> CWorld::setAsLoadingRegionCallbacks;

CWorldMap* CWorld::GetWorldMap()
{
    return *(CWorldMap**)((char*)this + 0x14);
}

void (__thiscall* CWorld::OEAMoveHeroToRegion)(CWorld*, CGameEvent const*) = nullptr;
void __fastcall CWorld::HEAMoveHeroToRegion(CWorld* _this, void* _EDX, CGameEvent const* event)
{
    OEAMoveHeroToRegion(_this, event);
}

void CWorld::EAMoveHeroToRegion(CGameEvent const* event)
{
    OEAMoveHeroToRegion(this, event);
}

void(__thiscall* CWorld::OHandleMoveHeroToRegionGameEvent)(CWorld*, CGameEvent const&) = nullptr;
void __fastcall CWorld::HHandleMoveHeroToRegionGameEvent(CWorld* _this, void* _EDX, CGameEvent const& game_event)
{
    OHandleMoveHeroToRegionGameEvent(_this, game_event);
}

void CWorld::HandleMoveHeroToRegionGameEvent(CGameEvent const& game_event)
{
    OHandleMoveHeroToRegionGameEvent(this, game_event);
}

void(__thiscall* CWorld::OSetAsLoadingRegion)(CWorld*, C3DVector const&, float, bool, bool, bool) = nullptr;
void __fastcall CWorld::HSetAsLoadingRegion(CWorld* _this, void* _EDX, C3DVector const& region_start_pos, float facing_angle_xy, bool via_teleporter, bool allow_during_cut_scenes, bool via_door)
{
    // Dispatch before the original so callbacks run while the current region
    // is still intact. Iterate a snapshot: callbacks may unregister themselves.
    auto callbacks = setAsLoadingRegionCallbacks;
    for (const auto& pair : callbacks)
    {
        if (pair.second)
            pair.second();
    }

    OSetAsLoadingRegion(_this, region_start_pos, facing_angle_xy, via_teleporter, allow_during_cut_scenes, via_door);
}

void CWorld::SetAsLoadingRegion(C3DVector const& region_start_pos, float facing_angle_xy, bool via_teleporter, bool allow_during_cut_scenes, bool via_door)
{
    OSetAsLoadingRegion(this, region_start_pos, facing_angle_xy, via_teleporter, allow_during_cut_scenes, via_door);
}

void(__thiscall* CWorld::OUpdateRegionLoad)(CWorld*) = nullptr;
void __fastcall CWorld::HUpdateRegionLoad(CWorld* _this, void* _EDX)
{
    OUpdateRegionLoad(_this);

    // Iterate a snapshot: callbacks may unregister themselves during dispatch.
    auto callbacks = updateRegionLoadCallbacks;
    for (const auto& pair : callbacks)
    {
        if (pair.second)
            pair.second();
    }
}

void(__thiscall* CWorld::OSetAsPaused)(CWorld*, bool) = nullptr;
void __fastcall CWorld::HSetAsPaused(CWorld* _this, void* _EDX, bool paused)
{
    OSetAsPaused(_this, paused);
}

void CWorld::Hook()
{
    ADD_HOOK(0x00629260, HEAMoveHeroToRegion, OEAMoveHeroToRegion);
    ADD_HOOK(0x0049EAF0, HHandleMoveHeroToRegionGameEvent, OHandleMoveHeroToRegionGameEvent);
    ADD_HOOK(0x0049E2C0, HSetAsLoadingRegion, OSetAsLoadingRegion);
    ADD_HOOK(0x004A3740, HUpdateRegionLoad, OUpdateRegionLoad);
    ADD_HOOK(0x0049D8F0, HSetAsPaused, OSetAsPaused);
}
