#pragma once

#include <functional>

#include "../SDK/Fable/AnimAction.h"

// Live combat/action tracer. Hooks CThingCreatureBase::DoCreatureAction
// (0x6644F0) — the single entry point every creature action flows through
// (wield, sheathe, attack, roll, block, cast, ...) — and logs each action's
// RTTI class name plus the acting creature. Playing the game with this on
// reveals the exact action classes and functions behind every combat move,
// which is how weapon/combat sync is reverse-engineered without a debugger.
//
// PlayAnimation-family actions additionally get their fields extracted and
// logged (anim name, loop count, flag bytes), and the most recent one is
// kept for the NUMPAD9 local replay test.
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

    // Observer for anim-context resolutions (hook on 0x662FA0): fires with
    // the creature, the resolved anim NAME, and the resolver flag. This is
    // the combat-anim funnel — unsheathe/sheathe/bow-load/fire resolve here
    // (flag=1) on the acting creature right before their actions run.
    using ResolveObserver = std::function<void(void* creature, const char* animName, int flag)>;
    void SetResolveObserver(ResolveObserver observer);

    // Last PlayAnimation-family action captured while logging was enabled
    // (from any creature, NPCs included). False if none captured yet.
    bool GetLastAnimCapture(AnimActionFields& out);

    // Last anim resolved on a player creature (ST_BLINK excluded — it fires
    // constantly and is never what a replay test wants). For NUMPAD9.
    bool GetLastHeroAnimResolve(char* nameOut, size_t nameOutSize, int* flagOut);
}
