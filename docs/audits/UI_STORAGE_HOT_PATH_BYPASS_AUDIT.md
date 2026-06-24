# UI Storage Hot-Path Bypass Audit

Status date: 2026-06-16

This audit is the separate map / SD / UI burn-down cut requested after the ESP
UI freeze regressions. It lists active UI hot-path entries that can touch SD,
LVGL FS, or display SPI, then records whether each entry is already behind a
runtime/worker boundary or still needs deletion/consolidation.

The rule from `UI_STORAGE_EVENT_RUNTIME_DESIGN_SPEC.md` is tightened here:

```text
UI owner context may build value objects, update renderer objects, and submit
runtime intents. It must not synchronously open/read/write/list storage, call
LVGL FS for SD-backed files, or wait on a display-shared SPI resource.
```

## Entry Inventory

| Entry | Resource touched | Current risk | Decision |
| --- | --- | --- | --- |
| `platform/esp/arduino_common/src/ui/widgets/map/map_tiles.cpp` UI tile source | LVGL FS path was present in ESP map source | High confusion risk: ESP UI source appeared able to read through LVGL FS | Burned in this cut. ESP UI source is path-only; SD reads remain in the worker `SdMapTileFileSystem`. |
| `platform/esp/arduino_common/src/ui/widgets/map/map_tiles.cpp` worker tile source | SD through `SdRuntimeFile` | Accepted only in worker domain | Keep. This is the map storage adapter behind the tile async runtime. |
| `platform/esp/arduino_common/src/ui/widgets/map/map_tiles.cpp` tile decode | LVGL image decoder, CPU/memory work | Medium. LVGL has no OS lock on ESP, so decoding must stay UI-owned until a non-LVGL decoder adapter exists | Remaining legacy. Worker-side LVGL decode was rejected because `LV_USE_OS` is `LV_OS_NONE`. |
| `modules/ui_shared/src/ui/i18n/resource_pack_registry.cpp::locale_preview_font` | External font load via `lv_binfont_create` / LVGL FS | High. Locale/settings UI preview could synchronously load SD-backed font packs | Burned in this cut for ESP. Preview uses only already-loaded fonts on ESP. |
| `modules/ui_shared/src/ui/i18n/resource_pack_registry.cpp::activate_locale_internal` | External UI/content font load | High on ESP if page rendering loads SD-backed fonts | Partially burned. Startup registry activation may load active external fonts once; ordinary UI activation/preview/content hot paths still defer. |
| `modules/ui_shared/src/ui/i18n/resource_pack_registry.cpp` external pack scan | LVGL FS dir/file reads | Medium. Usually registry/startup path, but still synchronous | Remaining legacy. Needs pack catalog worker or startup preflight outside visible UI frame budget. |
| `modules/ui_shared/src/ui/screens/gps/gps_page_runtime.cpp` route loaders | `ui::fs::read_text_file` | Medium to high when user opens/loads a route | Remaining legacy. Move GPX/CSV parse into route/track runtime worker and return overlay points by event. |
| `platform/esp/arduino_common/src/ui/screens/team/team_ui_store.cpp` | SD exists/open/write/remove/rename | High. A UI screen store owns durable persistence | Remaining legacy. Move team snapshot/key/event/chat/position persistence behind `TEAM_ACTION_RUNTIME_SPEC` storage worker. |
| `platform/esp/arduino_common/src/ui/runtime/pack_repository.cpp` | Flash/LVGL FS open/dir/read/write | Medium. Pack management can block UI actions | Remaining legacy. Keep as explicit pack runtime adapter only after UI callers submit commands; remove direct page calls. |
| `modules/ui_shared/src/ui/widgets/busy_overlay.cpp` | `lv_refr_now` display flush | Medium if called while storage owns display-shared SPI | Burned in this cut. Busy overlay now invalidates and lets the normal UI tick render. |
| startup shells / boot log | `lv_refr_now` / manual `lv_timer_handler` display flush | Medium. Startup runs before normal UI tick exists | Refined. Boot log avoids `lv_refr_now`; startup boot presentation invalidates and services one LVGL tick so the splash can appear during setup. |
| `platform/esp/arduino_common/src/LV_Helper_v9.cpp` | LVGL FS adapter to SD/flash | Adapter risk, not business risk | Keep as platform adapter. UI hot paths must not use it for SD-backed files. |
| `platform/esp/boards/src/display/DisplayInterface.cpp` | display SPI lock/flush | Frame-critical adapter | Keep. Other workers must yield to this domain through bounded wait, hold, and cooldown policy. |

