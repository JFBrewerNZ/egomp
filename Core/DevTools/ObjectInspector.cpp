#include "ObjectInspector.h"

#include <windows.h>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace
{
    // Pointers into buffers are followed one level deep; scan this much of
    // each pointed-to buffer looking for component-pointer arrays/lists.
    // (0x40 truncated the hero's ~70-component list mid-way.)
    const size_t BUFFER_SCAN_DWORDS = 0x100;

    struct ModuleRange
    {
        uintptr_t begin = 0;
        uintptr_t end = 0;
    };

    // Fable.exe's image range; vtables, RTTI locators and type descriptors
    // of game classes all live inside it.
    const ModuleRange& GameImage()
    {
        static ModuleRange range = [] {
            ModuleRange r;
            uintptr_t base = (uintptr_t)GetModuleHandleA(nullptr);
            const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)base;
            const IMAGE_NT_HEADERS* nt = (const IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
            r.begin = base;
            r.end = base + nt->OptionalHeader.SizeOfImage;
            return r;
        }();
        return range;
    }

    bool InGameImage(const void* p)
    {
        return (uintptr_t)p >= GameImage().begin && (uintptr_t)p < GameImage().end;
    }

    bool IsReadable(const void* p, size_t bytes)
    {
        if (!p)
            return false;

        MEMORY_BASIC_INFORMATION mbi = {};
        if (!VirtualQuery(p, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT)
            return false;
        if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))
            return false;

        // Enough that the region continues past the requested range.
        return (uintptr_t)p + bytes <= (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
    }

    void Log(std::string& sink, const std::string& line)
    {
        std::cout << line << std::endl;
        sink += line + "\n";
    }

    // Append to EgoMP-inspect.log next to the DLL so results survive the
    // console and can be shared.
    void WriteLogFile(const std::string& report)
    {
        char modulePath[MAX_PATH] = {};
        GetModuleFileNameA((HMODULE)&__ImageBase, modulePath, MAX_PATH);
        std::string logPath(modulePath);
        size_t slash = logPath.find_last_of("\\/");
        logPath = logPath.substr(0, slash + 1) + "EgoMP-inspect.log";

        FILE* f = nullptr;
        if (fopen_s(&f, logPath.c_str(), "a") == 0 && f)
        {
            SYSTEMTIME st;
            GetLocalTime(&st);
            fprintf(f, "=== %04u-%02u-%02u %02u:%02u:%02u ===\n%s\n",
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
                report.c_str());
            fclose(f);
        }
    }

    // ".?AVCTCInventoryClothing@@" -> "CTCInventoryClothing"
    std::string Demangle(const char* rttiName)
    {
        std::string name(rttiName);
        if (name.rfind(".?AV", 0) == 0 || name.rfind(".?AU", 0) == 0)
            name = name.substr(4);
        while (!name.empty() && name.back() == '@')
            name.pop_back();
        return name;
    }
}

namespace ObjectInspector
{
    const char* GetRttiName(const void* object)
    {
        if (!IsReadable(object, sizeof(void*)))
            return nullptr;

        const void* vptr = *(const void* const*)object;
        if (!InGameImage(vptr) || !IsReadable((const char*)vptr - sizeof(void*), sizeof(void*)))
            return nullptr;

        // MSVC x86: vtable[-1] -> CompleteObjectLocator
        //   { DWORD signature; DWORD offset; DWORD cdOffset;
        //     TypeDescriptor* pTypeDescriptor; ... }
        const DWORD* col = *((const DWORD* const*)vptr - 1);
        if (!InGameImage(col) || !IsReadable(col, 16) || col[0] != 0)
            return nullptr;

        // TypeDescriptor: { void* pVFTable; void* spare; char name[]; }
        const char* td = (const char*)(uintptr_t)col[3];
        if (!InGameImage(td) || !IsReadable(td, 16))
            return nullptr;

        const char* name = td + 8;
        if (memcmp(name, ".?A", 3) != 0)
            return nullptr;

        return name;
    }

    void Dump(const char* label, const void* object, size_t bytes)
    {
        std::string report;

        char header[256];
        const char* selfName = GetRttiName(object);
        sprintf_s(header, "[Inspect] %s @ %p (%s), scanning 0x%X bytes",
            label, object, selfName ? Demangle(selfName).c_str() : "no RTTI", (unsigned)bytes);
        Log(report, header);

        for (size_t offset = 0; offset + sizeof(void*) <= bytes; offset += sizeof(void*))
        {
            const char* member = (const char*)object + offset;
            if (!IsReadable(member, sizeof(void*)))
                break;

            const void* value = *(const void* const*)member;

            if (const char* name = GetRttiName(value))
            {
                char line[256];
                sprintf_s(line, "  +0x%03X -> %s @ %p",
                    (unsigned)offset, Demangle(name).c_str(), value);
                Log(report, line);
                continue;
            }

            // Not an object itself: maybe a buffer holding object pointers
            // (std::vector-style component list). One level deep only.
            if (InGameImage(value) || !IsReadable(value, sizeof(void*)))
                continue;

            std::string bufferLines;
            for (size_t i = 0; i < BUFFER_SCAN_DWORDS; i++)
            {
                const char* slot = (const char*)value + i * sizeof(void*);
                if (!IsReadable(slot, sizeof(void*)))
                    break;

                if (const char* name = GetRttiName(*(const void* const*)slot))
                {
                    char line[256];
                    sprintf_s(line, "    [%2u] %s @ %p",
                        (unsigned)i, Demangle(name).c_str(), *(const void* const*)slot);
                    bufferLines += std::string(line) + "\n";
                }
            }

            if (!bufferLines.empty())
            {
                char line[128];
                sprintf_s(line, "  +0x%03X -> buffer @ %p containing:", (unsigned)offset, value);
                Log(report, line);
                std::cout << bufferLines;
                report += bufferLines;
            }
        }

        Log(report, "[Inspect] done");
        WriteLogFile(report);
    }

    void DumpRaw(const char* label, const void* buffer, size_t dwords)
    {
        std::string report;

        char header[256];
        sprintf_s(header, "[Inspect] raw %s @ %p, %u dwords", label, buffer, (unsigned)dwords);
        Log(report, header);

        for (size_t i = 0; i < dwords; i++)
        {
            const char* slot = (const char*)buffer + i * sizeof(void*);
            if (!IsReadable(slot, sizeof(void*)))
            {
                Log(report, "  (unreadable, stopping)");
                break;
            }

            const void* value = *(const void* const*)slot;
            const char* name = GetRttiName(value);

            char line[256];
            sprintf_s(line, "  [%3u] +0x%03X  %08X%s%s",
                (unsigned)i, (unsigned)(i * sizeof(void*)), (unsigned)(uintptr_t)value,
                name ? "  " : "", name ? Demangle(name).c_str() : "");
            Log(report, line);
        }

        WriteLogFile(report);
    }

    void DumpMatchingObjects(const char* label, const void* buffer, size_t dwords,
        const char* const* substrings, size_t substringCount, size_t objectBytes)
    {
        std::cout << "[Inspect] scanning " << label << " for matching components..." << std::endl;

        for (size_t i = 0; i < dwords; i++)
        {
            const char* slot = (const char*)buffer + i * sizeof(void*);
            if (!IsReadable(slot, sizeof(void*)))
                break;

            const void* value = *(const void* const*)slot;
            const char* name = GetRttiName(value);
            if (!name)
                continue;

            for (size_t s = 0; s < substringCount; s++)
            {
                if (strstr(name, substrings[s]))
                {
                    Dump(Demangle(name).c_str(), value, objectBytes);
                    break;
                }
            }
        }
    }
}
