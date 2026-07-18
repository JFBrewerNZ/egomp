#include "DefinitionManager.h"

#include <windows.h>
#include <cstdint>

CDefinitionManager* CDefinitionManager::Get()
{
    return *(CDefinitionManager**)(0x13B879C);
}

int(__thiscall* CDefinitionManager::OGetDefGlobalIndexFromName)(CDefinitionManager*, const CCharString*) = nullptr;
int __fastcall CDefinitionManager::HGetDefGlobalIndexFromName(CDefinitionManager* _this, void* _EDX, const CCharString* instantiation_name)
{
    return OGetDefGlobalIndexFromName(_this, instantiation_name);
}

int CDefinitionManager::GetDefGlobalIndexFromName(const CCharString* instantiation_name)
{
    return OGetDefGlobalIndexFromName(this, instantiation_name);
}

void* CDefinitionManager::GetDefObjectByIndex(int globalIndex)
{
    if (globalIndex < 0)
        return nullptr;
    __try
    {
        void** table = *reinterpret_cast<void***>(reinterpret_cast<char*>(this) + 0xA4);
        if (!table)
            return nullptr;
        void* def = table[globalIndex];
        // Touch the object so a bad index/pointer faults here (caught), not in
        // the caller. Def objects live in the game heap, well above the image.
        if (reinterpret_cast<uintptr_t>(def) < 0x10000)
            return nullptr;
        volatile int probe = *reinterpret_cast<volatile int*>(def);
        (void)probe;
        return def;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return nullptr;
    }
}

void CDefinitionManager::Hook()
{
    ADD_HOOK(0x9AD410, HGetDefGlobalIndexFromName, OGetDefGlobalIndexFromName);
}
