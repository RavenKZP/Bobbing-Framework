#include "logger.h"
#include "Hooks.h"
#include "MCP.h"
#include "Settings.h"
#include "Manager.h"

void OnMessage(SKSE::MessagingInterface::Message* message) {
    if (message->type == SKSE::MessagingInterface::kDataLoaded) {
        MCP::Register();
        Bobbing::Manager::GetSingleton(); // init
    }
}

SKSEPluginLoad(const SKSE::LoadInterface *skse) {

    SetupLog();
    logger::info("Plugin loaded");
    SKSE::Init(skse);
    Hooks::InstallHooks();
    SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
    return true;
}
