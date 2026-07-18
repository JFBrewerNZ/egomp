#include "CpuAffinity.h"

#include "../Config/Config.h"
#include "ClientSlot.h"

#include <windows.h>
#include <iostream>

namespace
{
    void Log(const std::string& msg)
    {
        std::cout << "[EgoMP][affinity] " << msg << std::endl;
    }
}

namespace CpuAffinity
{
    void Install()
    {
        if (!Config::Get().cpuAffinity)
            return;

        // How many cores the process is allowed to use.
        DWORD_PTR processMask = 0, systemMask = 0;
        if (!GetProcessAffinityMask(GetCurrentProcess(), &processMask, &systemMask)
            || processMask == 0)
        {
            Log("could not read the process affinity mask; leaving it alone");
            return;
        }

        // Count usable cores and index them, so we pick a real allowed core
        // rather than assuming a contiguous 0..N-1 layout.
        int cores[64];
        int coreCount = 0;
        for (int bit = 0; bit < 64 && coreCount < 64; ++bit)
        {
            if (processMask & (static_cast<DWORD_PTR>(1) << bit))
                cores[coreCount++] = bit;
        }
        if (coreCount <= 1)
        {
            Log("only one core available; nothing to pin");
            return;
        }

        // Choose a core by client slot, skipping the first usable core (the
        // busiest). Client 1 -> cores[1], client 2 -> cores[2], wrapping if
        // there are more clients than cores. Slot 0 (no slot) -> cores[1] too.
        const int slot = ClientSlot::Number();
        const int base = 1; // skip cores[0]
        int idx = base + ((slot > 0 ? slot - 1 : 0) % (coreCount - base));
        const DWORD_PTR mask = static_cast<DWORD_PTR>(1) << cores[idx];

        if (SetProcessAffinityMask(GetCurrentProcess(), mask))
        {
            char b[96];
            sprintf_s(b, "client %d pinned to core %d (mask 0x%llX), %d cores available",
                slot, cores[idx], (unsigned long long)mask, coreCount);
            Log(b);
        }
        else
        {
            Log("SetProcessAffinityMask failed; affinity unchanged");
        }
    }
}
