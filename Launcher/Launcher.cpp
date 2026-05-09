#include <iostream>
#include <windows.h>

BOOL StartEXE(wchar_t* gamePath, STARTUPINFOW& si, PROCESS_INFORMATION& pi)
{
    BOOL result = CreateProcessW(NULL, gamePath, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, NULL, &si, &pi);
    if (!result)
    {
        std::wcerr << L"[CreateProcessW] Error code: " << GetLastError() << L"." << std::endl;
        return false;
    }

    return true;
}

BOOL InjectDLL(HANDLE hProcess, const wchar_t* corePath)
{
    SIZE_T size = (wcslen(corePath) + 1) * sizeof(wchar_t);
    LPVOID remoteMemory = VirtualAllocEx(hProcess, NULL, size, MEM_COMMIT, PAGE_READWRITE);
    if (!remoteMemory) {
        std::wcerr << L"[VirtualAllocEx] Error code: " << GetLastError() << L"." << std::endl;
        return false;
    }

    BOOL writeSuccess = WriteProcessMemory(hProcess, remoteMemory, corePath, size, NULL);
    if (!writeSuccess)
    {
        std::wcerr << L"[WriteProcessMemory] Error: " << GetLastError() << L"." << std::endl;
        VirtualFreeEx(hProcess, remoteMemory, 0, MEM_RELEASE);
        return false;
    }

    HMODULE kernel32Handle = GetModuleHandleW(L"kernel32.dll");
    if (!kernel32Handle) {
        std::wcerr << L"[GetModuleHandleW] Error code: " << GetLastError() << L"." << std::endl;
        VirtualFreeEx(hProcess, remoteMemory, 0, MEM_RELEASE);
        return false;
    }
    auto loadLibraryAddr = reinterpret_cast<LPTHREAD_START_ROUTINE>(
        GetProcAddress(kernel32Handle, "LoadLibraryW")
    );
    if (!loadLibraryAddr) {
        std::wcerr << L"[GetProcAddress] Error code: " << GetLastError() << L"." << std::endl;
        VirtualFreeEx(hProcess, remoteMemory, 0, MEM_RELEASE);
        return false;
    }

    HANDLE hThread = CreateRemoteThread(hProcess, NULL, NULL,
        (LPTHREAD_START_ROUTINE)loadLibraryAddr, remoteMemory, NULL, NULL);
    if (!hThread) {
        std::wcerr << L"[CreateRemoteThread] Error code: " << GetLastError() << L"." << std::endl;
        VirtualFreeEx(hProcess, remoteMemory, 0, MEM_RELEASE);
        return false;
    }

    WaitForSingleObject(hThread, INFINITE);
    CloseHandle(hThread);
    VirtualFreeEx(hProcess, remoteMemory, 0, MEM_RELEASE);

    return true;
}

BOOL FileExists(const std::wstring& path)
{
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

std::wstring GetDirectory(const std::wstring& fullPath)
{
    size_t pos = fullPath.find_last_of(L"\\/");
    if (pos != std::wstring::npos)
        return fullPath.substr(0, pos);
    return L"";
}

BOOL CheckFilePath(wchar_t* gamePath, wchar_t* corePath)
{
    if (!FileExists(gamePath))
    {
        MessageBoxW(NULL, L"Fable.exe not found.", L"Error", MB_ICONERROR);
        return false;
    }

    if (!FileExists(corePath))
    {
        MessageBoxW(NULL, L"EgoMP.dll not found.", L"Error", MB_ICONERROR);
        return false;
    }

    return true;
}

int main()
{
    wchar_t gamePath[] = L"..\\Fable.exe";
    wchar_t corePath[] = L"EgoMP.dll";

    wchar_t fullDllPath[MAX_PATH];
    if (!_wfullpath(fullDllPath, corePath, MAX_PATH)) return 1;

    if (CheckFilePath(gamePath, fullDllPath))
    {
        STARTUPINFOW si = { sizeof(si) };
        PROCESS_INFORMATION pi = { 0 };

        if (StartEXE(gamePath, si, pi))
        {
            if (InjectDLL(pi.hProcess, fullDllPath))
            {
                ResumeThread(pi.hThread);
            }
            else
            {
                TerminateProcess(pi.hProcess, 0);
            }

            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
    }

    return 0;
}
