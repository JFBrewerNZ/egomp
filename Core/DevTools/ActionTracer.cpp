#include "ActionTracer.h"
#include "ObjectInspector.h"

#include <windows.h>
#include <MinHook/include/MinHook.h>

#include <string>
#include <set>

namespace
{
    const uintptr_t FN_DO_CREATURE_ACTION = 0x6644F0;

    // CCreatureAction_PlayAnimation resolving ctor (121 callers — the main
    // "play animation by selector" API): __thiscall(buffer, creature,
    // selector, animKey*, a4..a8); resolves `selector` against the
    // creature's anim-set map (0x662FA0) into the action's +0x74 context.
    // Hooked to learn what the selector IS (int enum / string / def ptr) —
    // the portable animation identity for nameless anims.
    const uintptr_t FN_PLAY_ANIM_CTOR_RESOLVING = 0x8424F0;

    bool g_installed = false;
    bool g_enabled = false;
    ActionTracer::ActionObserver g_observer;

    // Suppress consecutive duplicates so a held-down action (e.g. running)
    // doesn't flood the log.
    std::string g_lastLine;
    std::string g_lastAnimLine;

    // Each hero action class is structure-dumped once per session so the
    // parameter layout (weapon refs, directions, anim ids) is captured
    // without flooding the log.
    std::set<std::string> g_dumpedClasses;

    // PlayAnimation-family actions get a bigger dump (their fields run past
    // +0x100) and are dumped for ANY creature — the hero may never emit one
    // himself, and PlayCombatAnimation's unknown layout matters most.
    std::set<std::string> g_dumpedAnimClasses;

    // Most recent PlayAnimation-family capture for the NUMPAD9 replay test.
    AnimActionFields g_lastAnim;
    bool g_hasLastAnim = false;

    // __thiscall(creature, action*) — trampoline uses the __fastcall(ecx,edx)
    // ABI to receive `this` in ecx and the action pointer as the stack arg.
    int(__fastcall* ODoCreatureAction)(void* creature, void* edx, void* action) = nullptr;

    void*(__fastcall* OPlayAnimCtorResolving)(void* self, void* edx,
        void* creature, void* selector, void* animKey,
        unsigned int a4, unsigned int a5, unsigned int a6,
        unsigned int a7, unsigned int a8) = nullptr;

    std::string g_lastCtorLine;

    void Demangle(const char* rtti, char* out, size_t outSize)
    {
        // ".?AVCCreatureAction_UnsheatheItemFromInventory@@" -> the class name
        const char* p = rtti;
        if (p[0] == '.' && p[1] == '?' && p[2] == 'A' && (p[3] == 'V' || p[3] == 'U'))
            p += 4;
        size_t i = 0;
        while (p[i] && p[i] != '@' && i + 1 < outSize)
        {
            out[i] = p[i];
            i++;
        }
        out[i] = '\0';
    }

    bool IsAnimFamilyClass(const char* actionName)
    {
        return AnimAction::ClassHasAnimLayout(actionName)
            || strstr(actionName, "PlayCombatAnimation") != nullptr;
    }

    // Best-effort description of an unknown pointer-sized value: literal
    // int, RTTI class, direct string, or string-node {char*, len}.
    // SEH-guarded internally (no unwindable locals here).
    void DescribeValueImpl(void* value, char* out, size_t outSize)
    {
        if ((uintptr_t)value < 0x10000)
        {
            sprintf_s(out, outSize, "int:%d", (int)(intptr_t)value);
            return;
        }

        if (const char* rtti = ObjectInspector::GetRttiName(value))
        {
            sprintf_s(out, outSize, "obj:%s", rtti);
            return;
        }

        if (ObjectInspector::IsReadableMemory(value, 8))
        {
            const char* direct = (const char*)value;
            bool plausible = true;
            for (int i = 0; i < 4 && plausible; i++)
                plausible = direct[i] >= 0x20 && direct[i] <= 0x7E;
            if (plausible)
            {
                sprintf_s(out, outSize, "str:%.48s", direct);
                return;
            }

            const char* indirect = *(const char* const*)value;
            if (ObjectInspector::IsReadableMemory(indirect, 8))
            {
                plausible = true;
                for (int i = 0; i < 4 && plausible; i++)
                    plausible = indirect[i] >= 0x20 && indirect[i] <= 0x7E;
                if (plausible)
                {
                    sprintf_s(out, outSize, "node-str:%.48s", indirect);
                    return;
                }
            }

            sprintf_s(out, outSize, "ptr:[%08X %08X]",
                ((const unsigned int*)value)[0], ((const unsigned int*)value)[1]);
            return;
        }

        sprintf_s(out, outSize, "%p", value);
    }

    void DescribeValue(void* value, char* out, size_t outSize)
    {
        __try
        {
            DescribeValueImpl(value, out, outSize);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            sprintf_s(out, outSize, "%p(!)", value);
        }
    }

