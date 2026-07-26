# Protocol-Partitioned Storage V2 Overview Design

## Context

The previous ESP runtime maintained overlapping facts in chat files,
`/nodes.bin`, `/contacts.dat`, and `/mesh/peers.bin`. A single remote node could
be represented by multiple records with different update semantics. Whole-file
projection writes also allowed an index failure to prevent a durable inbound
message from reaching the UI.

Storage v2 replaces that shape with two owners:

```text
ChatService / MessageLedger
  -> SdStore
       -> MT chat partition
       -> MC chat partition
       -> RT chat partition

Protocol adapters + ContactService
  -> IProtocolPeerRepository
       -> SdProtocolPeerRepository
            -> MT peer/contact partition
            -> MC peer/contact partition
            -> RT peer/contact partition
```

## Architectural Pattern

The design combines four patterns.

**Repository:** `SdProtocolPeerRepository` owns the aggregate of stable peer
identity, protocol facts, observations, and user contact facts.

**Ports and views:** Protocol adapters use `IMeshPeerDirectory`; application
contact logic uses `INodeStore` and `IContactStore`. Node/contact views do not
own storage and cannot diverge from the repository.

**Append-only event journal:** Messages, delivery status, read watermarks,
peer changes, and contact changes append immutable slots. Snapshots are compact
projections, not authoritative mutable blobs.

**CQRS projection:** UI queries read catalog/contact/node projections. Incoming
message and peer writes do not wait for expensive UI-oriented reconstruction.

**Two-level concurrency ownership:** Store/repository recursive mutexes protect
aggregate state and sequence invariants. They never represent device I/O
ownership. The storage service owns file transactions and the repository must
not hold a device transaction around a sequence of storage calls. The physical
shared-SPI mechanism is defined only in `docs/spi_bus_architecture.md`.

## High-Level Commit Flows

### Incoming Message

```text
protocol RX
  -> decode ChatMessage
  -> MessageLedger
  -> SdStore.appendIncomingDurably
       -> message slot
       -> RT seen identity when required
       -> update in-memory catalog projection
       -> attempt catalog delta; mark dirty on contention/failure
  -> observer publication
       -> model
       -> event bus
       -> notification
  -> background catalog projection retry/rebuild when dirty
```

### Peer Observation

```text
protocol announce/node info/key observation
  -> IMeshPeerDirectory.record
  -> merge by protocol stable identity
  -> preserve verified key and contact facts
  -> non-blocking peer delta append
       -> success
       -> or ordered PSRAM pending queue
```

### Contact Edit

```text
user contact intent
  -> ContactService
  -> IContactStore view
  -> resolve active-protocol stable identity
  -> durable contact delta
  -> update repository aggregate
  -> invalidate contact projection
```

## Why Protocol Partitions

Physical partitioning removes fields that are meaningless to a protocol:

- MT does not carry RT destination/identity/LXMF hashes;
- MC does not carry MT channel-key state;
- RT does not carry Meshtastic role/hardware/MAC fields.

The business layer remains shared. This preserves one delivery state machine,
one read-state meaning, one retry policy, and one UI presentation model.

## Capacity And Cost

Approximate fixed slot sizes are:

| Data | MT | MC | RT |
|---|---:|---:|---:|
| Message | 279 B | 217 B | 365 B |
| Peer | 217 B | 225 B | 264 B |
| Contact | 29 B | 61 B | 41 B |

The previous peer union was roughly 488 B and coexisted with a separate node
record of roughly 180 B. A normal MT peer therefore drops from two overlapping
records totaling about 668 B to one protocol peer slot of about 217 B, plus a
29 B contact slot only when the user owns contact facts.

At fixed-slot payload level, one MiB stores approximately:

- 3758 MT messages;
- 4832 MC messages;
- 2872 RT messages;
- 4832 MT peers, 4660 MC peers, or 3971 RT peers.

Filesystem, journal headers, segments, status deltas, and catalog projections
reduce those theoretical counts. There is no 200-node physical store limit and
no fixed message-count cap; practical capacity is governed by SD space and the
explicit runtime retention policy.

The runtime retains up to 2048 unprotected nearby peers per protocol. Contacts
and conversation-referenced peers are outside that eviction budget. Up to 4096
contact/user-fact projections are accepted in total; existing contacts are
never evicted to admit strangers.

## Evolution Space

The partition boundary allows protocol evolution without widening every
record:

- MT can add a new slot schema carrying MT-only telemetry or key state;
- MC can change route/peer-hash facts without touching MT or RT files;
- RT can add propagation-node, ratchet, or resource metadata only to RT;
- a new protocol can add a codec and partition while reusing MessageLedger,
  ChatService, delivery projection, and UI contracts;
- catalogs can gain secondary indexes without changing message durability;
- old message segments can be archived or pruned by segment, not per-message
  file churn;
- background compaction can move to a dedicated storage worker without
  changing repository semantics;
- active-protocol paging can replace the current transient node vector without
  changing persisted facts.

## Product Result

Protocol switching now changes the complete Chat/Contacts/Nearby projection,
not just a UI filter. A peer has one canonical repository record, contact facts
cannot be overwritten by node refreshes, and projection I/O can no longer hide
an already durable MQTT/LoRa/Reticulum message.
