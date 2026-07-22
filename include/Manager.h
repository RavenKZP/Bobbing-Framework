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
    bool processed = false;
};

struct TrackedRef {
    RE::ObjectRefHandle refHandle;
    RE::NiPoint3 basePos;
    RE::NiMatrix3 baseRot;
    std::unordered_map<RE::FormID, ChildRef> childrens;
    std::unordered_map<RE::FormID, RE::ActorHandle> Actors;

    RE::NiPoint3 prevWave = {0, 0, 0};
};

struct BobbingConfig {
    RE::FormID formID = 0;

    float minZ = -5.0f;
    float maxZ = 5.0f;

    RE::NiPoint3 minRot{-0.01f, -0.01f, -0.01f};
    RE::NiPoint3 maxRot{0.01f, 0.01f, 0.01f};

    float speed = 1.0f;
    float phaseOffset = 0.0f;
    float actorInfluence = 0.0f;

    std::set<RE::FormID> childrens;
    std::string filePath;
};

namespace Bobbing {
    class Manager : public REX::Singleton<Manager> {
    public:
        Manager() {
            LoadAllConfigs("Data\\SKSE\\Plugins\\BobbingFramework");
            logger::info("Loaded {} Bobbing Configs", configs.size());
        }

        bool HasConfig(RE::FormID formID) { 
            std::shared_lock lock(configsMutex);
            return configs.contains(formID); }

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

