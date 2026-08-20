# Protocol Adapter Parity Specification

This specification governs the consistent behavior of the same protocol in different platform adapters. It's not a UI spec, nor is it
Board level hardware capability table. What it defines is: when Trail Mate claims that a platform implements Meshtastic or
MeshCore adapter, which protocol semantics must be uniformly owned by the shared core, and which platform differences must
Explicitly exposed as capabilities, which behaviors disable silent downgrades.

The core position of this specification is that there can only be one implementation of a protocol business rule. ESP32 and nRF adapter should not each
Maintain a set of NodeInfo/Position/TraceRoute/ACK/PKI state machines. Platform adapters can only have
radio IO、SDK binding、storage binding、clock/random、queue scheduling、buffer
placement and other platform issues.

Meshtastic Android App's BLE connection, `ToRadio`/`FromRadio`/`FromNum` drain,
config snapshot, Admin response-drain-before-save, and `Nodes(0)`/`Module config received`
Diagnostic boundaries, see `MESHTASTIC_ANDROID_BLE_CONNECTION_SPEC.md`. This specification is the priority interpretation baseline for mobile phone BLE
connection issues.

See the shared owner of Meshtastic pure business rules
`MESHTASTIC_PROTOCOL_POLICY_SPEC.md`. When a certain Meshtastic judgment can be expressed as having no side effects
policy, the platform adapter/runtime must consume this shared policy instead of replicating conditions in the ESP32/nRF52 branch.

## Core Distinctions

### Protocol Behavior

Protocol behavior is a protocol layer fact, such as:

- Meshtastic `NODEINFO_APP`'s `want_response` reply;
- Meshtastic self NodeInfo reannounce after receiving peer NodeInfo;
- Meshtastic `TRACEROUTE_APP`'s `want_response` and unicast `want_ack`;
- MeshCore `PAYLOAD_TYPE_TRACE` and direct path/SNR collection;
- MeshCore NodeInfo query/info control frame.

The protocol behavior of the same protocol must be consistent across platforms by default.

Protocol behavior belongs to shared protocol core, not `platform/esp` or `platform/nrf52`
adapter. If a rule needs to be copied in two platform files, it is regarded as architecture drift by default.

### Platform Capability

Platform capability is whether an adapter can currently provide a certain behavior safely. Resource, hardware or engineering phase
Restrictions can cause capabilities to differ, but must not fool the caller into thinking that the behavior already exists.

If a platform lacks a certain protocol behavior, at least one condition must be met:

- `MeshCapabilities` explicitly does not declare the capability;
- The dedicated capability field clearly identifies the absence;
- The calling interface returns failure, and the upper-layer UI/use case does not interpret the failure as protocol success.

Capability can only describe whether the capabilities exposed by the shared core are available on a certain platform, and cannot wrap a set of platform-private
 protocol rules into "capability differences".

### Adapter Boundary

Platform adapter may own:

- radio driver calls and IRQ/RX/TX lifecycle;
- SDK-specific BLE, serial, preferences, filesystem, and timer binding;
- memory placement decisions such as scratch buffers and queue sizes;
- board-specific TX gating and power/runtime hooks;
- conversion between shared core commands and physical packet IO.

Platform adapter must not own:

- when to answer a NodeInfo request;
- when to reannounce self NodeInfo after peer NodeInfo;
- how Meshtastic `want_response` is interpreted;
- how TraceRoute request/reply state is built;
- how Position request/reply throttling works;
- MeshCore NodeInfo query/info semantics;
- MeshCore trace path semantics;
- PKI resync business rules.

These belong in shared protocol core with platform adapters supplying IO ports.

### Product UX

Product UX can only consume protocol behavior or capability. UX must not guess protocol
semantics based on the platform name, nor map Meshtastic portnum to MeshCore payload type.

## Required Parity Rules

### R1 Single Shared Protocol Core

Meshtastic and MeshCore business/protocol rules must live in shared code under
`modules/core_chat`, `modules/core_mesh`, or another explicit shared protocol module.

ESP32 and nRF adapter files must not independently implement the same protocol state machine.
If equivalent logic appears in both platform adapters, it is a defect unless a spec names it as
platform IO glue.

### R2 Same Protocol, Same Semantics

ESP32 Meshtastic adapter and nRF Meshtastic adapter must call the same Meshtastic core semantics.
If ESP32 MeshCore adapter and nRF MeshCore adapter both declare a certain MeshCore capability,
 they must also call the same MeshCore core semantics.

