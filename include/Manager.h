#pragma once
#include "Utils.h"
#include "Settings.h"

#include "REX/REX.h"

#include "ClibUtil/editorID.hpp"
#include "CLibUtilsQTR/Tasker.hpp"
#include "CLibUtilsQTR/DrawDebug.hpp"

#include <nlohmann/json.hpp>
#include <unordered_map>
#include <unordered_set>
#include <shared_mutex>

struct ChildRef {
    RE::ObjectRefHandle refHandle;
    RE::NiPoint3 OffsetPos;
    RE::NiMatrix3 OffsetRot;
    bool dynamic = false;
};

struct TrackedRef {
    RE::ObjectRefHandle refHandle;
    RE::NiPoint3 basePos;
    RE::NiMatrix3 baseRot;
    std::unordered_map<RE::FormID, ChildRef> childrens;
    std::unordered_map<RE::FormID, RE::ActorHandle> Actors;

    RE::NiPoint3 prevWave = {0, 0, 0};
    RE::NiPoint3 prevRot = {0, 0, 0};
    RE::NiPoint3 prevOffset = {0, 0, 0};
};

struct BobbingConfig {
    RE::FormID formID = 0;

    RE::NiPoint3 positionMin = {0.0f, 0.0f, -5.0f};
    RE::NiPoint3 positionMax = {0.0f, 0.0f, 5.0f};

    RE::NiPoint3 rotationMin{-0.01f, -0.01f, -0.01f};
    RE::NiPoint3 rotationMax{0.01f, 0.01f, 0.01f};

    RE::NiPoint3 speedPos = {1.0f, 1.0f, 1.0f};
    RE::NiPoint3 speedRot = {1.0f, 1.0f, 1.0f};

    RE::NiPoint3 BBoxMinOffset{0.0f, 0.0f, 0.0f};
    RE::NiPoint3 BBoxMaxOffset{0.0f, 0.0f, 0.0f};

    float phaseOffset = 0.0f;
    float actorInfluence = 0.0f;

    std::set<RE::FormID> childrens;
    std::string filePath;
};

namespace Bobbing {
    class Manager : public REX::Singleton<Manager> {
    public:
        void Init() {
            LoadAllConfigs("Data\\SKSE\\Plugins\\BobbingFramework");
            logger::info("Loaded {} Bobbing Configs", configs.size());
        }

        bool HasConfig(RE::FormID formID) {
            std::shared_lock lock(configsMutex);
            return configs.contains(formID);
        }

        bool IsPending(RE::FormID formID) {
            std::shared_lock lock(pendingChildrenMutex);
            return pendingChildren.contains(formID);
        }

        BobbingConfig GetConfig(RE::FormID formID) {
            std::shared_lock lock(configsMutex);
            auto it = configs.find(formID);
            if (it != configs.end()) {
                return it->second;
            }
            return {};
        }

        void AddNewConfig(BobbingConfig newConfig, bool save = true) {
            {
                std::unique_lock lock(configsMutex);
                configs[newConfig.formID] = newConfig;
            }
            if (save) {
                SaveConfigToFile(newConfig.formID, newConfig, newConfig.filePath);
            }
        }

        void RemoveConfig(RE::FormID formID) {
            auto config = GetConfig(formID);
            std::string path = config.filePath;
            if (std::filesystem::exists(path)) {
                std::filesystem::remove(path);
                logger::info("Removed Bobbing Config file for {:08X}", formID);
            } else {
                logger::warn("Bobbing Config file for {:08X} not found when trying to remove", formID);
            }
            std::unique_lock lock(configsMutex);
            configs.erase(formID);
        }

        TrackedRef* GetTrackedRef(RE::FormID formID) {
            // std::shared_lock lock(trackedRefsMutex);
            auto it = trackedRefs.find(formID);
            if (it != trackedRefs.end()) {
                return &it->second;
            }
            return nullptr;
        }

        void AddDynamicChild(RE::TESObjectREFR* parentRef, RE::TESObjectREFR* childRef) {
            if (!parentRef) return;

            auto TrackedRef = GetTrackedRef(parentRef->GetFormID());
            if (!TrackedRef) return;

            if (!childRef) return;

            auto childRefHandle = childRef->GetHandle();

            auto childFormID = childRef->GetFormID();

            if (TrackedRef->childrens.contains(childFormID)) {
                return;  // already tracked
            } else {
                if (auto child3D = childRef->Get3D()) {
                    ChildRef DynamicChild;
                    DynamicChild.dynamic = true;
                    DynamicChild.refHandle = childRefHandle;

                    DynamicChild.OffsetPos =
                        TrackedRef->baseRot.Transpose() * (child3D->local.translate - TrackedRef->basePos);

                    RE::NiMatrix3 childRot = child3D->local.rotate;
                    DynamicChild.OffsetRot = TrackedRef->baseRot.Transpose() * childRot;

                    TrackedRef->childrens[childFormID] = DynamicChild;
                }
            }
        }

