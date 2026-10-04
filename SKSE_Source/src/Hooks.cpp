#include "DamageHealing_Engine.h"

// MagicTarget::AddTarget on Character and PlayerCharacter: sees every effect applied to an actor,
// with the magic item and caster (instant potions included), so healing sources can be named.
namespace
{
    constexpr std::size_t kMagicTargetVtbl = 4;  // Actor bases: TESObjectREFR (0-3), MagicTarget (4)
    constexpr std::size_t kAddTargetIdx = 0x1;

    template <class T>
    struct AddTarget
    {
        static bool thunk(RE::MagicTarget* a_this, RE::MagicTarget::AddTargetData& a_data)
        {
            const bool added = func(a_this, a_data);
            if (added) {
                auto* actor = skyrim_cast<RE::Actor*>(a_this);
                DamageHealing_Engine::NoteHealingSource(actor, a_data.caster, a_data.magicItem, a_data.effect);
            }
            return added;
        }
        static inline REL::Relocation<decltype(thunk)> func;
    };
}

void Install_Hooks()
{
    REL::Relocation<std::uintptr_t> character{ RE::VTABLE_Character[kMagicTargetVtbl] };
    AddTarget<RE::Character>::func = character.write_vfunc(kAddTargetIdx, AddTarget<RE::Character>::thunk);

    REL::Relocation<std::uintptr_t> player{ RE::VTABLE_PlayerCharacter[kMagicTargetVtbl] };
    AddTarget<RE::PlayerCharacter>::func = player.write_vfunc(kAddTargetIdx, AddTarget<RE::PlayerCharacter>::thunk);

    SKSE::log::info("MagicTarget::AddTarget hooks installed");
}
