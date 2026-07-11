#pragma once

#include <functional>

// Live combat/action tracer. Hooks CThingCreatureBase::DoCreatureAction
// (0x6644F0) — the single entry point every creature action flows through
// (wield, sheathe, attack, roll, block, cast, ...) — and logs each action's
// RTTI class name plus the acting creature. Playing the game with this on
// reveals the exact action classes and functions behind every combat move,
// which is how weapon/combat sync is reverse-engineered without a debugger.
//
// Enable via [general] debug_keys=1 in EgoMP.ini. Output goes to the console
// and EgoMP-inspect.log.
namespace ActionTracer
{
    // Installs the DoCreatureAction hook. Safe to call once (no-op after).
    // Combat sync needs the hook regardless of debug logging, so this is
    // called unconditionally at mod setup.
    void Install();

    // Toggle debug logging at runtime (NUMPAD0) so the log isn't flooded
    // until you are ready to capture a specific move.
    void SetEnabled(bool enabled);
    bool IsEnabled();

    // Observer invoked (on the game thread) for every creature action:
    // (creature, action, demangled action class). Used by combat sync to
    // detect and broadcast the local hero's actions. Fires whether or not
    // debug logging is enabled.
    using ActionObserver = std::function<void(void* creature, void* action, const char* actionClass)>;
    void SetObserver(ActionObserver observer);
}
