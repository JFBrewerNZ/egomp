#include "CrashDiag.h"

#include "../Config/Config.h"
#include "../Platform/ClientSlot.h"

#include <windows.h>
#include <cstdio>
#include <cstring>
#include <share.h>

#include <MinHook/include/MinHook.h>

// Resolves to this DLL's module handle without needing DllMain plumbing.
extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace
{
    // ---------------------------------------------------------------- logging

    char logPath[MAX_PATH]  = {};
    char dumpPath[MAX_PATH] = {};

    // Shared-friendly append: _SH_DENYNO so a second client can never be
    // locked out of its own diagnostics by a sibling (fopen_s denies sharing).
    void Append(const char* text)
    {
        FILE* f = _fsopen(logPath, "a", _SH_DENYNO);
        if (!f)
            return;
        fputs(text, f);
        fclose(f);
    }

    void Appendf(const char* fmt, ...)
    {
        char buf[1024];
        va_list args;
        va_start(args, fmt);
        _vsnprintf_s(buf, _TRUNCATE, fmt, args);
        va_end(args);
        Append(buf);
    }

    void AppendTimestamp()
    {
        SYSTEMTIME st;
        GetLocalTime(&st);
        Appendf("[%02u:%02u:%02u.%03u] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    }

    // "Fable.exe+0x123456" style for any address, so frames are meaningful
    // without symbols.
    void FormatAddress(void* addr, char* out, size_t outLen)
    {
        HMODULE mod = nullptr;
        char modPath[MAX_PATH] = {};
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                   GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCSTR)addr, &mod) &&
            mod && GetModuleFileNameA(mod, modPath, MAX_PATH))
        {
            const char* base = strrchr(modPath, '\\');
            base = base ? base + 1 : modPath;
            _snprintf_s(out, outLen, _TRUNCATE, "%s+0x%08X",
                        base, (unsigned)((char*)addr - (char*)mod));
        }
        else
        {
            _snprintf_s(out, outLen, _TRUNCATE, "0x%08X", (unsigned)(UINT_PTR)addr);
        }
    }

    // ------------------------------------------------- CreateFile ring buffer

    struct FileOp
    {
        char  path[260];
        DWORD access;
        DWORD share;
        DWORD disposition;
        DWORD error;   // 0 on success
        DWORD tick;
        DWORD tid;
        bool  ok;
        bool  used;
    };

    constexpr int kRingSize = 64;
    FileOp        ring[kRingSize] = {};
    volatile LONG ringNext        = 0;

    using CreateFileA_t = HANDLE(WINAPI*)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES,
                                          DWORD, DWORD, HANDLE);
    using CreateFileW_t = HANDLE(WINAPI*)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES,
                                          DWORD, DWORD, HANDLE);
    CreateFileA_t oCreateFileA = nullptr;
    CreateFileW_t oCreateFileW = nullptr;

    void RecordOp(const char* path, DWORD access, DWORD share, DWORD disposition,
                  bool ok, DWORD error)
    {
        LONG slot = (InterlockedIncrement(&ringNext) - 1) % kRingSize;
        FileOp& op = ring[slot];
        strncpy_s(op.path, path ? path : "(null)", _TRUNCATE);
        op.access      = access;
        op.share       = share;
        op.disposition = disposition;
        op.ok          = ok;
        op.error       = ok ? 0 : error;
        op.tick        = GetTickCount();
        op.tid         = GetCurrentThreadId();
        op.used        = true;

        // A sharing violation is exactly the cross-client collision we are
        // hunting - log it the moment it happens, crash or not. Only that:
        // generic open failures are routine (AMD's shader cache retries
        // CREATE_NEW opens forever, DirectInput probes HID paths it cannot
        // access) and flooded the log in testing; they still show up in the
        // ring dump when a throw happens. Never for our own log file: logging
        // a failed log-open would recurse forever.
        if (!ok && error == ERROR_SHARING_VIOLATION && !strstr(op.path, "EgoMP-crash"))
        {
            AppendTimestamp();
            Appendf("FILE-FAIL err=%u access=0x%08X share=0x%X disp=%u path=%s\n",
                    error, access, share, disposition, op.path);
        }
    }

    void DumpRing()
    {
        Append("  recent CreateFile calls (oldest first):\n");
        LONG next = ringNext;
        for (int i = 0; i < kRingSize; ++i)
        {
            const FileOp& op = ring[(next + i) % kRingSize];
            if (!op.used)
                continue;
            Appendf("    t-%-6u tid=%-5u %s%s access=0x%08X share=0x%X err=%u %s\n",
                    GetTickCount() - op.tick, op.tid,
                    op.ok ? "ok  " : "FAIL",
                    (op.access & GENERIC_WRITE) ? " W" : "  ",
                    op.access, op.share, op.error, op.path);
        }
    }

    HANDLE WINAPI HCreateFileA(LPCSTR path, DWORD access, DWORD share,
                               LPSECURITY_ATTRIBUTES sa, DWORD disposition,
                               DWORD flags, HANDLE tmpl)
    {
        HANDLE h = oCreateFileA(path, access, share, sa, disposition, flags, tmpl);
        DWORD err = (h == INVALID_HANDLE_VALUE) ? GetLastError() : 0;
        RecordOp(path, access, share, disposition, h != INVALID_HANDLE_VALUE, err);
        if (h == INVALID_HANDLE_VALUE)
            SetLastError(err); // our logging must not disturb the caller's view
        return h;
    }

    HANDLE WINAPI HCreateFileW(LPCWSTR path, DWORD access, DWORD share,
                               LPSECURITY_ATTRIBUTES sa, DWORD disposition,
                               DWORD flags, HANDLE tmpl)
    {
        HANDLE h = oCreateFileW(path, access, share, sa, disposition, flags, tmpl);
        DWORD err = (h == INVALID_HANDLE_VALUE) ? GetLastError() : 0;
        char narrow[260] = {};
        if (path)
            WideCharToMultiByte(CP_ACP, 0, path, -1, narrow, sizeof(narrow) - 1,
                                nullptr, nullptr);
        RecordOp(path ? narrow : nullptr, access, share, disposition,
                 h != INVALID_HANDLE_VALUE, err);
        if (h == INVALID_HANDLE_VALUE)
            SetLastError(err);
        return h;
    }

    // ------------------------------------------- MSVC C++ exception RTTI (x86)

    constexpr DWORD kMsvcException = 0xE06D7363;
    constexpr DWORD kMsvcMagic     = 0x19930520;

    // 32-bit layouts: the "RVA" fields are absolute pointers on x86.
    struct ThrowInfo32
    {
        DWORD attributes;
        DWORD pmfnUnwind;
        DWORD pForwardCompat;
        DWORD pCatchableTypeArray;
    };
    struct CatchableTypeArray32
    {
        int   nCatchableTypes;
        DWORD arrayOfCatchableTypes[8]; // we only read what nCatchableTypes allows
    };
    struct CatchableType32
    {
        DWORD properties;
        DWORD pType; // TypeDescriptor*
    };
    struct TypeDescriptor32
    {
        void* pVFTable;
        void* spare;
        char  name[1]; // ".?AVCBBBFileException@@" style, NUL-terminated
    };

    // All reads of game-owned exception metadata are SEH-guarded; a bad
    // pointer just yields fewer names. (No C++ objects here: __try rules.)
    int ExtractTypeNames(const EXCEPTION_RECORD* rec,
                         char names[4][128])
    {
        int count = 0;
        __try
        {
            if (rec->NumberParameters < 3 || rec->ExceptionInformation[0] != kMsvcMagic)
                return 0;
            const ThrowInfo32* ti =
                (const ThrowInfo32*)rec->ExceptionInformation[2];
            if (!ti || !ti->pCatchableTypeArray)
                return 0;
            const CatchableTypeArray32* cta =
                (const CatchableTypeArray32*)ti->pCatchableTypeArray;
            int n = cta->nCatchableTypes;
            if (n < 0)
                return 0;
            if (n > 4)
                n = 4;
            for (int i = 0; i < n; ++i)
            {
                const CatchableType32* ct =
                    (const CatchableType32*)cta->arrayOfCatchableTypes[i];
                if (!ct || !ct->pType)
                    continue;
                const TypeDescriptor32* td = (const TypeDescriptor32*)ct->pType;
                strncpy_s(names[count], td->name, _TRUNCATE);
                ++count;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
        return count;
    }

    // Walk the EBP frame chain from the throw context. Frame-pointer walks
    // miss FPO frames but need no dbghelp and are async-safe.
    int WalkStack(const CONTEXT* ctx, void* frames[], int maxFrames)
    {
        int count = 0;
        __try
        {
            if (count < maxFrames)
                frames[count++] = (void*)ctx->Eip;
            const DWORD* ebp = (const DWORD*)ctx->Ebp;
            while (count < maxFrames && ebp)
            {
                DWORD ret = ebp[1];
                if (!ret)
                    break;
                frames[count++] = (void*)ret;
                const DWORD* next = (const DWORD*)ebp[0];
                if (next <= ebp) // must move up the stack
                    break;
                ebp = next;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
        return count;
    }

    // --------------------------------------------------------------- minidump

    volatile LONG dumpWritten = 0;

    using MiniDumpWriteDump_t = BOOL(WINAPI*)(HANDLE, DWORD, HANDLE, int,
                                              void*, void*, void*);

    void WriteDumpOnce(EXCEPTION_POINTERS* info)
    {
        if (InterlockedExchange(&dumpWritten, 1))
            return;

        HMODULE dbghelp = LoadLibraryW(L"dbghelp.dll");
        auto write = dbghelp ? (MiniDumpWriteDump_t)GetProcAddress(dbghelp, "MiniDumpWriteDump")
                             : nullptr;
        if (!write)
            return;

        HANDLE file = CreateFileA(dumpPath, GENERIC_WRITE, 0, nullptr,
                                  CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return;

        struct
        {
            DWORD               threadId;
            EXCEPTION_POINTERS* exceptionPointers;
            BOOL                clientPointers;
        } mei = { GetCurrentThreadId(), info, FALSE };

        // WithHandleData (0x4) | ScanMemory (0x10) | WithIndirectlyReferencedMemory
        // (0x40) | WithThreadInfo (0x1000): small (a few MB), but stacks, open
        // handles and pointed-to heap data are all in there.
        BOOL ok = write(GetCurrentProcess(), GetCurrentProcessId(), file,
                        0x00001054, info ? &mei : nullptr, nullptr, nullptr);
        CloseHandle(file);

        AppendTimestamp();
        Appendf("minidump %s: %s\n", ok ? "written" : "FAILED", dumpPath);
    }

    // ------------------------------------------------------- exception hooks

    volatile LONG inHandler   = 0;
    volatile LONG throwsSeen  = 0;
    constexpr LONG kMaxLoggedThrows = 50; // stop a throw-happy path flooding the log

    void LogException(const char* kind, EXCEPTION_POINTERS* info)
    {
        const EXCEPTION_RECORD* rec = info->ExceptionRecord;

        AppendTimestamp();
        Appendf("%s code=0x%08X tid=%u\n", kind, rec->ExceptionCode, GetCurrentThreadId());

        if (rec->ExceptionCode == kMsvcException)
        {
            char names[4][128] = {};
            int n = ExtractTypeNames(rec, names);
            if (n == 0)
                Append("  type: (unreadable ThrowInfo)\n");
            for (int i = 0; i < n; ++i)
                Appendf("  type[%d]: %s\n", i, names[i]);
        }
        else if (rec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
                 rec->NumberParameters >= 2)
        {
            Appendf("  AV: %s address 0x%08X\n",
                    rec->ExceptionInformation[0] ? "writing" : "reading",
                    (unsigned)rec->ExceptionInformation[1]);
        }

        void* frames[24] = {};
        int frameCount = WalkStack(info->ContextRecord, frames, 24);
        for (int i = 0; i < frameCount; ++i)
        {
            char where[192];
            FormatAddress(frames[i], where, sizeof(where));
            Appendf("  #%02d %s\n", i, where);
        }

        DumpRing();
    }

    LONG WINAPI VectoredHandler(EXCEPTION_POINTERS* info)
    {
        // First-chance, passive observer: log C++ throws only (AVs fire for
        // benign probes all over a hooked game) and always continue search.
        if (info->ExceptionRecord->ExceptionCode != kMsvcException)
            return EXCEPTION_CONTINUE_SEARCH;

        if (InterlockedCompareExchange(&inHandler, 1, 0) != 0)
            return EXCEPTION_CONTINUE_SEARCH; // reentrant probe fault: skip

        if (InterlockedIncrement(&throwsSeen) <= kMaxLoggedThrows)
        {
            LogException("C++ THROW (first chance)", info);
            WriteDumpOnce(info);
        }

        InterlockedExchange(&inHandler, 0);
        return EXCEPTION_CONTINUE_SEARCH;
    }

    LPTOP_LEVEL_EXCEPTION_FILTER oFilter = nullptr;

    LONG WINAPI UnhandledFilter(EXCEPTION_POINTERS* info)
    {
        InterlockedExchange(&inHandler, 0); // we are dying anyway; never skip this
        LogException("UNHANDLED EXCEPTION (process is about to die)", info);
        WriteDumpOnce(info);
        return oFilter ? oFilter(info) : EXCEPTION_CONTINUE_SEARCH;
    }
}

namespace CrashDiag
{
    void Note(const char* fmt, ...)
    {
        if (!logPath[0])
            return;
        char buf[512];
        va_list args;
        va_start(args, fmt);
        _vsnprintf_s(buf, _TRUNCATE, fmt, args);
        va_end(args);
        AppendTimestamp();
        Appendf("%s\n", buf);
    }

    void Install()
    {
        if (!Config::Get().crashDiag)
            return;

        char modulePath[MAX_PATH] = {};
        GetModuleFileNameA((HMODULE)&__ImageBase, modulePath, MAX_PATH);
        char* slash = strrchr(modulePath, '\\');
        if (slash)
            slash[1] = '\0';
        _snprintf_s(logPath, _TRUNCATE, "%sEgoMP-crash-%u.log",
                    modulePath, GetCurrentProcessId());
        _snprintf_s(dumpPath, _TRUNCATE, "%sEgoMP-crash-%u.dmp",
                    modulePath, GetCurrentProcessId());

        AddVectoredExceptionHandler(1, &VectoredHandler);
        oFilter = SetUnhandledExceptionFilter(&UnhandledFilter);

        // File-op ring: hook both ANSI and wide opens (the 2005-era game uses
        // ANSI; our own code and system DLLs use wide).
        HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
        void* a = k32 ? (void*)GetProcAddress(k32, "CreateFileA") : nullptr;
        void* w = k32 ? (void*)GetProcAddress(k32, "CreateFileW") : nullptr;
        bool hookedA = a &&
            MH_CreateHook(a, (void*)&HCreateFileA, (void**)&oCreateFileA) == MH_OK &&
            MH_EnableHook(a) == MH_OK;
        bool hookedW = w &&
            MH_CreateHook(w, (void*)&HCreateFileW, (void**)&oCreateFileW) == MH_OK &&
            MH_EnableHook(w) == MH_OK;

        SYSTEMTIME st;
        GetLocalTime(&st);
        Appendf("=== EgoMP crash diagnostics: pid %u, client slot %d, "
                "%04u-%02u-%02u %02u:%02u:%02u ===\n"
                "  CreateFileA hook: %s, CreateFileW hook: %s\n",
                GetCurrentProcessId(), ClientSlot::Number(),
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
                hookedA ? "ok" : "FAILED", hookedW ? "ok" : "FAILED");
    }
}
