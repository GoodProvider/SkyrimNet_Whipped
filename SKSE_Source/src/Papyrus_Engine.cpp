#include "DamageHealing_Engine.h"
#include "Overlay.h"

// Natives for Scripts/Source/SkyrimNet_Whipped_Engine.psc
namespace
{
    std::int32_t Hit(RE::StaticFunctionTag*, RE::Actor* actor)
    {
        const auto result = DamageHealing_Engine::Hit(actor);
        Overlay::Notify();
        return result;
    }

    bool HasOpenWounds(RE::StaticFunctionTag*, RE::Actor* actor)
    {
        return DamageHealing_Engine::HasOpenWounds(actor);
    }

    std::vector<RE::Actor*> TakeHealedActors(RE::StaticFunctionTag*)
    {
        auto result = DamageHealing_Engine::TakeHealedActors();
        Overlay::Notify();
        return result;
    }

    std::vector<std::int32_t> TakeHealed(RE::StaticFunctionTag*, RE::Actor* actor)
    {
        return DamageHealing_Engine::TakeHealed(actor);
    }

    std::vector<std::int32_t> SyncHealth(RE::StaticFunctionTag*)
    {
        auto result = DamageHealing_Engine::SyncHealth();
        Overlay::Notify();
        return result;
    }

    std::vector<RE::Actor*> SyncActors(RE::StaticFunctionTag*)
    {
        return DamageHealing_Engine::SyncActors();
    }

    std::vector<RE::Actor*> SyncCasters(RE::StaticFunctionTag*)
    {
        return DamageHealing_Engine::SyncCasters();
    }

    std::int32_t TrackedCount(RE::StaticFunctionTag*)
    {
        return DamageHealing_Engine::TrackedCount();
    }

    RE::BSFixedString PartName(RE::StaticFunctionTag*, std::int32_t part)
    {
        return DamageHealing_Engine::PartName(part);
    }

    RE::BSFixedString GetStateJson(RE::StaticFunctionTag*, RE::Actor* actor)
    {
        return DamageHealing_Engine::StateJson(actor);
    }

    void SetHoursPerPoint(RE::StaticFunctionTag*, float hours)
    {
        DamageHealing_Engine::SetHoursPerPoint(hours);
    }

    void SetRegenRate(RE::StaticFunctionTag*, float percent)
    {
        DamageHealing_Engine::SetRegenRate(percent);
    }

    void SetOverlayEnabled(RE::StaticFunctionTag*, bool enabled)
    {
        Overlay::SetEnabled(enabled);
    }

    void Forget(RE::StaticFunctionTag*, RE::Actor* actor)
    {
        DamageHealing_Engine::Forget(actor);
        Overlay::Notify();
    }
}

bool Register_Engine_Functions(RE::BSScript::IVirtualMachine* a_vm)
{
    constexpr std::string_view s = "SkyrimNet_Whipped_Engine";
    a_vm->RegisterFunction("Hit", s, Hit);
    a_vm->RegisterFunction("HasOpenWounds", s, HasOpenWounds);
    a_vm->RegisterFunction("TakeHealedActors", s, TakeHealedActors);
    a_vm->RegisterFunction("TakeHealed", s, TakeHealed);
    a_vm->RegisterFunction("SyncHealth", s, SyncHealth);
    a_vm->RegisterFunction("SyncActors", s, SyncActors);
    a_vm->RegisterFunction("SyncCasters", s, SyncCasters);
    a_vm->RegisterFunction("TrackedCount", s, TrackedCount);
    a_vm->RegisterFunction("PartName", s, PartName);
    a_vm->RegisterFunction("GetStateJson", s, GetStateJson);
    a_vm->RegisterFunction("SetHoursPerPoint", s, SetHoursPerPoint);
    a_vm->RegisterFunction("SetRegenRate", s, SetRegenRate);
    a_vm->RegisterFunction("SetOverlayEnabled", s, SetOverlayEnabled);
    a_vm->RegisterFunction("Forget", s, Forget);
    return true;
}
