# Sequence: Team payload to map/chat
```mermaid
sequenceDiagram
 actor U as user/TrackSampler
  participant Team as TeamService
  participant Codec as Team Codec + Crypto
  participant Mesh as Active Transport
  participant Remote as Remote TeamService
  participant Bus as Team Event Sink
  participant UI as Map / Chat
  U->>Team: sendPosition/Waypoint/Track/Chat
  Team->>Codec: encode + authenticate
  Codec->>Mesh: app-data payload
  Mesh->>Remote: received payload + Rx context
  Remote->>Codec: verify + decode
  Codec-->>Remote: typed Team message
  Remote->>Bus: TeamPosition/Waypoint/Track/Chat event
  Bus-->>UI: committed projection update
```

## Scenario and actor

TeamService receives business commands and verifies team status; Codec/Crypto owns Team envelope; Active Transport is only responsible for wire transmission; Remote TeamService verifies receiving context; Event Sink Submit by type; Map/Chat consume projection.

## Sending sequence

The business payload first checks the size, identity/revision and key usage, and then encodes and authenticates it; only the complete envelope is handed over to the transport. Transport success only means that the result is sent locally and cannot be faked to be seen by all remote members.

## Receive submissions

Remote uses Rx context, TeamId, key version and payload identity verification. After the typed message is formed, it is handed to the corresponding Event Sink; the UI will be updated only if the sink is submitted successfully. Partial track or invalid waypoint does not publish half-finished events.

## Deduplication and freshness

Positions are merged by member + revision/time, chats are deduplicated by message identity, and waypoints and trajectories are merged by object revision. Old locations can be saved in history but new map locations cannot be overwritten. Repeating envelopes allows retransmission of transport ACKs, but does not repeat business events.

## test

 Covers incorrect teams/keys, out-of-order locations, duplicate chats, segmented track missing pieces, Event Sink Deferred and transport switching.
