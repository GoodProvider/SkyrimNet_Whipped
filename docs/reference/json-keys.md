# JSON keys

Canonical rules for JSON that this mod emits for prompts and the overlay.

## Rule

Use **bare lowercase** keys only (`parts`, `stage`, `caster_is_target`). Do not invent Title Case or leading-`_` keys. Values shown to the LLM are words, never numbers — numeric fields are only for templates to turn into words ([../authors/prompts.md](../authors/prompts.md#no-numbers)).

## Decorator `skyrimnet_whipped_welts(uuid)`

`Main.Welts_Decorator` → native `GetStateJson` → `DamageHealing_Engine::StateJson`. One entry per hit part:

```json
{"parts":[{"part":"left arm","region":"arm","stage":"medium","text":8,"damage":5,"healing":0}]}
```

| Key | Meaning |
|-----|---------|
| `part` | body part name |
| `region` | region name (shares text tables) |
| `stage` | stage word (`light`, `medium`, `severe`, `recovering`, …) |
| `text` | `region * 6 + stage` — index into the prompt text tables in `0420_whip_welts` / `0420_whip_pain` |
| `damage` / `healing` | raw points (templates only) |

## `heal` (healing narration)

Built in `Main.HealText` / `SourceHealText`, rendered by `Main.RenderHealPrompt` as `ParseString(<helpers/healing raw source>, "heal", json)`. Documented at the top of [`helpers/healing.prompt`](../../SKSE/Plugins/SkyrimNet/external/goodprovider.whipped/prompts/helpers/healing.prompt):

| Key | Values |
|-----|--------|
| `kind` | int: 0 start, 1 pain gone, 2 fully healed, 3 stopped, 4 stages |
| `parts` | (kind 4 only) `[{part, stage}]`; stage 1 closed (severe→medium), 0 faded (medium→light), 3 scabbed (light→recovering), 4 scarred, 5 healed |
| `name`, `female` | target name, gender |
| `caster`, `caster_is_target` | caster name (`""` if none) |
| `source` | int: 0 none (regen / game time), 1 spell, 2 staff, 3 potion, 4 other magic |
| `percent` | healing progress; the template turns it into words |

Strings go through `Main.JsonString` (escapes `"` and `\`).

## Overlay (`whipUpdate`)

`DamageHealing_Engine::OverlayJson`, pushed by `Overlay::Notify` when it changes:

```json
{"actors":[{"id":20,"name":"Nina","parts":[[3,0],[0,11],...]}]}
```

`parts` is six `[damage, healing]` pairs in bar order head, body, right arm, left arm, right leg, left leg. An unhit part is `[0, 11]` (healed). Only loaded, living tracked actors are listed.
