# Meshtastic Protocol Policy Specification

This specification defines `modules/core_chat/include/chat/runtime/meshtastic_protocol_policy.h`
boundaries of responsibilities. Its purpose is narrow: to centralize Meshtastic protocol business rules that can be judged from pure inputs.
In the shared owner, prevent ESP32, nRF52, Linux or BLE bridge from forking to implement the same rule.

## Position

`meshtastic_protocol_policy.h` is Meshtastic's shared policy table / pure decision
owner. It only has the rules of "how to make decisions after given the facts", and does not have platform execution, protobuf encoding and decoding,
BLE SDK lifecycle, radio IO, persistence or UI rendering.

A rule should be placed in this file if and only if it satisfies these conditions:

- The rule belongs to the Meshtastic protocol or Meshtastic app-facing behavior;
- The rule can be expressed as an input to output without side effects;
- ESP32, nRF52, Linux Or the same result should be obtained when the test is run;
- Platform differences only affect whether the result can be executed, and do not change the rule itself.

Platform code can have execution actions, such as restarting BLE, writing NVS, sending LoRa frame, and updating advertising.
But platform code must not re-determine Meshtastic business conditions.

## Owned Rules

Currently this file has the following sharing rules:

| Rule | Public API | Ownership reason |
| --- | --- | --- |
| App-data send intent | `resolveMeshtasticAppDataSendPolicy(...)` | `dest == 0`, broadcast, `want_ack`, `want_response` are Meshtastic wire/app intents, not platform policies. |
| BLE visible-name change policy | `resolveMeshtasticBleVisibleNamePolicy(...)` | Meshtastic BLE visible name is determined by Meshtastic node id; owner long/short name change cannot allow a platform to decide to disconnect BLE on its own. |
| MQTT downlink accept/relay | `resolveMeshtasticMqttDownlinkPolicy(...)` | gateway echo/self/local/tx-disabled/relay Decisions must be consistent across platforms. |
| NodeInfo reannounce gate | `resolveMeshtasticNodeInfoReannouncePolicy(...)` | Whether to resend self NodeInfo after peer NodeInfo is a protocol behavior and is subject to MQTT/self/invalid/suppress gate. |
| NodeInfo reply gate | `resolveMeshtasticNodeInfoReplyPolicy(...)` | `want_response`, addressing, suppression are Meshtastic request/reply policies. |
| Position reply gate | `resolveMeshtasticPositionReplyPolicy(...)` | Position request/reply suppression is a protocol behavior and does not belong to board-level GPS or radio IO. |
| TraceRoute reply gate | `resolveMeshtasticTraceRouteReplyPolicy(...)` | request/response, broadcast in-flight, addressing gate are Meshtastic TraceRoute semantics. |

## Non-Owned Rules

These contents must not be stuffed into `meshtastic_protocol_policy.h`:

- BLE service start/stop, GATT characteristic, CCCD, Bluefruit/NimBLE API calls;
- nRF52/ESP32 whether to call `setEnabled(false)` or restart advertising execution details;
- packet wire encode/decode, belongs to `chat/infra/meshtastic`;
- self NodeInfo payload structure, belongs to `MeshtasticSelfAnnouncementCore`;
- Position payload structure and position availability, belongs to `MeshtasticPositionCore`;
- TraceRoute/Position action lifecycle tracking, belongs to `MeshtasticAppActionRuntime`;
- MeshCore rules must enter MeshCore runtime/policy;
- Whether the UI displays actions must be determined by capability and UI/action specs.

If a behavior requires both shared decision-making and platform execution, it must be split into two layers:

1. Return explicit policy / reason in `meshtastic_protocol_policy.h`.
2. Only consume the policy results and execute platform actions in the platform adapter/runtime.

## BLE Visible Name Rule

The protocol side fact of Meshtastic BLE visible name is:

```text
Meshtastic_<compact node id>
```

Thus:

- owner `long_name` changes does not change the Meshtastic BLE visible name;
- Change of owner `short_name` does not change the visible name of Meshtastic BLE;
- Change of node id will change the visible name of Meshtastic BLE;
- The platform shall not restart BLE on its own due to the change of long/short name in `set_owner` / `applyUserInfo`.

The platform execution layer can refresh the BLE service due to Meshtastic visible name changes only when `resolveMeshtasticBleVisibleNamePolicy(...)` returns
`visible_name_changed == true`.

`chat/runtime/self_identity_policy.h` can be responsible for cross-protocol identity projection and string formatting,
But whether Meshtastic needs to refresh the BLE visible name rule must be determined by this policy file.

## Admission Checklist

When adding or modifying Meshtastic behavior, first decide in the following order:

1. Is this a Meshtastic protocol/app-facing behavior or platform IO?
2. If it is a protocol/app-facing behavior, can it be expressed as a pure policy?
3. If possible, first put the rules into or extend `meshtastic_protocol_policy.h`.
4. Add shared tests to policy and put them first
   `modules/core_chat/tests/test_meshtastic_protocol_policy.cpp`.
5. Let ESP32, nRF52, Linux or BLE bridge only consume the policy results without copying the judgment conditions.
6. If behavior changes cross-platform parity, update
 `docs/specification/PROTOCOL_ADAPTER_PARITY_SPEC.md` or drift audit.

When there is no suitable owner, a new shared runtime/policy owner can be created; business rules must not be written into the platform branch just because the current fix occurs in the
nRF52 or ESP32 log.

## Review Rule

Any PR if in `platform/esp`, `platform/nrf52`, BLE bridge, board runtime or app facade
New Meshtastic judgment conditions, review must first ask:

```text
Why is this rule not in meshtastic_protocol_policy.h?
```

Platform-side changes are only true if the answer is "This is a platform execution, resource constraint, or SDK binding, not a Meshtastic business rule."
Platform-side changes are true.
