# Protocol-Partitioned Storage V2 Detailed Design

## Components

### `FixedSlotJournalEngine`

Owns journal framing and descriptor validation:

```text
magic | schema | protocol | kind | slot_size | descriptor_crc
slot 0
slot 1
...
```

The engine reports `Missing`, `Ready`, `Incompatible`, `PartialTail`, or
`IoError`. It never interprets protocol payloads.

`replaceFileAtomically` and `recoverAtomicFile` own snapshot rename recovery.
Callers must not duplicate remove/rename sequences.

### Protocol Chat Codecs

`protocol_chat_codec` owns MT, MC, and RT message/catalog/read/status/seen slot
layouts. Each slot has its own CRC.

Text capacity is protocol-fixed. It is not represented by a single large union
record. RT-only identity and LXMF hash fields exist only in RT slots.

### `SdStore`

Owns:

- message segments;
- catalog hot projection;
- read-watermark hot projection;
- outgoing status hot projection;
- RT seen hot projection.

Hot vectors use strict PSRAM allocators. The 512-byte codec scratch buffer is a
store member, not a large task-stack local.

### Protocol Peer Codecs

`protocol_peer_codec` owns protocol peer and contact slots.

Peer slot rules:

- common prefix: identity kind, timestamps, display fact, source, radio
  observations, device metrics, position;
- MT suffix: MT node facts, next hop, public key, verification state;
- MC suffix: MC node facts, public key, peer hash, next hop, node-id hint;
- RT suffix: destination/identity hashes, public keys, ratchet, delivery and
  propagation capabilities.

Contact slot rules:

- MT key: node ID;
- MC key: public key plus node-id projection hint;
- RT key: destination hash;
- payload: alias and user flags.

Peer slots intentionally exclude alias and user flags.

### `SdProtocolPeerRepository`

Owns canonical PSRAM vectors:

- `peers_`;
- `contacts_`;
- `pending_peer_deltas_`.

It also owns a temporary active-protocol `NodeEntry` projection required by the
current `INodeStore` API. That vector is a read projection and is never a fact
source.

`NodeStoreView` and `ContactStoreView` forward application operations into the
repository. They have no files and no independent caches.

## Initialization

```text
create SdStore
  -> acquire store aggregate mutex
  -> recover snapshot backups
  -> load protocol projections
  -> reconcile catalog from message segments
  -> rebuild RT seen ledger if required
  -> compact threshold-exceeding projections

create SdProtocolPeerRepository(SdStore)
  -> acquire peer aggregate mutex
  -> create /data/v2/{mt,mc,rt}
  -> recover peer/contact snapshot backups
  -> load peer snapshot + delta
  -> load contact snapshot + delta
  -> reconcile temporary/stable identities
  -> overlay contact facts
  -> compact threshold-exceeding journals

create ContactService(repository.nodeStoreView,
                      repository.contactStoreView)
```

The repository outlives `ContactService`; AppContext stores node/contact as
non-owning pointers and owns the repository in the chat service bundle.

## Message Transactions

### Inbound MT/MC

1. Append the protocol message slot and flush it.
2. Return durable success to `MessageLedger`.
3. Update the in-memory catalog.
4. Attempt catalog delta append.
5. If step 4 fails, mark the catalog dirty but keep durable success.

### Inbound RT

1. Detect an existing LXMF hash from hot seen state or the seen journal.
2. Append the RT message slot.
3. Append the LXMF hash to `seen.delta`.
4. Only after both writes succeed, return durable success.
5. Catalog failure remains a rebuildable projection failure.

If step 2 succeeds and step 3 fails, retry sees the same stored message and
completes the missing seen append without duplicating the message.

## Peer Merge Transaction

For an exact stable identity:

1. Merge non-empty names and fresh observations.
2. Preserve first-seen minimum and last-seen maximum.
3. Preserve contact alias and flags from the contact projection.
4. Preserve a verified key when a conflicting unverified key arrives.
5. Append a complete merged peer delta or queue it in PSRAM.

For temporary-to-stable identity upgrade:

1. Find temporary peer by projected node ID.
2. Merge temporary facts into the stable incoming record.
3. Append/queue the stable record.
4. Append/queue the temporary identity tombstone.
5. Replace the in-memory identity.

Stable-first ordering prefers a recoverable duplicate over loss. Startup
reconciliation merges a duplicate temporary/stable pair if power fails before
the tombstone is durable.

## Contact Transaction

1. Resolve the node ID in the active protocol partition.
2. Require MT node identity, MC public key, or RT destination identity.
3. Build a complete contact projection with alias and flags.
4. Hold the peer aggregate mutex while constructing the complete transaction.
5. Append and flush `contacts.delta`.
6. Apply the projection to memory.
7. Refresh only the affected peer/node projection.

