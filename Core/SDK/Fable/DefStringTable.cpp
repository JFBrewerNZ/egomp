#include "DefStringTable.h"

CDefStringTable* CDefStringTable::Get()
{
    return (CDefStringTable*)(0x013CA828);
}

CCharString* (__thiscall* CDefStringTable::OGetString)(CDefStringTable*, CCharString*, int) = nullptr;
CCharString* __fastcall CDefStringTable::HGetString(CDefStringTable* _this, void* _EDX, CCharString* result, int table_pos)
{
    return OGetString(_this, result, table_pos);
}

CCharString* CDefStringTable::GetString(CCharString* result, int table_pos)
{
    return OGetString(this, result, table_pos);
}

void CDefStringTable::Hook()
{
    ADD_HOOK(0x009D49B0, HGetString, OGetString);
}