        void AddNewConfig(RE::FormID formID, float minZ, float maxZ, RE::NiPoint3 minRot, RE::NiPoint3 maxRot,
                          float speed, float phaseOffset, float actorInfluence, std::set<RE::FormID> childrens, std::string filename,
                          bool save = true) {
            BobbingConfig cfg = {formID, minZ, maxZ, minRot, maxRot, speed, phaseOffset, actorInfluence, childrens};
            {
                std::unique_lock lock(configsMutex);
                configs[cfg.formID] = cfg;
            }
            if (save) {
                SaveConfigToFile(cfg.formID, cfg, filename);
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
            //std::shared_lock lock(trackedRefsMutex);
            auto it = trackedRefs.find(formID);
            if (it != trackedRefs.end()) {
                return &it->second;
            }
            return nullptr;
        }

        void AddDynamicFurniture(RE::TESObjectREFR* parentRef, RE::ObjectRefHandle furnitureRefHandle) {
            if (!parentRef) return;

            auto TrackedRef = GetTrackedRef(parentRef->GetFormID());
            if (!TrackedRef) return;

            auto furnitureRef = furnitureRefHandle.get().get();
            if (!furnitureRef) return;

            auto furnitureFormID = furnitureRef->GetFormID();

            if (TrackedRef->childrens.contains(furnitureFormID)) {
                return;  // already tracked
            } else {
                if (auto furn3D = furnitureRef->Get3D()) {
                    ChildRef DynamicFurniture;
                    DynamicFurniture.dynamic = true;
                    DynamicFurniture.refHandle = furnitureRefHandle;

                    DynamicFurniture.OffsetPos =
                        TrackedRef->baseRot.Transpose() * (furn3D->world.translate - TrackedRef->basePos);

                    RE::NiMatrix3 childRot = furn3D->world.rotate;
                    DynamicFurniture.OffsetRot = TrackedRef->baseRot.Transpose() * childRot;

                    TrackedRef->childrens[furnitureFormID] = DynamicFurniture;
                }
            }
        }

        void AddActor(RE::TESObjectREFR* parentRef, RE::Actor* actor) {
            if (!parentRef || !actor) return;

            auto TrackedRef = GetTrackedRef(parentRef->GetFormID());
            if (!TrackedRef) return;

            auto actorFormID = actor->GetFormID();

            if (TrackedRef->Actors.contains(actorFormID)) {
                return; // Actor already tracked
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

        static RE::NiPoint3 UpdateOccupantsAndGetCenter(RE::TESObjectREFR* ref) {

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
                if (dx > (extentX)) return false;
                if (dx < -(extentX)) return false;

                float dy = d.Dot(axisY);
                if (dy > (extentY)) return false;
                if (dy < -(extentY)) return false;

                float dz = d.Dot(axisZ);
                if (dz > (extentZ)) return false;
                if (dz < -(extentZ)) return false;

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
                    if (!otherActor->IsInJumpState() || !otherActor->IsGhost() || !otherActor->IsFlying() ||
                        !otherActor->IsInMidair() || !otherActor->IsSwimming()) {
                        actorPositions.push_back(pos);
                    }

                    Manager::GetSingleton()->AddActor(ref, otherActor);

                    /*
                    auto occupiedFurnitureHandle = otherActor->GetOccupiedFurniture();
                    if (auto occupiedFurniture = occupiedFurnitureHandle.get().get()) {

                        // Is this even working?!
                        //auto SitSleepState = otherActor->GetSitSleepState();
                        //logger::info("Actor {} SitState = {}", otherActor->GetName(), static_cast<int>(SitSleepState));
                        //if (SitSleepState == RE::SIT_SLEEP_STATE::kIsSitting ||
                        //    SitSleepState == RE::SIT_SLEEP_STATE::kIsSleeping) {

                            if (auto childNode = occupiedFurniture->Get3D()) {
                                DebugAPI_IMPL::DebugAPI::GetSingleton()->DrawLineForMS(childNode->world.translate,
                                                                                       otherActor->GetPosition());
                                DebugAPI_IMPL::DebugAPI::GetSingleton()->Update();
                                auto distance = childNode->world.translate.GetDistance(otherActor->GetPosition());
                                auto sizes = occupiedFurniture->GetBoundMax() - occupiedFurniture->GetBoundMin();
                                float treshold = std::max(sizes.x, sizes.y) / 2;
                                logger::debug("Actor {} distance to furniture {} treshold {}", otherActor->GetName(),
                                              distance, treshold);
                                if (distance < treshold) {
                                    otherActor->SetPosition(childNode->world.translate, true);
                                }
                            }
                        //}
                        if (occupiedFurniture->IsDynamicForm()) {
                            Manager::GetSingleton()->AddDynamicFurniture(ref, occupiedFurnitureHandle);
                        }
                    }
                    */
                    return RE::BSContainer::ForEachResult::kContinue;
                }

                if (auto other3D = other->Get3D()) {
                    // other->AddChange(RE::TESObjectREFR::ChangeFlags::kHavokMoved);
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

        void Update(float deltaTime) {
            static float time = 0.0f;
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

                // if speed is 0 or all min max params are 0 skip.
                if (cfg.speed == 0.0f ||
                    (cfg.minZ == 0.0f && cfg.maxZ == 0.0f && cfg.minRot.x == 0.0f && cfg.minRot.y == 0.0f &&
                     cfg.minRot.z == 0.0f && cfg.maxRot.x == 0.0f && cfg.maxRot.y == 0.0f && cfg.maxRot.z == 0.0f)) {
                    ++it;
                    continue;
                }

                float refTime = time * cfg.speed;

                // WAVE
                float phase = ((parentRefFormID % 10) / 10.0f) + cfg.phaseOffset;
                float zOffsetWave = (std::sin(refTime + phase * (2.0f * M_PI)) * 0.5f) + 0.5f;

                // Z movement
                float zOffset = std::lerp(cfg.minZ, cfg.maxZ, zOffsetWave);
                RE::NiPoint3 newParentPos = parent.basePos;
                newParentPos.z += zOffset;

                float xRotWave = (std::cos(refTime + phase * (2.0f * M_PI)) * 0.3f) + 0.5f;
                float yRotWave = (std::cos(refTime + phase * (2.0f * M_PI)) * 0.3f) + 0.5f;

                float zRotWave = (std::sin(refTime + phase * (2.0f * M_PI)) * 0.5f) + 0.5f;

                RE::NiPoint3 centerOfMass = UpdateOccupantsAndGetCenter(parentRef);

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
                    }
                }

                auto previousWave = parent.prevWave;

                auto SmoothWave = [&](float current, float previous) {
                    if (std::abs(previous - current) < 0.001f) return current;
                    float alpha = 1.0f - std::exp(-5.0f * deltaTime * cfg.speed);
                    return std::lerp(previous, current, alpha);
                };

                xRotWave = SmoothWave(xRotWave, previousWave.x);
                yRotWave = SmoothWave(yRotWave, previousWave.y);
                zRotWave = SmoothWave(zRotWave, previousWave.z);

                // Cache for next frame
                parent.prevWave = {xRotWave, yRotWave, zRotWave};

                // rotation
                RE::NiPoint3 rot;
                rot.x = std::lerp(cfg.minRot.x, cfg.maxRot.x, xRotWave);
                rot.y = std::lerp(cfg.minRot.y, cfg.maxRot.y, yRotWave);
                rot.z = std::lerp(cfg.minRot.z, cfg.maxRot.z, zRotWave);

                RE::NiMatrix3 RotMatrix;
                RotMatrix.EulerAnglesToAxesZXY(rot);
                RE::NiMatrix3 newParentRot = parent.baseRot * RotMatrix;

                // Option 1: Works but collision isnt updated
                // changing ref position is not good because it can change ref's cell and cause all sorts of issues
                // plus its serialized
                // parentRef->SetPosition(newParentPos);
                // parentRef->SetAngle(newParentRot);

                // Option 2: Object is invisable and random CTD, probably to spaming the MoveTo_Impl
                // auto handle = parentRef->GetHandle();
                // parentRef->MoveTo_Impl(handle, ref->GetParentCell(), ref->GetWorldspace(), newParentPos,
                // newParentRot); if (auto refptr = handle.get()) {
                //    if (auto ref = refptr.get()) {
                //        if (auto a3d = ref->Load3D(false)) {
                //            if (auto fade = a3d->AsFadeNode()) {
                //                fade->GetRuntimeData().currentFade = 1.f;
                //            }
                //        }
                //    }
                //}

                // Option 3: Works but collision isn't updated
                // better than Option 1 because we don't move ref, just it's model
                parentNode->local.translate = newParentPos;
                parentNode->local.rotate = newParentRot;

                // Option 4: Not working at all probably it's calculated from ref position plus loacl?
                // parentNode->world.translate = tracked.basePos;
                // parentNode->world.translate.z += zOffset;

                // Collision update atempts
                // parentNode->SetCollisionLayer(RE::COL_LAYER::kAnimStatic); // No
                // parentRef->MoveHavok(true); // No
                // parentNode->UpdateCollisionObject(true);  // No - why?

                //
                // if (auto colObj = parentNode->GetCollisionObject()) {
                //    colObj->flags.set(RE::bhkNiCollisionObject::Flag::kSyncOnUpdate);  // No - WHY?
                //}
                // if (auto parrntColObj = parentNode->GetCollisionObject()) {
                //    parrntColObj->flags.set(RE::bhkNiCollisionObject::Flag::kReset);  // No - WHY?
                //}
                // if (auto parrntColObj = parentNode->GetCollisionObject()) {
                //    parrntColObj->flags.set(RE::bhkNiCollisionObject::Flag::kDebugDisplay);  // No
                //}
                // if (auto parrntColObj = parentNode->GetCollisionObject()) {
                //    parrntColObj->flags.set(RE::bhkNiCollisionObject::Flag::kNotify);  // No
                //}

                // What to update?
                RE::NiUpdateData updData;
                updData.flags = RE::NiUpdateData::Flag::kNone;
                updData.time = 0.0f;

                // parentNode->UpdateWorldData(&updData);          // no - local   // no - world
                parentNode->UpdateTransformAndBounds(updData);  // yes - local  // no - world // no collision update
                // parentNode->Update(updData);                    // yes - local  // no - world // no collision update
                // parentNode->UpdateWorldBound();                 // no - local   // no - world
                // parentRef->Update3DPosition(true);           // yes - Option 1
                // parentRef->SetAltered(true); // no collision update
                // parentRef->SetCollision(true);
                // parentRef->UpdateAnimation(0.0f);

                // Turns out to be not needed
                // parentRef->InitItem();
                // parentRef->InitHavok();
                // parentRef->MoveHavok(true);
                // MakeRefColisionDynamic(parentRef);

                // APPLY TO ACTORS
                for (auto ActorIt = parent.Actors.begin(); ActorIt != parent.Actors.end();) {
                    auto& actorHandle = ActorIt->second;
                    RE::Actor* Actor = actorHandle.get().get();

                    if (!Actor) {
                        // invalid
                        ActorIt = parent.Actors.erase(ActorIt);
                        continue;
                    }

                    if (auto occupiedFurnitureHandle = Actor->GetOccupiedFurniture()) {
                        if (auto occupiedFurniture = occupiedFurnitureHandle.get().get()) {
                            auto FurnitureAsChild = parent.childrens.find(occupiedFurniture->GetFormID());

                            if (FurnitureAsChild != parent.childrens.end()) {
                                if (auto childNode = occupiedFurniture->Get3D()) {
                                    auto currActorPos = Actor->GetPosition();
                                    auto distance = childNode->world.translate.GetDistance(currActorPos);
                                    auto sizes = occupiedFurniture->GetBoundMax() - occupiedFurniture->GetBoundMin();
                                    float treshold = std::max(sizes.x, sizes.y) / 2;

                                    if (distance < treshold) {

                                        RE::NiPoint3 rotatedOffset = newParentRot * FurnitureAsChild->second.OffsetPos;
                                        RE::NiPoint3 childFinalPos = newParentPos + rotatedOffset;
                                        RE::NiMatrix3 childFinalRot = newParentRot * FurnitureAsChild->second.OffsetRot;

                                        RE::NiPoint3 offsetDiff = childFinalPos - childNode->local.translate;

                                        // APPLY
                                        childNode->local.translate = childFinalPos;
                                        childNode->local.rotate = childFinalRot;
                                        childNode->UpdateTransformAndBounds(updData);

                                        Actor->SetPosition(currActorPos + offsetDiff, true);

                                        FurnitureAsChild->second.processed = true;
                                        //logger::debug("occupiedFurniture {:08X}, processing with actor {}",
                                        //              occupiedFurniture->GetFormID(), Actor->GetName());
                                    }
                                }
                            }
                            if (occupiedFurniture->IsDynamicForm()) {
                                Manager::GetSingleton()->AddDynamicFurniture(parentRef, occupiedFurnitureHandle);
                            }
                        }
                    }
                    ++ActorIt;
                }
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

                    if (!child.processed) {
                        if (auto childNode = childRef->Get3D()) {
                            RE::NiPoint3 rotatedOffset = newParentRot * child.OffsetPos;
                            RE::NiPoint3 childFinalPos = newParentPos + rotatedOffset;
                            RE::NiMatrix3 childFinalRot = newParentRot * child.OffsetRot;

                            // APPLY
                            childNode->local.translate = childFinalPos;
                            childNode->local.rotate = childFinalRot;
                            childNode->UpdateTransformAndBounds(updData);
                        }
                    } else {
                        child.processed = false;
                        //logger::debug("child {:08X}, was processed before", childRef->GetFormID());
                    }
                    ++childIt;
                }
                parent.Actors.clear();
                ++it;
            }
        }

