# Wio Tracker L2 product adaptation

## Confirmed product contract

The user confirmed on 2026-09-10 that this is a complete Trail Mate port for
the L2 Pro touchscreen model. Its landscape 320 x 240 interface follows the
T-Deck environment's pages and functions. It has no physical keyboard or
trackball. Every alphabetic text-entry surface must offer a full touchscreen
keyboard, including chat composition, names, search and settings fields.

This clarification replaces the preliminary diagnostic-only environment with
the normal `apps/esp32_lvgl` product entrypoint. It changes the implementation
scope, not the ownership of the hardware or shared business modules.

The canonical board and environment identifier is `wio_tracker_l2`.
Development starts from local `main` commit `f84c4654` on branch
`feat/wio-tracker-l2`.

| Responsibility | Owner |
| --- | --- |
| Processor, memory and upload metadata | `boards/wio_tracker_l2.json` |
| Pins, buses and electrical facts | `boards/wio_tracker_l2/` |
| SDK objects, driver execution, bus/audio ownership and platform binding | `platform/esp/wio_tracker_l2/` |
| PlatformIO environment | `variants/wio_tracker_l2/envs/wio_tracker_l2.ini` |
| Product entrypoint | Existing `src/main.cpp` and `apps/esp32_lvgl` |
| Messaging, maps, settings and application state | Existing shared modules |
| Compact touch UX selection and text-editor modal | `modules/ui_lvgl_ux_packs/`, reusing the existing IME contract |
| Filesystem and storage ownership | Existing ESP storage contract with SDMMC transport |

The selected product route is `deck_touch` with `deck_touch_ui`, the existing
`deck_full_manifest` and `deck_wide` layout DTO. The actual Arduino startup
resolves TargetUxBinding, validates the registered pack, configures its input
layout, and supplies the pack ID to the startup shell. This route has no UX
fallback. T-Deck retains its existing contained route.

The wider architecture review and evidence are recorded in
`docs/audits/WIO_TRACKER_L2_ARCHITECTURE_REVIEW.md`; capability/authority bindings
are recorded in `docs/targets/wio_tracker_l2.capabilities.yaml`.

## Constraints

- Board hardware presence, compiled driver support, initialized readiness and
  exposed product capabilities are distinct facts.
- Reuse T-Deck's presentation conventions without defining `ARDUINO_T_DECK` or
  adopting its pins, keyboard, trackball or shared-SPI storage assumptions.
- Keep radio, QSPI display and 1-bit SDMMC on their actual separate buses.
- Preserve the existing filesystem and storage ownership semantics for maps,
  history and external storage. Do not add a second application file API.
- The optional e-paper connector and reserved microphone do not imply working
  e-paper or microphone capabilities. The selected product uses the IPS panel.
- Keep missing SD, absent GNSS fixes and radio errors from preventing the UI
  from starting. Do not report a missing sensor as a fabricated measurement.

## Build

From the repository root:

```powershell
pio run -e wio_tracker_l2
pio device monitor -e wio_tracker_l2
```

The image is `.pio/build/wio_tracker_l2/firmware.bin`. This environment uses
the shared Arduino ESP platform and product scripts, 16 MB QIO flash, 8 MB
OPI PSRAM and USB Serial/JTAG CDC. Firmware build success and on-device
validation are recorded separately.

## Acceptance

Text fields open a shared full-screen editor on tap. It preserves the original
page, supports lower/upper case, numbers and punctuation, and reuses the IME's
enabled language packs, including pinyin candidates. OK applies accepted text
to the original field; Cancel leaves it unchanged. Deleting the source field
or detaching its IME owner dismisses the editor. Existing IME consumers detach
on the UI owner before destruction. Existing T-Deck input remains unchanged.

The speaker path provides notification tones and received Codec2-1300 voice
playback. Recording remains unavailable because this hardware specification
reserves the microphone pins without establishing a fitted microphone.

- The environment builds the full product and explicitly resolves the L2
  board, storage transport and touch input.
- The UI follows T-Deck's landscape page layout and touch navigation. Chat,
  contact/name fields, search and text settings can be completed without a
  physical keyboard; text controls and confirmation remain reachable while
  the on-screen keyboard is visible.
- Display, touch, backlight, radio, GNSS, SD and power functions report actual
  driver readiness. Transmit settings retain the shared radio-region policy.
- Validate display colors/orientation, touch coordinates, radio exchange,
  GNSS fix, SD reads/writes and power transitions on real hardware. A firmware
  build alone does not establish these physical results.
