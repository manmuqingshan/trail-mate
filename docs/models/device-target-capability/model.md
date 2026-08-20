# Target Manifest, Capability and Authority

Model status: **confirmed; it is a product configuration model, not a hardware inventory**

## What a TargetManifestView represents

`TargetManifestView` combines six types of facts for the same target into a read-only view:

1. `TargetId`
2. `ProductDescriptor`
3. `PlatformDescriptor`
4. board / runtime / UI binding
5. `CapabilityStatus[]` and capability binding
6. `AuthorityBinding[]`

It answers "which host and provider combinations this product target consists of, what state the capability is in, and who owns the state" instead of just listing compiled macros.

## Capability is a state, not a boolean value

The actual vocabulary of `CapabilityState`:

```text
Unsupported → Absent → Present → Unbound → Initializing → Ready
                                      ↘ Degraded / Simulated / Error
```

The enumeration is not a strictly linear state machine, but it avoids suppressing "unsupported, missing hardware, not yet bound, initializing, degraded, simulated and failed" to `false`.

`CapabilityKind` covers LoRa, GPS, BLE, HostLink, Storage, Display, Input, Battery, Network, Audio and MapStorage.

## Link mode explains where the capability is located

- Radio:`LocalRadio / PacketProxy / CommandProxy`
- GPS:`LocalUart / RawStreamProxy / FixProxy / CommandProxy / Simulated`

This allows "target with GPS" to continue to drill down to "local UART, original stream agent or fix agent", preventing the master and companion from claiming owner at the same time.

## Authority is data ownership

`AuthorityBinding` binds `Identity / PeerKeyStore / NodeStore / MessageStore / Location / Time / Config / DeviceStatus / UiState` to `HostKind owner`. Capability indicates whether capabilities can be provided, and Authority indicates who is responsible for the facts. The two cannot be combined into the same field.

## Parts that need to be verified

- Manifest is a view, and whether specific target declarations completely cover all build targets requires a separate matrix check.
- Authority binding conflict is currently verified by who, and product composition needs to be tracked.
- `CapabilityStatus` only contains state and endpoint host, without reason code; the diagnostic capability of the UI may be insufficient.

## Drilldown and evidence

- [Target → Capability → Authority resolution](capability-resolution.md)
- `modules/core_device/include/device/target_manifest_types.h`
- `modules/core_device/include/device/capability_types.h`
- `modules/core_device/include/device/authority_types.h`