        static void MakeRefColisionDynamic(RE::TESObjectREFR* a_ref) {
            /*
            * All this and much more
            * And all i needed was: a_ref->SetMotionType(RE::hkpMotion::MotionType::kKeyframed, true);
            * 
            * 
            if (const auto root = a_ref->Get3D(); root) {
                const auto cell = a_ref->GetParentCell();

                root->SetCollisionLayer(RE::COL_LAYER::kAnimStatic);
                root->SetMotionType(RE::hkpMotion::MotionType::kBoxInertia);

                if (auto colObj = root->GetCollisionObject()) {
                    colObj->flags.set(RE::bhkNiCollisionObject::Flag::kActive);
                    colObj->flags.set(RE::bhkNiCollisionObject::Flag::kSetLocal);
                    colObj->flags.set(RE::bhkNiCollisionObject::Flag::kSyncOnUpdate);
                    if (auto colBody = colObj->body.get()) {
                        //logger::info("RTTI: {}", typeid(*colBody).name());
                        if (auto rigidBody = colBody->AsBhkRigidBody()) {
                            //logger::info("RTTI: {}", typeid(*rigidBody).name());
                            if (auto rigidBodyT = skyrim_cast<RE::bhkRigidBodyT*>(rigidBody) ) {
                                //logger::info("RTTI: {}", typeid(*rigidBodyT).name());
                            } else {
                                //logger::warn("Collision body is not bhkRigidBodyT");
                            }
                        }
                    }
                }
            }
            */
            // Truman THANK YOU <3
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
                        childRefData.OffsetPos = parentIt->second.baseRot.Transpose() * (ref3D->world.translate - parentIt->second.basePos);

                        RE::NiMatrix3 childRot = ref3D->world.rotate;
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

                    refData.basePos = ref3D->world.translate;

                    refData.baseRot = ref3D->world.rotate;

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
                                    continue;
                                }
                                ChildRef childRefData;
                                childRefData.refHandle = childRef->GetHandle();

