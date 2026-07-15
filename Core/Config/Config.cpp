#include "Config.h"

#include <windows.h>

// Resolves to this DLL's module handle without needing DllMain plumbing.
extern "C" IMAGE_DOS_HEADER __ImageBase;

const Config& Config::Get()
{
    static Config instance;
    return instance;
}

Config::Config()
{
    char modulePath[MAX_PATH] = {};
    GetModuleFileNameA((HMODULE)&__ImageBase, modulePath, MAX_PATH);

    std::string iniPath(modulePath);
    size_t slash = iniPath.find_last_of("\\/");
    iniPath = iniPath.substr(0, slash + 1) + "EgoMP.ini";

    char ip[64] = {};
    GetPrivateProfileStringA("client", "server_ip", serverIp.c_str(), ip, sizeof(ip), iniPath.c_str());
    serverIp = ip;

    serverPort = (unsigned short)GetPrivateProfileIntA("client", "server_port", serverPort, iniPath.c_str());
    autoConnect = GetPrivateProfileIntA("client", "auto_connect", autoConnect ? 1 : 0, iniPath.c_str()) != 0;

    hostPort = (unsigned short)GetPrivateProfileIntA("host", "port", hostPort, iniPath.c_str());

    windowed = GetPrivateProfileIntA("display", "windowed", windowed ? 1 : 0, iniPath.c_str()) != 0;
    reshapeWindow = GetPrivateProfileIntA("display", "reshape", reshapeWindow ? 1 : 0, iniPath.c_str()) != 0;
    windowWidth = GetPrivateProfileIntA("display", "window_width", windowWidth, iniPath.c_str());
    windowHeight = GetPrivateProfileIntA("display", "window_height", windowHeight, iniPath.c_str());
    int unlock = GetPrivateProfileIntA("display", "mouse_unlock", -1, iniPath.c_str());
    mouseUnlock = (unlock < 0) ? reshapeWindow : unlock != 0;
    mouseLock = GetPrivateProfileIntA("display", "mouse_lock", mouseLock ? 1 : 0, iniPath.c_str()) != 0;

    separateSaves = GetPrivateProfileIntA("storage", "separate_saves", separateSaves ? 1 : 0, iniPath.c_str()) != 0;
    char dir[512] = {};
    GetPrivateProfileStringA("storage", "data_root", dataRoot.c_str(), dir, sizeof(dir), iniPath.c_str());
    dataRoot = dir;
    seedNewClients = GetPrivateProfileIntA("storage", "seed_new_clients", seedNewClients ? 1 : 0, iniPath.c_str()) != 0;
    char seed[512] = {};
    GetPrivateProfileStringA("storage", "seed_from", seedFrom.c_str(), seed, sizeof(seed), iniPath.c_str());
    seedFrom = seed;

    showConsole = GetPrivateProfileIntA("general", "console", showConsole ? 1 : 0, iniPath.c_str()) != 0;
    debugKeys = GetPrivateProfileIntA("general", "debug_keys", debugKeys ? 1 : 0, iniPath.c_str()) != 0;
}
