#pragma once

#include <vector>
#include <functional>
#include <iostream>

#include "../Utils/Hook.h"

#include "3DVector.h"

class CTCPhysicsControlled
{
public:
    char pad[0x88];

    C3DVector ImpulseVelocity;
    C3DVector ForcedMoveToPos;

    void ApplyControlledAcceleration(C3DVector const& impulse, float max_speed_allowed);
    static void Hook();

private:
    static void(__thiscall* OApplyControlledAcceleration)(CTCPhysicsControlled*, C3DVector const&, float);
    static void __fastcall HApplyControlledAcceleration(CTCPhysicsControlled* _this, void* _EDX, C3DVector const& impulse, float max_speed_allowed);
};
