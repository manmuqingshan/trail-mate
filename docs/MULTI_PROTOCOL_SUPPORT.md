# Multi-protocol support implementation instructions

This document describes the current implementation of Trail Mate in the three-protocol scenario of Meshtastic, MeshCore, and Reticulum.
**The protocol will not be automatically determined or switched during operation**, but one of the protocols will be explicitly selected to run through the settings.

---

## 1) Protocol selection method

The current protocol is determined by the setting item:
- `AppConfig::mesh_protocol`
- Persistence key: `mesh_protocol`
- Value:
  - `Meshtastic`
  - `MeshCore`
  - `Reticulum`

Read the configuration when the system starts and create the corresponding adapter through `ProtocolFactory`. There is no dynamic switching during runtime.

---

## 2) Code structure (single protocol operation)

Core idea:
- **UI layer only interacts with `IMeshAdapter` and is not protocol-aware**
- Only create one adapter (Meshtastic, MeshCore or Reticulum)
- The original data of the Radio task is directly handed over to the selected adapter for processing

### Structure ASCII diagram

```
+------------------+
|   UI / UseCase   |
+------------------+
          |
          v
+-------------------------+
|     IMeshAdapter        |
+-------------------------+
          |
          v
+------------------+
| Meshtastic OR    |
| MeshCore OR      |
| Reticulum        |
| Adapter          |
+------------------+
```

---

## 3) Receiving process (no dynamic determination)

1. The Radio task receives the original packet
2. Directly call the current adapter's `handleRawPacket()`
3. The parsed text message enters `ChatService`

> No longer performs protocol prediction, nor maintains node protocol mapping.

### Business status unified boundary

 "Single protocol operation" does not mean that each protocol can have its own set of UI message status.

MT/MC/RT adapters can only map protocol facts to protocol-aware events:

- message identity
- queued / sending / sent / delivered / failed
- failure kind
- read/unread reference
- retry eligibility

These facts must go into the shared `MessageLedger`, `ChatDeliveryEventProjector`,
`ReadStateLedger` and conversation projection. Bubble status badge on UI,
conversation unread badge, delivery failure feedback, and retry actions must not be directly inferred from protocol adapter
private state.

See the complete owner boundary
`docs/specification/RUNTIME_OWNERSHIP_BOUNDARY_FREEZE.md`.

---

## 4) Settings planning

LVGL Settings are grouped by responsibilities instead of the old "Chat/Network/System" mixed grouping:

- `Profile`: user name, short name, active protocol, message/contact alerts.
- `Mesh`: protocol-specific mesh identity and internet bridge settings.
  - Meshtastic: region, active chat channel, primary/secondary channel enable/name/PSK, primary/secondary MQTT uplink/downlink flags, encryption mode, Meshtastic MQTT client settings.
  - MeshCore: channel slot/name/key and MeshCore MQTT client settings.
  - Reticulum: bearer policy, local identity display, Wi-Fi gateway host/port, auto Wi-Fi, anonymous peer.
- `Radio`: protocol-specific LoRa/air parameters.
  - Meshtastic: preset/manual modem parameters, TX power, hop limit, TX enable, channel slot, frequency offset/override, duty/utilization controls.
  - MeshCore: region preset, frequency/bandwidth/SF/CR, TX power, repeat/flood/profile controls.
  - Reticulum: manual LoRa parameters and TX enable when bearer policy includes LoRa; Wi-Fi-gateway-only mode hides LoRa radio settings.
- `Wi-Fi`: Wi-Fi enable/status/scan/SSID/password/connect/disconnect.
- `Location`: GPS receiver, position strategy, NMEA export, map source, contours, and track recording.
- `Device`: locale/IME, screen, speaker, vibration, C6 companion, time zone/date-time, and battery gauge calibration.
- `Maintenance`: firmware update, debug logs, mesh/node/message resets, and factory reset. The full editable configuration is maintained as the SD-first `/trailmate/config.tms` working document rather than through a separate backup/restore action.

Bluetooth is intentionally absent from Settings. For the ESP Arduino firmware profile, `TRAIL_MATE_ENABLE_BLE=0`, the NimBLE dependency is not part of the PlatformIO lib set, and the real ESP `src/ble/` implementation is excluded from the firmware build. Any legacy `ble_enabled=true` value is normalized back to `false`.

### Settings implementation contract

Settings items are bound through `SettingId` and `settings::ui::spec` instead of ad-hoc `pref_key` string dispatch in the page logic. Protocol visibility, dynamic option ownership, translated-label rules, and settings-store ownership are centralized in that spec layer. Page event handlers switch on `SettingId`, while `pref_key` remains only as the persistence key and as a one-time binding input for legacy aggregate item declarations.

Resource constraints are part of the contract:
- MT/MC channel key helpers use fixed-size caller-owned buffers and bounded key lengths.
- SD working-configuration blob handling uses bounded static/PSRAM scratch storage instead of dynamic byte vectors.
- Wi-Fi scan fills the Settings page's fixed network slots directly; it does not allocate an intermediate dynamic scan list.

## 5) Phone Independence Boundary

The device-side goal is to operate without a phone. The first required channel-management surface is:

- Meshtastic Primary channel: enabled flag, name, PSK, MQTT uplink/downlink flags.
- Meshtastic Secondary channel: enabled flag, name, PSK, MQTT uplink/downlink flags.
- MeshCore channel: slot, name, key.

These settings write into the existing `AppConfig` / `MeshConfig` fields and use the same NVS-mirrored, SD-first working-configuration path as the runtime configuration. Meshtastic PSK generation creates a 16-byte key by default, encoded as 32 uppercase hex characters; manual entry also accepts the existing 16/32-byte Meshtastic PSK formats. MeshCore channel key generation creates a 16-byte key.

## Current implementation status description

- Meshtastic: complete functions (including NodeInfo, channel identity/config, plain MQTT client)
- MeshCore: RAW_CUSTOM text sending and receiving closed loop, channel slot/name/key can be configured on the device side, plain MQTT client has independent configuration entry
- Reticulum: runs as the default independent protocol, Settings exposes bearer/gateway/identity Configuration; product call path only supports Sideband-compatible LXST
- BLE mobile phone bridge: ESP Arduino The current product firmware does not compile, does not start, and is not displayed in Settings

---

## Extension suggestions

If you need to support more protocols:
- Add a new protocol option in Settings
- Create a new corresponding adapter
- Still maintain the "single protocol at runtime" strategy to avoid mixed runs and misjudgments
