# GAT562 implementation mapping

This file is used to clearly describe the implementation point, layered ownership and current completion status of `gat562_mesh_evb_pro`.

Authoritative constraint source:
- GAT562 hardware reference in `.tmp/meshtastic-firmware`
- `docs/Best Practices/GAT562_REQUIREMENTS.md`
- `docs/Best Practices/new_hardware_adaptation_prompt.md`

---

## 1. Board-level facts

Placement:
- `boards/gat562_mesh_evb_pro.json`
- `boards/gat562_mesh_evb_pro/include/boards/gat562_mesh_evb_pro/board_profile.h`
- `variants/gat562_mesh_evb_pro/variant.h`
- `variants/gat562_mesh_evb_pro/variant.cpp`

Bearer content:
- nRF52840 / S140 / Flash / RAM / bootloader constraints
- OLED / LoRa / GNSS / LED / button / battery / 3V3 power supply pin
- Product boundary: support `Meshtastic / MeshCore / BLE / LoRa / GNSS`
- Product boundary: Not supported `Team / HostLink / SD / CJK / Pinyin IME`

Principle:
- Board-level parameters are only placed in `boards/` and `variants/`
- The business layer does not directly hard-code pins and hardware facts

---

## 2. Environment definition

Placement:
- `variants/gat562_mesh_evb_pro/envs/gat562_mesh_evb_pro.ini`

Bearer content:
- GAT562 independent compilation environment
- nRF52 include path / generated protobuf include path
- `RadioLib / TinyGPSPlus / nanopb` dependency
- GAT562 boundary clipping: exclude `Team / HostLink / USB/PC Link / CJK font library / Pinyin IME`

Principle:
- GAT562 compilation clipping is completed in the environment layer
- There is no need to write board-level ifdef everywhere in the shared module to hide the truth

---

## 3. Shared identity and self-declaration

Placement:
- `modules/core_chat/include/chat/runtime/self_identity_provider.h`
- `modules/core_chat/include/chat/runtime/self_identity_policy.h`
- `modules/core_chat/src/runtime/self_identity_policy.cpp`
- `modules/core_chat/include/chat/runtime/self_announcement_core.h`
- `modules/core_chat/include/chat/runtime/meshtastic_self_announcement_core.h`
- `modules/core_chat/src/runtime/meshtastic_self_announcement_core.cpp`
- `modules/core_chat/include/chat/runtime/meshcore_self_announcement_core.h`
- `modules/core_chat/src/runtime/meshcore_self_announcement_core.cpp`

Bearer content:
- Unified derivation rules for `long name / short name / node id / BLE name`
- Meshtastic NodeInfo group package
- MeshCore identity advert group package

Principle:
- Identity derivation rules belong to the shared business, not to the app
- Air interface self-declaration rules belong to the shared business, not to the board-level runtime

---

## 4. Shared app container has strong dependency on Team

Placement:
- `modules/core_sys/include/app/app_context_platform_bindings.h`
- `apps/esp_pio/src/app_context.cpp`

Bearer content:
- `create_team_services` is no longer a hard requirement for app context validity
- GAT562 can be accessed as a real "Teamless device" instead of a fake Team shell

---

## 5. nRF52 platform bridging and persistence

Placement:
- `platform/nrf52/arduino_common/include/platform/nrf52/arduino_common/device_identity.h`
- `platform/nrf52/arduino_common/src/device_identity.cpp`
- `platform/nrf52/arduino_common/include/platform/nrf52/arduino_common/settings_runtime.h`
- `platform/nrf52/arduino_common/src/settings_runtime.cpp`
- `platform/nrf52/arduino_common/include/platform/nrf52/arduino_common/self_identity_bridge.h`
- `platform/nrf52/arduino_common/src/self_identity_bridge.cpp`
- `platform/nrf52/arduino_common/include/platform/nrf52/arduino_common/app_config_store.h`
- `platform/nrf52/arduino_common/src/app_config_store.cpp`
- `platform/nrf52/arduino_common/include/platform/nrf52/arduino_common/chat/infra/blob_file_store.h`
- `platform/nrf52/arduino_common/src/chat/infra/blob_file_store.cpp`
- `platform/nrf52/arduino_common/include/platform/nrf52/arduino_common/chat/infra/contact_store.h`
- `platform/nrf52/arduino_common/src/chat/infra/contact_store.cpp`
- `platform/nrf52/arduino_common/include/platform/nrf52/arduino_common/chat/infra/meshtastic/node_store.h`
- `platform/nrf52/arduino_common/src/chat/infra/meshtastic/node_store.cpp`

Bearer content:
- Derive node id / MAC from `NRF_FICR->DEVICEADDR`
- Unify and normalize Meshtastic / MeshCore Configuration
- app config / node / contact storage on `InternalFS`
- Message prompt volume persistence entry
- Directly push back the radio / BLE / GNSS / identity running state after saving the configuration

