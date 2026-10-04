# Papyrus / code

Developer guide: paths, compile, settings, ESP.

Contracts: [../reference/papyrus-rules.md](../reference/papyrus-rules.md), [../reference/json-keys.md](../reference/json-keys.md). SKSE / overlay: [overlay.md](overlay.md). Architecture and flow: [../../CLAUDE.md](../../CLAUDE.md#flow). Quirks: [../../KNOWLEDGEBASE.md](../../KNOWLEDGEBASE.md).

## Paths

| Path | Role |
|------|------|
| `Scripts/Source/SkyrimNet_Whipped_Main.psc` | Quest script: setup, settings, `OnLash`, narration batching, healing poll, `SyncHealth` loop, decorator |
| `Scripts/Source/SkyrimNet_Whipped_Actions.psc` | `Whip_Target` registration, eligibility, execution |
| `Scripts/Source/SkyrimNet_Whipped_LashEffect.psc` | Enchantment effect on a real weapon hit → `Main.OnLash` |
| `Scripts/Source/SkyrimNet_Whipped_PlayerRef.psc` | Player alias (load game → setup) |
| `Scripts/Source/SkyrimNet_Whipped_Engine.psc` | Natives in `SkyrimNet_Whipped.dll` |
| `Scripts/` | Compiled `.pex` |
| `skyrimse.ppj` | Pyro project (imports SKSE, SkyrimNet, PapyrusUtil sources) |
| `Spriggit/SkyrimNet_Whipped/` | ESP ↔ JSON |

## Compile

`pyro skyrimse.ppj` (VS Code task **`compile: pyro`** when present). Do not invent Caprica / `papyrus.exe` one-offs unless asked.

## Settings

Schema: `SKSE/Plugins/SkyrimNet/config/plugins/SkyrimNet_Whipped/manifest.yaml`. `Main.ApplyPluginConfig` reads `Plugin_SkyrimNet_Whipped` with `SkyrimNetApi.GetConfig*`; defaults in Papyrus must match the schema `defaultValue`.

| Key | Used by |
|-----|---------|
| `whip.action.require_whip`, `whip.action.give_to_player`, `whip.action.max_strikes` | Action eligibility, setup; `max_strikes` needs a reload (re-registers the action) |
| `whip.healing.hours_per_point`, `whip.healing.regen_rate` | Passed to the DLL (`SetHoursPerPoint`, `SetRegenRate`) |
| `whip.healing.give_healing_hands` | Adds Healing Hands (Skyrim.esm 0x4D3F2) to the player |
| `whip.narration.enabled`, `.cooldown`, `.max_distance`, `.event_ttl` | Narration gate |
| `whip.overlay.enabled` | Overlay on/off |

## StorageUtil keys (per actor)

| Key | Role |
|-----|------|
| `skyrimnet_whipped.in_action` | Set during `Whip_Target` so the enchantment hit is not counted twice |
| `skyrimnet_whipped.pending_parts` / `_level` / `_count` | Hits since the last narration (part bitmask, worst level, count) |
| `skyrimnet_whipped.heal_pending` (IntList) / `heal_count` | Part heals (`part * 8 + stage`) since the last heal narration, takes count |
| `skyrimnet_whipped.heal_narrated` | A heal was narrated since the last lash (unset = next heal is fresh) |
| `skyrimnet_whipped.last_narration` | Real time of the last narration (cooldown) |

## ESP / Spriggit

Do not hand-edit `.esp`. `Spriggit/SkyrimNet_Whipped/` is the source of truth.

| Command | Direction |
|---------|-----------|
| `make serialize` | `.esp` → `Spriggit/` (after xEdit/CK edits) |
| `make esp` | `Spriggit/` → `.esp` |
| `make release` | dll + esp + pack `.7z` |

Form IDs: [../../CLAUDE.md](../../CLAUDE.md#form-ids-skyrimnet_whippedesp-esl).

## Content authors

Prompts → [../authors/prompts.md](../authors/prompts.md). Action → [../authors/actions.md](../authors/actions.md).
