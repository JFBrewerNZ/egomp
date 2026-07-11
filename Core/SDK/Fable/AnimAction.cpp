#include "AnimAction.h"

#include <cstring>
#include <windows.h>

namespace
{
    // Anim-name key: ctor 0x99EBF0(this, name, index) allocates a refcounted
    // 0x11-byte node via 0x99EA60 and stores it in dword0. The key object the
    // PlayAnimation ctor copies is 8 bytes.
    const uintptr_t FN_ANIM_KEY_CTOR = 0x99EBF0;

    // CCreatureAction_PlayAnimation full ctor (see repost vfunc 0x843B40):
    // __thiscall(buffer, creature, context, bAA, bAB, loops, animKey*,
    //            bA8, bA9, bB0, d20, d24), returns the action.
    const uintptr_t FN_PLAY_ANIM_CTOR = 0x8425E0;

    const uintptr_t FN_DO_CREATURE_ACTION = 0x6644F0;
    const uintptr_t FN_ACTION_DTOR = 0x693EF0;

    const size_t OFF_D20 = 0x20;
    const size_t OFF_D24 = 0x24;
    const size_t OFF_ANIM_KEY = 0x34;
    const size_t OFF_CONTEXT = 0x74;
    const size_t OFF_A8 = 0xA8;
    const size_t OFF_LOOPS = 0xAC;
    const size_t OFF_B0 = 0xB0;

    bool IsReadable(const void* p, size_t bytes)
    {
        if (!p)
            return false;
        MEMORY_BASIC_INFORMATION mbi = {};
        if (!VirtualQuery(p, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT)
            return false;
        if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))
            return false;
        return (uintptr_t)p + bytes <= (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
    }

    // A plausible anim name: printable, sane length. Names look like
    // "ANIM_HERO_MELEE_..." but NPC sets vary, so only structural checks.
    bool CopyPlausibleName(const char* candidate, char* out, size_t outSize)
    {
        if (!IsReadable(candidate, 2))
            return false;

        size_t i = 0;
        while (i + 1 < outSize)
        {
            if (!IsReadable(candidate + i, 1))
                return false;
            char c = candidate[i];
            if (c == '\0')
                break;
            if (c < 0x21 || c > 0x7E)
                return false;
            out[i++] = c;
        }

        if (i < 2 || i + 1 >= outSize)
            return false;

        out[i] = '\0';
        return true;
    }

    bool ExtractImpl(void* action, AnimActionFields& out)
    {
        const char* base = (const char*)action;

        if (!IsReadable(base, OFF_B0 + 1))
            return false;

        out.d20 = *(const unsigned int*)(base + OFF_D20);
        out.d24 = *(const unsigned int*)(base + OFF_D24);
        out.keyExtra = *(const unsigned int*)(base + OFF_ANIM_KEY + 4);
        out.localContext = *(void* const*)(base + OFF_CONTEXT);
        out.a8 = *(const unsigned char*)(base + OFF_A8 + 0);
        out.a9 = *(const unsigned char*)(base + OFF_A8 + 1);
        out.aa = *(const unsigned char*)(base + OFF_A8 + 2);
        out.ab = *(const unsigned char*)(base + OFF_A8 + 3);
        out.b0 = *(const unsigned char*)(base + OFF_B0);
        out.loops = *(const int*)(base + OFF_LOOPS);

        // Name-handle node (0x99EA60): dword0 is expected to be the char*
        // of the copied name; fall back to reading the node as inline chars
        // in case the layout is the other way round. The tracer logs which
        // interpretation produced the name.
        const char* handle = *(const char* const*)(base + OFF_ANIM_KEY);
        if (!IsReadable(handle, 8))
            return false;

        const char* indirect = *(const char* const*)handle;
        if (CopyPlausibleName(indirect, out.name, AnimActionFields::NAME_MAX))
            return true;

        return CopyPlausibleName(handle, out.name, AnimActionFields::NAME_MAX);
    }

    bool PlayImpl(CThingPlayerCreature* creature, const AnimActionFields& fields, void* context)
    {
        // 8-byte anim key rebuilt from the name. The key node is
        // deliberately not released afterwards: the action copies the key
        // dwords without an addref, so releasing here could free the node
        // under the action. One 0x11-byte node per play is an acceptable
        // leak until the ownership rule is confirmed.
        unsigned int animKey[2] = { 0, fields.keyExtra };
        ((void*(__thiscall*)(void*, const char*, int))FN_ANIM_KEY_CTOR)(
            animKey, fields.name, -1);

        if (!animKey[0])
            return false;

        // Real call sites construct the action in a ~0xB8 stack buffer.
        char actionBuffer[0x140] = {};
        void* action = ((void*(__thiscall*)(void*, void*, void*, int, int, int, void*,
            int, int, int, unsigned int, unsigned int))FN_PLAY_ANIM_CTOR)(
            actionBuffer, creature, context,
            fields.aa, fields.ab, fields.loops, animKey,
            fields.a8, fields.a9, fields.b0, fields.d20, fields.d24);

        if (!action)
            action = actionBuffer;

        ((int(__thiscall*)(void*, void*))FN_DO_CREATURE_ACTION)(creature, action);
        ((void(__thiscall*)(void*))FN_ACTION_DTOR)(actionBuffer);
        return true;
    }
}

namespace AnimAction
{
    bool ClassHasAnimLayout(const char* actionClass)
    {
        if (!actionClass)
            return false;

        // Shares the 0x693B30 base. "PlayCombatAnimation" does not contain
        // the substring "PlayAnimation", so the different-layout combat
        // variant is excluded without an explicit check.
        return std::strstr(actionClass, "PlayAnimation") != nullptr
            || std::strstr(actionClass, "PlayIntoLoopOutOfAnimation") != nullptr
            || std::strstr(actionClass, "PlayConversationAnimation") != nullptr;
    }

    bool Extract(void* action, AnimActionFields& out)
    {
        if (!action)
            return false;

        __try
        {
            return ExtractImpl(action, out);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool Play(CThingPlayerCreature* creature, const AnimActionFields& fields, void* context)
    {
        if (!creature || !fields.name[0])
            return false;

        __try
        {
            return PlayImpl(creature, fields, context);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }
}