        void AddActor(RE::TESObjectREFR* parentRef, RE::Actor* actor) {
            if (!parentRef || !actor) return;

            auto TrackedRef = GetTrackedRef(parentRef->GetFormID());
            if (!TrackedRef) return;

            auto actorFormID = actor->GetFormID();

            if (TrackedRef->Actors.contains(actorFormID)) {
                return;  // Actor already tracked
            } else {
                RE::ActorHandle actorHandle = actor->GetHandle();
                TrackedRef->Actors[actorFormID] = actorHandle;
            }
        }

        static void ApplyImpulseToNode(RE::NiAVObject* node, RE::hkVector4 velocity) {
            if (!node) return;
            if (auto* collisionObject = node->GetCollisionObject()) {
                if (auto* rigidBody = collisionObject->body->AsBhkRigidBody()) {
                    rigidBody->SetLinearImpulse(velocity);
                }
            }

            // If it's a NiNode recurse into children
            if (auto* niNode = node->AsNode()) {
                for (auto& child : niNode->GetChildren()) {
                    ApplyImpulseToNode(child.get(), velocity);
                }
            }
        }

        static RE::NiPoint3 UpdateOccupantsAndGetCenter(RE::TESObjectREFR* ref, RE::NiPoint3 minOffset, RE::NiPoint3 maxOffset) {
            auto c = Utils::GetBoundingBox(ref);

            // --- Compute center (OBB) ---
            RE::NiPoint3 center{0, 0, 0};
            for (auto& p : c) center += p;
            center /= 8.0f;

            // --- Compute axes + extents ---
            RE::NiPoint3 axisX = c[1] - c[0];
            float extentX = axisX.Length();
            if (extentX > 0.0f) axisX /= extentX;

            RE::NiPoint3 axisY = c[3] - c[0];
            float extentY = axisY.Length();
            if (extentY > 0.0f) axisY /= extentY;

            RE::NiPoint3 axisZ = c[4] - c[0];
            float extentZ = axisZ.Length();
            if (extentZ > 0.0f) axisZ /= extentZ;

            extentX *= 0.5f;
            extentY *= 0.5f;
            extentZ *= 0.5f;

            // --- OBB test ---
            auto isInsideOBB = [&](const RE::NiPoint3& point) {
                RE::NiPoint3 d = point - center;

                float dx = d.Dot(axisX);
                if (dx > (extentX + maxOffset.x)) return false;
                if (dx < -(extentX + minOffset.x)) return false;

                float dy = d.Dot(axisY);
                if (dy > (extentY + maxOffset.y)) return false;
                if (dy < -(extentY + minOffset.y)) return false;

                float dz = d.Dot(axisZ);
                if (dz > (extentZ + maxOffset.z)) return false;
                if (dz < -(extentZ + minOffset.z)) return false;

                return true;
            };

            // --- Broad phase radius ---
            float radius = 0.0f;
            for (auto& p : c) {
                float dist = (p - center).Length();
                if (dist > radius) radius = dist;
            }

            std::vector<RE::NiPoint3> actorPositions;

            RE::TES::GetSingleton()->ForEachReferenceInRange(center, radius, [&](RE::TESObjectREFR* other) {
                if (!other || other == ref || other->IsDisabled()) {
                    return RE::BSContainer::ForEachResult::kContinue;
                }
                auto pos = other->GetPosition();
                if (!isInsideOBB(pos)) {
                    return RE::BSContainer::ForEachResult::kContinue;
                }

                if (auto otherActor = other->As<RE::Actor>()) {
                    Manager::GetSingleton()->AddActor(ref, otherActor);

                    if (!otherActor->IsGhost()) {
                        actorPositions.push_back(pos);
                    }
                } else { // Not an actor
                    if (other->IsDynamicForm() && !other->IsWater()) {
                        Manager::GetSingleton()->AddDynamicChild(ref, other);
                    }
                }

                if (auto other3D = other->Get3D()) {
                    ApplyImpulseToNode(other3D, {0, 0, 0, 0});
                }

                return RE::BSContainer::ForEachResult::kContinue;
            });

            RE::NiPoint3 returnedCenter = ref->GetPosition();  // fallback
            if (!actorPositions.empty()) {
                RE::NiPoint3 avg{0, 0, 0};
                for (auto& p : actorPositions) avg += p;
                avg /= actorPositions.size();
                returnedCenter = avg;
            }
            return returnedCenter;
        }

