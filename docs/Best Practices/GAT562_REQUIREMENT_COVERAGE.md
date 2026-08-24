# GAT562 requirements coverage list

This file is used to track the current implementation status against `GAT562_REQUIREMENTS.md`.

## Implemented

- Identity single source of truth: `long name / short name / node id / BLE name` Unified `self_identity_policy` of `modules/core_chat`
- LoRa true configuration: `region / preset / channel / tx_power / MeshCore radio params` Can be pushed down to `SX1262`
- Meshtastic air interface self-announcement: sharing `MeshtasticSelfAnnouncementCore`
- MeshCore air interface self-announcement: share `MeshCoreSelfAnnouncementCore`
- Meshtastic Lite inbound: text + non-text `AppData`
- MeshCore Lite inbound: advert + direct/group `AppData`
- GNSS platform runtime: UART Read stream, fix status, basic timing
- Device runtime: battery percentage, sound volume persistence, prompt LED
- Monochrome 128x64 shared UI module: `modules/ui_mono_128x64` has been added
- GAT562 monochrome UI assembly: accessed startup log, screensaver, main menu, chat list, conversation, English/number/symbol input, identity/wireless/device/GNSS/action page
- Screensaver information: `mt/mc`, `MHz` frequency, time, date, day of the week, short node id
- GAT562 timezone runtime: A new independent platform time offset persistence interface has been added
- no-Team / no-HostLink / no-SD / no-CJK / no-Pinyin: has been cut at the boundary between env and shared UI
- `saveConfig()`: has been changed to "immediately push back the running state after disk placement"

## The skeleton has been set up but the loop is not closed

- nRF52 BLE manager: already has protocol switching, broadcast name linkage, basic connectable service
- nRF52 board runtime: already has 3V3 rail / LED / input snapshot / frequency format
- GAT562 app facade: has an independent assembly root and is no longer dependent on ESP app context
- Monochrome UI It has not been compiled and returned to the actual machine. It is currently "the structure has been implemented and is pending joint debugging and verification"

## Still to be completed

- Meshtastic mobile phone side BLE complete protocol
- MeshCore mobile phone side BLE complete protocol
-The setting page and the final page mapping of all real capabilities
-The final real-machine verification of the startup chain/main loop/IC coordination

## Conclusion

The current code has already put "shared identity/self-declaration/LoRa configuration/GNSS/board level" The basic main lines of "runtime / no-Team boundary" have been pushed back to the correct level.
But **GAT562 has not yet reached the "all completed" status in the requirements document**. The highest subsequent priority is still:

1. Monochrome UI closed loop
2. BLE mobile phone protocol closed loop
3. Real machine stability closed loop

---

## 2026-03-18 Incremental Coverage Notes

- nRF52 `Meshtastic BLE` now covers:
  - `get_device_connection_status_request`
  - `get_module_config_request`
  - `set_module_config`
  - `FromRadio.moduleConfig` snapshot emission during `want_config_id`
- nRF52 `MeshCore BLE` now additionally covers:
  - `CMD_GET_BATT_AND_STORAGE`
  - `CMD_SEND_LOGIN`
  - `CMD_SEND_STATUS_REQ`
  - `CMD_SEND_BINARY_REQ`
  - `CMD_SEND_PATH_DISCOVERY_REQ`
  - `CMD_SEND_RAW_DATA`
  - `CMD_SEND_TRACE_PATH`
  - `CMD_SEND_CONTROL_DATA`
  - `CMD_SET_FLOOD_SCOPE`
  - `CMD_HAS_CONNECTION`
  - `CMD_LOGOUT`
  - `CMD_RESET_PATH`
- These additions stay inside `platform/nrf52/arduino_common`, which keeps the board/app/module boundary consistent with `new_hardware_adaptation_prompt.md`.
- No build or device validation is claimed in this note.
- nRF52 `MeshCore BLE` coverage in this round also now includes:
  - `CMD_SEND_TELEMETRY_REQ`
  - `CMD_GET_ADVERT_PATH`
  - `CMD_GET_BATT_AND_STORAGE`
- nRF52 `Meshtastic BLE` in this round also now handles lightweight local admin helpers:
  - canned-message get/set
  - ringtone get/set
  - `set_time_only`
  - `store_ui_config`
  - `remove_by_nodenum` request path
