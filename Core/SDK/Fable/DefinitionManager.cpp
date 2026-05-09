#include "DefinitionManager.h"

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

void CDefinitionManager::Hook()
{
    ADD_HOOK(0x9AD410, HGetDefGlobalIndexFromName, OGetDefGlobalIndexFromName);
}
