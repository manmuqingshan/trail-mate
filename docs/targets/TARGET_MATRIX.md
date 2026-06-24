# Target Matrix

This matrix records Batch 3 final target architecture ownership. It separates
final target intent from the currently executable fallback paths already present
in this repository.

No missing target defaults, widget trees, or hardware facts are invented here.
Rows marked `PendingHardwareValidation` have final owners assigned, but still
need build or hardware evidence before they can be called complete product
routes.

| target_id | board_id | build_entrypoint | app_shell | platform | renderer | ux_pack_id | active_ux_pack | ui_profile_id | page_manifest_id | layout_profile_id | support_status | known_fallback | owner |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `tab5` | `tab5` | `builds/esp_idf` | `apps/esp32_lvgl` | ESP-IDF | LVGL | `tab5_touch` | `compatibility` | `tab5_touch_ui` | `tab5_touch_manifest` | `tab5_large_touch` | `ActiveWithFallback` | current executable UX pack is `compatibility` | `apps/esp32_lvgl + builds/esp_idf + boards/tab5` |
| `t_display_p4_tft` | `t_display_p4` | `builds/esp_idf` | `apps/esp32_lvgl` | ESP-IDF | LVGL | `t_display_p4_touch` | `t_display_p4_touch` | `t_display_p4_touch_ui` | `t_display_p4_touch_manifest` | `t_display_p4_touch` | `Active` | none for UX pack selection | `apps/esp32_lvgl + builds/esp_idf + boards/t_display_p4` |
| `t_display_p4_amoled` | `t_display_p4` | `builds/esp_idf` | `apps/esp32_lvgl` | ESP-IDF | LVGL | `t_display_p4_touch` | `t_display_p4_touch` | `t_display_p4_touch_ui` | `t_display_p4_touch_manifest` | `t_display_p4_touch` | `Active` | none for UX pack selection | `apps/esp32_lvgl + builds/esp_idf + boards/t_display_p4` |
| `tlora_pager` | `tlora_pager` | `builds/esp_idf` | `apps/esp32_lvgl` | ESP-IDF | LVGL | `pager_compact` | `compatibility` | `pager_compact_ui` | `pager_compact_manifest` | `pager_compact` | `PendingHardwareValidation` | existing target capability still records Arduino/PIO evidence | `apps/esp32_lvgl + builds/esp_idf + boards/tlora_pager` |
| `tdeck` | `tdeck` | `builds/esp_idf` | `apps/esp32_lvgl` | ESP-IDF | LVGL | `deck_full` | `compatibility` | `deck_wide_ui` | `deck_full_manifest` | `deck_wide` | `PendingHardwareValidation` | existing target capability still records Arduino/PIO evidence | `apps/esp32_lvgl + builds/esp_idf + boards/tdeck` |
| `twatch` | `twatch` | `builds/esp_idf` | `apps/esp32_lvgl` | ESP-IDF | LVGL | `watch_compact` | `compatibility` | `watch_compact_ui` | `watch_compact_manifest` | `watch_compact` | `PendingHardwareValidation` | current repo evidence is board and variant data, not migrated IDF defaults | `apps/esp32_lvgl + builds/esp_idf + boards/twatch` |
| `uconsole` | `uconsole` | `builds/linux_cmake` | `apps/linux_uconsole_gtk` | Linux | GTK | `uconsole_desktop` | `uconsole_desktop` | `uconsole_desktop_ui` | `uconsole_desktop_manifest` | `uconsole_desktop` | `Active` | none for UX pack selection | `apps/linux_uconsole_gtk + builds/linux_cmake + boards/uconsole` |
| `linux_sim` | `linux_sim` | `builds/linux_cmake` | `apps/linux_sim_shell` | Linux | ASCII | `simulator_full` | `simulator_full` | `simulator_full_ui` | `simulator_full_manifest` | `simulator_full` | `Active` | none for simulator UX pack selection | `apps/linux_sim_shell + builds/linux_cmake` |
| `cardputerzero` | `cardputerzero` | `builds/linux_cmake` | `apps/linux_cardputer_zero` | Linux | LVGL Wayland | `cardputer_compact` | `cardputer_compact` | `cardputer_compact_ui` | `cardputer_compact_manifest` | `cardputer_compact` | `PendingHardwareValidation` | shared main-menu profile is Pager-derived; per-page screenshots, Wayland APPLaunch package build, and explicit fbdev/evdev debug fallback exist; Wayland startup, keyboard mapping, notifyd session, Fcitx5 session, and real interaction still need hardware validation | `apps/linux_cardputer_zero + builds/linux_cmake + boards/cardputerzero` |
| `gat562_mesh_evb_pro` | `gat562_mesh_evb_pro` | `builds/pio_nrf52` | `apps/nrf52_node` | PlatformIO | Headless | `node_headless` | `tiny_node_status` | `node_headless_ui` | `node_headless_manifest` | `headless_node` | `Headless` | current executable UX pack is `tiny_node_status`; board hardware still records a 128 x 64 display | `apps/nrf52_node + builds/pio_nrf52 + boards/gat562_mesh_evb_pro` |

## Pending Evidence

| target_id | pending evidence | Final owner |
| --- | --- | --- |
| `tlora_pager` | ESP-IDF target defaults and hardware validation for the requested IDF route | `builds/esp_idf + boards/tlora_pager` |
| `tdeck` | ESP-IDF target defaults and hardware validation for the requested IDF route | `builds/esp_idf + boards/tdeck` |
| `twatch` | ESP-IDF target defaults and hardware validation for the requested IDF route | `builds/esp_idf + boards/twatch` |
| `cardputerzero` | real Cardputer Zero Wayland APPLaunch startup, keyboard mapping, notifyd session, Fcitx5 session, explicit fbdev/evdev debug fallback, and live page interaction validation | `apps/linux_cardputer_zero + boards/cardputerzero` |

## Current Firmware Evidence

| target_id | evidence | remaining hardware scope |
| --- | --- | --- |
| `t_display_p4_tft` | 2026-06-13 ESP-IDF `esp32p4` build produced `.tmp/build.t_display_p4_tft_real/trail-mate.bin`, size `0x1b3cf0`, app free `0x24c310` (57%) | board flash, display/touch inspection, and P4-C6 live HostLink validation |
| `t_display_p4_amoled` | 2026-06-13 ESP-IDF `esp32p4` build produced `.tmp/build.t_display_p4_amoled_real/trail-mate.bin`, size `0x1b3cf0`, app free `0x24c310` (57%) | board flash, display/touch inspection, and P4-C6 live HostLink validation |
| `c6_companion` | 2026-06-13 ESP-IDF `esp32c6` build produced `.tmp/build.c6_companion_real/trail-mate-c6-companion.bin`, size `0x129560`, app free `0x1d6aa0` (61%) | C6 flash, RF checks, and P4-C6 live HostLink validation |

## Batch 2 Status Tokens

The former Batch 2 statuses `migrated_foundation` and
`pending_final_profile` are historical labels. Batch 3 replaces them with
`Active`, `ActiveWithFallback`, `PendingHardwareValidation`, and `Headless`.
