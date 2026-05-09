#include "PhysicsControlled.h"

void(__thiscall* CTCPhysicsControlled::OApplyControlledAcceleration)(CTCPhysicsControlled*, C3DVector const&, float) = nullptr;
void __fastcall CTCPhysicsControlled::HApplyControlledAcceleration(CTCPhysicsControlled* _this, void* _EDX, C3DVector const& impulse, float max_speed_allowed)
{
	OApplyControlledAcceleration(_this, impulse, max_speed_allowed);
}

void CTCPhysicsControlled::ApplyControlledAcceleration(C3DVector const& impulse, float max_speed_allowed)
{
	OApplyControlledAcceleration(this, impulse, max_speed_allowed);
}

void CTCPhysicsControlled::Hook()
{
	ADD_HOOK(0x00725060, HApplyControlledAcceleration, OApplyControlledAcceleration);
}
