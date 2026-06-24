# ESP-IDF Build Entrypoint

Authoritative ESP build entrypoint.

Build authority:

```text
ESP / ESP32-P4 -> ESP-IDF
```

Removed historical component root:

```text
esp_idf implementation root
```

Final wrapper direction:

```text
builds/esp_idf
  -> invokes apps/esp32_lvgl app shell
```

This directory now owns the migrated ESP-IDF source list and target defaults
introduced in Batch 2:

- `ESP_IDF_COMPONENT_SOURCES.cmake`
- `targets/tab5/sdkconfig.defaults`
- `targets/t_display_p4_tft/sdkconfig.defaults`
- `targets/t_display_p4_amoled/sdkconfig.defaults`

Root legacy source has been removed. Any future physical ESP-IDF
`idf_component_register` owner must live under final build/app/platform
ownership, not under a restored historical source root.

Rules:

- thin wrapper only
- Build Entrypoint invokes
- App Shell composes
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