        void ResetBobbing() {
            for (auto it = trackedRefs.begin(); it != trackedRefs.end();) {
                auto& parent = it->second;

                auto parentRef = parent.refHandle.get().get();
                if (!parentRef) {
                    it = trackedRefs.erase(it);
                    continue;
                }

                auto parentRefFormID = parentRef->GetFormID();

                auto parentNode = parentRef->Get3D();
                if (!parentNode) {
                    ++it;
                    continue;
                }

                auto cfg = GetConfig(parentRefFormID);
                if (cfg.formID == 0) {  // if no ref config, than base config
                    auto parentBase = parentRef->GetBaseObject();
                    if (parentBase) cfg = GetConfig(parentBase->GetFormID());
                }

                if (cfg.formID == 0) {
                    it = trackedRefs.erase(it);
                    continue;
                }

                // Cache for next frame
                parent.prevWave = {0, 0, 0};

                parentNode->local.translate = parent.basePos;
                parentNode->local.rotate = parent.baseRot;

                RE::NiUpdateData updData;
                updData.flags = RE::NiUpdateData::Flag::kNone;
                updData.time = 0.0f;

                parentNode->UpdateTransformAndBounds(updData);

                parent.prevOffset = {0, 0, 0};
                parent.prevRot = {0, 0, 0};

                // APPLY TO CHILDREN
                for (auto childIt = parent.childrens.begin(); childIt != parent.childrens.end();) {
                    auto& child = childIt->second;
                    auto childRef = child.refHandle.get().get();

                    if (!childRef) {
                        childIt = parent.childrens.erase(childIt);
                        continue;
                    }

                    if (auto childNode = childRef->Get3D()) {
                        RE::NiPoint3 rotatedOffset = parent.baseRot * child.OffsetPos;
                        RE::NiPoint3 childFinalPos = parent.basePos + rotatedOffset;
                        RE::NiMatrix3 childFinalRot = parent.baseRot * child.OffsetRot;

                        childNode->local.translate = childFinalPos;
                        childNode->local.rotate = childFinalRot;
                        childNode->UpdateTransformAndBounds(updData);
                    }
                    ++childIt;
                }
                ++it;
            }
        }

        void OnLoadGame() { time = 0.0f; }

        void OnSaveGame() {
            ResetBobbing();
        }

        void FixInAir(RE::ActorHandle actorHandle) {
            clib_utilsQTR::Tasker::GetSingleton()->PushTask(
                [actorHandle]() {
                    SKSE::GetTaskInterface()->AddTask([actorHandle]() {
                        if (auto Actor = actorHandle.get().get()) {
                            if (RE::bhkCharacterController* controller = Actor->GetCharController()) {
                                if (controller->context.currentState == RE::hkpCharacterStateType::kInAir) {
                                    controller->context.currentState = RE::hkpCharacterStateType::kOnGround;
                                }
                            }
                        }
                    });
                },
                1000);
        }

