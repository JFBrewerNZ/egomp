#pragma once

#include <iostream>

#include "../Utils/Hook.h"

#include "CharString.h"

class CDefinitionManager
{
public:
    static CDefinitionManager* Get();
    int GetDefGlobalIndexFromName(const CCharString* instantiation_name);

    // The def object for a global index, or nullptr. Mirrors the game's own
    // resolve (GetDefNameFromGlobalIndex 0x9ACCC0): the def table base is at
    // this+0xA4, entries are one pointer each — def = (*(this+0xA4))[index].
    // SEH-guarded; rejects negative indices and unreadable pointers (the game
    // trusts the index, we can't).
    void* GetDefObjectByIndex(int globalIndex);

    static void Hook();

private:
    static int(__thiscall* OGetDefGlobalIndexFromName)(CDefinitionManager*, const CCharString*);
    static int __fastcall HGetDefGlobalIndexFromName(CDefinitionManager* _this, void* _EDX, const CCharString* instantiation_name);
};