#pragma once

#include <vector>
#include <functional>
#include <iostream>

#include "../Utils/Hook.h"

#include "3DVector.h"

class CTCPhysicsBase
{
public:
    char pad0[0x0C];

    C3DVector Position;
    C3DVector OldPosition;
    int OldPositionLastFrameSet;
    C3DVector Velocity;
    float Radius;

	void SetPosition(C3DVector const& pos);
	static void Hook();

private:
    static void(__thiscall* OSetPosition)(CTCPhysicsBase*, C3DVector const&);
	static void __fastcall HSetPosition(CTCPhysicsBase* _this, void* _EDX, C3DVector const& pos);
};
