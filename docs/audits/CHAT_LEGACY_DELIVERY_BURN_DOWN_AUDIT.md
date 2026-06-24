# Chat Legacy Delivery Burn-down Audit

## Scope

Phase 9.4 burned down the UI-level Chat delivery legacy bridge pair:

- `LegacyChatDeliveryActionBridge`
- `LegacyChatDeliveryEventBridge`

The follow-up burn-down removes the remaining event-side compatibility alias
and the core chat compatibility mapper files:

- `chat/delivery/legacy_chat_delivery_bridge.*`
- `chat/delivery/legacy_chat_send_result_mapper.*`

Their responsibilities are now owned by `ChatDeliveryMessageProjection` and
`ChatDeliverySendResultProjection`.

## Distinction

| Surface | Meaning | Phase 9.4 decision |
| --- | --- | --- |
| Chat delivery action port | Runtime/UI command port for retry, cancel, and clear-failure actions | formalized as `ui_chat_runtime::IChatDeliveryActionPort` |
| Chat delivery event port | Runtime/UI event sink for projected delivery events | formalized as `ui_chat_runtime::IChatDeliveryEventPort` |
| Chat delivery action adapter | Compatibility mapping from UI `MessageRef` to delivery action request | owned by `ui_chat_runtime::ChatDeliveryActionPortAdapter` |
| Chat delivery event projection adapter | Compatibility mapping from send-result/ACK events to delivery projection | owned by `ui_chat_runtime::ChatDeliveryEventProjectionAdapter` |
| Legacy UI bridge headers | Historical compatibility surface | action and event bridge headers are removed from the active build include surface |

## LegacyChatDeliveryActionBridge

| Field | Value |
| --- | --- |
| Current compatibility header | removed from active build include surface |
| Former implementation source | `modules/ui_legacy_adapters/src/legacy_chat_delivery_action_bridge.cpp` |
| Current source status | removed; no real implementation remains under `ui_legacy_adapters/src` |
| Current callers | no main runtime callers; documentation references only |
| Replacement | `modules/ui_chat_runtime/include/ui_chat_runtime/chat_delivery_action_port_adapter.h` and `modules/ui_chat_runtime/src/chat_delivery_action_port_adapter.cpp` |
| Port | `modules/ui_chat_runtime/include/ui_chat_runtime/chat_delivery_action_port.h` |
| Migration decision | main runtime code includes `ui_chat_runtime/chat_delivery_action_port_adapter.h`; the old header path is not build-visible |

Caller classification:

| Caller class | Status |
| --- | --- |
| Main runtime caller | migrated to `ChatDeliveryActionPortAdapter` |
| Test caller | `test_chat_delivery_action_port_adapter.cpp` tests the real adapter |
| Forwarding header | removed from `ui_legacy_adapters` and `ui_shared` |
| Audit/doc reference | allowed for burn-down history and removal planning |

## LegacyChatDeliveryEventBridge

| Field | Value |
| --- | --- |
| Current compatibility header | deleted |
| Former implementation source | `modules/ui_legacy_adapters/src/legacy_chat_delivery_event_bridge.cpp` |
| Current source status | removed; no implementation or compatibility alias remains |
| Current callers | none in active code |
| Replacement | `modules/ui_chat_runtime/include/ui_chat_runtime/chat_delivery_event_projection_adapter.h` and `modules/ui_chat_runtime/src/chat_delivery_event_projection_adapter.cpp` |
| Port | `modules/ui_chat_runtime/include/ui_chat_runtime/chat_delivery_event_port.h` |
| Migration decision | main runtime code includes `ui_chat_runtime/chat_delivery_event_projection_adapter.h`; the old header path is not supported |

Caller classification:

| Caller class | Status |
| --- | --- |
| Main runtime caller | migrated to `ChatDeliveryEventProjectionAdapter` |
| Test caller | `test_chat_delivery_event_projection_adapter.cpp` tests the real adapter |
| Forwarding header | removed from `ui_legacy_adapters` and `ui_shared` |
| Audit/doc reference | allowed for burn-down history only |

## Main Runtime Include Audit

The main runtime path now uses stable headers:

- `ui_chat_runtime/chat_delivery_action_port_adapter.h`
- `ui_chat_runtime/chat_delivery_event_projection_adapter.h`

The following direct includes are forbidden:

- `ui_legacy_adapters/legacy_chat_delivery_action_bridge.h`
- `ui/presentation_sources/legacy_chat_delivery_action_bridge.h`

## Removal Condition

The action and event legacy headers are removed from the active build include
surface. Downstream users must migrate to `ui_chat_runtime` headers rather than
reintroducing forwarding aliases.
