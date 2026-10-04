#include "DamageHealing_Engine.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <format>
#include <mutex>
#include <random>
#include <unordered_map>
#include <vector>

namespace DamageHealing_Engine
{
    namespace
    {
        constexpr int kMaxDamage = 20;
        constexpr int kHealed = 11;  // healing at this level is "healed"; the part stops accumulating
        constexpr int kStageHealed = 5;
        constexpr int kMaxDeficit = kMaxDamage + kHealed;  // per part: full damage, no healing yet
        constexpr float kWoundShare = 0.9f;   // every part at kMaxDeficit holds health down by 90%
        constexpr float kHealthFloor = 0.1f;  // a lash never takes health below 10% of max
        constexpr double kSourceLinger = 1.0;  // real seconds a healing source stays active after last seen

        struct Part
        {
            bool hit = false;
            std::int32_t damage = 0;
            std::int32_t healing = 0;
            double lastHours = 0.0;  // game hours when time was last applied
        };
        using Parts = std::array<Part, kPartCount>;

        // Body region per part (prompt text tables are per region): 0 head, 1 body, 2 arm, 3 leg.
        constexpr std::array<int, kPartCount> kRegion{ 0, 2, 2, 1, 3, 3 };
        constexpr std::array<std::string_view, 4> kRegionName{ "head", "body", "arm", "leg" };
        constexpr std::array<std::string_view, kPartCount> kPartName{
            "face", "left arm", "right arm", "back", "left leg", "right leg"
        };
        constexpr std::array<std::string_view, 6> kStageName{
            "light", "medium", "severe", "recovering", "mostly_healed", "healed"
        };

        // The healing source (spell, staff, potion, ...) currently working on an actor. Transient.
        struct Healer
        {
            RE::ActorHandle caster;
            int source = kSourceNone;
            int startDeficit = 0;
            double lastSeen = 0.0;  // real seconds
        };

        struct Event
        {
            RE::FormID actor = 0;
            RE::ActorHandle caster;
            int kind = 0;
            int source = kSourceNone;
            int percent = 0;
        };

        std::mutex g_lock;
        std::unordered_map<RE::FormID, Parts> g_actors;
        // Heal transitions not yet taken by Papyrus: latest stage per part, -1 = none. Transient.
        using Healed = std::array<int, kPartCount>;
        std::unordered_map<RE::FormID, Healed> g_healed;
        std::unordered_map<RE::FormID, Healer> g_healers;
        std::unordered_map<RE::FormID, bool> g_pained;       // had damage at the last sync
        std::unordered_map<RE::FormID, float> g_regenBank;   // regen health not yet a whole point
        std::vector<Event> g_events;                         // not yet taken by SyncHealth
        std::vector<RE::Actor*> g_syncActors;
        std::vector<RE::Actor*> g_syncCasters;
        float g_hoursPerPoint = 1.0f;
        float g_regenRate = 10.0f;

        double NowHours()
        {
            auto* cal = RE::Calendar::GetSingleton();
            return cal ? static_cast<double>(cal->GetHoursPassed()) : 0.0;
        }

        double NowSeconds()
        {
            using namespace std::chrono;
            return duration<double>(steady_clock::now().time_since_epoch()).count();
        }

        std::mt19937& Rng()
        {
            static std::mt19937 rng{ std::random_device{}() };
            return rng;
        }

        int RandomIndex(int count)
        {
            return std::uniform_int_distribution<int>(0, count - 1)(Rng());
        }

        int StageIndex(const Part& p)
        {
            if (p.damage >= 11) return 2;
            if (p.damage >= 4) return 1;
            if (p.damage >= 1) return 0;
            if (p.healing >= kHealed) return kStageHealed;
            if (p.healing >= 4) return 4;
            return 3;
        }