### R3 Capability Before UI

Any UI menu, automatic reply, diagnostic status or app-facing action must be defined by a capability or protocol
Mode authorization. Just because a platform adapter happens to accept a certain function call, the corresponding action cannot be displayed.

### R4 No Silent Downgrade

Interface parameters must not be silently discarded unless explicitly allowed by the specification. For example:

- `want_response` must enter the wire/data layer when supported by the protocol;
- If the adapter does not support `want_response`, it must be exposed through capability or return value;
- Request-style empty payload must be processed according to the protocol semantics and cannot be accidentally killed by ordinary payload verification.

### R5 Broadcast Semantics Are Protocol Semantics

Whether Broadcast allows `want_response` is a matter of protocol semantics, not a platform policy. Meshtastic Official Position
broadcast can carry request-replies semantics. Therefore adapter cannot be cleared when there is no specification basis
broadcast `want_response`.

### R6 Drift Requires A Ledger Entry

Any of the following changes must update the drift ledger or parity matrix:

- Add or modify `IMeshAdapter` behavior;
- Modify NodeInfo / Position / TraceRoute / ACK / of either ESP32 or nRF protocol adapter
PKI/discovery/app-data semantics;
- Added protocol-related actions in the UI;
- Change `MeshCapabilities`;
- Mark a protocol behavior as not supported yet.

## Meshtastic Required Behavior Matrix

| Behavior | Required cross-platform rule |
| --- | --- |
| App-data empty payload | Request-style ports such as `POSITION_APP` request and `TRACEROUTE_APP` must allow `len == 0` when payload is null or protobuf-empty. |
| `want_ack` | Unicast `want_ack` must set Meshtastic air ACK. Broadcast must not request air ACK. |
| `want_response` | Must be preserved for request-style packets, including supported broadcast cases. |
| NodeInfo request | `requestNodeInfo(dest, want_response)` must send `NODEINFO_APP`; unicast `want_response` requests a reply. |
| NodeInfo reply | Incoming `NODEINFO_APP` with `want_response` addressed to us or broadcast must send our NodeInfo reply, subject to 12h reply suppression. |
| NodeInfo peer reannounce | After decoding a valid peer NodeInfo, adapter should broadcast our NodeInfo once, subject to 60s reannounce suppression, and must skip MQTT/self/invalid node sources. |
| Position response | Incoming `POSITION_APP` with `want_response` addressed to us or broadcast should send own position if available, subject to 3m reply suppression. |
| TraceRoute request | Outgoing unicast TraceRoute must send `TRACEROUTE_APP`, set `want_response=true`, and set `want_ack=true`. |
| TraceRoute response | Incoming `TRACEROUTE_APP` request with `want_response` must send RouteDiscovery response when addressed to us or broadcast. |
| TraceRoute result | UI/app-facing status must not treat local enqueue as success. A TraceRoute action is delivered on `ROUTING_APP` ACK, failed on `ROUTING_APP` error, completed on matching `TRACEROUTE_APP` response, and timed out if no final result arrives. |
| Position exchange result | Position replies sent as responses to `POSITION_APP` `want_response` requests must preserve the original request packet id in `Data.request_id`; UI/app-facing status may complete only on a matching Position response or fail on timeout/routing error. |
| Routing errors | `NO_RESPONSE`, PKI, and channel errors must not be reported as local send success. |
| PKI resync | PKI unknown/missing-key paths should request or send NodeInfo consistently. |

Implementation ownership:

- packet encode/decode helpers already live in shared `chat/infra/meshtastic`;
- self NodeInfo packet building already lives in `MeshtasticSelfAnnouncementCore`;
- app-data destination/ACK/response intent, Meshtastic BLE visible-name change policy,
  NodeInfo peer-reannounce gating, NodeInfo/Position request-reply suppression gating,
  and TraceRoute reply gating live in `chat/runtime/meshtastic_protocol_policy.h`;
- TraceRoute payload mutation lives in shared `chat/infra/meshtastic/mt_protocol_helpers`;
- TraceRoute and Position Exchange UI/app action lifecycle tracking live in
  `chat/runtime/meshtastic_app_action_runtime.h`;
- Position availability and payload construction live in `MeshtasticPositionCore`;
- remaining platform GPS source selection and low-level send/channel mechanics must stay explicit
  and be extracted only when a shared runtime decision can own them without taking platform IO.

