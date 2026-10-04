# CLAUDE.md

SkyrimNet_Whipped: a standalone padded whip for SkyrimNet, modeled on `../SkyrimNet_SexLab`.

**Primary agent doc:** [AGENTS.md](AGENTS.md) (logs, docs map, standing rules, safety, confidence). Imported below so it is always loaded:

@AGENTS.md

## Key paths

- Papyrus: `Scripts/Source/` → `Scripts/`; project `skyrimse.ppj` (compile: pyro).
- SKSE plugin: `SKSE_Source/` → `make dll` → `SKSE/Plugins/SkyrimNet_Whipped.dll`. Natives: `SkyrimNet_Whipped_Engine.psc`.
- ESP source of truth: `Spriggit/SkyrimNet_Whipped/` → `make esp`. Never hand-edit `SkyrimNet_Whipped.esp`; after xEdit edits run `make serialize`.
- LLM content: `SKSE/Plugins/SkyrimNet/external/goodprovider.whipped/` (folder name = manifest `id`).
- Dashboard settings: `SKSE/Plugins/SkyrimNet/config/plugins/SkyrimNet_Whipped/manifest.yaml`, read as `Plugin_SkyrimNet_Whipped`.

## Form IDs (SkyrimNet_Whipped.esp, ESL)

| ID | Record |
|----|--------|
| 0x800 | QUST SkyrimNet_Whipped (scripts Main, Actions; alias PlayerRef) |
| 0x801 | GLOB skyrimnet_whipped_require_whip |
| 0x803 | WEAP SkyrimNet_Whipped_PaddedWhip |
| 0x804–0x80E | 1st-person STAT, IPDS, IPCTs, SNDRs, TXST decal (copied from DiaryOfMine.esm) |
| 0x80F | COBJ recipe |
| 0x810 / 0x811 | MGEF SkyrimNet_Whipped_LashEffect / ENCH SkyrimNet_Whipped_LashEnch |
| 0x812 | KYWD SkyrimNet_Whipped_WeapTypeWhip |

## Flow

Real weapon hit → ENCH → `SkyrimNet_Whipped_LashEffect.OnEffectStart` → `Main.OnLash`.
LLM action `Whip_Target` (registered in Papyrus so eligibility can check inventory) → `Main.Lash` (PlayImpactEffect + sound) → `Main.OnLash`. The `skyrimnet_whipped.in_action` StorageUtil flag stops double counting.
`OnLash` calls `SkyrimNet_Whipped_Engine.HasOpenWounds` then `Hit` (C++ DamageHealing_Engine: random body part, damage then healing over game time, co-saved). `NarrateLash` batches hits per victim (StorageUtil `skyrimnet_whipped.pending_*`): the first hit on a victim with no open wounds always narrates (RegisterEvent if narration is off or too far); later hits send one combined DirectNarration (all parts since the last narration, worst level) when `GetSpeechQueueSize() == 0` and the cooldown has passed, otherwise a RegisterShortLivedEvent per hit. Healing mirrors hitting: `TakeHealedActors`/`TakeHealed` return parts that reached a new stage (damage stage drops severe→medium→light→recovering included; transient C++ queue, filled by game time in `Advance` and by health conversion in `HealPoint`, stage 5 = part healed), drained by `OnUpdateGameTime` and every `OnUpdate` tick. `NarrateHealing` batches them per victim (StorageUtil `skyrimnet_whipped.heal_*`): the first heal since the last lash always narrates (RegisterEvent if narration is off or too far); later ones send one combined DirectNarration (latest stage per part) when the gate allows, otherwise a RegisterShortLivedEvent `whip_heal_*` per take; `OnLash` clears the heal batch. Health link: wounds hold health down by deficit (damage + missing healing points) × `0.9 × MaxHealth / 186` (6 parts × (20 damage + 11 healing)), so the wounded level never drops below 10% max (also clamped there); `Hit` takes the added points' health (never below 10% max). `Main.OnUpdate` runs every 0.2 s while `TrackedCount() > 0`, calling `SyncHealth`: health above the wounded level converts back to wound points (random part per point, damage first, then healing), fully while a healing source is active, natural regen at `whip.healing.regen_rate`% (the rest is taken back). Healing sources come from a `MagicTarget::AddTarget` vtable hook (`Hooks.cpp`) plus an active-effect scan; events (heal start, pain gone, fully healed, heal stopped) carry source kind + caster and are narrated by `Main.NarrateSourceHeal` / `SourceHealText`: fully healed is always a DirectNarration (while narration is on) and clears the heal batch; start, pain gone and stopped go through the usual gate or a `whip_heal` event. Setting `whip.healing.give_healing_hands` adds Healing Hands (Skyrim.esm 0x4D3F2) to the player.
Narration text = `Main.LashBatchText`; healing text (`Main.HealText` / `SourceHealText`) is rendered by `Main.RenderHealPrompt` from `prompts/helpers/healing.prompt`: `RenderTemplate` only loads the file (logic is in a `{% raw %}` block), then `ParseString(source, "heal", json)` renders it, because RenderTemplate binds its variable as a string; `kind`/`source` are ints; prompt text tables = `character_bio/0420_whip_welts.prompt` and `user_final_instructions/0420_whip_pain.prompt`, indexed by the decorator's `text` field.
Overlay (`whip.overlay.enabled`, "Show whipping overlay"): PrismaUI view `PrismaUI/views/SkyrimNet_Whipped/overlay.html`, created by `Overlay::Init` on kDataLoaded (`Overlay.cpp`, header vendored in `SKSE_Source/lib/PrismaUI/`). The Papyrus natives `Hit`, `SyncHealth`, `TakeHealedActors` and `Forget` call `Overlay::Notify`, which pushes `DamageHealing_Engine::OverlayJson` to `whipUpdate()` when it changes. The page shows one bar per tracked, not fully healed actor, most recently hit first (whole-body damage total, or healing progress once no damage is left; color = average stage); the whole overlay fades 3 s after the last change.
