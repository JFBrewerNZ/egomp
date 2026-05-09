#include "CharString.h"

void(__thiscall* CCharString::OCCharString)(CCharString*, const char*, int) = nullptr;
void __fastcall CCharString::HCCharString(CCharString* _this, void* _EDX, const char* string, int no_chars)
{
    OCCharString(_this, string, no_chars);
}

CCharString::CCharString(const char* string, int no_chars)
{
    OCCharString(this, string, no_chars);
}

char* (__thiscall* CCharString::OGetAsCharArray)(CCharString*) = nullptr;
char* __fastcall CCharString::HGetAsCharArray(CCharString* _this, void* _EDX)
{
    return OGetAsCharArray(_this);
}

const char* CCharString::GetAsCharArray()
{
    return OGetAsCharArray(this);
}

void CCharString::Hook()
{
    ADD_HOOK(0x0099EBF0, HCCharString, OCCharString);
    ADD_HOOK(0x00403A10, HGetAsCharArray, OGetAsCharArray);
}
