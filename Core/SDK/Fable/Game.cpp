#include "Game.h"

CGame* CGame::GetInstance()
{
    return *(CGame**)0x0173CF28;
}

void(__thiscall* CGame::OInitialise)(CGame*) = nullptr;
void __fastcall CGame::HInitialise(CGame* _this, void* _EDX)
{
    OInitialise(_this);
}

void CGame::Hook()
{
    ADD_HOOK(0x00413120, HInitialise, OInitialise);
}