        void Update(float deltaTime) {
            time += deltaTime;

            std::unique_lock trackedRefsLock(trackedRefsMutex);
            for (auto it = trackedRefs.begin(); it != trackedRefs.end();) {
                auto& parent = it->second;

                auto parentRef = parent.refHandle.get().get();
                if (!parentRef) {
                    it = trackedRefs.erase(it);
                    continue;
                }

                auto parentRefFormID = parentRef->GetFormID();

                auto parentNode = parentRef->Get3D();
                if (!parentNode) {
                    ++it;
                    continue;
                }

                auto cfg = GetConfig(parentRefFormID);
                if (cfg.formID == 0) {  // if no ref config, than base config
                    auto parentBase = parentRef->GetBaseObject();
                    if (parentBase) cfg = GetConfig(parentBase->GetFormID());
                }

                // Should never happen
                if (cfg.formID == 0) {
                    it = trackedRefs.erase(it);
                    continue;
                }

                RE::NiPoint3 zeroPoint3{0.0f, 0.0f, 0.0f};
                // if all speed is 0 or all min max params are 0 skip.
                if ((cfg.speedPos == zeroPoint3 && cfg.speedRot == zeroPoint3) ||
                    (cfg.positionMax == zeroPoint3 && cfg.positionMin == zeroPoint3 && cfg.rotationMax == zeroPoint3 &&
                     cfg.rotationMin == zeroPoint3)) {
                    ++it;
                    continue;
                }

                // WAVE
                float phase = ((parentRefFormID % 10) / 10.0f) + cfg.phaseOffset;

                RE::NiPoint3 posTimes = cfg.speedPos * time;
                RE::NiPoint3 rotTimes = cfg.speedRot * time;

                // movement
                float xOffsetWave = (std::sin(posTimes.x + phase * (2.0f * M_PI)) * 0.5f) + 0.5f;
                float yOffsetWave = (std::sin(posTimes.y + phase * (2.0f * M_PI)) * 0.5f) + 0.5f;
                float zOffsetWave = (std::sin(posTimes.z + phase * (2.0f * M_PI)) * 0.5f) + 0.5f;

                float xOffset = std::lerp(cfg.positionMin.x, cfg.positionMax.x, xOffsetWave);
                float yOffset = std::lerp(cfg.positionMin.y, cfg.positionMax.y, yOffsetWave);
                float zOffset = std::lerp(cfg.positionMin.z, cfg.positionMax.z, zOffsetWave);

                RE::NiPoint3 newParentPos = parent.basePos;
                newParentPos.x += xOffset;
                newParentPos.y += yOffset;
                newParentPos.z += zOffset;


                // rotation
                float xRotWave = (std::sin(rotTimes.x + phase * (2.0f * M_PI)) * 0.3f) + 0.5f;
                float yRotWave = (std::sin(rotTimes.y + phase * (2.0f * M_PI)) * 0.3f) + 0.5f;
                float zRotWave = (std::sin(rotTimes.z + phase * (2.0f * M_PI)) * 0.5f) + 0.5f;

                RE::NiPoint3 centerOfMass = UpdateOccupantsAndGetCenter(parentRef, cfg.BBoxMinOffset, cfg.BBoxMaxOffset);

                auto minAngles = cfg.rotationMin;
                auto maxAngles = cfg.rotationMax;

                if (cfg.actorInfluence > 0.0f) {
                    RE::NiPoint3 parentRefPos = parentRef->GetPosition();
                    if (centerOfMass != parentRefPos) {
                        RE::NiPoint3 delta = centerOfMass - parentRefPos;

                        RE::NiPoint3 ang = parentRef->GetAngle();
                        float yaw = ang.z;

                        float cy = std::sin(yaw);
                        float sy = std::cos(yaw);

                        RE::NiPoint3 forward{cy, sy, 0.0f};
                        RE::NiPoint3 right{-sy, cy, 0.0f};

                        float forwardOffset = delta.Dot(forward);
                        float rightOffset = delta.Dot(right);

                        float actorInfluence = cfg.actorInfluence * 0.01f;

                        float pitchBias = rightOffset * actorInfluence;
                        float rollBias = forwardOffset * actorInfluence;

                        xRotWave = std::clamp(xRotWave + rollBias, 0.0f, 1.0f);
                        yRotWave = std::clamp(yRotWave + pitchBias, 0.0f, 1.0f);

                        minAngles.x *= 2;
                        maxAngles.x *= 2;
                        minAngles.y *= 2;
                        maxAngles.y *= 2;
                    }
                }

                auto previousWave = parent.prevWave;

                auto SmoothWave = [&](float current, float previous, float speed) {
                    float alpha = 1.0f - std::exp(-2.0f * deltaTime * speed);
                    return std::lerp(previous, current, alpha);
                };

                xRotWave = SmoothWave(xRotWave, previousWave.x, cfg.speedRot.x);
                yRotWave = SmoothWave(yRotWave, previousWave.y, cfg.speedRot.y);
                zRotWave = SmoothWave(zRotWave, previousWave.z, cfg.speedRot.z);

                // Cache for next frame
                parent.prevWave = {xRotWave, yRotWave, zRotWave};

                // rotation
                RE::NiPoint3 rot;
                rot.x = std::lerp(minAngles.x, maxAngles.x, xRotWave);
                rot.y = std::lerp(minAngles.y, maxAngles.y, yRotWave);
                rot.z = std::lerp(minAngles.z, maxAngles.z, zRotWave);

                RE::NiMatrix3 RotMatrix;
                RotMatrix.EulerAnglesToAxesZXY(rot);
                RE::NiMatrix3 newParentRot = parent.baseRot * RotMatrix;

                parentNode->local.translate = newParentPos;
                parentNode->local.rotate = newParentRot;

                RE::NiUpdateData updData;
                updData.flags = RE::NiUpdateData::Flag::kNone;
                updData.time = 0.0f;

                parentNode->UpdateTransformAndBounds(updData);
               
                // APPLY TO ACTORS
                RE::NiPoint3 parentRefPos = parentRef->GetPosition();
                RE::NiPoint3 parentRefAngles = parentRef->GetAngle();

                RE::NiPoint3 oldRefPos =
                    parentRefPos + RE::NiPoint3(parent.prevOffset.x, parent.prevOffset.y, parent.prevOffset.z);
                RE::NiPoint3 oldRefAngles = parentRefAngles + parent.prevRot;

                RE::NiMatrix3 oldRefRot;
                oldRefRot.EulerAnglesToAxesZXY(oldRefAngles);

                RE::NiPoint3 newRefPos = parentRefPos + RE::NiPoint3(xOffset, yOffset, zOffset);
                RE::NiPoint3 newRefAngles = parentRefAngles + rot;

                RE::NiMatrix3 newRefRot;
                newRefRot.EulerAnglesToAxesZXY(newRefAngles);

                for (auto ActorIt = parent.Actors.begin(); ActorIt != parent.Actors.end();) {
                    auto& actorHandle = ActorIt->second;
                    RE::Actor* Actor = actorHandle.get().get();

                    if (!Actor) {
                        ++ActorIt;
                        continue;
                    }
                    if (Actor->IsInJumpState()) {
                        ++ActorIt;
                        continue;
                    }

                    RE::NiPoint3 actorWorldPos = Actor->GetPosition();
                    RE::NiPoint3 localOffset = oldRefRot.Transpose() * (actorWorldPos - oldRefPos);
                    RE::NiPoint3 newActorPos = newRefPos + (newRefRot * localOffset);

                    // Actors should never be rotated in X and Y, only Z (yaw) is allowed
                    RE::NiPoint3 newActorRot = Actor->GetAngle();
                    float deltaYaw = rot.z - parent.prevRot.z;
                    newActorRot.z += deltaYaw;

                    if (RE::bhkCharacterController* controller = Actor->GetCharController()) {
                        RE::hkVector4 LinearVelocity;
                        controller->GetLinearVelocityImpl(LinearVelocity);
                        auto currentState = controller->context.currentState;
                        auto currentSwimmingState = Actor->actorState1.swimming;
                        Actor->SetPosition(newActorPos, true);
                        Actor->SetAngle(newActorRot);

                        // Forward the volicity, so actor keeps moving in the same direction
                        controller->SetLinearVelocityImpl(LinearVelocity);
                        // Forward the sates
                        // SetPosition(newActorPos, true), is setting the state to kOnAir and swimming
                        controller->context.currentState = currentState;

                        if (currentState == RE::hkpCharacterStateType::kInAir) {
                            FixInAir(Actor->GetHandle());
                        }
                        Actor->actorState1.swimming = currentSwimmingState;
                    }

                    ++ActorIt;
                }

                parent.prevOffset = {xOffset, yOffset, zOffset};
                parent.prevRot = rot;
                parent.Actors.clear();

                // APPLY TO CHILDREN
                for (auto childIt = parent.childrens.begin(); childIt != parent.childrens.end();) {
                    auto& child = childIt->second;
                    auto childRef = child.refHandle.get().get();

                    if (!childRef) {
                        if (!child.dynamic) {
                            std::unique_lock pendingChildrenLock(pendingChildrenMutex);
                            pendingChildren[childIt->first] = parentRef->GetFormID();
                        }
                        childIt = parent.childrens.erase(childIt);
                        continue;
                    }

                    if (auto childNode = childRef->Get3D()) {
                        RE::NiPoint3 rotatedOffset = newParentRot * child.OffsetPos;
                        RE::NiPoint3 childFinalPos = newParentPos + rotatedOffset;
                        RE::NiMatrix3 childFinalRot = newParentRot * child.OffsetRot;

                        // APPLY
                        childNode->local.translate = childFinalPos;
                        childNode->local.rotate = childFinalRot;
                        childNode->UpdateTransformAndBounds(updData);
                    }
                    ++childIt;
                }
                ++it;
            }
        }

