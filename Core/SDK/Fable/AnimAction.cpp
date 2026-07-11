#include "AnimAction.h"

#include <cstring>
#include <string>
#include <unordered_map>
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

    // Anim-context resolver: __thiscall(creature, animKey*, flag) — looks
    // the key up in the creature's anim-set map and returns a fresh
    // playback context (the object PlayAnimation stores at +0x74).
    const uintptr_t FN_RESOLVE_ANIM_CONTEXT = 0x662FA0;

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

        if (IsReadable(out.localContext, 8))
        {
            out.ctxId0 = ((const unsigned int*)out.localContext)[0];
            out.ctxId1 = ((const unsigned int*)out.localContext)[1];
            AnimAction::LookupContextName(out.ctxId0, out.ctxId1,
                out.ctxName, sizeof(out.ctxName), &out.ctxFlag);
        }
        out.a8 = *(const unsigned char*)(base + OFF_A8 + 0);
        out.a9 = *(const unsigned char*)(base + OFF_A8 + 1);
        out.aa = *(const unsigned char*)(base + OFF_A8 + 2);
        out.ab = *(const unsigned char*)(base + OFF_A8 + 3);
        out.b0 = *(const unsigned char*)(base + OFF_B0);
        out.loops = *(const int*)(base + OFF_LOOPS);

        // Name-handle node (0x99EA60/0x9A0300): {char* chars, int len, ...}.
        // A null handle is normal — nameless anims (NPC ambients) select
        // their animation via the resolved +0x74 context and d24 instead.
        const char* handle = *(const char* const*)(base + OFF_ANIM_KEY);
        out.localKey0 = (void*)handle;

        if (IsReadable(handle, 8))
        {
            const char* chars = *(const char* const*)handle;
            CopyPlausibleName(chars, out.name, AnimActionFields::NAME_MAX);
        }

        return true;
    }

    bool PlayImpl(CThingPlayerCreature* creature, const AnimActionFields& fields, void* context)
    {
        // 8-byte anim key. Nameless captures replay with an empty key —
        // exactly what the game passed. Named ones rebuild the key node
        // from the name; that node is deliberately not released afterwards:
        // the action copies the key dwords without an addref, so releasing
        // here could free the node under the action. One small node per
        // named play is an acceptable leak until the ownership rule is
        // confirmed.
        unsigned int animKey[2] = { 0, fields.keyExtra };
        if (fields.name[0])
        {
            ((void*(__thiscall*)(void*, const char*, int))FN_ANIM_KEY_CTOR)(
                animKey, fields.name, -1);

            if (!animKey[0])
                return false;
        }

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
        if (!creature)
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

    struct ContextNameEntry
    {
        char name[AnimActionFields::NAME_MAX];
        int flag;
    };

    // Anim id -> name, learned at the resolver hook. Keyed by the context's
    // SECOND dword only: live capture showed dword0 is mutable state (the
    // hero's ST_IDLE_SUBTLE context read 0xBE1, then 0xB72, then 0xACB)
    // while dword1 is the stable anim identity (ST_IDLE_SUBTLE always
    // 0x49C95, ST_BLINK 0x55615, ...).
    static std::unordered_map<unsigned int, ContextNameEntry> g_contextNames;

    // SEH portion of NoteResolvedContext (no unwindable locals allowed
    // here). selectorKey's dword0 points at the name — live capture showed
    // per-call transient heap copies whose contents compare equal (the map
    // hashes the key value, 0x5DC020, and compares via a polymorphic
    // comparator), so try the chars directly first, then one indirection
    // deeper (the 0x99EBF0 node layout: node+0 = char*).
    static bool ReadResolvedInfo(void* context, void* selectorKey,
        unsigned int* id0, unsigned int* id1, char* name, size_t nameSize)
    {
        __try
        {
            if (!IsReadable(context, 8) || !IsReadable(selectorKey, 4))
                return false;

            *id0 = ((const unsigned int*)context)[0];
            *id1 = ((const unsigned int*)context)[1];
            if (*id0 == 0 && *id1 == 0)
                return false;

            const char* node = *(const char* const*)selectorKey;
            if (!IsReadable(node, 8))
                return false;

            if (CopyPlausibleName(node, name, nameSize))
                return true;

            return CopyPlausibleName(*(const char* const*)node, name, nameSize);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool NoteResolvedContext(void* context, void* selectorKey, int flag,
        char* nameOut, size_t nameOutSize)
    {
        unsigned int id0 = 0, id1 = 0;
        char name[AnimActionFields::NAME_MAX] = "";

        if (!ReadResolvedInfo(context, selectorKey, &id0, &id1, name, sizeof(name)))
            return false;

        ContextNameEntry& entry = g_contextNames[id1];
        strcpy_s(entry.name, name);
        entry.flag = flag;

        if (nameOut)
            strcpy_s(nameOut, nameOutSize, name);
        return true;
    }

    bool LookupContextName(unsigned int id0, unsigned int id1,
        char* nameOut, size_t nameOutSize, int* flagOut)
    {
        (void)id0; // mutable state, not identity — see the map comment
        auto it = g_contextNames.find(id1);
        if (it == g_contextNames.end())
            return false;

        strcpy_s(nameOut, nameOutSize, it->second.name);
        if (flagOut)
            *flagOut = it->second.flag;
        return true;
    }

    static void* ResolveContextImpl(CThingPlayerCreature* creature, const char* name, int flag)
    {
        unsigned int animKey[2] = { 0, 0 };
        ((void*(__thiscall*)(void*, const char*, int))FN_ANIM_KEY_CTOR)(
            animKey, name, -1);

        if (!animKey[0])
            return nullptr;

        // Key node deliberately not released — same ownership caveat as in
        // PlayImpl.
        return ((void*(__thiscall*)(void*, void*, int))FN_RESOLVE_ANIM_CONTEXT)(
            creature, animKey, flag);
    }

    void* ResolveContext(CThingPlayerCreature* creature, const char* name, int flag)
    {
        if (!creature || !name || !name[0])
            return nullptr;

        __try
        {
            return ResolveContextImpl(creature, name, flag);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }
}