                                childRefData.OffsetPos =
                                    refData.baseRot.Transpose() * (child3D->world.translate - refData.basePos);

                                RE::NiMatrix3 childRot = child3D->world.rotate;
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

                if (!j["children"].is_array()) {
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

                // --- Z movement ---
                if (j.contains("minZ")) cfg.minZ = j["minZ"].get<float>();
                if (j.contains("maxZ")) cfg.maxZ = j["maxZ"].get<float>();

                // --- Rotation ---
                if (j.contains("minRot") && j["minRot"].is_array() && j["minRot"].size() == 3) {
                    cfg.minRot.x = j["minRot"][0].get<float>();
                    cfg.minRot.y = j["minRot"][1].get<float>();
                    cfg.minRot.z = j["minRot"][2].get<float>();
                }

                if (j.contains("maxRot") && j["maxRot"].is_array() && j["maxRot"].size() == 3) {
                    cfg.maxRot.x = j["maxRot"][0].get<float>();
                    cfg.maxRot.y = j["maxRot"][1].get<float>();
                    cfg.maxRot.z = j["maxRot"][2].get<float>();
                }

                if (cfg.minZ > cfg.maxZ) std::swap(cfg.minZ, cfg.maxZ);
                if (cfg.minRot.x > cfg.maxRot.x) std::swap(cfg.minRot.x, cfg.maxRot.x);
                if (cfg.minRot.y > cfg.maxRot.y) std::swap(cfg.minRot.y, cfg.maxRot.y);
                if (cfg.minRot.z > cfg.maxRot.z) std::swap(cfg.minRot.z, cfg.maxRot.z);

                if (j.contains("speed")) cfg.speed = j["speed"].get<float>();

                if (j.contains("phaseOffset")) cfg.phaseOffset = j["phaseOffset"].get<float>();

                if (j.contains("actorInfluence")) cfg.actorInfluence = j["actorInfluence"].get<float>();

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
                {
                    std::unique_lock lock(configsMutex);
                    configs[formID] = cfg;
                }
            }
        }

