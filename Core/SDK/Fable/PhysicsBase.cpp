#include "PhysicsBase.h"

void(__thiscall* CTCPhysicsBase::OSetPosition)(CTCPhysicsBase*, C3DVector const&) = nullptr;
void __fastcall CTCPhysicsBase::HSetPosition(CTCPhysicsBase* _this, void* _EDX, C3DVector const& pos)
{
	OSetPosition(_this, pos);
}

void CTCPhysicsBase::SetPosition(C3DVector const& pos)
{
	OSetPosition(this, pos);
}

void CTCPhysicsBase::Hook()
{
	ADD_HOOK(0x006B0C10, HSetPosition, OSetPosition);
}
