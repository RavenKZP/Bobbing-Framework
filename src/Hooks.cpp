#include "Hooks.h"
#include "Settings.h"
#include "Manager.h"

namespace Hooks {
    void UpdateHook::Update(RE::Actor* a_this, float a_delta) {
        auto* conf = Config::GetSingleton();
        if (conf->ModActive) {
            auto start = std::chrono::high_resolution_clock::now();
            Bobbing::Manager::GetSingleton()->Update(a_delta);
            if (conf->EnableTimeLogging) {
                auto end = std::chrono::high_resolution_clock::now();
                std::chrono::duration<double, std::milli> elapsed = end - start;
                logger::info("BobbingFramework Update {}ms", elapsed.count());
            }
        }
        Update_(a_this, a_delta);
    }
    
    void DrawHook::thunk(std::uint32_t a_timer) {
        func(a_timer);
        auto* conf = Config::GetSingleton();
        if (conf->ModActive) {
            static auto lastFrameTime = std::chrono::high_resolution_clock::now();
            auto start = std::chrono::high_resolution_clock::now();
            std::chrono::duration<float, std::milli> deltaTime = start - lastFrameTime;
            Bobbing::Manager::GetSingleton()->Update(deltaTime.count() / 1000.0f);
            if (conf->EnableTimeLogging) {
                auto end = std::chrono::high_resolution_clock::now();
                std::chrono::duration<double, std::milli> elapsed = end - start;
                logger::info("BobbingFramework Update {}ms", elapsed.count());
            }
            lastFrameTime = start;
        }
    }

    static void LoadQueue(RE::ObjectRefHandle refHandle) {
        clib_utilsQTR::Tasker::GetSingleton()->PushTask(
            [refHandle]() {
                SKSE::GetTaskInterface()->AddTask([refHandle]() {
                    if (auto ref = refHandle.get().get()) {
                        auto* conf = Config::GetSingleton();
                        auto start = std::chrono::high_resolution_clock::now();
                        if (Bobbing::Manager::GetSingleton()->RefLoad(ref)) {
                            if (conf->EnableTimeLogging) {
                                auto end = std::chrono::high_resolution_clock::now();
                                std::chrono::duration<double, std::milli> elapsed = end - start;
                                logger::info("BobbingFramework RefLoad {} ms", elapsed.count());
                            }
                        } else {
                            logger::debug("Still Loading ref {:08X}", ref->GetFormID());
                            LoadQueue(refHandle);
                        }
                    }
                });
            },
            1000);  // 1s delay to allow the ref to finish loading before attempting to load bobbing data
    }

    RE::NiAVObject* RefLoadHook::Load3D(RE::TESObjectREFR* a_this, bool a_backgroundLoading) {
        static auto* conf = Config::GetSingleton();
        if (conf->ModActive) {
            auto BobbingManager = Bobbing::Manager::GetSingleton();
            auto formID = a_this->GetFormID();
            auto baseID = a_this->GetBaseObject()->GetFormID();
            if (BobbingManager->HasConfig(formID) || BobbingManager->HasConfig(baseID) ||
                BobbingManager->IsPending(formID)) {
                auto thisHandle = a_this->GetHandle();
                logger::debug("Loading ref {:08X}", formID);
                LoadQueue(thisHandle);
            }
        }
        return Load3D_(a_this, a_backgroundLoading);
    }

    RE::NiAVObject* HazardLoadHook::Load3D(RE::TESObjectREFR* a_this, bool a_backgroundLoading) {
        static auto* conf = Config::GetSingleton();
        if (conf->ModActive) {
            auto BobbingManager = Bobbing::Manager::GetSingleton();
            auto formID = a_this->GetFormID();
            auto baseID = a_this->GetBaseObject()->GetFormID();
            if (BobbingManager->HasConfig(formID) || BobbingManager->HasConfig(baseID) ||
                BobbingManager->IsPending(formID)) {
                auto thisHandle = a_this->GetHandle();
                logger::debug("Loading ref {:08X}", formID);
                LoadQueue(thisHandle);
            }
        }
        return Load3D_(a_this, a_backgroundLoading);
    }
}