Principle:
- The platform layer is responsible for "reading the true value" and "dropping to disk"
- The shared layer only consumes abstract results

---

## 6. nRF52 LoRa transport layer

Placement:
- `platform/nrf52/arduino_common/include/platform/nrf52/arduino_common/chat/infra/radio_packet_io.h`
- `platform/nrf52/arduino_common/src/chat/infra/radio_packet_io.cpp`
- `platform/nrf52/arduino_common/include/platform/nrf52/arduino_common/chat/infra/sx1262_radio_packet_io.h`
- `platform/nrf52/arduino_common/src/chat/infra/sx1262_radio_packet_io.cpp`

Bearer content:
- `IRadioPacketIo` abstraction
- GAT562 SX1262 + RadioLib implementation
- 3V3 / radio power rail / SPI / Radio chip initialization
- Meshtastic / MeshCore two sets of radio parameter applications
- Return to RX after TX, fill in `RSSI / SNR / freq / bw / sf / cr`
- Frequency string formatting capability is provided in the board-level runtime, and subsequent screensaver pages are directly consumed

Principle:
- radio The real parameters that take effect belong to the platform layer
- the app is only responsible for delivering the current protocol configuration to the platform layer

---

## 7. nRF52 protocol adapter

Placement:
- `platform/nrf52/arduino_common/include/platform/nrf52/arduino_common/chat/infra/protocol_factory.h`
- `platform/nrf52/arduino_common/src/chat/infra/protocol_factory.cpp`
- `platform/nrf52/arduino_common/include/platform/nrf52/arduino_common/chat/infra/meshtastic/meshtastic_radio_adapter.h`
- `platform/nrf52/arduino_common/src/chat/infra/meshtastic/meshtastic_radio_adapter.cpp`
- `platform/nrf52/arduino_common/include/platform/nrf52/arduino_common/chat/infra/meshcore/meshcore_radio_adapter.h`
- `platform/nrf52/arduino_common/src/chat/infra/meshcore/meshcore_radio_adapter.cpp`

Bearer content:
- Meshtastic Lite Adapter: text, AppData, NodeInfo, self-declaration
- MeshCore Lite Adapter: text/AppData forwarding, identity advert, self-declaration
- Meshtastic packet collection press `channel_hash -> key` Decryption, no longer only supports no PSK

Principle:
- Reuse protocol logic as much as possible with `modules/core_chat`
- Platform adapter only supplements transport / runtime glue

---

## 8. nRF52 GNSS / Device runtime

Placement:
- `platform/nrf52/arduino_common/include/platform/nrf52/arduino_common/gps_runtime.h`
- `platform/nrf52/arduino_common/src/platform_ui_gps_runtime.cpp`
- `platform/nrf52/arduino_common/src/platform_ui_device_runtime.cpp`
- `platform/nrf52/arduino_common/src/platform_ui_time_runtime.cpp`

Bearer content:
- nRF52 implementation of `platform::ui::gps::*`
- nRF52 implementation of `platform::ui::device::*`
- UART GNSS input analysis
-Basic battery percentage reading
-GPS time adjustment entry
-Local time zone offset persistence and local time conversion
-Unified `board_runtime` completes 3V3 rail / LED / input pin initialization

Principle:
- The UI reads the shared platform runtime interface
- The specific serial port, ADC, and RTC determinations belong to the platform layer

---

## 9. nRF52 BLE runtime

Placement:
- `platform/nrf52/arduino_common/include/ble/ble_manager.h`
- `platform/nrf52/arduino_common/src/ble/ble_manager.cpp`

Bearer content:
- Bluefruit basic broadcast/connection entrance
- Switch the broadcast service UUID according to the current protocol
- BLE names uniformly use the shared identity derivation rules of `modules/core_chat`

Current status:
- The nRF52 side BLE manager boundary and basic broadcast capabilities have been established
- Meshtastic / MeshCore still needs to be completed Complete mobile phone side protocol service

---

## 9.5 nRF52 board-level runtime

Placement:
- `platform/nrf52/arduino_common/include/platform/nrf52/arduino_common/board_runtime.h`
- `platform/nrf52/arduino_common/src/board_runtime.cpp`

Bearer content:
- 3V3 rail initialization
- Status LED/Prompt LED control
- GAT562 joystick and button raw input snapshot
- LoRa frequency string formatting

Principle:
- Power rail / LED / GPIO input fact belongs to platform board level runtime
- LoRa / GPS / UI no longer individually initialize the same board level pins

---

## 9.6 nRF52 input runtime

Placement:
- `platform/nrf52/arduino_common/include/platform/nrf52/arduino_common/input_runtime.h`
- `platform/nrf52/arduino_common/src/input_runtime.cpp`

Bearer content:
- Joystick/button debouncing events
-Last activity timestamp
-Current raw input snapshot

Principle:
-Raw GPIO -> Conversion of consumable input events belongs to platform runtime
-UI does not directly poll scattered GPIO

---

## 10. GAT562 app assembly layer

