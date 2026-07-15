#include "SaveRedirect.h"

#include "../Config/Config.h"

#include <windows.h>
#include <string>
#include <iostream>

#include <MinHook/include/MinHook.h>

// We only need one constant and one signature from the shell headers, so we
// declare them locally rather than pulling in <shlobj.h> (and shell32.lib).
namespace
{
    constexpr int kCsidlPersonal = 0x0005; // CSIDL_PERSONAL == the Documents folder
    constexpr int kMaxClients    = 64;

    using SHGetFolderPathW_t = HRESULT(WINAPI*)(HWND, int, HANDLE, DWORD, LPWSTR);
    SHGetFolderPathW_t oSHGetFolderPathW = nullptr;

    // A per-client slot mutex, held for the life of the process to reserve this
    // client's number. Intentionally never closed: the OS releases it on exit,
    // which frees the slot for the next client to reuse.
    HANDLE       slotHandle   = nullptr;
    int          clientNumber = 0;
    std::wstring redirectBase; // replaces the Documents path for this client

    void Log(const std::string& msg)
    {
        std::cout << "[EgoMP][saves] " << msg << std::endl;
    }

    // Best-effort recursive mkdir (CreateDirectoryW only makes the leaf).
    void CreateDirTree(const std::wstring& path)
    {
        for (size_t i = 0; i <= path.size(); ++i)
        {
            if (i == path.size() || path[i] == L'\\')
            {
                if (i < 3) continue; // skip the "C:\" root
                CreateDirectoryW(path.substr(0, i).c_str(), nullptr);
            }
        }
    }

    // Claim the lowest free client number by grabbing the first slot mutex that
    // no other running client already holds. CreateMutexW is atomic, so a race
    // between two launches resolves cleanly (one gets the slot, the other moves
    // on to the next number).
    int ClaimClientSlot()
    {
        for (int n = 1; n <= kMaxClients; ++n)
        {
            wchar_t name[64];
            swprintf_s(name, L"EgoMP-Client-Slot-%d", n);
            HANDLE h = CreateMutexW(nullptr, FALSE, name);
            if (!h)
                continue;
            if (GetLastError() == ERROR_ALREADY_EXISTS)
            {
                CloseHandle(h); // held by another client; try the next number
                continue;
            }
            slotHandle = h; // keep open -> hold this slot until we exit
            return n;
        }
        return 0;
    }

    HRESULT WINAPI HSHGetFolderPathW(HWND hwnd, int csidl, HANDLE token, DWORD flags, LPWSTR out)
    {
        HRESULT hr = oSHGetFolderPathW(hwnd, csidl, token, flags, out);
        // csidl carries flags in the high bits (e.g. CSIDL_FLAG_CREATE); the
        // folder id is the low byte.
        if (SUCCEEDED(hr) && out && (csidl & 0xFF) == kCsidlPersonal && !redirectBase.empty())
        {
            CreateDirTree(redirectBase);
            wcscpy_s(out, MAX_PATH, redirectBase.c_str());
        }
        return hr;
    }

    std::wstring Widen(const std::string& s)
    {
        if (s.empty()) return L"";
        int n = MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, nullptr, 0);
        std::wstring w(n > 0 ? n - 1 : 0, L'\0');
        if (n > 0) MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, &w[0], n);
        return w;
    }

    std::string Narrow(const std::wstring& w)
    {
        if (w.empty()) return "";
        int n = WideCharToMultiByte(CP_ACP, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string s(n > 0 ? n - 1 : 0, '\0');
        if (n > 0) WideCharToMultiByte(CP_ACP, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr);
        return s;
    }
}

namespace SaveRedirect
{
    void Install()
    {
        if (!Config::Get().separateSaves)
            return;

        // Assign this launch a client number: 1 for the first running client, 2
        // for the next, and so on (freed numbers are reused).
        clientNumber = ClaimClientSlot();
        if (clientNumber == 0)
        {
            Log("no free client slot; using the shared save location");
            return;
        }

        // Base folder for all clients; each gets a "Client<N>" subfolder. Config
        // wins; otherwise default to a private, non-OneDrive per-user folder.
        std::wstring root = Widen(Config::Get().dataRoot);
        if (root.empty())
        {
            wchar_t localAppData[MAX_PATH] = {};
            if (GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH))
                root = std::wstring(localAppData) + L"\\EgoMP";
        }
        if (root.empty())
        {
            Log("could not resolve a data root; using the shared save location");
            return;
        }
        redirectBase = root + L"\\Client" + std::to_wstring(clientNumber);

        HMODULE shell32 = GetModuleHandleW(L"shell32.dll");
        if (!shell32)
            shell32 = LoadLibraryW(L"shell32.dll");
        void* target = shell32 ? reinterpret_cast<void*>(GetProcAddress(shell32, "SHGetFolderPathW"))
                               : nullptr;
        if (!target)
        {
            Log("SHGetFolderPathW not found; using the shared save location");
            return;
        }

        if (MH_CreateHook(target, reinterpret_cast<void*>(&HSHGetFolderPathW),
                          reinterpret_cast<void**>(&oSHGetFolderPathW)) != MH_OK ||
            MH_EnableHook(target) != MH_OK)
        {
            Log("failed to hook SHGetFolderPathW; using the shared save location");
            return;
        }

        Log("client " + std::to_string(clientNumber) + " Documents -> " + Narrow(redirectBase)
            + " (Fable data at that path + \\My Games\\Fable)");
    }
}
