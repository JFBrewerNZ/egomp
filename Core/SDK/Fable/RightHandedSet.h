#pragma once

#include <vector>
#include <functional>
#include <iostream>

#include "../Utils/Hook.h"

#include "3DVector.h"

class CRightHandedSet
{
public:
    C3DVector Up;
    C3DVector Forward;

    static void Hook();
};