Failure before step 5 leaves memory unchanged and returns failure to the user
intent. A contact edit is never queued invisibly.

## Retention Algorithm

Insertion first resolves exact or upgrade identity. Capacity handling occurs
only for a genuinely new peer.

At the 2048 nearby threshold, the repository loads the protocol conversation
catalog once, then scans peers once. It does not query storage separately for
each peer.

A candidate is eligible only when:

- it belongs to the target protocol;
- it has no contact/user-fact projection;
- no conversation references its stable identity or projected node ID.

The eligible peer with the smallest `last_seen_s` receives a tombstone. If no
eligible peer exists, insertion returns `CapacityExceeded`.

## Pending Peer Delta Queue

Peer observations use bounded per-operation SD access and must not own the
physical SPI bus across a repository transaction.

The queue uses a PSRAM vector plus a head index; draining does not repeatedly
erase the first vector element. Consecutive pending full-record updates for the
same identity and deletion state are coalesced to the newest projection.

Each service tick drains at most four entries. Call realtime preemption prevents
the AppContext flush call, so audio resource ownership remains intact.

## Snapshot Compaction

Snapshot compaction uses complete current projections and runs at startup when
delta thresholds are exceeded:

- catalog: 512 deltas;
- read state: 256 deltas;
- status: 2048 deltas;
- peer/contact: 1024 deltas.

Normal chat `flush()` does not inspect and compact all healthy journals. It
only retries a dirty protocol projection, at most once per five seconds. Each
filesystem operation uses the bounded SD runtime guard; compaction never wraps
the complete operation sequence in a physical SPI lease.

Snapshot replacement sequence:

1. remove stale temp;
2. create and completely flush temp journal;
3. remove stale backup;
4. rename final to backup;
5. rename temp to final;
6. remove backup;
7. truncate/remove the now-covered delta.

Startup recovery restores backup when final is missing. If final exists,
stale temp and backup are removed.

## Corruption Recovery

Message segments are authoritative and are never discarded because a catalog
slot is corrupt.

Catalog reconciliation enumerates conversation segment directories and
reconstructs count, latest preview, timestamp, and unread state.

RT seen recovery streams all RT message slots and writes every valid LXMF hash
into a new seen snapshot. It does not allocate a complete message corpus in
memory.

Peer/contact incompatible v2 headers fail repository startup for that v2
partition. This is an explicit storage error, not permission to read legacy
files. Partial tails retain complete slots; contact/peer snapshots are recovered
through backup files.

## Concurrency And SPI

`SdStore` and `SdProtocolPeerRepository` each own a recursive FreeRTOS mutex.
The mutex serializes in-memory vectors, journal sequence numbers, merge
invariants, and the logical transaction that connects them. Recursive locking
is required because public query/update methods may call another method on the
same owner. This mutex is a state lock, not a bus lock.

`sd_card_runtime` is the only normal owner of physical SD/display/radio SPI
arbitration. `open`, `read`, `write`, `flush`, `exists`, `rename`, and `remove`
take bounded per-operation guards. Store/repository/page-cache code must not
acquire `PersistenceBusGate` or `SharedSpiBusAdapter` around a sequence of those
operations. Doing so stretches physical ownership across CPU work and creates
radio/display starvation even when every nested file call is individually
bounded.

Explicit hardware sessions are separate: SD unmount/recovery, USB mass storage,
and user-visible external font loading may own an exclusive bus/session token
under their dedicated lifecycle specification. They must not be copied into a
normal message, peer, or page-cache repository.

No renderer waits for radio TX, MQTT forwarding, LoRa airtime, or projection
compaction. SPI contention can delay or fail a bounded filesystem operation;
the ledger/repository retry policy handles that result without converting a
state mutex into physical bus ownership.

## Test Matrix

| Case | Expected result |
|---|---|
| MQTT inbound, catalog append fails | Message publishes; catalog dirty |
| RT message slot exists, seen append failed | Retry completes seen; no duplicate message |
| Seen journal corrupt | Rebuilt from RT message journals |
| Peer append SPI busy | Peer visible in memory; ordered delta queued |
| Contact append SPI busy | Contact edit fails; memory unchanged |
| Chat append while radio IRQ polling | SD and radio take separate bounded physical turns; no transaction-wide SPI hold |
| Nomad page cache read during LoRa activity | Page state is serialized; SD runtime releases SPI between file operations |
| MC node observed before key | Temporary peer persists; cannot become contact |
| MC key later arrives | Stable record then temporary tombstone |
| RT protocol event before destination identity | Temporary record upgrades to destination |
| Conflicting unverified key | Verified key retained |
| Nearby capacity full | Oldest unprotected nearby evicted |
| All peers protected | New nearby rejected; contacts retained |
| Power loss during snapshot rename | Backup restored at startup |
