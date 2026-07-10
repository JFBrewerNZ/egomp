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

    showConsole = GetPrivateProfileIntA("general", "console", showConsole ? 1 : 0, iniPath.c_str()) != 0;
}