## Burned In This Cut

### ESP map UI source no longer owns a storage path

The ESP map file now has two explicit source roles:

- UI path source: path-only, no `exists`, `isDirectory`, or `readFile` behavior.
- Worker source: SD-backed `SdMapTileFileSystem`, used by the async tile runtime.

This removes the misleading ESP `LvglMapTileFileSystem` implementation from the
active map source file. Path resolution remains in `FilesystemMapTileSource`;
file reads stay in the worker adapter.

### Locale preview cannot load external fonts on ESP

`locale_preview_font()` no longer calls `ensure_font_pack_loaded()` on ESP.
Preview UI can compose with an external font only if it is already loaded.
Otherwise it falls back to the base font and returns immediately.

This prevents settings/list rendering from repeatedly entering
`lv_binfont_create()` and LVGL FS just because Chinese text appeared in a preview.

### Locale activation is split between startup and UI hot paths

`activate_locale_internal()` may synchronously load active external UI/content
fonts only while the registry is rebuilding during startup. Ordinary UI
activation, preview, and content missing-glyph paths still defer unloaded
external fonts.

This restores Chinese UI after boot without allowing page rendering to repeatedly
enter `lv_binfont_create()`. The proper recovery path is still an i18n font-load
runtime command that loads external fonts outside interactive UI paths and
publishes a font-chain update event.

### Busy overlay no longer forces immediate display refresh

`busy_overlay::show/update/set_progress/hide` no longer call `lv_refr_now()`.
They invalidate the overlay or active screen and let the normal UI tick perform
rendering.

This keeps slow-operation feedback from directly competing with SD or other
display-shared SPI work in the same call stack.

### Startup paths do not force immediate refresh

Boot log updates no longer call `lv_refr_now()`. Startup shell boot presentation
invalidates the top layer and services `lv_timer_handler()` once because the
normal UI tick is not running yet during setup.

This restores boot visibility without reintroducing direct forced refresh.

### Invalid map focus no longer scans default London tiles

The map page previously treated `snapshot.header.valid` as enough to make the
map focus valid. That made no-fix/no-viewport startup fall back to the London
default coordinate and then probe SD for tiles such as
`/maps/base/osm/12/2046/1362.png`.

The focus is now valid only when there is a real viewport center or a valid self
position. Without either, the map viewport cleans up tile records and does not
enqueue tile IO.

## Remaining Burn-Down Order

1. Move i18n deferred external font activation into an event-driven font-load
   runtime. Preserve Chinese UI by loading packs asynchronously and applying the
   new font chain on a UI-owner event.
2. Move GPS GPX/CSV route loading into a route runtime worker. UI submits a path
   and receives parsed/downsampled overlay points.
3. Move team UI persistence out of `platform/esp/.../ui/screens` into a storage
   worker owned by the team action runtime.
4. Convert pack repository actions into commands/events. Keep LVGL FS and flash
   APIs only in platform adapters.
5. Replace UI-side LVGL image decoding with a platform decoder adapter that
   has no dependency on LVGL global decoder state.

## Verification Gate For Each Follow-Up Cut

Each follow-up must pass these checks before commit:

- GitNexus impact analysis for every edited symbol.
- `rg` audit showing the edited UI hot path no longer calls SD, LVGL FS, or
  display SPI primitives directly.
- A compile check for one ESP target.
- `gitnexus detect-changes` to confirm the affected surface matches the planned
  runtime/worker slice.
