#include "Hooks.h"
#include "Settings.h"
#include "Manager.h"

namespace Hooks {

    void UpdateHook::Update(RE::Actor* a_this, float a_delta) {

        Update_(a_this, a_delta);

        auto* conf = Config::GetSingleton();
        if (conf->ModActive) {
            if (REL::Module::IsVR()) {
                auto start = std::chrono::high_resolution_clock::now();
                Bobbing::Manager::GetSingleton()->Update(a_delta);
                if (conf->EnableTimeLogging) {
                    auto end = std::chrono::high_resolution_clock::now();
                    std::chrono::duration<double, std::milli> elapsed = end - start;
                    logger::info("BobbingFramework Update {}ms", elapsed.count());
                }
            } else {
                auto start = std::chrono::high_resolution_clock::now();
                if (Bobbing::Manager::GetSingleton()->IsPlayerOnboard()) {
                    if (RE::bhkCharacterController* controller = a_this->GetCharController()) {
                        auto currentState = controller->context.currentState;
                        if (currentState == RE::hkpCharacterStateType::kJumping || a_this->IsInJumpState()) {
                            return;
                        }
                        if (currentState == RE::hkpCharacterStateType::kInAir) {
                            auto actor3d = GetActor3d(a_this);

                            const auto evaluator = [actor3d](RE::NiAVObject* mesh) {
                                if (mesh == actor3d) {
                                    return false;
                                }
                                return true;
                            };

                            auto result = RayCast::Cast(a_this, evaluator);
                            if (result.object) {
                                auto height = a_this->GetPositionZ() - result.position.z;
                                logger::debug("Actor {}, standing on {:08X}, above {}", a_this->GetName(),
                                              result.object->GetFormID(), height);
                                if (height < 50) {
                                    controller->context.currentState = RE::hkpCharacterStateType::kOnGround;
                                }
                            } else {
                                logger::debug("Actor {}, RayCast didn't hit anything", a_this->GetName());
                            }
                        }
                    }
                }
                if (conf->EnableTimeLogging) {
                    auto end = std::chrono::high_resolution_clock::now();
                    std::chrono::duration<double, std::milli> elapsed = end - start;
                    logger::info("BobbingFramework PlayerUpdate {}ms", elapsed.count());
                }
            }
        }
    }

    void DrawHook::thunk(std::uint32_t a_timer) {
        func(a_timer);
        auto* conf = Config::GetSingleton();
        if (conf->ModActive) {

            static auto lastFrameTime = std::chrono::high_resolution_clock::now();
            auto FrameTime = std::chrono::high_resolution_clock::now();

            if (!RE::UI::GetSingleton()->GameIsPaused()) {
                std::chrono::duration<float, std::milli> deltaTime = FrameTime - lastFrameTime;
                Bobbing::Manager::GetSingleton()->Update(deltaTime.count() / 1000.0f);
                if (conf->EnableTimeLogging) {
                    auto end = std::chrono::high_resolution_clock::now();
                    std::chrono::duration<double, std::milli> elapsed = end - FrameTime;
                    logger::info("BobbingFramework Update {}ms", elapsed.count());
                }
            }
            lastFrameTime = FrameTime;
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