        static void MakeRefColisionDynamic(RE::TESObjectREFR* a_ref) {
            a_ref->SetMotionType(RE::hkpMotion::MotionType::kKeyframed, true);
        }

        bool RefLoad(RE::TESObjectREFR* ref) {
            if (ref) {
                return RefLoad(ref, ref->Get3D());
            }
            return false;
        }

        bool RefLoad(RE::TESObjectREFR* ref, RE::NiAVObject* ref3D) {
            if (!ref || !ref3D) return false;

            if (ref->IsDisabled() || ref->IsDeleted()) return false;

            auto baseObj = ref->GetBaseObject();

            auto* conf = Config::GetSingleton();
            if (conf->ModActive) {
                auto baseFormID = baseObj->GetFormID();
                auto formID = ref->GetFormID();

                bool hasBaseConfig = HasConfig(baseFormID);
                bool hasRefConfig = HasConfig(formID);

                // 1. Check if this ref is pending child of some parent, if yes - attach to parent

                std::unique_lock trackedRefsLock(trackedRefsMutex);
                std::unique_lock pendingChildrenLock(pendingChildrenMutex);

                auto itPending = pendingChildren.find(formID);
                if (itPending != pendingChildren.end()) {
                    RE::FormID parentID = itPending->second;

                    auto parentIt = trackedRefs.find(parentID);
                    if (parentIt != trackedRefs.end()) {
                        ChildRef childRefData;
                        childRefData.refHandle = ref->GetHandle();
                        // Pending
                        childRefData.OffsetPos =
                            parentIt->second.baseRot.Transpose() * (ref3D->local.translate - parentIt->second.basePos);
                        RE::NiMatrix3 childRot = ref3D->local.rotate;
                        childRefData.OffsetRot = parentIt->second.baseRot.Transpose() * childRot;
                        parentIt->second.childrens[formID] = childRefData;

                        MakeRefColisionDynamic(ref);

                        logger::debug("Attached pending child {:08X} to parent {:08X}", formID, parentID);
                    }

                    pendingChildren.erase(itPending);
                }
                pendingChildrenLock.unlock();
                trackedRefsLock.unlock();

                // 2. If this ref has config - track it
                if (hasBaseConfig || hasRefConfig) {
                    TrackedRef refData;
                    refData.refHandle = ref->GetHandle();
                    refData.basePos = ref3D->local.translate;
                    refData.baseRot = ref3D->local.rotate;

                    MakeRefColisionDynamic(ref);

                    logger::debug("Tracking ref {:08X} with base {:08X}", formID, baseFormID);

                    // Load children (only RefConfig can have children)
                    if (hasRefConfig) {
                        auto config = GetConfig(formID);
                        for (auto childID : config.childrens) {
                            auto form = RE::TESForm::LookupByID(childID);
                            auto childRef = form ? form->As<RE::TESObjectREFR>() : nullptr;

                            if (childRef) {
                                auto child3D = childRef->Get3D();
                                if (!child3D) {
                                    pendingChildrenLock.lock();
                                    pendingChildren[childID] = formID;
                                    pendingChildrenLock.unlock();
                                    logger::debug("Child {:08X} of parent {:08X} is not loaded yet, added to pending",
                                                  childID, formID);
                                    continue;
                                }
                                ChildRef childRefData;
                                childRefData.refHandle = childRef->GetHandle();

                                childRefData.OffsetPos =
                                    refData.baseRot.Transpose() * (child3D->local.translate - refData.basePos);
                                RE::NiMatrix3 childRot = child3D->local.rotate;
                                childRefData.OffsetRot = refData.baseRot.Transpose() * childRot;
                                refData.childrens[childRef->GetFormID()] = childRefData;

                                MakeRefColisionDynamic(childRef);

                                logger::debug("Attached child {:08X} to parent {:08X}", childID, formID);
                            } else {
                                pendingChildrenLock.lock();
                                pendingChildren[childID] = formID;
                                pendingChildrenLock.unlock();
                                logger::debug("Child {:08X} of parent {:08X} is not loaded yet, added to pending",
                                              childID, formID);
                            }
                        }
                    }

                    trackedRefs[formID] = refData;
                }
            }
            return true;
        }

