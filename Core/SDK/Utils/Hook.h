#pragma once

#include <MinHook/include/MinHook.h>

#define INIT_HOOKS()                                                           \
    if (MH_Initialize() != MH_OK) {                                            \
        MessageBoxW(NULL, L"[MH_Initialize] Failed.", L"Error", MB_ICONERROR); \
        return;                                                                \
    }

#define ADD_HOOK(address, hookFunc, originalFuncPtr)                              \
    if (MH_CreateHook(reinterpret_cast<void*>(address),                           \
                      reinterpret_cast<void*>(hookFunc),                          \
                      reinterpret_cast<void**>(&originalFuncPtr)) != MH_OK) {     \
        MessageBoxW(NULL, L"MH_CreateHook failed: " L#hookFunc, L"Error", MB_OK); \
        return;                                                                   \
    }                                                                             \
    if (MH_EnableHook(reinterpret_cast<void*>(address)) != MH_OK) {               \
        MessageBoxW(NULL, L"MH_EnableHook failed: " L#hookFunc, L"Error", MB_OK); \
        return;                                                                   \
    }
