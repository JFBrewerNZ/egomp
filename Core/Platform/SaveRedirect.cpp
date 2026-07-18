#include "SaveRedirect.h"

#include "../Config/Config.h"
#include "ClientSlot.h"

#include <windows.h>
#include <string>
#include <iostream>

#include <MinHook/include/MinHook.h>

// We only need one constant and one signature from the shell headers, so we
// declare them locally rather than pulling in <shlobj.h> (and shell32.lib).
namespace
{
    constexpr int kCsidlPersonal = 0x0005; // CSIDL_PERSONAL == the Documents folder
    constexpr int kCsidlAppData  = 0x001A; // CSIDL_APPDATA == Roaming AppData

    using SHGetFolderPathW_t = HRESULT(WINAPI*)(HWND, int, HANDLE, DWORD, LPWSTR);
    SHGetFolderPathW_t oSHGetFolderPathW = nullptr;

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

    // Recursively copy a directory tree (overwriting). CopyFileW hydrates
    // OneDrive placeholders as it reads them.
    void CopyDirTree(const std::wstring& src, const std::wstring& dst)
    {
        CreateDirTree(dst);
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((src + L"\\*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE)
            return;
        do
        {
            const std::wstring name = fd.cFileName;
            if (name == L"." || name == L"..")
                continue;
            const std::wstring s = src + L"\\" + name;
            const std::wstring d = dst + L"\\" + name;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                CopyDirTree(s, d);
            else
                CopyFileW(s.c_str(), d.c_str(), FALSE);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }

    bool PathExists(const std::wstring& p)
    {
        return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES;
    }

    HRESULT WINAPI HSHGetFolderPathW(HWND hwnd, int csidl, HANDLE token, DWORD flags, LPWSTR out)
    {
        HRESULT hr = oSHGetFolderPathW(hwnd, csidl, token, flags, out);
        if (FAILED(hr) || !out || redirectBase.empty())
            return hr;

        // csidl carries flags in the high bits (e.g. CSIDL_FLAG_CREATE); the
        // folder id is the low byte. The game derives TWO per-user locations:
        //  - Documents  -> "My Games\Fable"  (saves, tattoos, profiles)
        //  - AppData    -> "Microsoft\Fable" (comfront/comback.dat, a cache it
        //    opens read/write with NO sharing; a second client's world load
        //    dies on a CBBBFileException when the first client holds it)
        // Both must be per-client, so both are rerouted under this client's
        // folder. Distinct subtrees ("My Games\..." vs "AppData\Microsoft\...")
        // keep them from ever colliding with each other.
        const int folder = csidl & 0xFF;
        if (folder == kCsidlPersonal)
        {
            CreateDirTree(redirectBase);
            wcscpy_s(out, MAX_PATH, redirectBase.c_str());
        }
        else if (folder == kCsidlAppData)
        {
            const std::wstring appData = redirectBase + L"\\AppData";
            CreateDirTree(appData);
            wcscpy_s(out, MAX_PATH, appData.c_str());
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

    // Copy the seed template into a brand-new client folder, so a fresh client
    // starts with the hero(es) from the template instead of empty.
    void SeedIfNew(const std::wstring& root)
    {
        if (!Config::Get().seedNewClients)
            return;
        const std::wstring fableDir = redirectBase + L"\\My Games\\Fable";
        if (PathExists(fableDir))
            return; // already populated; leave it alone

        std::wstring seedFrom = Widen(Config::Get().seedFrom);
        if (seedFrom.empty())
            seedFrom = root + L"\\Template\\My Games\\Fable";
        if (!PathExists(seedFrom))
        {
            Log("no seed template at " + Narrow(seedFrom) + "; new client starts empty");
            return;
        }
        Log("seeding new client from " + Narrow(seedFrom));
        CopyDirTree(seedFrom, fableDir);
    }
}

namespace SaveRedirect
{
    void Install()
    {
        if (!Config::Get().separateSaves)
            return;

        const int clientNumber = ClientSlot::Number();
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

        // Populate a fresh client folder from the template before the game reads
        // it (we run at attach, before the game resolves any folder).
        SeedIfNew(root);

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
            + " (saves at +\\My Games\\Fable, AppData cache at +\\AppData\\Microsoft\\Fable)");
    }
}
