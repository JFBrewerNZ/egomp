#pragma once

#include <iostream>

#include "../Utils/Hook.h"

class CCharString
{
public:
    char pad[0x04];

    CCharString(const char* string, int no_chars = -1);
    const char* GetAsCharArray();

    static void Hook();

private:
    static void(__thiscall* OCCharString)(CCharString*, const char*, int);
    static void __fastcall HCCharString(CCharString* _this, void* _EDX, const char* string, int no_chars);

    static char* (__thiscall* OGetAsCharArray)(CCharString*);
    static char* __fastcall HGetAsCharArray(CCharString* _this, void* _EDX);
};
