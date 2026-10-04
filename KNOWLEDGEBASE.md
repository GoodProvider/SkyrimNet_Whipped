# KNOWLEDGEBASE

Quirks and lessons for SkyrimNet_Whipped. Read before changes; append a dated entry (newest first) after a session that found something non-obvious. Entries marked *(from SkyrimNet_SexLab)* were learned there and apply here too.

## PrismaUI view path (from SkyrimNet_SexLab, 2026-07-24)

`CreateView("SkyrimNet_Whipped/overlay.html")` loads from **`Data/PrismaUI/views/`**, not `SKSE/Plugins/`. `make release` copies `PrismaUI/` into the package. A missing file gives a valid-looking C++ path but no visible UI. Do not `Focus` a view that should not pause the game; the overlay is `Show`-only.

## Separate PrismaUI Invokes are not applied in order (from SkyrimNet_SexLab, 2026-10-03)

`PrismaUI->Invoke` is async; two Invokes can land in either order. A state change that must not interleave goes out as **one** Invoke calling one JS entry point (here: `whipUpdate(json)` with the whole state).

## `window.prompt` / `confirm` do nothing in PrismaUI (from SkyrimNet_SexLab, 2026-09-27)

Ultralight returns as if cancelled. Build text entry and confirmations in HTML.

## Papyrus default args are filled at the caller's compile (from SkyrimNet_SexLab)

Adding a parameter (even with a default) needs every caller recompiled, or the stale `.pex` logs `Expected N, got M` and the call returns None. Pass every argument in cross-script calls. See [docs/reference/papyrus-rules.md](docs/reference/papyrus-rules.md).

## ESL FormIDs are signed in Papyrus (from SkyrimNet_SexLab, 2026-09-15)

`0xFE057894` arrives as `-33195884`. C++ must `static_cast<uint32_t>` before `LookupByID`.

## SKSE native params must use engine types (from SkyrimNet_SexLab, 2026-07-25)

Natives take `RE::Actor*`, `RE::BSFixedString`, etc. Mismatched types fail to bind or crash.

## The .pex and the .dll deploy in opposite directions (from SkyrimNet_SexLab, 2026-09-23)

Pyro writes `.pex` into this repo's `Scripts/`; the DLL build copies into this repo's `SKSE/Plugins/`. Whatever the game loads comes from the MO2 mod folder — confirm the game is reading this folder (not an installed release copy) before concluding a fix "did not work".

## Action eligibility cannot use Papyrus decorators (from SkyrimNet_SexLab, 2026-09-14)

`CallDecoratorDirect` returns empty on a cache miss. Use inventory, factions, globals, or Papyrus eligibility functions (as `Whip_IsEligible` does).

## Papyrus runs while a focused PrismaUI view pauses the game (from SkyrimNet_SexLab, 2026-09-28)

Paused game time does not stop Papyrus. Not an issue for the Show-only overlay, but matters if a focused panel is ever added.

## RenderTemplate binds its variable as a string; SkyrimNet inja has no `lower` / `+` (2026-10-04)

`SkyrimNetApi.RenderTemplate(name, "heal", json)` sets `heal` to the JSON *string*: SkyrimNet.log shows `variable 'heal.kind' not found`, the template silently takes its last `else` branch, and the result passes the "Error" / `{{` checks. Load the template with `RenderTemplate` (logic inside `{% raw %}`), then render with `ParseString(source, "heal", json)`, which parses JSON. SkyrimNet's inja also lacks `lower()` and string `+` (`variable 'add' not found`); write joined phrases inline with `{% if %}` blocks, and send ints rather than Papyrus string literals (those get re-cased, e.g. `Start`, `SPELL`). Check `SkyrimNet.log` for `PromptEngineRender.cpp` "missing variable(s)" after any helper prompt change.

## inja array literals are parsed as JSON: no variables inside `[...]` (2026-10-04)

`join([heal.caster, "'s ", noun], "")` fails the whole render while parsing, not just its branch: `PromptEngineRender.cpp ... RenderTemplateString: Exception during rendering: [json.exception.parse_error.101] ... last read: '[h'`. Every heal narration fell back to the Papyrus sentence. Only constant arrays work; build phrases inline (`{% if named_caster %}{{ heal.caster }}'s{% else %}the{% endif %} {{ noun }}`).

## Health ratio must cover the full deficit, or the wounded level goes negative (2026-10-04)

`Deficit()` is damage **plus missing healing** per part, so its maximum is 6 × (20 + 11) = 186, not 6 × 20. With `/ 120`, a deficit of 180 gave `wounded:-35`; the regen take-back pushed health to -21.5 (bleedout), and when health came back (bleedout recovery jumped it to full) the whole deficit converted in one 0.2 s tick: "pain gone" and "fully healed" at once. `Ratio()` now divides by `kPartCount * kMaxDeficit`, and `ConvertHealth` clamps the wounded level to `kHealthFloor`.
