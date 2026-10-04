#pragma once

#include <string>
#include <vector>

// Per-actor, per-body-part whip damage and healing.
//
// Each hit damages one random part. Wounds are tied to health: an actor's deficit (damage plus
// missing healing points over all hit parts) holds health down by deficit * Ratio, so the health
// bar shows the wounds. Health above that target is healing and is converted back into wound
// points by SyncHealth (full speed while a healing source is active, natural regen at the regen
// rate), one point at a time on a random part. Game time also drains damage, then accumulates healing; an actor is forgotten once every
// hit part is fully healed. Time is applied lazily on every access (no tick thread).
namespace DamageHealing_Engine
{
    constexpr std::uint32_t kRecord = 'DHEN';
    constexpr std::uint32_t kRecordVersion = 1;

    constexpr int kPartCount = 6;  // face, left arm, right arm, back, left leg, right leg

    // Hit levels (the part's state before the hit), used to pick the narration.
    enum HitLevel : int { kUndamaged = 0, kLight = 1, kMedium = 2, kSevere = 3 };

    // SyncHealth events and the healing source attached to them.
    enum SyncKind : int { kHealStart = 0, kPainGone = 1, kFullyHealed = 2, kHealStopped = 3 };
    enum SourceKind : int { kSourceNone = 0, kSourceSpell = 1, kSourceStaff = 2, kSourcePotion = 3, kSourceMagic = 4 };

    // Damages a random part and takes the matching health (never below 10% of max health).
    // Returns part * 4 + HitLevel, or -1 for None/dead actors.
    int Hit(RE::Actor* actor);

    // True if any hit part still has damage (false: untracked, or every hit part is healing).
    bool HasOpenWounds(RE::Actor* actor);

    // Applies time to every tracked actor; returns those with parts that healed to a new stage
    // since the last TakeHealed.
    std::vector<RE::Actor*> TakeHealedActors();

    // part * 8 + stage index (0 light .. 4 mostly healed, 5 healed) for each part that healed to a new stage
    // (game time or SyncHealth); clears them.
    std::vector<std::int32_t> TakeHealed(RE::Actor* actor);

    // Called from the MagicTarget::AddTarget hook for every effect applied to an actor.
    void NoteHealingSource(RE::Actor* target, RE::TESObjectREFR* caster, RE::MagicItem* item, RE::Effect* effect);

    // Converts healing on loaded tracked actors into wound points and returns the queued events as
    // kind * 10000 + source * 1000 + percent. SyncActors/SyncCasters are parallel to the last result.
    std::vector<std::int32_t> SyncHealth();
    std::vector<RE::Actor*> SyncActors();
    std::vector<RE::Actor*> SyncCasters();

    // Tracked actors plus events not yet taken by SyncHealth.
    int TrackedCount();

    // Narration noun for a part: "face", "left arm", ... ("" when out of range).
    std::string PartName(int part);

    // {"parts":[{"part":"left arm","region":"arm","stage":"medium","text":8,"damage":5,"healing":0},...]}
    // Hit parts only. "text" = region * 6 + stage index, the prompts' text-array index.
    std::string StateJson(RE::Actor* actor);

    // Overlay snapshot of loaded tracked actors, without applying time:
    // {"actors":[{"id":N,"name":"...","parts":[[damage,healing],...]}]} with parts in the order
    // head, body, right arm, left arm, right leg, left leg; a part never hit is [0,11] (healed).
    std::string OverlayJson();

    void SetHoursPerPoint(float hours);
    // Percent of natural regeneration that heals wounds (the rest of the regen is taken back).
    void SetRegenRate(float percent);
    void Forget(RE::Actor* actor);

    void Save(SKSE::SerializationInterface* intfc);
    void Load(SKSE::SerializationInterface* intfc, std::uint32_t version, std::uint32_t length);
    void Revert();
}