        // Damage plus missing healing points over all hit parts.
        int Deficit(const Parts& parts)
        {
            int total = 0;
            for (const auto& p : parts) {
                if (p.hit) {
                    total += p.damage + (kHealed - p.healing);
                }
            }
            return total;
        }

        int TotalDamage(const Parts& parts)
        {
            int total = 0;
            for (const auto& p : parts) {
                if (p.hit) {
                    total += p.damage;
                }
            }
            return total;
        }

        // Health per wound point.
        float Ratio(float maxHealth)
        {
            return kWoundShare * maxHealth / static_cast<float>(kPartCount * kMaxDeficit);
        }

        // DEBUG-WHIP begin
        std::string DbgParts(const Parts& parts)
        {
            std::string s;
            for (int i = 0; i < kPartCount; ++i) {
                const Part& p = parts[i];
                if (p.hit) {
                    s += std::format("{}{}:d{}/h{}", s.empty() ? "" : " ", kPartName[i], p.damage, p.healing);
                }
            }
            return s.empty() ? "(none)" : s;
        }

        std::string DbgHealth(RE::Actor* actor)
        {
            auto* av = actor->AsActorValueOwner();
            const float maxHealth = av->GetPermanentActorValue(RE::ActorValue::kHealth);
            return std::format("hp:{:.1f}/{:.1f} ratio:{:.2f}", av->GetActorValue(RE::ActorValue::kHealth), maxHealth,
                Ratio(maxHealth));
        }

        bool DbgSame(const Parts& a, const Parts& b)
        {
            for (int i = 0; i < kPartCount; ++i) {
                if (a[i].hit != b[i].hit || a[i].damage != b[i].damage || a[i].healing != b[i].healing) {
                    return false;
                }
            }
            return true;
        }
        // DEBUG-WHIP end

        // A part reached a new stage while healing: queue it for TakeHealed (latest stage wins).
        void QueueHealed(RE::FormID id, int part, int stage)
        {
            auto [it, inserted] = g_healed.try_emplace(id);
            if (inserted) {
                it->second.fill(-1);
            }
            it->second[part] = stage;
        }

        // Spend elapsed heal points: first drain damage to 0, then raise healing.
        // A part that reaches a new stage is queued in g_healed.
        void Advance(RE::FormID id, Parts& parts, double now)
        {
            const double step = g_hoursPerPoint;
            for (int i = 0; i < kPartCount; ++i) {
                Part& p = parts[i];
                if (!p.hit) {
                    continue;
                }
                if ((p.damage == 0 && p.healing >= kHealed) || now < p.lastHours) {
                    p.lastHours = now;
                    continue;
                }
                const int points = static_cast<int>(std::floor((now - p.lastHours) / step));
                if (points <= 0) {
                    continue;
                }
                const int before = StageIndex(p);
                const double dbgElapsed = now - p.lastHours;  // DEBUG-WHIP
                const int dbgHealing = p.healing;             // DEBUG-WHIP
                p.lastHours += points * step;
                const int drained = std::min(points, static_cast<int>(p.damage));
                p.damage -= drained;
                p.healing = std::min(kHealed, p.healing + (points - drained));
                const int dbgAfter = StageIndex(p);  // DEBUG-WHIP
                SKSE::log::info("[dbg] advance {:08X} {} elapsed:{:.2f}h points:{} drained:{} damage->{} healing:{}->{} stage:{}->{}{}",  // DEBUG-WHIP
                    id, kPartName[i], dbgElapsed, points, drained, p.damage, dbgHealing, p.healing, kStageName[before],  // DEBUG-WHIP
                    kStageName[dbgAfter], dbgAfter != before ? " queued" : "");  // DEBUG-WHIP
                if (const int after = StageIndex(p); after != before) {
                    QueueHealed(id, i, after);
                }
            }
        }

        bool FullyHealed(const Parts& parts)
        {
            for (const auto& p : parts) {
                if (p.hit && (p.damage > 0 || p.healing < kHealed)) {
                    return false;
                }
            }
            return true;
        }

