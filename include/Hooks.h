#pragma once

namespace Hooks {

    struct UpdateHook {
        static void Update(RE::Actor* a_this, float a_delta);
        static inline REL::Relocation<decltype(Update)> Update_;
    };

    struct DrawHook {
        static void thunk(float a_timer);
        static inline REL::Relocation<decltype(thunk)> func;
    };

    struct RefLoadHook {
        static RE::NiAVObject* Load3D(RE::TESObjectREFR* a_this, bool a_backgroundLoading);
        static inline REL::Relocation<decltype(Load3D)> Load3D_;
    };

    inline void InstallHooks() {
        UpdateHook::Update_ = REL::Relocation<std::uintptr_t>(RE::VTABLE_PlayerCharacter[0])
                                  .write_vfunc(REL::Relocate(0xAD, 0xAD, 0xAF), UpdateHook::Update);

        RefLoadHook::Load3D_ =
            REL::Relocation<std::uintptr_t>(RE::VTABLE_TESObjectREFR[0]).write_vfunc(0x6A, RefLoadHook::Load3D);
    }
}