## MeshCore Required Behavior Matrix

MeshCore is not Meshtastic with different port numbers. MeshCore behavior must be modeled with MeshCore
payload types, control frames, direct paths, and identity/key rules.

| Behavior | Rule |
| --- | --- |
| NodeInfo | If `supports_node_info=true`, adapter must implement MeshCore NodeInfo query/info control frame semantics, including request/reply intent. |
| Discovery | If `supports_discovery_actions=true`, actions must map to MeshCore discover/advert/control behavior, not generic app-data. |
| App-data ACK | If `supports_appdata_ack=true`, `want_ack` must create/track MeshCore ACK semantics. |
| App-data `want_response` | If no MeshCore app-level response semantic exists for a port, adapter must not claim that it supports the response. |
| Trace | MeshCore trace must use `PAYLOAD_TYPE_TRACE` and direct path/SNR semantics. Meshtastic `TRACEROUTE_APP` is forbidden. |
| Position exchange | No MeshCore position exchange action may be exposed until a MeshCore-native telemetry/location request-response is specified and implemented. |
| PKI/identity | Identity keys, group/channel keys, and direct peer secrets are MeshCore-specific and must not reuse Meshtastic PKI assumptions. |

Implementation ownership:

- MeshCore payload helpers and protocol strategy already exist in shared modules;
- NodeInfo query/reply, discover request/response decisions, trace, ACK tracking, route selection policy,
  and identity/key policy must live in shared MeshCore runtime core instead of parallel ESP32/nRF adapter
  copies. Platform adapters may still own radio scheduling, route-cache persistence, and hardware identity
  storage while they execute runtime effects.

## Capability Surface Requirements

`MeshCapabilities` keeps the coarse legacy flags for existing UI code, but protocol-sensitive callers should
prefer these fine-grained flags over generic app-data support:

- `supports_node_info_query`;
- `supports_node_info_reply`;
- `supports_node_info_reannounce`;
- `supports_position_request`;
- `supports_position_reply`;
- `supports_trace_route_request`;
- `supports_trace_route_reply`;
- `supports_protocol_app_response`;
- `supports_protocol_ack_tracking`;
- `supports_meshcore_direct_route_table`;
- `supports_meshcore_identity_keys`;
- `supports_meshcore_peer_secret_derivation`;
- `supports_meshcore_rich_trace_projection`.

When a fine-grained flag is false, UI must be conservative and protocol-specific specs, such as
`NODE_ACTION_PROTOCOL_SPEC.md`, remain authoritative.

## Extraction Requirement

When drift is found between ESP32 and nRF implementations, the preferred fix order is:

1. Extract the protocol rule into shared core.
2. Add a shared unit test for the rule.
3. Make ESP32 and nRF adapters call the shared core.
4. Only then adjust platform-specific IO, queues, or storage.

Directly copying logic from one platform adapter into the other is allowed only as a temporary stopgap
when hardware validation is urgent. The drift audit must then record the copied behavior as technical debt
with an extraction target.

## Review Checklist

Before merging protocol adapter changes:

1. Identify the protocol behavior being changed.
2. Identify the shared protocol core owner.
3. If no shared owner exists, create or extend one before adding more platform adapter logic.
4. Compare ESP32 and nRF adapters only as platform IO consumers of that shared core.
5. Decide whether any remaining difference is a real capability difference.
6. Update this spec or the drift audit if behavior is not equal.
7. Add or update tests where the behavior is reachable without hardware.
8. Verify UI does not expose unavailable protocol actions.
9. Run GitNexus impact analysis for edited symbols and `gitnexus detect-changes` before commit.

## Relationship To Other Specs

- `PROTOCOL_RUNTIME_DESIGN_SPEC.md` defines the Strategy / Command / State / Bridge /
  Adapter design used to enforce shared protocol ownership.
- `MESHTASTIC_PROTOCOL_POLICY_SPEC.md` defines the specific ownership contract for
  `chat/runtime/meshtastic_protocol_policy.h`.
- `NODE_ACTION_PROTOCOL_SPEC.md` defines user-facing node action legality for TraceRoute,
  Exchange Position, and Compass.
- `NRF52_NODE_ID_AND_CHANNEL_KEY_SPEC.md` defines stable nRF identity and key terminology.
- This spec defines cross-platform protocol adapter parity and capability drift policy.
