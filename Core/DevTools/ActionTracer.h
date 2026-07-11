#pragma once

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
    void Install();

    // Toggle logging at runtime (NUMPAD0) so the log isn't flooded until you
    // are ready to capture a specific move.
    void SetEnabled(bool enabled);
    bool IsEnabled();
}