        // One wound point healed on a random part that still needs it, like Hit: damage drains
        // first, then healing rises. A new stage is queued in g_healed. False when nothing is left.
        bool HealPoint(RE::FormID id, Parts& parts, double now)
        {
            std::array<int, kPartCount> open{};
            int count = 0;
            for (int i = 0; i < kPartCount; ++i) {
                const Part& p = parts[i];
                if (p.hit && (p.damage > 0 || p.healing < kHealed)) {
                    open[count++] = i;
                }
            }
            if (count == 0) {
                return false;
            }
            const int index = open[RandomIndex(count)];
            Part& p = parts[index];
            const int before = StageIndex(p);
            if (p.damage > 0) {
                --p.damage;
            } else {
                ++p.healing;
            }
            p.lastHours = now;
            SKSE::log::info("[dbg] heal point {} damage:{} healing:{}", kPartName[index], p.damage, p.healing);  // DEBUG-WHIP
            if (const int after = StageIndex(p); after != before) {
                QueueHealed(id, index, after);
            }
            return true;
        }

        int Percent(const Healer& healer, int deficit)
        {
            if (healer.startDeficit <= 0) {
                return 100;
            }
            return std::clamp(100 * (healer.startDeficit - deficit) / healer.startDeficit, 0, 100);
        }

        Healer* ActiveHealer(RE::FormID id, double now)
        {
            const auto it = g_healers.find(id);
            if (it == g_healers.end() || now - it->second.lastSeen >= kSourceLinger) {
                return nullptr;
            }
            return &it->second;
        }

        void PushEvent(RE::FormID id, int kind, const Healer* healer, int percent)
        {
            Event e;
            e.actor = id;
            e.kind = kind;
            e.percent = percent;
            if (healer) {
                e.caster = healer->caster;
                e.source = healer->source;
            }
            g_events.push_back(e);
        }

        void Erase(RE::FormID id)
        {
            SKSE::log::info("[dbg] erase {:08X}", id);  // DEBUG-WHIP
            g_actors.erase(id);
            g_healed.erase(id);
            g_healers.erase(id);
            g_pained.erase(id);
            g_regenBank.erase(id);
        }

        // Applies time to the actor's entry; drops it when fully healed (queuing kFullyHealed) or
        // the actor is dead. Returns the entry or nullptr. Caller holds g_lock.
        Parts* Current(RE::Actor* actor, double now)
        {
            const RE::FormID id = actor->GetFormID();
            const auto it = g_actors.find(id);
            if (it == g_actors.end()) {
                return nullptr;
            }
            Advance(id, it->second, now);
            if (actor->IsDead()) {
                SKSE::log::info("forget {:08X} (dead)", id);
                Erase(id);
                return nullptr;
            }
            if (FullyHealed(it->second)) {
                SKSE::log::info("forget {:08X} (healed)", id);
                PushEvent(id, kFullyHealed, ActiveHealer(id, NowSeconds()), 100);
                Erase(id);
                return nullptr;
            }
            return &it->second;
        }

        bool IsHealing(const RE::EffectSetting* mgef, float magnitude)
        {
            using Flag = RE::EffectSetting::EffectSettingData::Flag;
            return mgef && magnitude > 0.0f &&
                   mgef->GetArchetype() == RE::EffectSetting::Archetype::kValueModifier &&
                   mgef->data.primaryAV == RE::ActorValue::kHealth && !mgef->IsDetrimental() &&
                   !mgef->data.flags.any(Flag::kRecover) &&
                   mgef->data.castingType != RE::MagicSystem::CastingType::kConstantEffect;
        }