    void TraceAnimAction(void* creature, void* action,
        const char* actionName, const char* creatureName)
    {
        AnimActionFields fields;
        bool extracted = AnimAction::Extract(action, fields);

        if (extracted)
        {
            g_lastAnim = fields;
            g_hasLastAnim = true;
        }

        char ctxDesc[96] = "null";
        if (fields.localContext)
            DescribeValue(fields.localContext, ctxDesc, sizeof(ctxDesc));

        char line[480];
        sprintf_s(line,
            "[AnimAction] %-38s on %-22s name=%s key0=%p d20=%08X d24=%08X keyX=%08X "
            "loops=%d a8=%u a9=%u aa=%u ab=%u b0=%u ctx=%p(%s)",
            actionName, creatureName,
            fields.name[0] ? fields.name : "<none>",
            fields.localKey0,
            fields.d20, fields.d24, fields.keyExtra, fields.loops,
            fields.a8, fields.a9, fields.aa, fields.ab, fields.b0,
            fields.localContext, ctxDesc);

        if (g_lastAnimLine != line)
        {
            g_lastAnimLine = line;
            ObjectInspector::LogLine(line);
        }

        // One raw dump per class per session — from any creature — so
        // unknown layouts (PlayCombatAnimation) can be mapped offline.
        if (g_dumpedAnimClasses.insert(actionName).second)
        {
            char label[160];
            sprintf_s(label, "anim action params: %s", actionName);
            ObjectInspector::Dump(label, action, 0x140);
        }
    }

    void* __fastcall HPlayAnimCtorResolving(void* self, void* edx,
        void* creature, void* selector, void* animKey,
        unsigned int a4, unsigned int a5, unsigned int a6,
        unsigned int a7, unsigned int a8)
    {
        if (g_enabled)
        {
            char creatureName[128] = "?";
            const char* creatureRtti = ObjectInspector::GetRttiName(creature);
            if (creatureRtti) Demangle(creatureRtti, creatureName, sizeof(creatureName));

            char selDesc[96];
            DescribeValue(selector, selDesc, sizeof(selDesc));
            char keyDesc[96];
            DescribeValue(animKey, keyDesc, sizeof(keyDesc));

            char line[420];
            sprintf_s(line,
                "[AnimCtor] creature=%-22s selector=%p(%s) key=%p(%s) "
                "a4=%08X a5=%08X a6=%08X a7=%08X a8=%08X",
                creatureName, selector, selDesc, animKey, keyDesc,
                a4, a5, a6, a7, a8);

            if (g_lastCtorLine != line)
            {
                g_lastCtorLine = line;
                ObjectInspector::LogLine(line);
            }
        }

        return OPlayAnimCtorResolving(self, edx, creature, selector, animKey,
            a4, a5, a6, a7, a8);
    }

    int __fastcall HDoCreatureAction(void* creature, void* edx, void* action)
    {
        const char* actionRtti = ObjectInspector::GetRttiName(action);
        char actionName[128] = "?";
        if (actionRtti) Demangle(actionRtti, actionName, sizeof(actionName));

        // Combat sync observer runs regardless of debug logging.
        if (g_observer && actionRtti)
            g_observer(creature, action, actionName);

        if (g_enabled)
        {
            const char* creatureRtti = ObjectInspector::GetRttiName(creature);
            char creatureName[128] = "?";
            if (creatureRtti) Demangle(creatureRtti, creatureName, sizeof(creatureName));

            if (actionRtti && IsAnimFamilyClass(actionName))
            {
                TraceAnimAction(creature, action, actionName, creatureName);
            }
            else
            {
                char line[320];
                sprintf_s(line, "[Action] %-40s on %-24s (action=%p)",
                    actionName, creatureName, action);

                if (g_lastLine != line)
                {
                    g_lastLine = line;
                    ObjectInspector::LogLine(line);
                }
            }

            // First time we see each action class on the player hero, dump
            // its structure so its parameters can be mapped for replication.
            if (creatureRtti && strstr(creatureRtti, "PlayerCreature")
                && actionRtti && g_dumpedClasses.insert(actionName).second)
            {
                char label[160];
                sprintf_s(label, "action params: %s", actionName);
                ObjectInspector::Dump(label, action, 0x60);
            }
        }

        return ODoCreatureAction(creature, edx, action);
    }
}

namespace ActionTracer
{
    void Install()
    {
        if (g_installed)
            return;

        if (MH_CreateHook(reinterpret_cast<void*>(FN_DO_CREATURE_ACTION),
                reinterpret_cast<void*>(&HDoCreatureAction),
                reinterpret_cast<void**>(&ODoCreatureAction)) == MH_OK
            && MH_EnableHook(reinterpret_cast<void*>(FN_DO_CREATURE_ACTION)) == MH_OK)
        {
            g_installed = true;
        }

        // Diagnostic-only hook; combat sync doesn't depend on it, so a
        // failure here is silently tolerated.
        if (MH_CreateHook(reinterpret_cast<void*>(FN_PLAY_ANIM_CTOR_RESOLVING),
                reinterpret_cast<void*>(&HPlayAnimCtorResolving),
                reinterpret_cast<void**>(&OPlayAnimCtorResolving)) == MH_OK)
        {
            MH_EnableHook(reinterpret_cast<void*>(FN_PLAY_ANIM_CTOR_RESOLVING));
        }
    }

    void SetEnabled(bool enabled)
    {
        g_enabled = enabled;
        g_lastLine.clear();
        g_lastAnimLine.clear();
        g_lastCtorLine.clear();
    }

    bool IsEnabled()
    {
        return g_enabled;
    }

    void SetObserver(ActionObserver observer)
    {
        g_observer = std::move(observer);
    }

    bool GetLastAnimCapture(AnimActionFields& out)
    {
        if (!g_hasLastAnim)
            return false;
        out = g_lastAnim;
        return true;
    }
}
