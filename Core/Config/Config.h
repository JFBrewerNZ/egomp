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

    // Force the game into a window (see [display] in EgoMP.ini). Recommended
    // when running two clients on one machine so they don't fight over the
    // exclusive fullscreen display.
    bool windowed = true;

    // Reshape the game window into a titled, movable, resizable window placed
    // side by side by client number (applied a few frames in, so it doesn't
    // trip Fable's init-failure relaunch). windowWidth/windowHeight are the
    // client size in pixels; 0 = auto (half the screen width, ~90% height).
    bool reshapeWindow = true;
    int  windowWidth   = 0;
    int  windowHeight  = 0;

    // Make the game window manageable with the mouse (drag the title bar,
    // resize, click between clients) despite Fable's DirectInput mouse grab.
    // Absent from the ini it follows reshape; 0/1 override explicitly.
    bool mouseUnlock = true;

    // How mouse_unlock frees the mouse. 1 (default): keep the retail
    // exclusive grab while playing and release it only while Alt is held.
    // 0: make the mouse permanently shared -- dragging works without Alt,
    // but the Windows cursor roams during play and a click outside the
    // window steals the game's focus.
    bool mouseLock = true;

    // Give each client on this machine its own Documents\My Games\Fable so the
    // clients don't collide on save/tattoo files. Each launch gets its own
    // "<dataRoot>\Client<N>"; dataRoot empty = a default under
    // %LOCALAPPDATA%\EgoMP.
    bool separateSaves = true;
    std::string dataRoot = "";

    // Seed a brand-new client folder from a template so it isn't empty.
    // seedFrom is the "My Games\Fable" to copy; empty = "<dataRoot>\Template\
    // My Games\Fable". Set seedNewClients = 0 to turn seeding off entirely.
    bool seedNewClients = true;
    std::string seedFrom = "";

    bool showConsole = true;

    // Reverse-engineering aids (NUMPAD5 object inspector). Off by default;
    // not for distributed builds.
    bool debugKeys = false;

    // Crash diagnostics: log every C++ throw (exception type + stack), recent
    // file opens, and a one-shot minidump to EgoMP-crash-<pid>.log/.dmp next
    // to the DLL. Cheap and passive; on by default while the second-client
    // world-load crash is under investigation.
    bool crashDiag = true;

    // Pin each client to a single CPU core (by slot). Fable TLC has timing/
    // threading bugs across multiple cores (the classic `start /affinity` fix);
    // two clients amplify it. On by default. 0 to leave affinity untouched.
    bool cpuAffinity = true;

private:
    Config();
};