        int SourceOf(RE::MagicItem* item)
        {
            if (!item) {
                return kSourceMagic;
            }
            using Type = RE::MagicSystem::SpellType;
            switch (item->GetSpellType()) {
            case Type::kSpell:
            case Type::kPower:
            case Type::kLesserPower:
            case Type::kVoicePower:
            case Type::kScroll:
                return kSourceSpell;
            case Type::kStaffEnchantment:
                return kSourceStaff;
            case Type::kPotion:
                return item->IsFood() ? kSourceMagic : kSourcePotion;
            default:
                return kSourceMagic;
            }
        }

        // A healing source touched a tracked actor: refresh it, or start one (queuing kHealStart).
        void Seen(RE::FormID id, const Parts& parts, RE::Actor* caster, RE::MagicItem* item, double now)
        {
            if (const auto it = g_healers.find(id); it != g_healers.end()) {
                it->second.lastSeen = now;
                return;
            }
            const int deficit = Deficit(parts);
            if (deficit <= 0) {
                return;
            }
            Healer healer;
            if (caster) {
                healer.caster = caster->GetHandle();
            }
            healer.source = SourceOf(item);
            healer.startDeficit = deficit;
            healer.lastSeen = now;
            g_healers[id] = healer;
            PushEvent(id, kHealStart, &healer, 0);
            SKSE::log::info("heal start {:08X} source:{} caster:{:08X} deficit:{}", id, healer.source,
                caster ? caster->GetFormID() : 0, deficit);
        }

        // Healing effects still running on the actor (concentration beams, heal over time).
        void ScanActiveEffects(RE::Actor* actor, RE::FormID id, const Parts& parts, double now)
        {
            auto* target = actor->AsMagicTarget();
            auto* list = target ? target->GetActiveEffectList() : nullptr;
            if (!list) {
                return;
            }
            using Flag = RE::ActiveEffect::Flag;
            for (auto* ae : *list) {
                if (!ae || ae->flags.any(Flag::kInactive, Flag::kDispelled) || !IsHealing(ae->GetBaseObject(), ae->magnitude)) {
                    continue;
                }
                Seen(id, parts, ae->caster.get().get(), ae->spell, now);
            }
        }

        // Health above MaxHealth - deficit * ratio is healing: turn it into wound points. A healing
        // source converts all of it; natural regen only g_regenRate percent, the rest is taken back.
        void ConvertHealth(RE::Actor* actor, RE::FormID id, Parts& parts, double now, bool fullSpeed)
        {
            auto* av = actor->AsActorValueOwner();
            const float maxHealth = av->GetPermanentActorValue(RE::ActorValue::kHealth);
            if (maxHealth <= 0.0f) {
                return;
            }
            const float ratio = Ratio(maxHealth);
            // Never below kHealthFloor: the take-back below must not push an actor into bleedout.
            const float wounded = std::max(maxHealth - Deficit(parts) * ratio, kHealthFloor * maxHealth);
            const float excess = av->GetActorValue(RE::ActorValue::kHealth) - wounded;
            if (excess <= 0.0f) {
                return;
            }
            const float dbgHealth = av->GetActorValue(RE::ActorValue::kHealth);  // DEBUG-WHIP
            const int dbgDeficit = Deficit(parts);                               // DEBUG-WHIP
            int points;
            if (fullSpeed) {
                points = static_cast<int>(std::floor(excess / ratio));
            } else {
                float& bank = g_regenBank[id];
                bank += excess * g_regenRate / 100.0f;
                points = static_cast<int>(std::floor(bank / ratio));
                bank -= points * ratio;
            }
            int healed = 0;
            while (healed < points && HealPoint(id, parts, now)) {
                ++healed;
            }
            if (!fullSpeed) {
                if (const float extra = excess - healed * ratio; extra > 0.0f) {
                    av->DamageActorValue(RE::ActorValue::kHealth, extra);
                }
            }
            // DEBUG-WHIP begin
            if (healed > 0) {
                SKSE::log::info("[dbg] convert {:08X} {} hp:{:.1f}/{:.1f} wounded:{:.1f} deficit:{}->{} excess:{:.2f} ratio:{:.2f} "
                                "points:{} healed:{} bank:{:.2f} taken_back:{:.2f} | {}",
                    id, fullSpeed ? "source" : "regen", dbgHealth, maxHealth, wounded, dbgDeficit,
                    Deficit(parts), excess, ratio, points, healed, fullSpeed ? 0.0f : g_regenBank[id],
                    fullSpeed ? 0.0f : std::max(0.0f, excess - healed * ratio), DbgParts(parts));
            }
            // DEBUG-WHIP end
        }

