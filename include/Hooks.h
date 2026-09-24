#pragma once

namespace Hooks {

    struct UpdateHook {
        static void Update(RE::Actor* a_this, float a_delta);
        static inline REL::Relocation<decltype(Update)> Update_;
    };

    struct DrawHook {
        static void thunk(std::uint32_t a_timer);
        static inline REL::Relocation<decltype(thunk)> func;
    };

    struct RefLoadHook {
        static RE::NiAVObject* Load3D(RE::TESObjectREFR* a_this, bool a_backgroundLoading);
        static inline REL::Relocation<decltype(Load3D)> Load3D_;
    };

    struct HazardLoadHook {
        static RE::NiAVObject* Load3D(RE::TESObjectREFR* a_this, bool a_backgroundLoading);
        static inline REL::Relocation<decltype(Load3D)> Load3D_;
    };

    inline void InstallHooks() {
        // Do not instyall RE::BSGraphics::Renderer::End in VR - CTD
        // use old UpdateHook::Update for VR
        if (!REL::Module::IsVR()) {
            auto& trampoline = SKSE::GetTrampoline();
            constexpr size_t size_per_hook = 14;
            trampoline.create(size_per_hook);

            const REL::Relocation<std::uintptr_t> target{
                REL::RelocationID(75461, 77246)};  // RE::BSGraphics::Renderer::End();
            DrawHook::func = trampoline.write_call<5>(target.address() + 0x9, DrawHook::thunk);
        }
        UpdateHook::Update_ = REL::Relocation<std::uintptr_t>(RE::VTABLE_PlayerCharacter[0])
                                  .write_vfunc(REL::Relocate(0xAD, 0xAD, 0xAF), UpdateHook::Update);

        RefLoadHook::Load3D_ =
            REL::Relocation<std::uintptr_t>(RE::VTABLE_TESObjectREFR[0]).write_vfunc(0x6A, RefLoadHook::Load3D);

        HazardLoadHook::Load3D_ =
            REL::Relocation<std::uintptr_t>(RE::VTABLE_Hazard[0]).write_vfunc(0x6A, HazardLoadHook::Load3D);
    }
}