    private:
        void LoadAllConfigs(const std::string& folder) {
            logger::info("Loading All Configs");
            if (!std::filesystem::exists(folder) || !std::filesystem::is_directory(folder)) {
                logger::error("{} not exists or is not directory", folder);
                return;
            }

            for (const auto& entry : std::filesystem::directory_iterator(folder)) {
                if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;

                logger::info("Reading {}", entry.path().string());

                nlohmann::json j;
                std::ifstream file(entry.path());
                if (!file.is_open()) {
                    logger::error("Failed to open file: {}", entry.path().string());
                    continue;
                }
                file >> j;

                if (!j.contains("FormID")) {
                    logger::error("Invalid config format in file {}", entry.path().string());
                    continue;
                }

                RE::FormID formID = Utils::ParseForm(j["FormID"].get<std::string>());
                if (formID == 0) {
                    logger::error("Failed to parse FormID {} '{}'", j["FormID"].get<std::string>(), formID);
                    continue;
                }

                BobbingConfig cfg;
                cfg.formID = formID;
                cfg.filePath = entry.path().string();

                // presence flags - used for merge logic
                bool containsMinPos = false;
                bool containsMaxPos = false;
                bool containsMinRot = false;
                bool containsMaxRot = false;
                bool containsSpeed = false;
                bool containsPhaseOffset = false;
                bool containsActorInfluence = false;
                bool containsBoundingBox = false;

                // Backward Compatibility for old config files
                // --- Z movement ---
                if (j.contains("minZ")) {
                    containsMinPos = true;
                    cfg.positionMin.z = j["minZ"].get<float>();
                }
                if (j.contains("maxZ")) {
                    containsMaxPos = true;
                    cfg.positionMax.z = j["maxZ"].get<float>();
                }
                // --- Rotation ---
                if (j.contains("minRot") && j["minRot"].is_array() && j["minRot"].size() == 3) {
                    containsMinRot = true;
                    cfg.rotationMin.x = j["minRot"][0].get<float>();
                    cfg.rotationMin.y = j["minRot"][1].get<float>();
                    cfg.rotationMin.z = j["minRot"][2].get<float>();
                }
                if (j.contains("maxRot") && j["maxRot"].is_array() && j["maxRot"].size() == 3) {
                    containsMaxRot = true;
                    cfg.rotationMax.x = j["maxRot"][0].get<float>();
                    cfg.rotationMax.y = j["maxRot"][1].get<float>();
                    cfg.rotationMax.z = j["maxRot"][2].get<float>();
                }
                if (j.contains("speed")) {
                    containsSpeed = true;
                    float speed = j["speed"].get<float>();
                    cfg.speedPos.x = speed;
                    cfg.speedPos.y = speed;
                    cfg.speedPos.z = speed;
                    cfg.speedRot.x = speed;
                    cfg.speedRot.y = speed;
                    cfg.speedRot.z = speed;
                }

                // New config files
                if (j.contains("positionMin")) {
                    containsMinPos = true;
                    cfg.positionMin.x = j["positionMin"][0].get<float>();
                    cfg.positionMin.y = j["positionMin"][1].get<float>();
                    cfg.positionMin.z = j["positionMin"][2].get<float>();
                }
                if (j.contains("positionMax")) {
                    containsMaxPos = true;
                    cfg.positionMax.x = j["positionMax"][0].get<float>();
                    cfg.positionMax.y = j["positionMax"][1].get<float>();
                    cfg.positionMax.z = j["positionMax"][2].get<float>();
                }
                if (j.contains("rotationMin")) {
                    containsMinRot = true;
                    cfg.rotationMin.x = j["rotationMin"][0].get<float>();
                    cfg.rotationMin.y = j["rotationMin"][1].get<float>();
                    cfg.rotationMin.z = j["rotationMin"][2].get<float>();
                }
                if (j.contains("rotationMax")) {
                    containsMaxRot = true;
                    cfg.rotationMax.x = j["rotationMax"][0].get<float>();
                    cfg.rotationMax.y = j["rotationMax"][1].get<float>();
                    cfg.rotationMax.z = j["rotationMax"][2].get<float>();
                }

                if (j.contains("speedPos")) {
                    containsSpeed = true;
                    cfg.speedPos.x = j["speedPos"][0].get<float>();
                    cfg.speedPos.y = j["speedPos"][1].get<float>();
                    cfg.speedPos.z = j["speedPos"][2].get<float>();
                }
                if (j.contains("speedRot")) {
                    containsSpeed = true;
                    cfg.speedRot.x = j["speedRot"][0].get<float>();
                    cfg.speedRot.y = j["speedRot"][1].get<float>();
                    cfg.speedRot.z = j["speedRot"][2].get<float>();
                }

                if (j.contains("bBoxMinOffset")) {
                    containsBoundingBox = true;
                    cfg.BBoxMinOffset.x = j["bBoxMinOffset"][0].get<float>();
                    cfg.BBoxMinOffset.y = j["bBoxMinOffset"][1].get<float>();
                    cfg.BBoxMinOffset.z = j["bBoxMinOffset"][2].get<float>();
                }
                if (j.contains("bBoxMaxOffset")) {
                    containsBoundingBox = true;
                    cfg.BBoxMaxOffset.x = j["bBoxMaxOffset"][0].get<float>();
                    cfg.BBoxMaxOffset.y = j["bBoxMaxOffset"][1].get<float>();
                    cfg.BBoxMaxOffset.z = j["bBoxMaxOffset"][2].get<float>();
                }

                if (j.contains("phaseOffset")) {
                    containsPhaseOffset = true;
                    cfg.phaseOffset = j["phaseOffset"].get<float>();
                }

                if (j.contains("actorInfluence")) {
                    containsActorInfluence = true;
                    cfg.actorInfluence = j["actorInfluence"].get<float>();
                }

                for (const auto& child : j["children"]) {
                    if (!child.is_string()) {
                        logger::error("Invalid child entry in file {}: expected string", entry.path().string());
                        continue;
                    }
                    RE::FormID childFormID = Utils::ParseForm(child.get<std::string>());
                    if (childFormID == 0) {
                        logger::error("Failed to parse FormID {} in file {}", child.get<std::string>(),
                                      entry.path().string());
                        continue;
                    }
                    cfg.childrens.insert(childFormID);
                }

                // Merge or insert
                {
                    std::unique_lock lock(configsMutex);
                    auto it = configs.find(formID);
                    if (it == configs.end()) {
                        // new
                        configs[formID] = cfg;
                        logger::info("Config for {:08X} loaded", formID);
                    } else {
                        // merge
                        BobbingConfig& existing = it->second;

                        // children: union
                        for (auto cid : cfg.childrens) existing.childrens.insert(cid);

                        // overwrite existing
                        if (containsMinPos) existing.positionMin = cfg.positionMin;
                        if (containsMaxPos) existing.positionMax = cfg.positionMax;
                        if (containsMinRot) existing.rotationMin = cfg.rotationMin;
                        if (containsMaxRot) existing.rotationMax = cfg.rotationMax;
                        if (containsSpeed) {
                            existing.speedPos = cfg.speedPos;
                            existing.speedRot = cfg.speedRot;
                        }
                        if (containsBoundingBox) {
                            existing.BBoxMinOffset = cfg.BBoxMinOffset;
                            existing.BBoxMaxOffset = cfg.BBoxMaxOffset;
                        }
                        if (containsPhaseOffset) existing.phaseOffset = cfg.phaseOffset;
                        if (containsActorInfluence) existing.actorInfluence = cfg.actorInfluence;

                        existing.filePath = entry.path().string();
                        logger::info("Config for {:08X} updated", formID);
                    }
                }
            }
        }

