#pragma once

#include <vector>
#include <functional>
#include <iostream>

#include "../Utils/Hook.h"

class CThingPlayerCreatureInit {
public:
	char pad[0xC];

	static void Hook();
};
