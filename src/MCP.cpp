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

                static bool drawBBox = false;
                static std::vector<RE::TESObjectREFR*> childrens;

                static BobbingConfig configCreator;

                auto base = ref->GetBaseObject();
                auto BobbingMgr = Bobbing::Manager::GetSingleton();

                bool hasRefConfig = BobbingMgr->HasConfig(ref->GetFormID());
                bool hasBaseConfig = BobbingMgr->HasConfig(base->GetFormID());

                if (lastRef != ref || ImGuiMCP::Button("Reset all settings to default")) {
                    drawBBox = false;
                    lastRef = ref;
                    configCreator = {};

                    childrens.clear();

                    if (hasRefConfig) {
                        auto cfg = BobbingMgr->GetConfig(ref->GetFormID());
                        configCreator = cfg;
                        for (auto& childID : cfg.childrens) {
                            auto form = RE::TESForm::LookupByID(childID);
                            auto childRef = form ? form->As<RE::TESObjectREFR>() : nullptr;
                            if (childRef) {
                                childrens.push_back(childRef);
                            }
                        }
                    }
                }

                std::string refLabel = hasRefConfig ? "Update Ref Config" : "Add Ref Config";
                std::string baseLabel = hasBaseConfig ? "Update Base Config" : "Add Base Config";

                bool updateOffsets = false;

                // --- Z movement ---
                ImGuiMCP::Text("Bobbing - Position");
                ImGuiMCP::SliderFloat3("Min Pos;", &configCreator.positionMin.x, -10.0f, 10.0f);
                ImGuiMCP::SliderFloat3("Max Pos;", &configCreator.positionMax.x, -10.0f, 10.0f);
                ImGuiMCP::InputFloat3("Min Pos:", &configCreator.positionMin.x);
                ImGuiMCP::InputFloat3("Max Pos:", &configCreator.positionMax.x);

                if (ImGuiMCP::Button("Reset Position")) {
                    configCreator.positionMin = {0.0f, 0.0f, -5.0f};
                    configCreator.positionMax = {0.0f, 0.0f, 5.0f};
                }

                // --- Rotation ---
                ImGuiMCP::Separator();
                ImGuiMCP::Text("Bobbing - Rotation");
                ImGuiMCP::SliderFloat3("Min Rot;", &configCreator.rotationMin.x, -0.2f, 0.2f);
                ImGuiMCP::SliderFloat3("Max Rot;", &configCreator.rotationMax.x, -0.2f, 0.2f);
                ImGuiMCP::InputFloat3("Min Rot:", &configCreator.rotationMin.x);
                ImGuiMCP::InputFloat3("Max Rot:", &configCreator.rotationMax.x);

                if (configCreator.positionMin.x > configCreator.positionMax.x)
                    std::swap(configCreator.positionMin.x, configCreator.positionMax.x);
                if (configCreator.positionMin.y > configCreator.positionMax.y)
                    std::swap(configCreator.positionMin.y, configCreator.positionMax.y);
                if (configCreator.positionMin.z > configCreator.positionMax.z)
                    std::swap(configCreator.positionMin.z, configCreator.positionMax.z);

                if (configCreator.rotationMin.x > configCreator.rotationMax.x)
                    std::swap(configCreator.rotationMin.x, configCreator.rotationMax.x);
                if (configCreator.rotationMin.y > configCreator.rotationMax.y)
                    std::swap(configCreator.rotationMin.y, configCreator.rotationMax.y);
                if (configCreator.rotationMin.z > configCreator.rotationMax.z)
                    std::swap(configCreator.rotationMin.z, configCreator.rotationMax.z);

                if (ImGuiMCP::Button("Reset Rotation")) {
                    configCreator.rotationMin = {-0.01f, -0.01f, -0.01f};
                    configCreator.rotationMin = {0.01f, 0.01f, 0.01f};
                }

                if (ImGuiMCP::CollapsingHeader("Advanced Options")) {
                    ImGuiMCP::SliderFloat3("Position Speed;", &configCreator.speedPos.x, 0.0f, 5.0f);
                    ImGuiMCP::InputFloat3("Position Speed:", &configCreator.speedPos.x);
                    ImGuiMCP::SliderFloat3("Rotation Speed;", &configCreator.speedRot.x, 0.0f, 5.0f);
                    ImGuiMCP::InputFloat3("Rotation Speed:", &configCreator.speedRot.x);
                    if (ImGuiMCP::Button("Randomize Speeds")) {
                        configCreator.speedPos.x = Utils::RandomFloat(0.9f, 1.1f);
                        configCreator.speedPos.y = Utils::RandomFloat(0.9f, 1.1f);
                        configCreator.speedPos.z = Utils::RandomFloat(0.9f, 1.1f);
                        configCreator.speedRot.x = Utils::RandomFloat(0.9f, 1.1f);
                        configCreator.speedRot.y = Utils::RandomFloat(0.9f, 1.1f);
                        configCreator.speedRot.z = Utils::RandomFloat(0.9f, 1.1f);
                    }
                    ImGuiMCP::SameLine();
                    if (ImGuiMCP::Button("Reset Speeds")) {
                        configCreator.speedPos = {1.0f, 1.0f, 1.0f};
                        configCreator.speedRot = {1.0f, 1.0f, 1.0f};
                    }

                    ImGuiMCP::SliderFloat("Phase Offset;", &configCreator.phaseOffset, -1.0f, 1.0f);
                    ImGuiMCP::InputFloat("Phase Offset:", &configCreator.phaseOffset);

                    ImGuiMCP::SliderFloat("Actor Influence;", &configCreator.actorInfluence, 0.0f, 10.0f);
                    ImGuiMCP::InputFloat("Actor Influence:", &configCreator.actorInfluence);
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
                    if (ImGuiMCP::DragFloat("Pos X", &configCreator.BBoxMaxOffset.x, 0.1f)) {
                        updateOffsets = true;
                    }
                    ImGuiMCP::PopStyleColor();

                    ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Text, uiRedNeg);
                    if (ImGuiMCP::DragFloat("Neg X", &configCreator.BBoxMinOffset.x, 0.1f)) {
                        updateOffsets = true;
                    }
                    ImGuiMCP::PopStyleColor();

                    ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Text, uiGreenPos);
                    if (ImGuiMCP::DragFloat("Pos Y", &configCreator.BBoxMaxOffset.y, 0.1f)) {
                        updateOffsets = true;
                    }
                    ImGuiMCP::PopStyleColor();

                    ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Text, uiGreenNeg);
                    if (ImGuiMCP::DragFloat("Neg Y", &configCreator.BBoxMinOffset.y, 0.1f)) {
                        updateOffsets = true;
                    }
                    ImGuiMCP::PopStyleColor();

                    ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Text, uiBluePos);
                    if (ImGuiMCP::DragFloat("Pos Z", &configCreator.BBoxMaxOffset.z, 0.1f)) {
                        updateOffsets = true;
                    }
                    ImGuiMCP::PopStyleColor();

                    ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Text, uiBlueNeg);
                    if (ImGuiMCP::DragFloat("Neg Z", &configCreator.BBoxMinOffset.z, 0.1f)) {
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

                    auto Bbox = Utils::GetBoundingBox(ref);

                    // --- Compute center ---
                    RE::NiPoint3 center{0, 0, 0};
                    for (auto& point : Bbox) center += point;
                    center /= 8.0f;

                    // --- Compute axes + extents ---
                    RE::NiPoint3 axisX = Bbox[1] - Bbox[0];
                    float extentX = axisX.Length();
                    axisX /= extentX;

                    RE::NiPoint3 axisY = Bbox[3] - Bbox[0];
                    float extentY = axisY.Length();
                    axisY /= extentY;

                    RE::NiPoint3 axisZ = Bbox[4] - Bbox[0];
                    float extentZ = axisZ.Length();
                    axisZ /= extentZ;

                    extentX *= 0.5f;
                    extentY *= 0.5f;
                    extentZ *= 0.5f;

                    // --- OBB test ---
                    auto isInsideOBB = [&](const RE::NiPoint3& point) {
                        RE::NiPoint3 d = point - center;

                        float dx = d.Dot(axisX);
                        if (dx > (extentX + configCreator.BBoxMaxOffset.x)) return false;
                        if (dx < -(extentX + configCreator.BBoxMinOffset.x)) return false;

                        float dy = d.Dot(axisY);
                        if (dy > (extentY + configCreator.BBoxMaxOffset.y)) return false;
                        if (dy < -(extentY + configCreator.BBoxMinOffset.y)) return false;

                        float dz = d.Dot(axisZ);
                        if (dz > (extentZ + configCreator.BBoxMaxOffset.z)) return false;
                        if (dz < -(extentZ + configCreator.BBoxMinOffset.z)) return false;

                        return true;
                    };

                    // --- Broad phase radius ---
                    float radius = 0.0f;
                    for (auto& point : Bbox) {
                        float dist = (point - center).Length();
                        if (dist > radius) radius = dist;
                    }

                    float maxOffset =
                        std::max({fabs(configCreator.BBoxMaxOffset.x), fabs(configCreator.BBoxMinOffset.x),
                                  fabs(configCreator.BBoxMaxOffset.y), fabs(configCreator.BBoxMinOffset.y),
                                  fabs(configCreator.BBoxMaxOffset.z), fabs(configCreator.BBoxMinOffset.z)});

                    radius += maxOffset;

                    auto AdjustedBBox = Bbox;

                    for (size_t i = 0; i < 8; ++i) {
                        RE::NiPoint3 rel = AdjustedBBox[i] - center;

                        float dx = rel.Dot(axisX);
                        float dy = rel.Dot(axisY);
                        float dz = rel.Dot(axisZ);

                        RE::NiPoint3 ofs{0.0f, 0.0f, 0.0f};

                        ofs += (dx >= 0.0f ? axisX : -axisX) *
                               (dx >= 0.0f ? configCreator.BBoxMaxOffset.x : configCreator.BBoxMinOffset.x);
                        ofs += (dy >= 0.0f ? axisY : -axisY) *
                               (dy >= 0.0f ? configCreator.BBoxMaxOffset.y : configCreator.BBoxMinOffset.y);
                        ofs += (dz >= 0.0f ? axisZ : -axisZ) *
                               (dz >= 0.0f ? configCreator.BBoxMaxOffset.z : configCreator.BBoxMinOffset.z);

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
                            if (other->IsWater()) return RE::BSContainer::ForEachResult::kContinue;
                            auto pos = other->GetPosition();
                            if (!isInsideOBB(pos)) return RE::BSContainer::ForEachResult::kContinue;
                            if (auto node = other->Get3D()) {
                                node->TintScenegraph(HighlightColor);
                            }
                            childrens.push_back(other);
                            return RE::BSContainer::ForEachResult::kContinue;
                        });
                    }

                    static char addChildFormID[255];  // hex input for manual add
                    static char Filter[255];
                    static bool keepChildListOpen = false;  // keep header open when removing/adding

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
                                        for (auto* child : childrens) {
                                            if (child->GetFormID() == childRef->GetFormID()) {
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
                    ImGuiMCP::InputText("Filter", Filter, 255);

                    std::set<RE::FormType> typesSet;
                    for (auto* child : childrens) {
                        if (!child) continue;
                        if (child->IsDynamicForm()) continue;
                        if (auto childBase = child->GetBaseObject()) {
                            typesSet.insert(childBase->GetFormType());
                        }
                    }

                    std::vector<RE::FormType> typesVec(typesSet.begin(), typesSet.end());

                    // persistent selection set
                    static std::unordered_set<RE::FormType> selectedTypeSet;

                    // UI: checkboxes 5 per line
                    ImGuiMCP::Text("Filter childrens by object type (multi-select):");
                    int cols = 5;
                    int idx = 0;
                    for (auto t : typesVec) {
                        const auto typeName = FormTypeToString(t);
                        const std::string label = std::format("{}##type{}", typeName, static_cast<int>(t));
                        bool checked = selectedTypeSet.contains(t);
                        if (ImGuiMCP::Checkbox(label.c_str(), &checked)) {
                            if (checked) {
                                selectedTypeSet.insert(t);
                            } else {
                                selectedTypeSet.erase(t);
                            }
                        }
                        ++idx;
                        if ((idx % cols) != 0 && idx < typesVec.size()) ImGuiMCP::SameLine();
                    }

                    if (ImGuiMCP::Button("Highlight selected types")) {
                        if (selectedTypeSet.empty()) {
                        } else {
                            for (auto* child : childrens) {
                                if (!child) continue;
                                if (auto childBase = child->GetBaseObject()) {
                                    if (selectedTypeSet.contains(childBase->GetFormType())) {
                                        child->Get3D()->TintScenegraph(HighlightColor);
                                    }
                                }
                            }
                        }
                    }
                    ImGuiMCP::SameLine();
                    if (ImGuiMCP::Button("UnHighlight selected types")) {
                        if (selectedTypeSet.empty()) {
                        } else {
                            for (auto* child : childrens) {
                                if (!child) continue;
                                if (auto childBase = child->GetBaseObject()) {
                                    if (selectedTypeSet.contains(childBase->GetFormType())) {
                                        child->Get3D()->TintScenegraph(RE::NiColorA(0.0f, 0.0f, 0.0f, 0.0f));
                                    }
                                }
                            }
                        }
                    }
                    ImGuiMCP::SameLine();
                    if (ImGuiMCP::Button("Remove selected types")) {
                        if (selectedTypeSet.empty()) {
                        } else {
                            // remove children matching any selected type
                            auto newEnd = std::remove_if(childrens.begin(), childrens.end(), [&](RE::TESObjectREFR* c) {
                                if (!c) return false;
                                if (auto childBase = c->GetBaseObject()) {
                                    if (selectedTypeSet.contains(childBase->GetFormType())) {
                                        if (c->Get3D()) {
                                            c->Get3D()->TintScenegraph(RE::NiColorA(0.0f, 0.0f, 0.0f, 0.0f));
                                            c->Disable();
                                            c->Enable(false);
                                        }
                                        return true;
                                    }
                                }
                                return false;
                            });
                            childrens.erase(newEnd, childrens.end());
                        }
                    }

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

                    static size_t filtered = 0;
                    for (size_t i = 0; i < childrens.size(); ++i) {
                        auto* child = childrens[i];
                        if (!child) continue;
                        if (child->IsDynamicForm()) continue;
                        auto childFormID = child->GetFormID();
                        std::string id = "Unknown";

                        if (auto childBase = child->GetBaseObject()) {
                            id = clib_util::editorID::get_editorID(child->GetBaseObject());
                            std::string childFormIDStr = std::format("{:08X}", childFormID);
                            if (Filter[0] && (id.find(Filter) == std::string::npos &&
                                              childFormIDStr.find(Filter) == std::string::npos)) {
                                filtered++;
                                continue;
                            }
                            if (!selectedTypeSet.empty()) {
                                if (!selectedTypeSet.contains(childBase->GetFormType())) {
                                    filtered++;
                                    continue;
                                }
                            }
                        }
                    }
                    std::string label =
                        std::format("Selected Ref Childrens: {}/{}###ChildList", childrens.size() - filtered, childrens.size());
                    filtered = 0;
                    if (keepChildListOpen) {
                        ImGuiMCP::SetNextItemOpen(true, ImGuiMCP::ImGuiCond_Always);
                    }
                    if (ImGuiMCP::CollapsingHeader(label.c_str())) {
                        // ensure stable ordering by FormID
                        std::sort(childrens.begin(), childrens.end(), [](RE::TESObjectREFR* a, RE::TESObjectREFR* b) {
                            return a->GetFormID() < b->GetFormID();
                        });

                        std::optional<size_t> toRemove;
                        for (size_t i = 0; i < childrens.size(); ++i) {
                            auto* child = childrens[i];
                            if (!child) continue;
                            if (child->IsDynamicForm()) continue;
                            auto childFormID = child->GetFormID();
                            std::string id = "Unknown";
                            std::string typeName = "Unknown";

                            if (auto childBase = child->GetBaseObject()) {
                                id = clib_util::editorID::get_editorID(child->GetBaseObject());
                                std::string childFormIDStr = std::format("{:08X}", childFormID);
                                if (Filter[0] && (id.find(Filter) == std::string::npos &&
                                                  childFormIDStr.find(Filter) == std::string::npos)) {
                                    continue;
                                }
                                
                                if (!selectedTypeSet.empty()) {
                                    if (!selectedTypeSet.contains(childBase->GetFormType())) {
                                        continue;
                                    }
                                }
                                typeName = FormTypeToString(childBase->GetFormType());
                            }
                            ImGuiMCP::PushID(childFormID);
                            ImGuiMCP::Text("0x%08X", childFormID);
                            ImGuiMCP::SameLine();
                            ImGuiMCP::Text("%s", id.c_str());
                            ImGuiMCP::SameLine();
                            ImGuiMCP::Text("%s", typeName.c_str());
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
                        keepChildListOpen = false;
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

                        // Build new BobbingConfig from UI
                        BobbingConfig newCfg = configCreator;
                        newCfg.formID = ref->GetFormID();
                        newCfg.childrens.clear();
                        for (auto* child : childrens) newCfg.childrens.insert(child->GetFormID());

                        // If an existing config exists, remove it first to ensure clean save
                        if (hasRefConfig) {
                            BobbingMgr->RemoveConfig(ref->GetFormID());
                        }

                        // Add and save
                        BobbingMgr->AddNewConfig(newCfg, true);

                        for (auto* child : childrens) {
                            BobbingMgr->RefLoad(child);
                        }
                        BobbingMgr->RefLoad(ref);
                    }

                    if (hasRefConfig) {
                        BobbingConfig newCfg = configCreator;
                        newCfg.formID = ref->GetFormID();
                        newCfg.childrens.clear();
                        for (auto* child : childrens) newCfg.childrens.insert(child->GetFormID());
                        BobbingMgr->AddNewConfig(newCfg, false);

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
                }
                if (ImGuiMCP::Button(baseLabel.c_str())) {
                    std::set<RE::FormID> childrensFormIDs;  // Always empty for Base

                    // Build config for base
                    BobbingConfig newCfg = configCreator;
                    newCfg.formID = ref->GetBaseObject()->GetFormID();
                    BobbingMgr->AddNewConfig(newCfg, true);

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