        void SaveConfigToFile(const RE::FormID formID, BobbingConfig& cfg, std::string filename) {
            std::filesystem::create_directories("Data\\SKSE\\Plugins\\BobbingFramework");
            std::string path;
            if (filename.empty()) {
                path = std::format("Data\\SKSE\\Plugins\\BobbingFramework\\{:08X}.json", formID);
            } else {
                path = filename;
            }

            logger::info("Saving Config for {:08X}", formID);

            nlohmann::json j;
            j["FormID"] = Utils::FormIDToString(formID);

            BobbingConfig defaultCfg;

            if (cfg.positionMin != defaultCfg.positionMin) {
                j["positionMin"] = {cfg.positionMin.x, cfg.positionMin.y, cfg.positionMin.z};
            }
            if (cfg.positionMax != defaultCfg.positionMax) {
                j["positionMax"] = {cfg.positionMax.x, cfg.positionMax.y, cfg.positionMax.z};
            }
            if (cfg.rotationMin != defaultCfg.rotationMin) {
                j["rotationMin"] = {cfg.rotationMin.x, cfg.rotationMin.y, cfg.rotationMin.z};
            }
            if (cfg.rotationMax != defaultCfg.rotationMax) {
                j["rotationMax"] = {cfg.rotationMax.x, cfg.rotationMax.y, cfg.rotationMax.z};
            }

            if (cfg.BBoxMinOffset != defaultCfg.BBoxMinOffset) {
                j["bBoxMinOffset"] = {cfg.BBoxMinOffset.x, cfg.BBoxMinOffset.y, cfg.BBoxMinOffset.z};
            }
            if (cfg.BBoxMaxOffset != defaultCfg.BBoxMaxOffset) {
                j["bBoxMaxOffset"] = {cfg.BBoxMaxOffset.x, cfg.BBoxMaxOffset.y, cfg.BBoxMaxOffset.z};
            }

            if (cfg.speedPos != defaultCfg.speedPos) {
                j["speedPos"] = {cfg.speedPos.x, cfg.speedPos.y, cfg.speedPos.z};
            }
            if (cfg.speedRot != defaultCfg.speedRot) {
                j["speedRot"] = {cfg.speedRot.x, cfg.speedRot.y, cfg.speedRot.z};
            }

            if (cfg.phaseOffset != defaultCfg.phaseOffset) {
                j["phaseOffset"] = cfg.phaseOffset;
            }
            if (cfg.actorInfluence != defaultCfg.actorInfluence) {
                j["actorInfluence"] = cfg.actorInfluence;
            }

            if (!cfg.childrens.empty()) {
                j["children"] = nlohmann::json::array();
                for (const auto& entry : cfg.childrens) {
                    j["children"].push_back(Utils::FormIDToString(entry));
                }
            }

            std::ofstream file(path);
            if (!file.is_open()) {
                logger::error("Failed to open file for writing: {}", path);
                return;
            }
            file << j.dump(4);

            cfg.filePath = path;
        }

        std::shared_mutex trackedRefsMutex;
        std::unordered_map<RE::FormID, TrackedRef> trackedRefs;
        std::shared_mutex configsMutex;
        std::unordered_map<RE::FormID, BobbingConfig> configs;

        std::shared_mutex pendingChildrenMutex;
        std::unordered_map<RE::FormID, RE::FormID> pendingChildren;  // child formID -> parent formID

        float time{0.0f};
    };
}