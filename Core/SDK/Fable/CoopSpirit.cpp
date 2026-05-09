#include "CoopSpirit.h"

CTCCoopSpirit::CTCCoopSpirit(CThing& thing)
{
    OCTCCoopSpirit(this, thing);
}

void(__thiscall* CTCCoopSpirit::OCTCCoopSpirit)(CTCCoopSpirit*, CThing &) = nullptr;
void __fastcall CTCCoopSpirit::HCTCCoopSpirit(CTCCoopSpirit* _this, void* _EDX, CThing &thing)
{
    OCTCCoopSpirit(_this, thing);
}

int(__thiscall* CTCCoopSpirit::OGetScore)(CTCCoopSpirit*) = nullptr;
int __fastcall CTCCoopSpirit::HGetScore(CTCCoopSpirit* _this, void* _EDX)
{
    return OGetScore(_this);
}

void CTCCoopSpirit::Hook()
{
    ADD_HOOK(0x00670050, HCTCCoopSpirit, OCTCCoopSpirit);
    ADD_HOOK(0x0066FB20, HGetScore, OGetScore);
}
