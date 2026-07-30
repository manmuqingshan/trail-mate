# ESP-IDF Build Assets

Authoritative ESP-IDF component assets and target defaults.

Build authority:

```text
ESP / ESP32-P4 -> root CMakeLists.txt -> ESP-IDF
```

This directory is intentionally not an `idf.py -C builds/esp_idf` project. The
repository root `CMakeLists.txt` is the single ESP-IDF project entrypoint; it
selects a concrete board through `TRAIL_MATE_IDF_TARGET` and consumes the
component assets stored here.

Correct invocation examples:

```bash
idf.py -B build.t_display_p4_tft -DTRAIL_MATE_IDF_TARGET=t_display_p4_tft build
idf.py -B build.t_display_p4_amoled -DTRAIL_MATE_IDF_TARGET=t_display_p4_amoled build
```

Removed historical component root:

```text
esp_idf implementation root
```

Final asset ownership:

```text
builds/esp_idf
  -> main ESP-IDF component registration
  -> ESP_IDF_COMPONENT_SOURCES.cmake
  -> target sdkconfig.defaults files
```

This directory now owns the migrated ESP-IDF source list and target defaults
introduced in Batch 2:

- `ESP_IDF_COMPONENT_SOURCES.cmake`
- `main/CMakeLists.txt`
- `targets/tab5/sdkconfig.defaults`
- `targets/t_display_p4_tft/sdkconfig.defaults`
- `targets/t_display_p4_amoled/sdkconfig.defaults`

Root legacy source has been removed. Any future physical ESP-IDF
`idf_component_register` owner must live under final build/app/platform
ownership, not under a restored historical source root.

Rules:

- no local ESP-IDF project wrapper
- root `CMakeLists.txt` is the only ESP-IDF entrypoint
- this directory contributes components and target defaults only
- do not assemble Chat/Map/GPS runtime here
- do not choose UX pack here
- do not define board facts here
- do not invent target defaults that are not backed by repository evidence

Runtime facade rule:

- ESP-IDF targets that expose business pages such as Chat, Contacts, Team, or
  Settings must bind an AppFacade before shell initialization.
- `apps/esp32_lvgl/src/esp32_lvgl_idf_app_facade_runtime.*` is the current
  IDF-native facade owner for P4/Tab5 final builds. It brings up the UI-facing
  Chat/Contacts/AppConfig contracts without importing Arduino AppContext.
- Wireless transport readiness is separate from facade availability. A page may
  show a NotReady send result while C6/LoRa transport is still being completed,
  but it must not white-screen because `app::hasAppFacade()` is false.
