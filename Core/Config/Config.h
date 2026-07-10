#pragma once

#include <string>

// Settings read once from EgoMP.ini, which lives next to EgoMP.dll.
// Missing file or keys fall back to the defaults below.
class Config
{
public:
    static const Config& Get();

    std::string serverIp = "127.0.0.1";
    unsigned short serverPort = 60000;
    bool autoConnect = true;

    unsigned short hostPort = 60000;

    bool showConsole = true;

private:
    Config();
};