        // Health for wound points just added, never below kHealthFloor of max (whipping can't kill).
        void TakeHealth(RE::Actor* actor, int points)
        {
            auto* av = actor->AsActorValueOwner();
            const float maxHealth = av->GetPermanentActorValue(RE::ActorValue::kHealth);
            const float health = av->GetActorValue(RE::ActorValue::kHealth);
            const float floor = kHealthFloor * maxHealth;
            if (points <= 0 || maxHealth <= 0.0f || health <= floor) {
                SKSE::log::info("[dbg] take health skipped points:{} hp:{:.1f}/{:.1f} floor:{:.1f}", points, health, maxHealth, floor);  // DEBUG-WHIP
                return;
            }
            const float loss = std::min(points * Ratio(maxHealth), health - floor);
            av->DamageActorValue(RE::ActorValue::kHealth, loss);
            SKSE::log::info("[dbg] take health points:{} hp:{:.1f}/{:.1f} floor:{:.1f} loss:{:.1f}{}", points, health, maxHealth,  // DEBUG-WHIP
                floor, loss, loss < points * Ratio(maxHealth) ? " (floored)" : "");  // DEBUG-WHIP
        }
    }

    int Hit(RE::Actor* actor)
    {
        if (!actor || actor->IsDead()) {
            return -1;
        }
        std::lock_guard lock(g_lock);
        const double now = NowHours();
        Parts* existing = Current(actor, now);
        Parts& parts = existing ? *existing : g_actors[actor->GetFormID()];
        const int deficitBefore = Deficit(parts);
        SKSE::log::info("[dbg] hit {:08X} before {} deficit:{} | {}", actor->GetFormID(), DbgHealth(actor), deficitBefore, DbgParts(parts));  // DEBUG-WHIP

        const int index = RandomIndex(kPartCount);
        Part& p = parts[index];
        if (const auto healed = g_healed.find(actor->GetFormID()); healed != g_healed.end()) {
            healed->second[index] = -1;  // the hit overrides any heal not yet narrated
        }
        const char* dbgBranch = !p.hit ? "new" : p.damage == 0 ? "reopened" : "increment";  // DEBUG-WHIP
        int level;
        if (!p.hit) {
            level = kUndamaged;
            p.hit = true;
            p.damage = 1;
            p.healing = 0;
            p.lastHours = now;
        } else if (p.damage == 0) {
            // A healing part reopens straight to medium damage.
            level = kLight;
            p.damage = 4;
            p.healing = 0;
            p.lastHours = now;
        } else {
            level = p.damage >= 11 ? kSevere : p.damage >= 4 ? kMedium : kLight;
            p.damage = std::min(kMaxDamage, p.damage + 1);
        }
        g_pained[actor->GetFormID()] = true;
        TakeHealth(actor, Deficit(parts) - deficitBefore);
        SKSE::log::info("hit {:08X} {} level:{} damage:{}", actor->GetFormID(), kPartName[index], level, p.damage);
        SKSE::log::info("[dbg] hit {:08X} after {} {} deficit:{} | {}", actor->GetFormID(), dbgBranch, DbgHealth(actor),  // DEBUG-WHIP
            Deficit(parts), DbgParts(parts));  // DEBUG-WHIP
        return index * 4 + level;
    }

    bool HasOpenWounds(RE::Actor* actor)
    {
        if (!actor) {
            return false;
        }
        std::lock_guard lock(g_lock);
        const Parts* parts = Current(actor, NowHours());
        return parts && TotalDamage(*parts) > 0;
    }

