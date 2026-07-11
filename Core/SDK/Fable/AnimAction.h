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
// Live capture (2026-07-11): NPC ambient PlayAnimation actions carry an
// EMPTY name key (key dword0 = 0) — they pick the animation via a selector
// resolved against the creature's anim-set map (0x662FA0 -> +0x74 context)
// plus the small d24 enum. Named keys are used by scripted/spell anims. The
// name node (when present) is a string object: {char* chars, int len, ...}
// (setters 0x9A0590/0x9A0300), so [handle+0] is the name.
struct AnimActionFields
{
    static const size_t NAME_MAX = 96;

    char name[NAME_MAX] = "";     // recovered anim name; "" = nameless anim
    unsigned int keyExtra = 0;    // action+0x38 (second anim-key dword)
    unsigned int d20 = 0;         // action+0x20
    unsigned int d24 = 0;         // action+0x24 (anim variant enum?)
    int loops = 0;                // action+0xAC
    unsigned char a8 = 0, a9 = 0, aa = 0, ab = 0, b0 = 0;

    // Machine-local pointers captured for diagnostics / local replay only —
    // never sent over the wire.
    void* localContext = nullptr; // action+0x74
    void* localKey0 = nullptr;    // action+0x34 raw (name-node ptr or null)
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
    // from the name-handle when one is present (name stays "" for nameless
    // anims). SEH-guarded; false only if the action memory is unreadable.
    bool Extract(void* action, AnimActionFields& out);

    // Posts a stack-constructed CCreatureAction_PlayAnimation on the
    // creature via DoCreatureAction, replaying the captured fields. A named
    // capture gets its key rebuilt from the name (0x99EBF0); a nameless one
    // is replayed with an empty key, exactly as the game constructed it.
    // `context` is the +0x74 context object (pass nullptr when replaying a
    // remote player's action — their pointer is meaningless here).
    // SEH-guarded; false on failure.
    bool Play(CThingPlayerCreature* creature, const AnimActionFields& fields, void* context);
}
