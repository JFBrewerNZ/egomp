#pragma once

#include <vector>
#include <functional>
#include <iostream>

#include "../Utils/Hook.h"

#include "3DVector.h"

class CThingCreatureBase
{
public:
	char pad[0x10C];
	C3DVector MovementVector;

    static void Hook();

private:
	static void(__thiscall* OUpdateAnimation)(CThingCreatureBase*, float);
	static void __fastcall HUpdateAnimation(CThingCreatureBase* _this, void* _EDX, float distance_from_player);
};