    std::vector<RE::Actor*> TakeHealedActors()
    {
        std::lock_guard lock(g_lock);
        const double now = NowHours();
        std::vector<RE::FormID> ids;
        ids.reserve(g_actors.size());
        for (const auto& [id, parts] : g_actors) {
            ids.push_back(id);
        }
        for (const auto id : ids) {
            if (auto* actor = RE::TESForm::LookupByID<RE::Actor>(id)) {
                Current(actor, now);  // applies time; may queue heals and forget the actor
            }
        }
        std::vector<RE::Actor*> result;
        for (auto it = g_healed.begin(); it != g_healed.end();) {
            auto* actor = RE::TESForm::LookupByID<RE::Actor>(it->first);
            const bool any = std::ranges::any_of(it->second, [](int stage) { return stage >= 0; });
            if (!actor || !any) {
                it = g_healed.erase(it);
                continue;
            }
            result.push_back(actor);
            ++it;
        }
        // DEBUG-WHIP begin
        for (auto* actor : result) {
            SKSE::log::info("[dbg] take healed actors {:08X}", actor->GetFormID());
        }
        // DEBUG-WHIP end
        return result;
    }

    std::vector<std::int32_t> TakeHealed(RE::Actor* actor)
    {
        std::vector<std::int32_t> codes;
        if (!actor) {
            return codes;
        }
        std::lock_guard lock(g_lock);
        const auto it = g_healed.find(actor->GetFormID());
        if (it == g_healed.end()) {
            return codes;
        }
        for (int i = 0; i < kPartCount; ++i) {
            if (it->second[i] >= 0) {
                codes.push_back(i * 8 + it->second[i]);
                SKSE::log::info("[dbg] take healed {:08X} {} stage:{}", it->first, kPartName[i], kStageName[it->second[i]]);  // DEBUG-WHIP
            }
        }
        g_healed.erase(it);
        return codes;
    }

    void NoteHealingSource(RE::Actor* target, RE::TESObjectREFR* caster, RE::MagicItem* item, RE::Effect* effect)
    {
        if (!target || !effect || !IsHealing(effect->baseEffect, effect->effectItem.magnitude)) {
            return;
        }
        std::lock_guard lock(g_lock);
        const auto it = g_actors.find(target->GetFormID());
        if (it == g_actors.end()) {
            return;
        }
        SKSE::log::info("[dbg] healing source {:08X} item:{:08X} mgef:{:08X} magnitude:{:.1f} caster:{:08X}", it->first,  // DEBUG-WHIP
            item ? item->GetFormID() : 0, effect->baseEffect->GetFormID(), effect->effectItem.magnitude,  // DEBUG-WHIP
            caster ? caster->GetFormID() : 0);  // DEBUG-WHIP
        Seen(it->first, it->second, caster ? caster->As<RE::Actor>() : nullptr, item, NowSeconds());
    }

