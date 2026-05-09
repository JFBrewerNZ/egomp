#pragma once

#include <map>
#include <functional>
#include <iostream>

#include "../Utils/Hook.h"

#include "World.h"
#include "PlayerManager.h"

class CMainGameComponent
{
public:
    static CMainGameComponent* Get();

    CPlayerManager* GetPlayerManager();
    CWorld* GetWorld();

    void AddPostInitCallback(const std::string& id, std::function<void()> callback) { postInitCallbacks[id] = callback; }
    void RemovePostInitCallback(const std::string& id) { postInitCallbacks.erase(id); }

    void AddUpdateCallback(const std::string& id, std::function<void()> callback) { updateCallbacks[id] = callback; }
    void RemoveUpdateCallback(const std::string& id) { updateCallbacks.erase(id); }

    static void Hook();

private:
    static std::map<std::string, std::function<void()>> postInitCallbacks;
    static std::map<std::string, std::function<void()>> updateCallbacks;

    static void(__thiscall* OInit)(CMainGameComponent*);
    static void __fastcall HInit(CMainGameComponent* _this, void* _EDX);

    static void(__thiscall* OPostInit)(CMainGameComponent*);
    static void __fastcall HPostInit(CMainGameComponent* _this, void* _EDX);

    static void(__thiscall* OUpdate)(CMainGameComponent*);
    static void __fastcall HUpdate(CMainGameComponent* _this, void* _EDX);
};
