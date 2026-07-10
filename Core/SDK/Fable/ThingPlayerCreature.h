#pragma once

#include <map>
#include <functional>
#include <iostream>

#include "../Utils/Hook.h"

#include "GameAction.h"
#include "3DVector.h"
#include "ThingPlayerCreatureInit.h"

class CThingPlayerCreature
{
public:
	char pad[0x180];
	C3DVector MovementAcceleration;

	static CThingPlayerCreature* Create(long, C3DVector const&, long, CThingPlayerCreatureInit const&);

	// Callbacks are global (the maps are static): they run for every creature's resolve,
	// receiving the resolving creature so they can filter. Add/Remove are static so
	// callbacks can be unregistered without a live creature pointer.
	static void AddResolveMovementAccelerationCallback(const std::string& id, std::function<void(CThingPlayerCreature*)> callback) { resolveMovementAccelerationCallbacks[id] = callback; }
	static void RemoveResolveMovementAccelerationCallback(const std::string& id) { resolveMovementAccelerationCallbacks.erase(id); }

	static void AddResolveFacingDirectionCallback(const std::string& id, std::function<void(CThingPlayerCreature*)> callback) { resolveFacingDirectionCallbacks[id] = callback; }
	static void RemoveResolveFacingDirectionCallback(const std::string& id) { resolveFacingDirectionCallbacks.erase(id); }

	static void Hook();

private:
	static std::map<std::string, std::function<void(CThingPlayerCreature*)>> resolveMovementAccelerationCallbacks;
	static std::map<std::string, std::function<void(CThingPlayerCreature*)>> resolveFacingDirectionCallbacks;

	static CThingPlayerCreature* (__fastcall* OCreate)(long, C3DVector const&, long, CThingPlayerCreatureInit const &);
	static CThingPlayerCreature* __fastcall HCreate(long global_def_index, C3DVector const& pos, long player, CThingPlayerCreatureInit const &init);

	static void (__thiscall* OResolveMovementAcceleration)(CThingPlayerCreature*);
	static void __fastcall HResolveMovementAcceleration(CThingPlayerCreature* _this, void* _EDX);

	static void(__thiscall* OResolveFacingDirection)(CThingPlayerCreature*);
	static void __fastcall HResolveFacingDirection(CThingPlayerCreature* _this, void* _EDX);

	static bool(__thiscall* OWorldUpdate)(CThingPlayerCreature*);
	static bool __fastcall HWorldUpdate(CThingPlayerCreature* _this, void* _EDX);

	static void(__thiscall* OUpdateWalkingControlForces)(CThingPlayerCreature*);
	static void __fastcall HUpdateWalkingControlForces(CThingPlayerCreature* _this, void* _EDX);

	static void(__thiscall* OApplyRelativeMovementAcceleration)(CThingPlayerCreature*, C3DVector const&, EGameAction);
	static void __fastcall HApplyRelativeMovementAcceleration(CThingPlayerCreature* _this, void* _EDX, C3DVector const& vect, EGameAction game_action);
};
