#include "ThingCreatureBase.h"

void(__thiscall* CThingCreatureBase::OUpdateAnimation)(CThingCreatureBase*, float) = nullptr;
void __fastcall CThingCreatureBase::HUpdateAnimation(CThingCreatureBase* _this, void* _EDX, float distance_from_player)
{
	OUpdateAnimation(_this, distance_from_player);
}

void CThingCreatureBase::Hook()
{
	ADD_HOOK(0x00665860, HUpdateAnimation, OUpdateAnimation);
}
