#pragma once

// This client's number (1-based) among the EgoMP clients running on this
// machine. The first running client is 1, the next 2, and so on; closing a
// client frees its number for the next launch to reuse.
namespace ClientSlot
{
    // Claimed on first call and held for the life of the process. Returns 0 if
    // no slot could be claimed. Safe to call from anywhere after attach.
    int Number();
}
