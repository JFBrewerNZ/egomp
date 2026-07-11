#include "ActionTracer.h"
#include "ObjectInspector.h"

#include <windows.h>
#include <MinHook/include/MinHook.h>

#include <string>
#include <set>

namespace
{
    const uintptr_t FN_DO_CREATURE_ACTION = 0x6644F0;

    // CCreatureAction_PlayAnimation resolving ctors: both resolve a
    // `selector` against the creature's anim-set map (0x662FA0) into the
    // action's +0x74 context. Hooked to learn what the selector IS (int
    // enum / string / def ptr) — the portable animation identity for
    // nameless anims. 0x8424F0 = 8-arg variant (121 callers), 0x842660 =
    // 11-arg variant (the first capture session saw actions but no
    // [AnimCtor] lines, so the ambients likely come through this one).
    const uintptr_t FN_PLAY_ANIM_CTOR_RESOLVING = 0x8424F0;
    const uintptr_t FN_PLAY_ANIM_CTOR_RESOLVING11 = 0x842660;

    // The anim-context resolver itself: __thiscall(creature, animKey*,
    // flag) -> fresh playback context. Hooked ALWAYS (not just while
    // logging): it is where context ids get associated with anim names —
    // the wire currency of animation sync.
    const uintptr_t FN_RESOLVE_ANIM_CONTEXT = 0x662FA0;

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

    void*(__fastcall* OPlayAnimCtorResolving11)(void* self, void* edx,
        void* creature, void* selector, void* animKey,
        unsigned int a4, unsigned int a5, unsigned int a6,
        unsigned int a7, unsigned int a8,
        unsigned int a9, unsigned int a10, unsigned int a11) = nullptr;

    void*(__fastcall* OResolveAnimContext)(void* creature, void* edx,
        void* animKey, int flag) = nullptr;

    std::string g_lastCtorLine;
    std::string g_lastResolveLine;

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

    void TraceAnimAction(const AnimActionFields& fields,
        const char* actionName, const char* creatureName)
    {
        char ctxDesc[96] = "null";
        if (fields.localContext)
            DescribeValue(fields.localContext, ctxDesc, sizeof(ctxDesc));

        char line[560];
        sprintf_s(line,
            "[AnimAction] %-38s on %-22s name=%s ctxName=%s key0=%p d20=%08X d24=%08X keyX=%08X "
            "loops=%d a8=%u a9=%u aa=%u ab=%u b0=%u ctx=%p(%s)",
            actionName, creatureName,
            fields.name[0] ? fields.name : "<none>",
            fields.ctxName[0] ? fields.ctxName : "<unknown>",
            fields.localKey0,
            fields.d20, fields.d24, fields.keyExtra, fields.loops,
            fields.a8, fields.a9, fields.aa, fields.ab, fields.b0,
            fields.localContext, ctxDesc);

        if (g_lastAnimLine != line)
        {
            g_lastAnimLine = line;
            ObjectInspector::LogLine(line);
        }
    }

    void LogAnimCtor(const char* tag, void* creature, void* selector, void* animKey,
        const unsigned int* extra, size_t extraCount)
    {
        char creatureName[128] = "?";
        const char* creatureRtti = ObjectInspector::GetRttiName(creature);
        if (creatureRtti) Demangle(creatureRtti, creatureName, sizeof(creatureName));

        char selDesc[96];
        DescribeValue(selector, selDesc, sizeof(selDesc));
        char keyDesc[96];
        DescribeValue(animKey, keyDesc, sizeof(keyDesc));

        char line[480];
        int len = sprintf_s(line,
            "[%s] creature=%-22s selector=%p(%s) key=%p(%s) args:",
            tag, creatureName, selector, selDesc, animKey, keyDesc);
        for (size_t i = 0; i < extraCount && len > 0 && (size_t)len + 12 < sizeof(line); i++)
            len += sprintf_s(line + len, sizeof(line) - len, " %08X", extra[i]);

        if (g_lastCtorLine != line)
        {
            g_lastCtorLine = line;
            ObjectInspector::LogLine(line);
        }
    }

    void* __fastcall HPlayAnimCtorResolving(void* self, void* edx,
        void* creature, void* selector, void* animKey,
        unsigned int a4, unsigned int a5, unsigned int a6,
        unsigned int a7, unsigned int a8)
    {
        if (g_enabled)
        {
            unsigned int extra[] = { a4, a5, a6, a7, a8 };
            LogAnimCtor("AnimCtor8", creature, selector, animKey, extra, 5);
        }

        return OPlayAnimCtorResolving(self, edx, creature, selector, animKey,
            a4, a5, a6, a7, a8);
    }

    // 11-arg variant: selector is arg2 (resolved via 0x6924B0 like the
    // 8-arg one) but the anim key is arg6 here (pushed as base-arg5).
    void* __fastcall HPlayAnimCtorResolving11(void* self, void* edx,
        void* creature, void* selector, void* a3,
        unsigned int a4, unsigned int a5, unsigned int animKey,
        unsigned int a7, unsigned int a8,
        unsigned int a9, unsigned int a10, unsigned int a11)
    {
        if (g_enabled)
        {
            unsigned int extra[] = { (unsigned int)(uintptr_t)a3, a4, a5, a7, a8, a9, a10, a11 };
            LogAnimCtor("AnimCtor11", creature, selector, (void*)(uintptr_t)animKey, extra, 8);
        }

        return OPlayAnimCtorResolving11(self, edx, creature, selector, a3,
            a4, a5, animKey, a7, a8, a9, a10, a11);
    }

