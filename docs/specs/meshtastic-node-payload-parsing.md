# Meshtastic Node Payload Parsing Specification

Status: baseline
Updated: 2026-06-28

This document defines where Meshtastic node-fact payload semantics live in
Trail Mate.

## Confusion

`NODEINFO_APP` is not just a UI contact update.

`NODEINFO_APP` is not just a legacy `User` protobuf.

`MAP_REPORT_APP` is not a random MQTT payload.

`POSITION_APP` is not a platform radio driver detail.

Linux, ESP Arduino, ESP-IDF, and nRF radio adapters must not each maintain their
own interpretation of NodeInfo, User, MapReport, Position, device metrics, MQTT
origin, or public-key facts.

## Boundary

Meshtastic node-fact parsing is shared protocol semantics and belongs in
`modules/core_chat`.

The shared parser owns:

- full `meshtastic_NodeInfo`
- legacy `meshtastic_User`
- MQTT map `meshtastic_MapReport`
- embedded `meshtastic_Position` inside `NodeInfo`
- standalone `POSITION_APP`
- `via_mqtt`
- device metrics
- ignored and key-verification flags
- public-key state presence and key bytes

Platform adapters own only transport context and projection:

- sender node id fallback
- channel index or channel hash
- RSSI and SNR
- hop count
- receive timestamp
- duplicate suppression or retransmit policy
- publishing events or updating `ContactService`
- platform-specific persistence of keys or diagnostics

## Required Entry Points

All raw Meshtastic receive paths must use:

- `chat::meshtastic::isNodeMetadataPayload(...)`
- `chat::meshtastic::decodeNodeMetadataPayload(...)`
- `chat::meshtastic::decodePositionPayload(...)`

`decodeNodeInfoPayload(...)` remains a compatibility entry point for tests and
code that explicitly has a `NODEINFO_APP` payload. Platform receive paths must
prefer `decodeNodeMetadataPayload(...)` so `NODEINFO_APP` and `MAP_REPORT_APP`
stay aligned.

Current implementation file:

- `modules/core_chat/src/infra/meshtastic/mt_node_payload.cpp`

## Invalid Implementations

The following are invalid in platform receive paths:

- direct `pb_decode(... meshtastic_NodeInfo_fields ...)`
- direct `pb_decode(... meshtastic_User_fields ...)`
- direct `pb_decode(... meshtastic_MapReport_fields ...)`
- direct `pb_decode(... meshtastic_Position_fields ...)`
- separate per-platform mapping from protobuf fields into contact facts
- parsing only legacy `User` on one platform while another platform parses full
  `NodeInfo`
- treating `MAP_REPORT_APP` as an opaque app packet without updating node
  identity facts

## MQTT MapReport Rule

Official Meshtastic MQTT map reports carry public node identity facts:
`long_name`, `short_name`, role, hardware model, and optional opted-in position.
Trail Mate must decode those facts through `decodeNodeMetadataPayload(...)` and
apply them to the same `NodeUpdate` / `NodeInfoUpdateEvent` path used by
`NODEINFO_APP`.

`MAP_REPORT_APP` is metadata-only for MQTT downlink. It may be accepted while
normal decoded downlink payloads are rejected by encrypted MQTT settings, but it
must not be transmitted into the LoRa mesh as a regular downlink packet.

Because `MapReport` has no public-key or local-trust field, the decoded result
must not clear a previously known public-key state or manual verification state.
`DecodedNodePayload::has_public_key_state`,
`NodeInfoUpdateEvent::has_public_key_state`, and
`NodeInfoUpdateEvent::has_key_manually_verified_state` distinguish "this event
says the node has/does not have a key/trust state" from "this event has no
opinion about keys/trust".

These operations are valid outside platform receive semantics when they are
constructing local outbound payloads, serving BLE phone API data, or testing the
shared parser.

## Acceptance Check

A platform adapter is aligned with this specification when its receive path
passes `meshtastic_Data` plus transport context into the shared parser and only
projects the decoded result into its local event/store mechanism.
