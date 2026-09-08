#pragma once

namespace Hooks {

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
        
        auto& trampoline = SKSE::GetTrampoline();
        constexpr size_t size_per_hook = 14;
        trampoline.create(size_per_hook);

        const REL::Relocation<std::uintptr_t> target{REL::RelocationID(75461, 77246)};  // RE::BSGraphics::Renderer::End()
        DrawHook::func = trampoline.write_call<5>(target.address() + 0x9, DrawHook::thunk);

        RefLoadHook::Load3D_ =
            REL::Relocation<std::uintptr_t>(RE::VTABLE_TESObjectREFR[0]).write_vfunc(0x6A, RefLoadHook::Load3D);

        HazardLoadHook::Load3D_ =
            REL::Relocation<std::uintptr_t>(RE::VTABLE_Hazard[0]).write_vfunc(0x6A, HazardLoadHook::Load3D);
    }
}