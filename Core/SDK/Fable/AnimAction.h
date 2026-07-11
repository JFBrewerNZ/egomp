#pragma once

#include "ThingPlayerCreature.h"

// CCreatureAction_PlayAnimation — the generic "play this animation" creature
// action (vtable 0x1273A0C). NPCs are driven by it constantly, which makes it
// the universal way to mirror the local hero's moves on remote puppets
// without reconstructing each combat action's machine-specific arguments.
//
// Layout (base ctor 0x693B30, full derived ctor 0x8425E0, repost template
// in vfunc 0x843B40 — see Tools/RE-NOTES.md "Animation sync RE"):
//   +0x08 CIntelligentPointer<creature>
//   +0x20/+0x24 two caller dwords (base ctor args 6/7)
//   +0x34 anim-name key: dword0 = refcounted name-handle built by
//         0x99EBF0(name, -1) -> 0x99EA60; dword1 = second key dword
//   +0x74 optional context thing (base ctor arg 4, via 0x693030)
//   +0xA8..+0xAB, +0xB0 behaviour flag bytes
//   +0xAC loop count (-1 = infinite, 0 = once)
struct AnimActionFields
{
    static const size_t NAME_MAX = 96;

    char name[NAME_MAX] = "";     // recovered anim name; "" = none
    unsigned int keyExtra = 0;    // action+0x38 (second anim-key dword)
    unsigned int d20 = 0;         // action+0x20
    unsigned int d24 = 0;         // action+0x24
    int loops = 0;                // action+0xAC
    unsigned char a8 = 0, a9 = 0, aa = 0, ab = 0, b0 = 0;

    // Machine-local pointers captured for diagnostics / local replay only —
    // never sent over the wire.
    void* localContext = nullptr; // action+0x74
};

namespace AnimAction
{
    // True for demangled action classes that share the 0x693B30 base layout
    // (anim key at +0x34): PlayAnimation, PlayAnimationWithLookTurning,
    // PlayIntoLoopOutOfAnimation, PlayConversationAnimation,
    // PlayAnimationFromIndex. PlayCombatAnimation uses a DIFFERENT base
    // (0x857F30) and is excluded.
    bool ClassHasAnimLayout(const char* actionClass);

    // Reads the fields out of a live action object, recovering the anim name
    // from the name-handle. SEH-guarded; false if the name can't be
    // recovered (e.g. index-only PlayAnimationFromIndex actions).
    bool Extract(void* action, AnimActionFields& out);

    // Rebuilds the anim-name key (0x99EBF0) and posts a stack-constructed
    // CCreatureAction_PlayAnimation on the creature via DoCreatureAction.
    // `context` is the +0x74 context thing (pass nullptr when replaying a
    // remote player's action — their pointer is meaningless here).
    // SEH-guarded; false on failure.
    bool Play(CThingPlayerCreature* creature, const AnimActionFields& fields, void* context);
}
