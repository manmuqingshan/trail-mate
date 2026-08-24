# Node Action Protocol Specification

This specification defines the protocol-related action boundaries in the node list operation menu, focusing on constraining the Trace Route and Exchange Position behaviors on the nRF mono UI.
 Its goal is to avoid
mistakenly projecting Meshtastic's app port semantics to MeshCore, or mischaracterizing local sending into the queue as
remote action success.

## Source Baseline

- Meshtastic official code:
  - `.tmp/firmware/src/modules/TraceRouteModule.cpp`
  - `.tmp/firmware/src/mesh/PhoneAPI.cpp`
  - `.tmp/firmware/src/modules/PositionModule.cpp`
  - `.tmp/firmware/src/mesh/MeshModule.cpp`
- MeshCore official code:
  - `.tmp/MeshCore/src/Packet.h`
  - `.tmp/MeshCore/src/Mesh.cpp`
  - `.tmp/MeshCore/examples/companion_radio/MyMesh.cpp`
-Trail Mate current adaptation code:
  - `modules/ui_mono/src/runtime.cpp`
  - `platform/nrf52/arduino_common/src/chat/infra/meshtastic/meshtastic_radio_adapter.cpp`
  - `platform/nrf52/arduino_common/src/chat/infra/meshcore/meshcore_radio_adapter.cpp`
  - `platform/esp/arduino_common/src/chat/infra/meshcore/meshcore_adapter.cpp`

## Distinctions

### Local Send Admission

The success of local sending into the queue or handing over to the radio adapter only means that the request is accepted by the local machine.
It cannot indicate that the remote end has received, the remote end has responded, TraceRoute has been completed, or Position
has been successfully exchanged.

UI copy must use wait semantics, such as `WAIT REPLY`. It is only displayed when the local transmission fails
`SEND FAILED`.

### Remote Response

The remote response must consist of subsequent received protocol messages, ACK/NAK, routing error, timeout or node
Position updates to confirm. `SUCCESS` shall not be displayed without such subsequent facts.

### Protocol Ownership

Meshtastic node actions can only use Meshtastic portnum and Meshtastic wire semantics.
MeshCore node actions can only use MeshCore payload type / command semantics.

It is illegal to send app-data across protocols, even if the field names look similar.

## Meshtastic Trace Route

Meshtastic Trace Route requests must satisfy:

 - Target a non-broadcast, non-native Meshtastic node.
- `decoded.portnum = meshtastic_PortNum_TRACEROUTE_APP`.
- The payload is `meshtastic_RouteDiscovery`, and the content can be empty encoded.
- `decoded.want_response = true`.
- `want_ack = true` must be set for non-broadcast targets, consistent with the official
 `TraceRouteModule::startTraceRoute()` and `PhoneAPI` reliable delivery upgrades.

The UI copy after successful local sending is `WAIT REPLY`. The final result should be driven by a subsequent
`TRACEROUTE_APP` response, routing error, or timeout.

## Meshtastic Exchange Position

Meshtastic Exchange Position request must meet:

- Target is a Meshtastic node.
- `decoded.portnum = meshtastic_PortNum_POSITION_APP`.
- The request can have an empty payload.
- `decoded.want_response = true`.

The UI copy after successful local sending is `WAIT REPLY`. The remote end may not reply due to lack of new GPS, policy restrictions,
or the official 3-minute throttling of `PositionModule::allocReply()`. Final success should be confirmed by subsequent
Position messages updating the node position.

## MeshCore Trace And Position

The official MeshCore protocol does have `PAYLOAD_TYPE_TRACE`, but it is not Meshtastic
`TRACEROUTE_APP`. Official `Mesh::createTrace()` creates trace payload,
`Mesh::sendDirect()` appends the target path to the end of the payload, `onTraceRecv()` returns
path hash and SNR data.

Trail Mate's current MeshCore adapter does not yet provide an equivalent Trace Route operation interface;
`sendAppData()` will not carry MeshCore's native trace semantics. Therefore mono UI must not display or execute `TRACE ROUTE` in the
MeshCore node menu.

MeshCore position request is the same: currently Trail Mate does not implement a MeshCore position exchange action equivalent to Meshtastic
`POSITION_APP + want_response`. MeshCore adapter
Currently also does not handle app-data's `want_response`. Therefore mono UI must not display or execute `EXCHANGE POSITION` in the MeshCore
 node menu.

In the MeshCore node menu, the legal action related to the current position is to open
`OPEN COMPASS` based on the persisted node position. It is not a location exchange request.

## Current Mono UI Rule

Meshtastic mode:

- `DETAIL`
- `REPLY`
- `ADD CONTACT`
- `IGNORE NODE` / `UNIGNORE NODE`
- `TRACE ROUTE`
- `EXCHANGE POSITION`
- `OPEN COMPASS`

MeshCore mode:

- `DETAIL`
- `REPLY`
- `ADD CONTACT`
- `IGNORE NODE` / `UNIGNORE NODE`
- `OPEN COMPASS`

If you implement MeshCore native trace or native location request in the future, you must first provide a clear
 interface at the adapter layer and model it according to MeshCore official `PAYLOAD_TYPE_TRACE` or corresponding location/telemetry semantics.
 Meshtastic portnum cannot be reused.
