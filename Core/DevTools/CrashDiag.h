#pragma once

// Crash diagnostics for the "second client dies on world load" family of
// crashes (WER shows an unhandled MSVC C++ exception, 0xE06D7363, plus a /GS
// fastfail, but keeps no dump - so the thrown TYPE and the file being touched
// were never captured).
//
// What it records, all to EgoMP-crash-<pid>.log next to the DLL:
//  - every C++ throw (first chance, before any unwinding): the exception
//    class name(s) recovered from the MSVC ThrowInfo RTTI, plus a stack trace
//    with module+offset per frame;
//  - the last 64 CreateFileA/W calls (path, access, share mode, result),
//    dumped alongside each throw - if a cross-client sharing collision is the
//    trigger, the failing open is right there;
//  - CreateFile failures with ERROR_SHARING_VIOLATION (or any failed write
//    open) the moment they happen, even without a crash;
//  - a one-shot minidump (EgoMP-crash-<pid>.dmp) at the first C++ throw and
//    at the unhandled-exception filter, whichever comes first.
//
// Logging is passive: the handler always returns "continue search", so game
// behaviour is unchanged.
namespace CrashDiag
{
    // Install after MinHook is initialised (Multiplayer::GetInstance()).
    // Gated on [general] crash_diag in EgoMP.ini (default on).
    void Install();
}
