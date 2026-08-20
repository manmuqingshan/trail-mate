# Leader / Member pairing message exchange

```mermaid
sequenceDiagram
  participant Leader as Leader Coordinator
  participant LTransport as Leader Transport
  participant MTransport as Member Transport
  participant Member as Member Coordinator
  Leader->>Leader: startLeader(team_id, key_id, psk, leader_id, name)
  Leader->>LTransport: send Beacon
  LTransport-->>MTransport: beacon bytes
  MTransport->>Member: handleBeacon
  Member->>MTransport: send Join(member_id, nonce)
  MTransport-->>LTransport: join bytes
  LTransport->>Leader: handleJoin
  Leader->>LTransport: send Key(team credentials)
  LTransport-->>MTransport: key bytes
  MTransport->>Member: handleKey
  Member->>Member: state = Completed
```

This is an interaction diagram reconstructed from the coordinator method and handler name. Message fields, retries and timeouts must continue to be based on `team_pairing_coordinator.cpp`.
