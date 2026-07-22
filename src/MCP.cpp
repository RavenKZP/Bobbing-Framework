#include "MCP.h"

#include "SKSEMCP/SKSEMenuFramework.hpp"
#include "Manager.h"
#include "Settings.h"

#include "ClibUtil/editorID.hpp"
#include "CLibUtilsQTR/DrawDebug.hpp"

namespace MCP {

    void Register() {
        if (!SKSEMenuFramework::IsInstalled()) {
            logger::warn("SKSE Menu Framework is not installed. Cannot register menu.");
            return;
        }
        SKSEMenuFramework::SetSection("Bobbing Framework");
        SKSEMenuFramework::AddSectionItem("Settings", RenderConfig);
        SKSEMenuFramework::AddSectionItem("Framework Options", RenderFrameworkTools);
#ifndef NDEBUG
        SKSEMenuFramework::AddSectionItem("Log", RenderLog);
#endif

        logger::info("SKSE Menu Framework registered.");
    }

    void __stdcall RenderConfig() {
        auto* cfg = Config::GetSingleton();
        ImGuiMCP::Checkbox("Mod Active", &cfg->ModActive);
        ImGuiMCP::Checkbox("Enable Time Logging", &cfg->EnableTimeLogging);
    }

    /*
    static void RefreshRefCollision(RE::TESObjectREFR* a_ref, RE::bhkNiCollisionObject::Flag flag) {
        if (const auto root = a_ref->Get3D(); root) {
            const auto cell = a_ref->GetParentCell();

            root->SetCollisionLayer(RE::COL_LAYER::kAnimStatic);

            if (auto colObj = root->GetCollisionObject()) {
                colObj->flags.set(flag);
            }
        }
    }
    */

    /*
    static void MakeRefColisionDynamic(RE::TESObjectREFR* a_ref) {
        if (auto root = a_ref->Get3D()) {
            root->SetCollisionLayer(RE::COL_LAYER::kAnimStatic);
            root->SetMotionType(RE::hkpMotion::MotionType::kBoxInertia);

            if (auto colObj = root->GetCollisionObject()) {
                colObj->flags.set(RE::bhkNiCollisionObject::Flag::kActive);
                colObj->flags.set(RE::bhkNiCollisionObject::Flag::kSetLocal);
                colObj->flags.set(RE::bhkNiCollisionObject::Flag::kSyncOnUpdate);
                if (auto colBody = colObj->body.get()) {
                    // logger::info("RTTI: {}", typeid(*colBody).name());
                    if (auto rigidBody = colBody->AsBhkRigidBody()) {
                        // logger::info("RTTI: {}", typeid(*rigidBody).name());
                        if (auto rigidBodyT = skyrim_cast<RE::bhkRigidBodyT*>(rigidBody)) {
                            // logger::info("RTTI: {}", typeid(*rigidBodyT).name());
                        } else {
                            // logger::warn("Collision body is not bhkRigidBodyT");
                        }
                    }
                }
            }
        }
    }
    */

