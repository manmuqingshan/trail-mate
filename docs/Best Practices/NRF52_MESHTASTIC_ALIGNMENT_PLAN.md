# GAT562 / nRF52 alignment `.tmp/meshtastic-firmware` modification list

## Baseline principles

- Based on the platform layering method of `.tmp/meshtastic-firmware`, no self-innovation architecture
- The core of the shared protocol is responsible for business semantics
-The platform Bluetooth host is only responsible for transport / callback / notify
-The board-level owner is only responsible for hardware facts: LoRa / GPS / I2C / input / time source

## Reference implementation mapping

- Reference shared core
  - `.tmp/meshtastic-firmware/src/mesh/PhoneAPI.cpp`
  - `.tmp/meshtastic-firmware/src/mesh/MeshService.cpp`
- Reference platform host
  - `.tmp/meshtastic-firmware/src/platform/nrf52/NRF52Bluetooth.cpp`
  - `.tmp/meshtastic-firmware/src/nimble/NimbleBluetooth.cpp`

## Current deviation

- `platform/nrf52/arduino_common/src/ble/meshtastic_ble.cpp`
 - Still responsible for too much Meshtastic phone protocol and configuration response logic
- `platform/nrf52/arduino_common/src/ble/meshcore_ble.cpp`
 - Still responsible for too much MeshCore command interpretation and device status assembly logic
- `platform/nrf52/arduino_common/src/chat/infra/meshtastic/meshtastic_radio_adapter.cpp`
 - Simultaneously responsible for radio codec, part of the business package, node declaration
- `platform/nrf52/arduino_common/src/chat/infra/meshcore/meshcore_radio_adapter.cpp`
 - Simultaneously responsible for radio codec and MeshCore business bridging
- nRF BLE file previously directly relied on `gat562_board`
 - Violation of "board owner provides facts, the upper layer only consumes"

## Transformation sequence

### 1. First make the BLE host thinner

- The BLE layer no longer depends directly on `boards/gat562_mesh_evb_pro`
- The BLE layer consumes through `app::IAppBleFacade`:
 - Current time synchronization entry
  - node store
  - chat service
  - mesh adapter

### 2. Pump Meshtastic shared phone core

- From `platform/nrf52/arduino_common/src/ble/meshtastic_ble.cpp` sink:
  - `handleToRadio`
  - `handleToRadioPacket`
 - admin/config/module config response
 - `FromRadio` encoding and queue status generation
- Target form:
 - Platform independent `MeshtasticPhoneCore`
 - nRF BLE only retains characteristic / advertising / read-write callback

### 3. Pump MeshCore shared phone core

- Dropped from `platform/nrf52/arduino_common/src/ble/meshcore_ble.cpp`:
 - Command distribution
 - contact/status/device info frame assembly
 - raw data push / telemetry push rules
- Target form:
 - Platform independent `MeshCorePhoneCore`
 - nRF BLE only retains NUS style transceiver host

### 4. Collapse radio entry

- Keep `boards/gat562_mesh_evb_pro/src/sx1262_radio_packet_io.cpp` as the only board-level radio owner
- Meshtastic / MeshCore sharing `IRadioPacketIo`
- adapter/core no longer touches board level SPI / pin / IRQ

### 5. Collapse GPS / time owner

- GAT562 board level continues to provide:
 - GPS power-on/serial port/NMEA analysis
 - Current epoch fact
 - time synced status
- The upper layer only consumes through the facade / runtime interface, not directly connected to the board class

### 6. Reprocessing naming and residual layers

- When `*_lite` Rename when no longer accurate
- No compatibility layer is retained
- No temporary runtime is added

## This round has started execution

- `IAppBleFacade::syncCurrentEpochSeconds()` has been added
- nRF BLE's Meshtastic / MeshCore time synchronization has been changed facade instead of directly accessing `gat562_board`

## Next batch of code actions

- Extract the phone protocol logic in Meshtastic BLE into an independent core
- Let `MeshtasticBleService` only have Bluefruit host responsibility
- Do the same with MeshCore