Placement:
- `apps/gat562_mesh_evb_pro/include/apps/gat562_mesh_evb_pro/app_facade_runtime.h`
- `apps/gat562_mesh_evb_pro/src/app_facade_runtime.cpp`
- `apps/gat562_mesh_evb_pro/include/apps/gat562_mesh_evb_pro/app_runtime_access.h`
- `apps/gat562_mesh_evb_pro/src/app_runtime_access.cpp`
- `apps/gat562_mesh_evb_pro/include/apps/gat562_mesh_evb_pro/startup_runtime.h`
- `apps/gat562_mesh_evb_pro/src/startup_runtime.cpp`
- `apps/gat562_mesh_evb_pro/include/apps/gat562_mesh_evb_pro/loop_runtime.h`
- `apps/gat562_mesh_evb_pro/src/loop_runtime.cpp`
- `apps/gat562_mesh_evb_pro/include/apps/gat562_mesh_evb_pro/arduino_entry.h`
- `apps/gat562_mesh_evb_pro/src/arduino_entry.cpp`
- `apps/gat562_mesh_evb_pro/include/apps/gat562_mesh_evb_pro/ui_runtime.h`
- `apps/gat562_mesh_evb_pro/src/ui_runtime.cpp`
- `src/main.cpp`

Bearer content:
- GAT562 independent app facade, no longer reuses the ESP biased `AppContext`
- Assembly at startup:
  - app config
  - identity bridge
  - node/contact store
  - protocol adapters
  - BLE manager
  - SX1262 radio packet io
  - GPS runtime
- Driver in loop:
  - GPS tick
  - Mono OLED UI tick
 - Input events -> UI action mapping
  - raw packet -> active adapter
  - chat core update
  - BLE update
  - event dispatch

Principle:
- `apps/` is only responsible for assemble, not responsible for inventing shared services

---

## 11. Correspondence to requirements documents

Main lines already covered:
- `User Name / Short Name` -> Native identity / BLE name / LoRa Self-declaration linkage
- GAT562 real `no-Team / no-HostLink / no-SD / no-CJK`
- LoRa parameters driven by settings real radio configuration
- GNSS / battery / device runtime no longer fake entry
- GAT562 monochrome UI already has independent shared module with nRF52 backend
- Startup log/screensaver/main menu/session/text input/identity/wireless/device/GNSS/action page has a clear focus
- MeshCore Lite no longer only recognizes advert, but can now inbound direct/group app payload into `AppData`
- Meshtastic Lite no longer only recognizes text, but can now inbound non-text `AppData` into `AppData`

Main lines that still need to be completed:
- Meshtastic / MeshCore complete BLE mobile protocol service
- More complete message reception/ACK/control services
- Monochrome UI compilation and real machine joint debugging closed loop

---

## 12. Layered conclusion

The long-term structure determined in this round:
- `modules/`: shared services, protocols, self-declaration, identity policy
- `platform/nrf52/`:nRF52 transport / BLE / GNSS / FS / device runtime
- `boards/gat562_mesh_evb_pro/`: board-level facts and boundaries
- `apps/gat562_mesh_evb_pro/`: Device startup and assembly
- `variants/gat562_mesh_evb_pro/`: Compilation environment and capability tailoring

This is also the standard landing point when continuing to complete GAT562 and adapting to more new hardware.

## Board Ownership Update

- `boards/gat562_mesh_evb_pro/src/gat562_board.cpp` is now the single board owner for GAT562 board-specific hardware coordination.
- Board-owned concerns include: 3V3 power rail, GPIO input mapping, OLED/I2C access, GPS UART bring-up, board default identity, and LoRa radio binding/config application.
- `apps/gat562_mesh_evb_pro/*` remains composition-only: it wires board, shared platform BLE, shared protocol adapters, and shared UI together.
- GAT562 app code must not keep a second board-specific LoRa/GPIO/bus/power owner outside `Gat562Board`.
- Shared BLE logic should stay in `platform/nrf52/arduino_common`, with board-specific prerequisites absorbed by `Gat562Board`.
- BLE ownership rule update: `apps/gat562_mesh_evb_pro` no longer keeps a board-specific `ble_owner` layer. The app composes the shared `platform/nrf52` `BleManager` directly, while board-specific defaults/prerequisites stay in `Gat562Board`.
- GPS startup rule update: app startup should call a single board-facing GPS runtime entry, rather than splitting UART bring-up and config application across multiple app/runtime locations.
- Radio hardware ownership update: GAT562 LoRa rail enable and SPI bus bring-up are board-owned concerns and now live behind `Gat562Board::prepareRadioHardware()`. `sx1262_radio_packet_io` should only perform chip-level RadioLib initialization and packet I/O.
- I2C ownership update: the former standalone `i2c_bus` helper is now folded into `Gat562Board`. Shared OLED/I2C access and the RAII lock live behind `Gat562Board::i2cWire()` and `Gat562Board::I2cGuard`, keeping board bus ownership at a single entry point.
