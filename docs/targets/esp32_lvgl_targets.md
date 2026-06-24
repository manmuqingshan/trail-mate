# ESP32 LVGL Target Profiles

Phase 8.3 target profile baseline plus the closed T-Display-P4 touch UX route.

```text
Build Entrypoint invokes.
App Shell composes.
Target chooses.
Board describes.
UX Pack presents.
```

| Target | Board | Platform | Build entrypoint | App shell | UX profile | Status |
| --- | --- | --- | --- | --- | --- | --- |
| `tab5` | `tab5` | ESP32-P4 + ESP32-C6 | `builds/esp_idf` | `apps/esp32_lvgl` | `tab5_touch` | active with fallback |
| `t_display_p4_tft` | `t_display_p4` | ESP32-P4 + ESP32-C6 | `builds/esp_idf` | `apps/esp32_lvgl` | `t_display_p4_touch` | active |
| `t_display_p4_amoled` | `t_display_p4` | ESP32-P4 + ESP32-C6 | `builds/esp_idf` | `apps/esp32_lvgl` | `t_display_p4_touch` | active |
| `tdeck` | `tdeck` | ESP32-S3 | `builds/esp_idf` | `apps/esp32_lvgl` | `deck_full` | planned |
| `tdeck_pro` | `tdeck_pro` | ESP32-S3 | `builds/esp_idf` | `apps/esp32_lvgl` | `deck_full` | planned |
| `tlora_pager` | `tlora_pager` | ESP32 family | `builds/esp_idf` | `apps/esp32_lvgl` | `pager_compact` | planned |
| `twatchs3` | `twatchs3` | ESP32-S3 | `builds/esp_idf` | `apps/esp32_lvgl` | `watch_quick` | planned |

## Naming Contract

The canonical repository spelling for the T-Display-P4 family is
`t_display_p4`. Use it for target ids, build directories, target defaults,
board package directories, UX pack ids, page manifests, layout profiles, and
documentation. `TDisplayP4*` remains valid only as a C++ type-name projection.

The supported ESP-IDF product targets are:

```text
t_display_p4_tft
t_display_p4_amoled
```

There must not be a second tracked collapsed spelling without underscores for
the same target family.

## T-Display-P4 Product Contract

Both T-Display-P4 targets build the ESP32-P4 product firmware through
`builds/esp_idf` and select the same `t_display_p4_touch` UX pack. The TFT and
AMOLED targets differ in display defaults and board runtime selection, not in
Trail Mate product meaning.

The P4 firmware owns:

- Trail Mate business state, UI, storage, GPS, LoRa, maps, Chat, Contacts,
  Team, Tracker, PC Link, Energy Sweep, Walkie, SSTV, Extensions, and Settings.
- Target profile, UX pack, page manifest, menu profile, page layout profile,
  and downgrade decisions when the C6 companion is absent or not ready.

The `t_display_p4_touch_manifest` page set is:

```text
dashboard
chat
contacts
map
sky_plot
gps
team
tracker
pc_link
energy_sweep
walkie_talkie
sstv
extensions
settings
```

The P4 firmware build must link the UI source for those pages and the core
domain/use-case sources needed by the pages. Registering a page id without the
backing source in the final ESP-IDF component is not considered adapted.

## C6 Companion Contract

The board package records that the T-Display-P4 hardware includes an ESP32-C6
companion. The companion firmware is an in-repository peer project at:

```text
firmware/c6_companion
```

It is built and flashed separately from the P4 product firmware. The C6 owns
BLE, ESP-NOW, Wi-Fi facade mechanics, diagnostics, and HostLink session
mechanics. It must not own Trail Mate business meaning, message history, keys,
Team membership, maps, LoRa policy, or UI.

## Validation Gates

For this target family to be called complete in the repository, all of the
following must pass:

- Host CMake tests covering `t_display_p4_touch` UX pack, menu profile, page
  profile, target profile, target UX binding, target build binding, page
  manifest, and C6 service functional smoke.
- ESP-IDF build of `firmware/c6_companion` for `esp32c6`.
- ESP-IDF build of `TRAIL_MATE_IDF_TARGET=t_display_p4_tft` for `esp32p4`.
- ESP-IDF build of `TRAIL_MATE_IDF_TARGET=t_display_p4_amoled` for `esp32p4`.
- Architecture checks proving target defaults, board facts, source ownership,
  and final target composition do not drift.
- A tracked-file search proving no legacy collapsed target spelling remains.

Current local repository evidence from 2026-06-13:

- `TRAIL_MATE_IDF_TARGET=t_display_p4_tft` built for `esp32p4` and produced
  `.tmp/build.t_display_p4_tft_real/trail-mate.bin`; binary size `0x1b3cf0`,
  smallest app partition `0x400000`, free `0x24c310` (57%).
- `TRAIL_MATE_IDF_TARGET=t_display_p4_amoled` built for `esp32p4` and produced
  `.tmp/build.t_display_p4_amoled_real/trail-mate.bin`; binary size `0x1b3cf0`,
  smallest app partition `0x400000`, free `0x24c310` (57%).
- `firmware/c6_companion` built for `esp32c6` and produced
  `.tmp/build.c6_companion_real/trail-mate-c6-companion.bin`; binary size
  `0x129560`, smallest app partition `0x300000`, free `0x1d6aa0` (61%).

This evidence proves the repository-owned P4 and C6 firmware artifacts build and
link with the expected target composition. It does not replace board flashing,
live RF checks, or P4-C6 SDIO HostLink interoperability validation.

## Non-Goals

This document does not move sdkconfig defaults, ESP-IDF CMake files, app
runtime code, or LVGL screen implementations.

## C6 and Orientation Policy

`tab5`, `t_display_p4_tft`, and `t_display_p4_amoled` select the ESP32-C6 wireless
companion as their BLE backend. P4 remains the Trail Mate business authority,
and C6 is the BLE / ESP-NOW / Wi-Fi facade.

All three ESP32-P4 targets are motion-sensor aware but landscape locked in the
current release. Motion sensor presence is a board fact; portrait UI support is
not enabled by this target profile.