        void SaveConfigToFile(const RE::FormID formID, BobbingConfig& cfg, std::string filename) {
            std::filesystem::create_directories("Data\\SKSE\\Plugins\\BobbingFramework");
            std::string path;
            if (filename.empty()) {
                path = std::format("Data\\SKSE\\Plugins\\BobbingFramework\\{:08X}.json", formID);
            } else {
                path = "Data\\SKSE\\Plugins\\BobbingFramework\\" + filename + ".json";
            }

            logger::info("Saving BaseObjSwapConfig for {:08X}", formID);

            nlohmann::json j;
            j["FormID"] = Utils::FormIDToString(formID);

            j["minZ"] = cfg.minZ;
            j["maxZ"] = cfg.maxZ;

            j["minRot"] = {cfg.minRot.x, cfg.minRot.y, cfg.minRot.z};
            j["maxRot"] = {cfg.maxRot.x, cfg.maxRot.y, cfg.maxRot.z};

            j["speed"] = cfg.speed;
            j["phaseOffset"] = cfg.phaseOffset;
            j["actorInfluence"] = cfg.actorInfluence;

            j["children"] = nlohmann::json::array();
            for (const auto& entry : cfg.childrens) {
                j["children"].push_back(Utils::FormIDToString(entry));
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

    };
}