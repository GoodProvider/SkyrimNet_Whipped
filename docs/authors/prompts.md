# Prompts

Edit the SkyrimNet prompt files shipped with this mod.

Contracts: [../reference/json-keys.md](../reference/json-keys.md).

## Paths

```
SKSE/Plugins/SkyrimNet/external/goodprovider.whipped/
  manifest.json            (id must equal the folder name)
  prompts/
    helpers/
      healing.prompt
    submodules/
      character_bio/
        0420_whip_welts.prompt
      user_final_instructions/
        0420_whip_pain.prompt
```

| File | Role |
|------|------|
| `character_bio/0420_whip_welts.prompt` | What each wounded part looks like to an observer |
| `user_final_instructions/0420_whip_pain.prompt` | How the speaker's own wounds feel |
| `helpers/healing.prompt` | Healing narration sentence, rendered by Papyrus (`Main.RenderHealPrompt`) |

## Wound prompts

Both submodules call the decorator `skyrimnet_whipped_welts(uuid)` and index their text tables by each part's `text` field (`region * 6 + stage`). Changing the region or stage order in `DamageHealing_Engine.cpp` breaks both tables — update them together.

## Healing helper

Papyrus builds the `heal` JSON. `RenderTemplate("helpers/healing", "", "")` only loads the file: its logic sits in a `{% raw %}` block and comes back unrendered. `ParseString(source, "heal", json)` then renders it with `heal` parsed as JSON (`RenderTemplate` binds its variable as a plain string, so `heal.*` would be undefined). SkyrimNet's inja has no `lower()` and no string `+`, and it parses `[...]` literals as JSON, so `join([heal.name, "'s"], "")` breaks the whole render: write joined phrases inline (`{% if pron %}{{ his }}{% else %}{{ heal.name }}'s{% endif %}`). Send numeric codes for anything Papyrus would send as a string literal. An empty, error-looking or unrendered (`{{` / `{%`) result falls back to the Papyrus sentence. Keys are documented at the top of the file and in [json-keys.md](../reference/json-keys.md#heal-healing-narration). Hit narration (`Main.LashBatchText`) is built in Papyrus, not a prompt.

## No numbers

LLM-facing text — prompts, DirectNarration, events — uses descriptive stages ("light welts", "about half way healed"), never digits or percentages. Numeric JSON fields exist only for templates to choose words.

## Checklist

- [ ] Keys bare lowercase
- [ ] No digits / percentages reach the LLM
- [ ] Text tables still match the `text` index
- [ ] Narration-enabled smoke test in game (check `Papyrus.0.log` for `RenderHealPrompt` failures)
