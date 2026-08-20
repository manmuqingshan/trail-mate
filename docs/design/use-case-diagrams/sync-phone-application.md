# Use Case: Provide protocol compatibility services to mobile applications

Status: **confirmed integration behavior**
Business Boundary: External application integrated with host

## User goal

Let Meshtastic or MeshCore mobile applications read nodes/config/GPS, exchange messages and modify allowed settings through their native BLE contract, while maintaining a single App capability facade and active protocol owner inside the device.

## Main scene

1. Target capability determines whether to enable phone BLE; active mesh protocol selects Meshtastic BLE core or MeshCore BLE service.
2. BLE service completes the connection and protocol handshake, and parses ToRadio/commands.
3. `IPhoneAppFacade / AppPhoneFacade` provides user information, configuration, nodes, channels, GPS, messages and allowed tuning operations.
4. The facade maps the request to the current application service/active backend; the response is encoded by the corresponding protocol core into its own wire frame.
5. Clean the BLE session/ring state when disconnected, without changing the submitted device configuration or messages.

## Boundary rules

- Meshtastic protobuf and MeshCore BLE framing are not merged.
- The phone app is an external client and does not own a radio, ContactService, or message ledger.
- Large frame/protobuf uses member scratch or fixed ring slot, not placed in ESP task stack.
- "companion App BLE has been removed" refers to the old companion workflow, which does not mean that the two phone protocol services do not exist.

Source code: `modules/core_phone/include/phone/ports/i_phone_app_facade.h`, `platform/esp/arduino_common/include/ble/app_phone_facade.h`, Meshtastic/MeshCore BLE cores.

## Drill down

- [Activity](sync-phone-application/activity.md)
- [Sequence](sync-phone-application/sequences/sequence-sync-phone-application.md)
- [Composite Structure](sync-phone-application/composite-structures/phone-protocol-boundary.md)