    static void DrawObjectBoundsBox(BoundaryBox points, int lifetimeMS,
                                    float thickness) {

        static auto* DebugAPI = DebugAPI_IMPL::DebugAPI::GetSingleton();

        RE::NiPoint3 p000 = {points[0].x, points[0].y, points[0].z};
        RE::NiPoint3 p100 = {points[1].x, points[1].y, points[1].z};
        RE::NiPoint3 p110 = {points[2].x, points[2].y, points[2].z};
        RE::NiPoint3 p010 = {points[3].x, points[3].y, points[3].z};
        RE::NiPoint3 p001 = {points[4].x, points[4].y, points[4].z};
        RE::NiPoint3 p101 = {points[5].x, points[5].y, points[5].z};
        RE::NiPoint3 p111 = {points[6].x, points[6].y, points[6].z};
        RE::NiPoint3 p011 = {points[7].x, points[7].y, points[7].z};

        const RE::NiColorA WhiteTest{1.0f, 1.0f, 1.0f, 1.0f};
        const RE::NiColorA BlackTest{0.0f, 0.0f, 0.0f, 1.0f};

        const RE::NiColorA redPos{1.00f, 0.55f, 0.55f, 1.0f};
        const RE::NiColorA redNeg{0.85f, 0.30f, 0.30f, 1.0f};
        const RE::NiColorA greenPos{0.55f, 1.00f, 0.55f, 1.0f};
        const RE::NiColorA greenNeg{0.30f, 0.85f, 0.30f, 1.0f};
        const RE::NiColorA bluePos{0.55f, 0.55f, 1.00f, 1.0f};
        const RE::NiColorA blueNeg{0.30f, 0.30f, 0.85f, 1.0f};

        // Bottom face (min.z)
        DebugAPI->DrawLineForMS(p000, p100, lifetimeMS, blueNeg, thickness);
        DebugAPI->DrawLineForMS(p100, p110, lifetimeMS, redPos, thickness);
        DebugAPI->DrawLineForMS(p110, p010, lifetimeMS, bluePos, thickness);
        DebugAPI->DrawLineForMS(p010, p000, lifetimeMS, redNeg, thickness);

        // Top face (max.z)
        DebugAPI->DrawLineForMS(p001, p101, lifetimeMS, blueNeg, thickness);
        DebugAPI->DrawLineForMS(p101, p111, lifetimeMS, redPos, thickness);
        DebugAPI->DrawLineForMS(p111, p011, lifetimeMS, bluePos, thickness);
        DebugAPI->DrawLineForMS(p011, p001, lifetimeMS, redNeg, thickness);

        // Vertical edges
        DebugAPI->DrawLineForMS(p000, p001, lifetimeMS, greenNeg, thickness);
        DebugAPI->DrawLineForMS(p100, p101, lifetimeMS, greenNeg, thickness);
        DebugAPI->DrawLineForMS(p110, p111, lifetimeMS, greenPos, thickness);
        DebugAPI->DrawLineForMS(p010, p011, lifetimeMS, greenPos, thickness);
    }