    std::vector<std::int32_t> SyncHealth()
    {
        std::lock_guard lock(g_lock);
        const double nowHours = NowHours();
        const double now = NowSeconds();
        std::vector<RE::FormID> ids;
        ids.reserve(g_actors.size());
        for (const auto& [id, parts] : g_actors) {
            ids.push_back(id);
        }
        for (const auto id : ids) {
            auto* actor = RE::TESForm::LookupByID<RE::Actor>(id);
            if (!actor) {
                Erase(id);
                continue;
            }
            if (!actor->Is3DLoaded()) {
                continue;
            }
            Parts* parts = Current(actor, nowHours);
            if (!parts) {
                continue;
            }
            ScanActiveEffects(actor, id, *parts, now);
            const Healer* healer = ActiveHealer(id, now);
            const Parts dbgBefore = *parts;  // DEBUG-WHIP
            ConvertHealth(actor, id, *parts, nowHours, healer != nullptr);
            // DEBUG-WHIP begin
            if (!DbgSame(dbgBefore, *parts)) {
                SKSE::log::info("[dbg] sync {:08X} {} healer:{} | {}", id, DbgHealth(actor), healer ? healer->source : -1, DbgParts(*parts));
            }
            // DEBUG-WHIP end

            const bool pained = TotalDamage(*parts) > 0;
            if (auto [it, inserted] = g_pained.try_emplace(id, pained); !inserted) {
                if (it->second && !pained) {
                    PushEvent(id, kPainGone, healer, healer ? Percent(*healer, Deficit(*parts)) : 0);
                    SKSE::log::info("pain gone {:08X}", id);
                }
                it->second = pained;
            }
            if (FullyHealed(*parts)) {
                SKSE::log::info("forget {:08X} (healed)", id);
                PushEvent(id, kFullyHealed, healer, 100);
                Erase(id);
                continue;
            }
            if (const auto it = g_healers.find(id); it != g_healers.end() && !healer) {
                const int percent = Percent(it->second, Deficit(*parts));
                SKSE::log::info("heal stopped {:08X} {}%", id, percent);
                PushEvent(id, kHealStopped, &it->second, percent);
                g_healers.erase(it);
            }
        }

        std::vector<std::int32_t> codes;
        g_syncActors.clear();
        g_syncCasters.clear();
        for (const auto& e : g_events) {
            auto* actor = RE::TESForm::LookupByID<RE::Actor>(e.actor);
            if (!actor) {
                continue;
            }
            codes.push_back(e.kind * 10000 + e.source * 1000 + e.percent);
            SKSE::log::info("[dbg] sync event {:08X} kind:{} source:{} percent:{} caster:{:08X}", e.actor, e.kind, e.source,  // DEBUG-WHIP
                e.percent, e.caster.get() ? e.caster.get()->GetFormID() : 0);  // DEBUG-WHIP
            g_syncActors.push_back(actor);
            g_syncCasters.push_back(e.caster.get().get());
        }
        g_events.clear();
        return codes;
    }

    std::vector<RE::Actor*> SyncActors()
    {
        std::lock_guard lock(g_lock);
        return g_syncActors;
    }

    std::vector<RE::Actor*> SyncCasters()
    {
        std::lock_guard lock(g_lock);
        return g_syncCasters;
    }

    int TrackedCount()
    {
        std::lock_guard lock(g_lock);
        return static_cast<int>(g_actors.size() + g_events.size());
    }

    std::string PartName(int part)
    {
        if (part < 0 || part >= kPartCount) {
            return {};
        }
        return std::string(kPartName[part]);
    }

    std::string StateJson(RE::Actor* actor)
    {
        if (!actor) {
            return R"({"parts":[]})";
        }
        std::lock_guard lock(g_lock);
        const Parts* parts = Current(actor, NowHours());
        if (!parts) {
            return R"({"parts":[]})";
        }
        std::string json = R"({"parts":[)";
        bool first = true;
        for (int i = 0; i < kPartCount; ++i) {
            const Part& p = (*parts)[i];
            if (!p.hit) {
                continue;
            }
            const int stage = StageIndex(p);
            const int region = kRegion[i];
            json += std::format(R"({}{{"part":"{}","region":"{}","stage":"{}","text":{},"damage":{},"healing":{}}})",
                first ? "" : ",", kPartName[i], kRegionName[region], kStageName[stage], region * 6 + stage,
                p.damage, p.healing);
            first = false;
        }
        json += "]}";
        return json;
    }

