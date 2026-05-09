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

	void AddResolveMovementAccelerationCallback(const std::string& id, std::function<void()> callback) { resolveMovementAccelerationCallbacks[id] = callback; }
	void RemoveResolveMovementAccelerationCallback(const std::string& id) { resolveMovementAccelerationCallbacks.erase(id); }

	void AddResolveFacingDirectionCallback(const std::string& id, std::function<void()> callback) { resolveFacingDirectionCallbacks[id] = callback; }
	void RemoveResolveFacingDirectionCallback(const std::string& id) { resolveFacingDirectionCallbacks.erase(id); }

	static void Hook();

private:
	static std::map<std::string, std::function<void()>> resolveMovementAccelerationCallbacks;
	static std::map<std::string, std::function<void()>> resolveFacingDirectionCallbacks;

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
