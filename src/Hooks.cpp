#include "Hooks.h"
#include "Settings.h"
#include "Manager.h"

namespace Hooks {

    void DrawHook::thunk(std::uint32_t a_timer) {
        func(a_timer);
        auto* conf = Config::GetSingleton();
        if (conf->ModActive) {
            auto* calendar = RE::Calendar::GetSingleton();
            if (!calendar) {
                return;
            }
            static auto lastGameMinutes = 0.0f;
            auto GameMinutes = calendar->GetMinutes();

            static auto lastFrameTime = std::chrono::high_resolution_clock::now();
            auto FrameTime = std::chrono::high_resolution_clock::now();

            if (GameMinutes != lastGameMinutes) {
                std::chrono::duration<float, std::milli> deltaTime = FrameTime - lastFrameTime;
                Bobbing::Manager::GetSingleton()->Update(deltaTime.count() / 1000.0f);
                if (conf->EnableTimeLogging) {
                    auto end = std::chrono::high_resolution_clock::now();
                    std::chrono::duration<double, std::milli> elapsed = end - FrameTime;
                    logger::info("BobbingFramework Update {}ms", elapsed.count());
                }
            }
            lastFrameTime = FrameTime;
            lastGameMinutes = GameMinutes;
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