    void __stdcall RenderFrameworkTools() {
        DebugAPI_IMPL::DebugAPI::GetSingleton()->Update();

        static RE::TESObjectREFR* lastRef{nullptr};
        if (auto refPtr = RE::Console::GetSelectedRef()) {
            if (auto ref = refPtr.get()) {
                if (ref->IsActor()) {
                    ImGuiMCP::Text("(~*_*)~ Sorry, you can't bobbing an actor.... with this mod ~(*_*~)");
                    return;
                }

                /*
                if (const auto root = ref->Get3D(); root) {
                    if (auto colObj = root->GetCollisionObject()) {
                        if (auto colBody = colObj->body.get()) {
                            ImGuiMCP::Text("RTTI: %s", typeid(*colBody).name());
                            if (auto rigidBody = colBody->AsBhkRigidBody()) {
                                ImGuiMCP::Text("RTTI: %s", typeid(*rigidBody).name());
                                if (auto rigidBodyT = skyrim_cast<RE::bhkRigidBodyT*>(rigidBody)) {
                                    ImGuiMCP::Text("Collision body is bhkRigidBodyT");
                                } else {
                                    ImGuiMCP::Text("Collision body is not bhkRigidBodyT");
                                }
                            }
                        }
                    }
                    auto colLayer = root->GetCollisionLayer();
                    ImGuiMCP::Text("Collision Layer: %d", colLayer);

                    auto flags = root->GetFlags();
                    ImGuiMCP::Text("Flags: %d", flags);
                }

                if (ImGuiMCP::Button("MakeRefColisionDynamic")) {
                    MakeRefColisionDynamic(ref);
                }
                ImGuiMCP::SameLine();
                if (ImGuiMCP::Button("Update3DPosition")) {
                    ref->Update3DPosition(true);
                }
                ImGuiMCP::SameLine();
                if (ImGuiMCP::Button("SetAltered")) {
                    ref->data.objectReference->SetAltered(true);
                    ref->SetAltered(true);
                }
                if (ImGuiMCP::Button("UpdateCollisionObject true")) {
                    ref->Get3D()->UpdateCollisionObject(true);
                }
                ImGuiMCP::SameLine();
                if (ImGuiMCP::Button("UpdateCollisionObject false")) {
                    ref->Get3D()->UpdateCollisionObject(false);
                }
                ImGuiMCP::SameLine();
                if (ImGuiMCP::Button("MotionType::kKeyframed true")) {
                    ref->SetMotionType(RE::hkpMotion::MotionType::kKeyframed, true);
                }

                ImGuiMCP::Text("RefreshRefCollision:");
                if (ImGuiMCP::Button("kReset")) {
                    RefreshRefCollision(ref, RE::bhkNiCollisionObject::Flag::kReset);
                }
                ImGuiMCP::SameLine();
                if (ImGuiMCP::Button("kActive")) {
                    RefreshRefCollision(ref, RE::bhkNiCollisionObject::Flag::kActive);
                }
                ImGuiMCP::SameLine();
                if (ImGuiMCP::Button("kDebugDisplay")) {
                    RefreshRefCollision(ref, RE::bhkNiCollisionObject::Flag::kDebugDisplay);
                }
                ImGuiMCP::SameLine();
                if (ImGuiMCP::Button("kNotify")) {
                    RefreshRefCollision(ref, RE::bhkNiCollisionObject::Flag::kNotify);
                }
                ImGuiMCP::SameLine();
                if (ImGuiMCP::Button("kSetLocal")) {
                    RefreshRefCollision(ref, RE::bhkNiCollisionObject::Flag::kSetLocal);
                }
                ImGuiMCP::SameLine();
                if (ImGuiMCP::Button("kSyncOnUpdate")) {
                    RefreshRefCollision(ref, RE::bhkNiCollisionObject::Flag::kSyncOnUpdate);
                }

                RE::NiUpdateData updData;
                updData.flags = RE::NiUpdateData::Flag::kNone;
                updData.time = 0.0f;
                if (ImGuiMCP::Button("UpdateWorldData")) {
                    ref->Get3D()->UpdateWorldData(&updData);
                }
                ImGuiMCP::SameLine();
                if (ImGuiMCP::Button("UpdateTransformAndBounds")) {
                    ref->Get3D()->UpdateTransformAndBounds(updData);
                }
                ImGuiMCP::SameLine();
                if (ImGuiMCP::Button("Update")) {
                    ref->Get3D()->Update(updData);
                }
                ImGuiMCP::SameLine();
                if (ImGuiMCP::Button("UpdateWorldBound")) {
                    ref->Get3D()->UpdateWorldBound();
                }
                if (ImGuiMCP::Button("Enable")) {
                    ref->Enable(false);
                }
                ImGuiMCP::SameLine();
                if (ImGuiMCP::Button("Disable")) {
                    ref->Disable();
                }
                */

                static std::string filename;
                static std::vector<RE::TESObjectREFR*> childrens;
                static RE::NiPoint2 minMaxZ = {-5.0f, 5.0f};
                static float maxZ = 10.0f;

                static RE::NiPoint3 minRot = {-0.01f, -0.01f, -0.01f};  // X, Y, Z
                static RE::NiPoint3 maxRot = {0.01f, 0.01f, 0.01f};     // X, Y, Z

                static float speed = 1.0f;
                static float phaseOffset = 0.0f;
                static float actorInfluence = 0.0f;

                static float offsetPosX = 0.0f;
                static float offsetNegX = 0.0f;
                static float offsetPosY = 0.0f;
                static float offsetNegY = 0.0f;
                static float offsetPosZ = 0.0f;
                static float offsetNegZ = 0.0f;

                static bool drawBBox = false;

                auto base = ref->GetBaseObject();
                auto BobbingMgr = Bobbing::Manager::GetSingleton();

                bool hasRefConfig = BobbingMgr->HasConfig(ref->GetFormID());
                bool hasBaseConfig = BobbingMgr->HasConfig(base->GetFormID());

                if (lastRef != ref) {
                    drawBBox = false;
                    lastRef = ref;
                    filename.erase();
                    minMaxZ = {-5.0f, 5.0f};
                    minRot = {-0.01f, -0.01f, -0.01f};
                    maxRot = {0.01f, 0.01f, 0.01f};

                    speed = 1.0f;
                    phaseOffset = 0.0f;
                    actorInfluence = 0.0f;

                    offsetPosX = 0.0f;
                    offsetNegX = 0.0f;
                    offsetPosY = 0.0f;
                    offsetNegY = 0.0f;
                    offsetPosZ = 0.0f;
                    offsetNegZ = 0.0f;

                    childrens.clear();

                    if (hasRefConfig) {
                        auto cfg = BobbingMgr->GetConfig(ref->GetFormID());
                        minMaxZ.x = cfg.minZ;
                        minMaxZ.y = cfg.maxZ;
                        minRot = cfg.minRot;
                        maxRot = cfg.maxRot;
                        speed = cfg.speed;
                        phaseOffset = cfg.phaseOffset;
                        actorInfluence = cfg.actorInfluence;
                        if (childrens.empty()) {
                            for (auto& childID : cfg.childrens) {
                                auto form = RE::TESForm::LookupByID(childID);
                                auto childRef = form ? form->As<RE::TESObjectREFR>() : nullptr;
                                if (childRef) {
                                    childrens.push_back(childRef);
                                }
                            }
                        }
                    }
                }
                /*
                auto refNode = ref->Get3D();
                auto nodeVectX = refNode->local.rotate.GetVectorX();
                auto nodeVectY = refNode->local.rotate.GetVectorY();
                auto nodeVectZ = refNode->local.rotate.GetVectorZ();
                bool changed = false;
                if (ImGuiMCP::SliderFloat3("Ref Rotation X", &nodeVectX.x, -5.0f, 5.0f)) {
                    changed = true;
                }
                if (ImGuiMCP::SliderFloat3("Ref Rotation Y", &nodeVectY.y, -5.0f, 5.0f)) {
                    changed = true;
                }
                if (ImGuiMCP::SliderFloat3("Ref Rotation Z", &nodeVectZ.z, -5.0f, 5.0f)) {
                    changed = true;
                }

                if (changed) {
                    refNode->local.rotate = RE::NiMatrix3(nodeVectZ, nodeVectY, nodeVectX);
                    RE::NiUpdateData updData;
                    updData.flags = RE::NiUpdateData::Flag::kNone;
                    updData.time = 0.0f;
                    refNode->Update(updData);
                }
                */

                std::string refLabel = hasRefConfig ? "Update Ref Config" : "Add Ref Config";
                std::string baseLabel = hasBaseConfig ? "Update Base Config" : "Add Base Config";

                bool updateOffsets = false;

                // --- Z movement ---
                ImGuiMCP::Text("Bobbing - Position Z");
                ImGuiMCP::SliderFloat2("Min / Max;", &minMaxZ.x, -10.0f, 10.0f);
                ImGuiMCP::InputFloat2("Min / Max:", &minMaxZ.x);

                // --- Rotation ---
                ImGuiMCP::Separator();
                ImGuiMCP::Text("Bobbing - Rotation");

                ImGuiMCP::SliderFloat3("Min Rot;", &minRot.x, -0.2f, 0.2f);
                ImGuiMCP::SliderFloat3("Max Rot;", &maxRot.x, -0.2f, 0.2f);
                ImGuiMCP::InputFloat3("Min Rot:", &minRot.x);
                ImGuiMCP::InputFloat3("Max Rot:", &maxRot.x);

                if (minMaxZ.x > minMaxZ.y) std::swap(minMaxZ.x, minMaxZ.y);
                if (minRot.x > maxRot.x) std::swap(minRot.x, maxRot.x);
                if (minRot.y > maxRot.y) std::swap(minRot.y, maxRot.y);
                if (minRot.z > maxRot.z) std::swap(minRot.z, maxRot.z);

                if (ImGuiMCP::CollapsingHeader("Advanced Options")) {
                    ImGuiMCP::SliderFloat("Speed;", &speed, 0.01f, 1.0f);
                    ImGuiMCP::InputFloat("Speed:", &speed);

                    ImGuiMCP::SliderFloat("Phase Offset;", &phaseOffset, -1.0f, 1.0f);
                    ImGuiMCP::InputFloat("Phase Offset:", &phaseOffset);

                    ImGuiMCP::SliderFloat("Actor Influence;", &actorInfluence, 0.0f, 10.0f);
                    ImGuiMCP::InputFloat("Actor Influence:", &actorInfluence);
                }

                if (ImGuiMCP::CollapsingHeader("Childrens Options")) {

                    ImGuiMCP::Checkbox("Draw Boundary Box", &drawBBox);

                    const ImGuiMCP::ImVec4 uiRedPos{1.00f, 0.55f, 0.55f, 1.0f};
                    const ImGuiMCP::ImVec4 uiRedNeg{0.85f, 0.30f, 0.30f, 1.0f};
                    const ImGuiMCP::ImVec4 uiGreenPos{0.55f, 1.00f, 0.55f, 1.0f};
                    const ImGuiMCP::ImVec4 uiGreenNeg{0.30f, 0.85f, 0.30f, 1.0f};
                    const ImGuiMCP::ImVec4 uiBluePos{0.55f, 0.55f, 1.00f, 1.0f};
                    const ImGuiMCP::ImVec4 uiBlueNeg{0.30f, 0.30f, 0.85f, 1.0f};

                    ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Text, uiRedPos);
                    if (ImGuiMCP::DragFloat("Pos X", &offsetPosX, 0.1f)) {
                        updateOffsets = true;
                    }
                    ImGuiMCP::PopStyleColor();

                    ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Text, uiRedNeg);
                    if (ImGuiMCP::DragFloat("Neg X", &offsetNegX, 0.1f)) {
                        updateOffsets = true;
                    }
                    ImGuiMCP::PopStyleColor();

                    ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Text, uiGreenPos);
                    if (ImGuiMCP::DragFloat("Pos Y", &offsetPosY, 0.1f)) {
                        updateOffsets = true;
                    }
                    ImGuiMCP::PopStyleColor();

                    ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Text, uiGreenNeg);
                    if (ImGuiMCP::DragFloat("Neg Y", &offsetNegY, 0.1f)) {
                        updateOffsets = true;
                    }
                    ImGuiMCP::PopStyleColor();

                    ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Text, uiBluePos);
                    if (ImGuiMCP::DragFloat("Pos Z", &offsetPosZ, 0.1f)) {
                        updateOffsets = true;
                    }
                    ImGuiMCP::PopStyleColor();

                    ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Text, uiBlueNeg);
                    if (ImGuiMCP::DragFloat("Neg Z", &offsetNegZ, 0.1f)) {
                        updateOffsets = true;
                    }
                    ImGuiMCP::PopStyleColor();

                    
                    static ImGuiMCP::ImVec4 MCPHighlightColor{0.0f, 1.0f, 0.0f, 1.0f};
                    static RE::NiColorA HighlightColor(0.0f, 1.0f, 0.0f, 1.0f);

                    ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Text, MCPHighlightColor);
                    if (ImGuiMCP::SliderFloat4("Highlight Color", &HighlightColor.red, 0.0f, 1.0f)) {
                        MCPHighlightColor.x = HighlightColor.red;
                        MCPHighlightColor.y = HighlightColor.green;
                        MCPHighlightColor.z = HighlightColor.blue;
                        MCPHighlightColor.w = HighlightColor.alpha;
                    }
                    ImGuiMCP::PopStyleColor();

                    if (updateOffsets) {
                        for (auto* child : childrens) {
                            child->Get3D()->TintScenegraph(RE::NiColorA(0.0f, 0.0f, 0.0f, 0.0f));
                            child->Update3DPosition(true);
                        }
                        childrens.clear();
                    }

                    auto c = Utils::GetBoundingBox(ref);

                    // --- Compute center ---
                    RE::NiPoint3 center{0, 0, 0};
                    for (auto& p : c) center += p;
                    center /= 8.0f;

                    // --- Compute axes + extents ---
                    RE::NiPoint3 axisX = c[1] - c[0];
                    float extentX = axisX.Length();
                    axisX /= extentX;

                    RE::NiPoint3 axisY = c[3] - c[0];
                    float extentY = axisY.Length();
                    axisY /= extentY;

                    RE::NiPoint3 axisZ = c[4] - c[0];
                    float extentZ = axisZ.Length();
                    axisZ /= extentZ;

                    extentX *= 0.5f;
                    extentY *= 0.5f;
                    extentZ *= 0.5f;

                    // --- OBB test ---
                    auto isInsideOBB = [&](const RE::NiPoint3& point) {
                        RE::NiPoint3 d = point - center;

                        float dx = d.Dot(axisX);
                        if (dx > (extentX + offsetPosX)) return false;
                        if (dx < -(extentX + offsetNegX)) return false;

                        float dy = d.Dot(axisY);
                        if (dy > (extentY + offsetPosY)) return false;
                        if (dy < -(extentY + offsetNegY)) return false;

                        float dz = d.Dot(axisZ);
                        if (dz > (extentZ + offsetPosZ)) return false;
                        if (dz < -(extentZ + offsetNegZ)) return false;

                        return true;
                    };

                    // --- Broad phase radius ---
                    float radius = 0.0f;
                    for (auto& p : c) {
                        float dist = (p - center).Length();
                        if (dist > radius) radius = dist;
                    }

                    float maxOffset = std::max({fabs(offsetPosX), fabs(offsetNegX), fabs(offsetPosY), fabs(offsetNegY),
                                                fabs(offsetPosZ), fabs(offsetNegZ)});

                    radius += maxOffset;

                    auto AdjustedBBox = c;

                    for (size_t i = 0; i < 8; ++i) {
                        RE::NiPoint3 rel = AdjustedBBox[i] - center;

                        float dx = rel.Dot(axisX);
                        float dy = rel.Dot(axisY);
                        float dz = rel.Dot(axisZ);

                        RE::NiPoint3 ofs{0.0f, 0.0f, 0.0f};

                        ofs += (dx >= 0.0f ? axisX : -axisX) * (dx >= 0.0f ? offsetPosX : offsetNegX);
                        ofs += (dy >= 0.0f ? axisY : -axisY) * (dy >= 0.0f ? offsetPosY : offsetNegY);
                        ofs += (dz >= 0.0f ? axisZ : -axisZ) * (dz >= 0.0f ? offsetPosZ : offsetNegZ);

                        AdjustedBBox[i] += ofs;
                    }

                    if (drawBBox) {
                        DrawObjectBoundsBox(AdjustedBBox, 100, 2.0f);
                    }

                    if (ImGuiMCP::Button("Add refs inside selected ref Boundaruy Box to Childrens") || updateOffsets) {
                        drawBBox = true;
                        RE::TES::GetSingleton()->ForEachReferenceInRange(center, radius, [&](RE::TESObjectREFR* other) {
                            if (!other || other == ref) return RE::BSContainer::ForEachResult::kContinue;
                            if (other->IsDynamicForm()) return RE::BSContainer::ForEachResult::kContinue;
                            if (other->IsActor()) return RE::BSContainer::ForEachResult::kContinue;
                            auto pos = other->GetPosition();
                            if (!isInsideOBB(pos)) return RE::BSContainer::ForEachResult::kContinue;
                            if (auto node = other->Get3D()) {
                                node->TintScenegraph(HighlightColor);
                            }
                            childrens.push_back(other);
                            return RE::BSContainer::ForEachResult::kContinue;
                        });
                    }


                    static char addChildFormID[255];      // hex input for manual add
                    static bool keepChildListOpen = false;  // keep header open when removing/adding

                    std::string label = std::format("Selected Ref has Childrens: {}", childrens.size());

                    if (keepChildListOpen) {
                        ImGuiMCP::SetNextItemOpen(true, ImGuiMCP::ImGuiCond_Always);
                    }
                    if (ImGuiMCP::CollapsingHeader(label.c_str())) {
                        // ensure stable ordering by FormID
                        std::sort(childrens.begin(), childrens.end(), [](RE::TESObjectREFR* a, RE::TESObjectREFR* b) {
                            return a->GetFormID() < b->GetFormID();
                        });

                        ImGuiMCP::InputText("Add Child FormID (hex)", addChildFormID, 255);
                        ImGuiMCP::SameLine();
                        if (ImGuiMCP::Button("Add Child")) {
                            // trim and optional 0x
                            std::string s = addChildFormID;
                            // remove spaces
                            s.erase(std::remove_if(s.begin(), s.end(), ::isspace), s.end());
                            if (!s.empty()) {
                                try {
                                    if (s.rfind("0x", 0) == 0 || s.rfind("0X", 0) == 0) s = s.substr(2);
                                    auto fid = static_cast<RE::FormID>(std::stoul(s, nullptr, 16));
                                    if (auto form = RE::TESForm::LookupByID(fid)) {
                                        if (auto childRef = form->As<RE::TESObjectREFR>()) {
                                            bool exists = false;
                                            for (auto* c : childrens) {
                                                if (c->GetFormID() == childRef->GetFormID()) {
                                                    exists = true;
                                                    break;
                                                }
                                            }
                                            if (!exists) {
                                                childrens.push_back(childRef);
                                                keepChildListOpen = true;
                                            }
                                        }
                                    }
                                } catch (...) {
                                    // ignore parse errors
                                }
                            }
                        }

                        std::optional<size_t> toRemove;
                        for (size_t i = 0; i < childrens.size(); ++i) {
                            auto* child = childrens[i];
                            ImGuiMCP::PushID(child->GetFormID());
                            ImGuiMCP::Text("0x%08X", child->GetFormID());
                            ImGuiMCP::SameLine();
                            const auto id = clib_util::editorID::get_editorID(child->GetBaseObject());
                            ImGuiMCP::Text("%s", id.c_str());
                            ImGuiMCP::SameLine();

                            if (ImGuiMCP::Button("Remove")) {
                                if (child && child->Get3D()) {
                                    child->Get3D()->TintScenegraph(RE::NiColorA(0.0f, 0.0f, 0.0f, 0.0f));
                                    child->Disable();
                                    child->Enable(false);
                                }
                                toRemove = i;
                                keepChildListOpen = true;
                            }
                            ImGuiMCP::SameLine();
                            if (ImGuiMCP::Button("Highlight")) {
                                child->Get3D()->TintScenegraph(HighlightColor);
                            }
                            ImGuiMCP::SameLine();
                            if (ImGuiMCP::Button("UnHighlight")) {
                                child->Get3D()->TintScenegraph(RE::NiColorA(0.0f, 0.0f, 0.0f, 0.0f));
                            }
                            ImGuiMCP::PopID();
                        }
                        if (toRemove.has_value()) {
                            childrens.erase(childrens.begin() + toRemove.value());
                        }
                    } else {
                        // header closed by user — reset keep flag
                        keepChildListOpen = false;
                    }

                    if (childrens.size() > 0) {
                        if (ImGuiMCP::Button("Highlight All")) {
                            for (auto* child : childrens) {
                                child->Get3D()->TintScenegraph(HighlightColor);
                            }
                        }
                        ImGuiMCP::SameLine();
                        if (ImGuiMCP::Button("UnHighlight All")) {
                            for (auto* child : childrens) {
                                child->Get3D()->TintScenegraph(RE::NiColorA(0.0f, 0.0f, 0.0f, 0.0f));
                            }
                        }
                        ImGuiMCP::SameLine();
                        if (ImGuiMCP::Button("Remove All Childrens")) {
                            drawBBox = false;
                            for (auto* child : childrens) {
                                if (child && child->Get3D()) {
                                    child->Get3D()->TintScenegraph(RE::NiColorA(0.0f, 0.0f, 0.0f, 0.0f));
                                    child->Disable();
                                    child->Enable(false);
                                }
                            }
                            childrens.clear();
                        }
                    }
                }

                if (!ref->IsDynamicForm()) {
                    if (ImGuiMCP::Button(refLabel.c_str())) {
                        drawBBox = false;
                        for (auto* child : childrens) {
                            child->Get3D()->TintScenegraph(RE::NiColorA(0.0f, 0.0f, 0.0f, 0.0f));
                        }
                        if (hasRefConfig) {
                            ref->Get3D()->TintScenegraph(RE::NiColorA(0.0f, 0.0f, 0.0f, 0.0f));
                            ref->Disable();
                            ref->Enable(false);
                            auto cfg = BobbingMgr->GetConfig(ref->GetFormID());
                            for (auto& childID : cfg.childrens) {
                                auto form = RE::TESForm::LookupByID(childID);
                                auto childRef = form ? form->As<RE::TESObjectREFR>() : nullptr;
                                if (childRef && childRef->Get3D()) {
                                    childRef->Get3D()->TintScenegraph(RE::NiColorA(0.0f, 0.0f, 0.0f, 0.0f));
                                    childRef->Disable();
                                    childRef->Enable(false);
                                }
                            }
                        }
                        BobbingMgr->RemoveConfig(ref->GetFormID());
                        std::set<RE::FormID> childrensFormIDs;
                        for (auto* child : childrens) {
                            childrensFormIDs.insert(child->GetFormID());
                        }
                        BobbingMgr->AddNewConfig(ref->GetFormID(), minMaxZ.x, minMaxZ.y, minRot, maxRot, speed,
                                                 phaseOffset, actorInfluence, childrensFormIDs, filename);

                        for (auto* child : childrens) {
                            BobbingMgr->RefLoad(child);
                        }
                        BobbingMgr->RefLoad(ref);
                    }
                }
                if (hasRefConfig) {
                    std::set<RE::FormID> childrensFormIDs;
                    for (auto* child : childrens) {
                        childrensFormIDs.insert(child->GetFormID());
                    }
                    // Update without saving
                    BobbingMgr->AddNewConfig(ref->GetFormID(), minMaxZ.x, minMaxZ.y, minRot, maxRot, speed, phaseOffset,
                                             actorInfluence, childrensFormIDs, filename, false);
                    ImGuiMCP::SameLine();
                    if (ImGuiMCP::Button("Remove Ref from Framework")) {
                        drawBBox = false;
                        for (auto* child : childrens) {
                            child->Get3D()->TintScenegraph(RE::NiColorA(0.0f, 0.0f, 0.0f, 0.0f));
                        }
                        ref->Get3D()->TintScenegraph(RE::NiColorA(0.0f, 0.0f, 0.0f, 0.0f));
                        ref->Disable();
                        ref->Enable(false);
                        auto cfg = BobbingMgr->GetConfig(ref->GetFormID());
                        for (auto& childID : cfg.childrens) {
                            auto form = RE::TESForm::LookupByID(childID);
                            auto childRef = form ? form->As<RE::TESObjectREFR>() : nullptr;
                            if (childRef && childRef->Get3D()) {
                                childRef->Get3D()->TintScenegraph(RE::NiColorA(0.0f, 0.0f, 0.0f, 0.0f));
                                childRef->Disable();
                                childRef->Enable(false);
                            }
                        }
                        BobbingMgr->RemoveConfig(ref->GetFormID());
                    }
                }
                if (ImGuiMCP::Button(baseLabel.c_str())) {
                    std::set<RE::FormID> childrensFormIDs;  // Always empty for Base
                    BobbingMgr->AddNewConfig(ref->GetBaseObject()->GetFormID(), minMaxZ.x, minMaxZ.y, minRot, maxRot,
                                             speed, phaseOffset, actorInfluence, childrensFormIDs, filename);

                    // Unoptimal? Yeah but only from MCP menu :*
                    RE::TES::GetSingleton()->ForEachReference([&](RE::TESObjectREFR* other_ref) {
                        if (ref->GetBaseObject() == other_ref->GetBaseObject()) BobbingMgr->RefLoad(other_ref);
                        return RE::BSContainer::ForEachResult::kContinue;
                    });
                }
                if (hasBaseConfig) {
                    ImGuiMCP::SameLine();
                    if (ImGuiMCP::Button("Remove Base from Framework")) {
                        BobbingMgr->RemoveConfig(ref->GetBaseObject()->GetFormID());
                        RE::TES::GetSingleton()->ForEachReference([&](RE::TESObjectREFR* other_ref) {
                            if (ref->GetBaseObject() == other_ref->GetBaseObject()) {
                                if (auto other3D = other_ref->Get3D())
                                    other3D->TintScenegraph(RE::NiColorA(0.0f, 0.0f, 0.0f, 0.0f));
                                other_ref->Disable();
                                other_ref->Enable(false);
                            }
                            return RE::BSContainer::ForEachResult::kContinue;
                        });
                    }
                }

            } else {
                ImGuiMCP::Text("No Ref Selected in the console");
            }
        } else {
            ImGuiMCP::Text("No Ref Selected in the console");
        }
    }

    void __stdcall MCP::RenderLog() {
        ImGuiMCP::Checkbox("Trace", &MCPLog::log_trace);
        ImGuiMCP::SameLine();
        ImGuiMCP::Checkbox("Info", &MCPLog::log_info);
        ImGuiMCP::SameLine();
        ImGuiMCP::Checkbox("Warning", &MCPLog::log_warning);
        ImGuiMCP::SameLine();
        ImGuiMCP::Checkbox("Error", &MCPLog::log_error);
        ImGuiMCP::InputText("Custom Filter", MCPLog::custom, 255);

        // if"Generate Log" button is pressed, read the log file
        if (ImGuiMCP::Button("Generate Log")) {
            logLines = MCPLog::ReadLogFile();
        }

        // Display each line in a new ImGuiMCP::Text() element
        for (const auto& line : logLines) {
            if (line.find("trace") != std::string::npos && !MCPLog::log_trace) continue;
            if (line.find("info") != std::string::npos && !MCPLog::log_info) continue;
            if (line.find("warning") != std::string::npos && !MCPLog::log_warning) continue;
            if (line.find("error") != std::string::npos && !MCPLog::log_error) continue;
            if (line.find(MCPLog::custom) == std::string::npos && MCPLog::custom != "") continue;
            ImGuiMCP::Text(line.c_str());
        }
    }
}

namespace MCPLog {
    std::filesystem::path GetLogPath() {
        const auto logsFolder = SKSE::log::log_directory();
        if (!logsFolder) SKSE::stl::report_and_fail("SKSE log_directory not provided, logs disabled.");
        auto pluginName = SKSE::PluginDeclaration::GetSingleton()->GetName();
        auto logFilePath = *logsFolder / std::format("{}.log", pluginName);
        return logFilePath;
    }

    std::vector<std::string> ReadLogFile() {
        std::vector<std::string> logLines;

        // Open the log file
        std::ifstream file(GetLogPath().c_str());
        if (!file.is_open()) {
            // Handle error
            return logLines;
        }

        // Read and store each line from the file
        std::string line;
        while (std::getline(file, line)) {
            logLines.push_back(line);
        }

        file.close();

        return logLines;
    }
}