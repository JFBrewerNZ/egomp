#include "ClientSlot.h"

#include <windows.h>
#include <cstdio>

namespace
{
    constexpr int kMaxClients = 64;

    int  cachedNumber = -1;     // -1 = not claimed yet
    HANDLE slotHandle = nullptr; // held for process lifetime to reserve the slot

    // Grab the first per-slot mutex that no other running client holds.
    // CreateMutexW is atomic, so simultaneous launches resolve cleanly.
    int Claim()
    {
        for (int n = 1; n <= kMaxClients; ++n)
        {
            wchar_t name[64];
            swprintf_s(name, L"EgoMP-Client-Slot-%d", n);
            HANDLE h = CreateMutexW(nullptr, FALSE, name);
            if (!h)
                continue;
            if (GetLastError() == ERROR_ALREADY_EXISTS)
            {
                CloseHandle(h); // held by another client; try the next number
                continue;
            }
            slotHandle = h; // keep open -> hold this slot until we exit
            return n;
        }
        return 0;
    }
}

namespace ClientSlot
{
    int Number()
    {
        if (cachedNumber < 0)
            cachedNumber = Claim();
        return cachedNumber;
    }
}
