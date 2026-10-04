# SKSE plugin + PrismaUI overlay

C++ plugin `SkyrimNet_Whipped.dll` (wound engine, healing hooks, Papyrus natives) and the PrismaUI whipping overlay.

Quirks: [../../KNOWLEDGEBASE.md](../../KNOWLEDGEBASE.md). JSON: [../reference/json-keys.md](../reference/json-keys.md#overlay-whipupdate).

## Paths

| Path | Role |
|------|------|
| `SKSE_Source/src/plugin.cpp` | Entry, logger (`SKSE\SkyrimNet_Whipped.log`), messaging, co-save registration |
| `SKSE_Source/src/DamageHealing_Engine.cpp` | Per-actor six-part damage/healing, health link, co-save, `StateJson`, `OverlayJson` |
| `SKSE_Source/src/Papyrus_Engine.cpp` | Natives bound to `SkyrimNet_Whipped_Engine.psc` |
| `SKSE_Source/src/Hooks.cpp` | `MagicTarget::AddTarget` vtable hook (healing sources) |
| `SKSE_Source/src/Overlay.cpp`, `include/Overlay.h` | PrismaUI view + push |
| `SKSE_Source/lib/PrismaUI/` | Vendored PrismaUI API header |
| `SKSE/Plugins/SkyrimNet_Whipped.dll` | Built plugin (copied by the build) |
| `PrismaUI/views/SkyrimNet_Whipped/overlay.html` | Overlay page under `Data/PrismaUI/views/` |

## Build

`make dll` → `cmake --preset release && cmake --build --preset build-release` in `SKSE_Source/`. Needs `VCPKG_ROOT`. CommonLibSSE-NG comes from `../SkyrimNet_SexLab/SKSE_Source/lib/` (override with `-DCommonLibPath=`). Do not edit `SKSE_Source/build/` or vendored `lib/`.

## Natives

Declared in `Scripts/Source/SkyrimNet_Whipped_Engine.psc`, registered in `Papyrus_Engine.cpp`. Native parameters must use engine types (`RE::Actor*`, `RE::BSFixedString`, …). A signature change means rebuilding the DLL and recompiling the `.psc` and every caller.

## Overlay

- Setting `whip.overlay.enabled` ("Show whipping overlay").
- `Overlay::Init` on `kDataLoaded`: PrismaUI API, `CreateView("SkyrimNet_Whipped/overlay.html")`. PrismaUI is a listed requirement; if it is missing anyway → no overlay, everything else works.
- The natives `Hit`, `SyncHealth`, `TakeHealedActors` and `Forget` call `Overlay::Notify`, which pushes `OverlayJson` to JS `whipUpdate(json)` only when it differs from the last push.
- The view is shown, never focused (no pause, no cursor).
- Page: one block per tracked, not fully healed actor, most recently hit first; red damage bar or yellow healing bar per part; the whole overlay fades 3 s after the last change.

## Lifecycle

- Plugin load: `Install_Hooks()` (healing-source hook), co-save callbacks.
- `kDataLoaded`: overlay view.
- Load / new game: co-save read; Papyrus `PlayerRef.OnPlayerLoadGame` → `Main.Setup` → `ApplyPluginConfig`.
- Dashboard save → `SkyrimNet_OnPluginConfigSaved` → `Main.OnPluginConfigSaved` → `ApplyPluginConfig` (settings pushed to the DLL).
