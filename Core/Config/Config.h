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

    // Force the game into a (borderless) window (see [display] in EgoMP.ini).
    // Recommended when running two clients on one machine so they don't fight
    // over the exclusive fullscreen display.
    bool windowed = true;

    // Give each client on this machine its own Documents\My Games\Fable so the
    // clients don't collide on save/tattoo files. Each launch gets its own
    // "<dataRoot>\Client<N>"; dataRoot empty = a default under
    // %LOCALAPPDATA%\EgoMP.
    bool separateSaves = true;
    std::string dataRoot = "";

    bool showConsole = true;

    // Reverse-engineering aids (NUMPAD5 object inspector). Off by default;
    // not for distributed builds.
    bool debugKeys = false;

private:
    Config();
};