    std::string OverlayJson()
    {
        // Overlay bar order: head, body, right arm, left arm, right leg, left leg.
        constexpr std::array<int, kPartCount> kOrder{ 0, 3, 2, 1, 5, 4 };
        std::lock_guard lock(g_lock);
        std::string json = R"({"actors":[)";
        bool first = true;
        for (const auto& [id, parts] : g_actors) {
            auto* actor = RE::TESForm::LookupByID<RE::Actor>(id);
            if (!actor || !actor->Is3DLoaded() || actor->IsDead()) {
                continue;
            }
            std::string name;
            for (const char c : std::string_view(actor->GetDisplayFullName())) {
                if (c == '"' || c == '\\') {
                    name += '\\';
                }
                if (static_cast<unsigned char>(c) >= 0x20) {
                    name += c;
                }
            }
            json += std::format(R"({}{{"id":{},"name":"{}","parts":[)", first ? "" : ",", id, name);
            for (int i = 0; i < kPartCount; ++i) {
                const Part& p = parts[kOrder[i]];
                json += std::format("{}[{},{}]", i ? "," : "", p.hit ? p.damage : 0, p.hit ? p.healing : kHealed);
            }
            json += "]}";
            first = false;
        }
        json += "]}";
        return json;
    }

    void SetHoursPerPoint(float hours)
    {
        std::lock_guard lock(g_lock);
        g_hoursPerPoint = std::max(0.05f, hours);
    }

    void SetRegenRate(float percent)
    {
        std::lock_guard lock(g_lock);
        g_regenRate = std::clamp(percent, 0.0f, 100.0f);
    }

    void Forget(RE::Actor* actor)
    {
        if (!actor) {
            return;
        }
        std::lock_guard lock(g_lock);
        Erase(actor->GetFormID());
    }

    void Save(SKSE::SerializationInterface* intfc)
    {
        std::lock_guard lock(g_lock);
        if (!intfc->OpenRecord(kRecord, kRecordVersion)) {
            SKSE::log::error("co-save open failed");
            return;
        }
        intfc->WriteRecordData(static_cast<std::uint32_t>(g_actors.size()));
        for (const auto& [id, parts] : g_actors) {
            intfc->WriteRecordData(id);
            for (const auto& p : parts) {
                intfc->WriteRecordData(p.hit);
                intfc->WriteRecordData(p.damage);
                intfc->WriteRecordData(p.healing);
                intfc->WriteRecordData(p.lastHours);
            }
        }
    }

    void Load(SKSE::SerializationInterface* intfc, std::uint32_t version, std::uint32_t)
    {
        if (version != kRecordVersion) {
            SKSE::log::warn("co-save version {} unsupported, skipped", version);
            return;
        }
        std::lock_guard lock(g_lock);
        g_actors.clear();
        g_healed.clear();
        g_healers.clear();
        g_pained.clear();
        g_regenBank.clear();
        g_events.clear();
        std::uint32_t count = 0;
        if (!intfc->ReadRecordData(count)) {
            return;
        }
        for (std::uint32_t a = 0; a < count; ++a) {
            RE::FormID oldId = 0;
            Parts parts{};
            if (!intfc->ReadRecordData(oldId)) {
                SKSE::log::error("co-save truncated");
                return;
            }
            for (auto& p : parts) {
                if (!intfc->ReadRecordData(p.hit) || !intfc->ReadRecordData(p.damage) ||
                    !intfc->ReadRecordData(p.healing) || !intfc->ReadRecordData(p.lastHours)) {
                    SKSE::log::error("co-save truncated");
                    return;
                }
            }
            RE::FormID id = 0;
            if (intfc->ResolveFormID(oldId, id)) {
                g_actors[id] = parts;
                SKSE::log::info("[dbg] load {:08X} | {}", id, DbgParts(parts));  // DEBUG-WHIP
            }
        }
        SKSE::log::info("co-save loaded {} actors", g_actors.size());
    }

    void Revert()
    {
        std::lock_guard lock(g_lock);
        g_actors.clear();
        g_healed.clear();
        g_healers.clear();
        g_pained.clear();
        g_regenBank.clear();
        g_events.clear();
        g_syncActors.clear();
        g_syncCasters.clear();
    }
}