    void* __fastcall HResolveAnimContext(void* creature, void* edx,
        void* animKey, int flag)
    {
        void* context = OResolveAnimContext(creature, edx, animKey, flag);

        // Associate the context's portable id with the anim name the game
        // resolved it from — feeds the wire-side name lookup.
        AnimAction::NoteResolvedContext(context, animKey, flag);

        if (g_enabled)
        {
            char keyDesc[96];
            DescribeValue(animKey, keyDesc, sizeof(keyDesc));

            char creatureName[128] = "?";
            const char* creatureRtti = ObjectInspector::GetRttiName(creature);
            if (creatureRtti) Demangle(creatureRtti, creatureName, sizeof(creatureName));

            char line[360];
            sprintf_s(line, "[AnimResolve] creature=%-22s key=%p(%s) flag=%d -> ctx=%p",
                creatureName, animKey, keyDesc, flag, context);

            if (g_lastResolveLine != line)
            {
                g_lastResolveLine = line;
                ObjectInspector::LogLine(line);
            }
        }

        return context;
    }

    int __fastcall HDoCreatureAction(void* creature, void* edx, void* action)
    {
        const char* actionRtti = ObjectInspector::GetRttiName(action);
        char actionName[128] = "?";
        if (actionRtti) Demangle(actionRtti, actionName, sizeof(actionName));

        // Extraction is limited to the 0x693B30-layout classes
        // (PlayCombatAnimation differs and would extract garbage). The
        // capture is kept tracer-on-or-off so NUMPAD9 always has material.
        bool isAnimFamily = actionRtti && IsAnimFamilyClass(actionName);
        AnimActionFields animFields;
        bool animExtracted = false;
        if (actionRtti && AnimAction::ClassHasAnimLayout(actionName))
        {
            animExtracted = AnimAction::Extract(action, animFields);
            if (animExtracted)
            {
                g_lastAnim = animFields;
                g_hasLastAnim = true;
            }
        }

        // Combat sync observer runs regardless of debug logging.
        if (g_observer && actionRtti)
            g_observer(creature, action, actionName);

        if (g_enabled)
        {
            const char* creatureRtti = ObjectInspector::GetRttiName(creature);
            char creatureName[128] = "?";
            if (creatureRtti) Demangle(creatureRtti, creatureName, sizeof(creatureName));

            if (isAnimFamily)
            {
                if (animExtracted)
                    TraceAnimAction(animFields, actionName, creatureName);

                // One raw dump per class per session — from any creature —
                // so unknown layouts (PlayCombatAnimation) can be mapped.
                if (g_dumpedAnimClasses.insert(actionName).second)
                {
                    char label[160];
                    sprintf_s(label, "anim action params: %s", actionName);
                    ObjectInspector::Dump(label, action, 0x140);
                }
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

        // Diagnostic-only hooks; combat sync doesn't depend on them. Their
        // install status is logged once so a silent failure (as suspected
        // in the first capture session, which produced no [AnimCtor] lines)
        // is visible.
        bool ctor8Ok = MH_CreateHook(reinterpret_cast<void*>(FN_PLAY_ANIM_CTOR_RESOLVING),
                reinterpret_cast<void*>(&HPlayAnimCtorResolving),
                reinterpret_cast<void**>(&OPlayAnimCtorResolving)) == MH_OK
            && MH_EnableHook(reinterpret_cast<void*>(FN_PLAY_ANIM_CTOR_RESOLVING)) == MH_OK;

        bool ctor11Ok = MH_CreateHook(reinterpret_cast<void*>(FN_PLAY_ANIM_CTOR_RESOLVING11),
                reinterpret_cast<void*>(&HPlayAnimCtorResolving11),
                reinterpret_cast<void**>(&OPlayAnimCtorResolving11)) == MH_OK
            && MH_EnableHook(reinterpret_cast<void*>(FN_PLAY_ANIM_CTOR_RESOLVING11)) == MH_OK;

        // The resolver hook is load-bearing for animation sync (it supplies
        // the ctxId -> anim-name mapping), unlike the diagnostic ctor hooks.
        bool resolveOk = MH_CreateHook(reinterpret_cast<void*>(FN_RESOLVE_ANIM_CONTEXT),
                reinterpret_cast<void*>(&HResolveAnimContext),
                reinterpret_cast<void**>(&OResolveAnimContext)) == MH_OK
            && MH_EnableHook(reinterpret_cast<void*>(FN_RESOLVE_ANIM_CONTEXT)) == MH_OK;

        char line[160];
        sprintf_s(line, "[AnimTracer] hooks: resolver %s, ctor8 %s, ctor11 %s",
            resolveOk ? "ok" : "FAILED",
            ctor8Ok ? "ok" : "FAILED", ctor11Ok ? "ok" : "FAILED");
        ObjectInspector::LogLine(line);
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
