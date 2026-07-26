# Protocol-Partitioned Storage V2 Specification

## Purpose

This specification freezes the authoritative storage model for MT, MC, and RT
chat, peer, contact, delivery, and read-state data on the ESP Arduino product.

The design has four goals:

- a received message becomes visible only after its authoritative message
  record is durable;
- projection failures never suppress an already durable message;
- protocol-specific facts are not stored in a cross-protocol union;
- contacts and conversation-referenced peers cannot be displaced by nearby
  discovery traffic.

## Product Boundary

The active ESP Arduino runtime must use only the v2 layout:

```text
/data/v2/mt/
/data/v2/mc/
/data/v2/rt/
```

It must not read or register compatibility readers for:

```text
/chat/*
/nodes.bin
/contacts.dat
/mesh/peers.bin
```

Failure to load a v2 journal may trigger v2 recovery from an authoritative v2
journal. It must not fall back to a v1 file.

## Ownership

`SdStore` owns authoritative chat message journals and their projections.

`SdProtocolPeerRepository` is the single owner of protocol peer facts and user
contact facts. It exposes three ports:

- `IMeshPeerDirectory` for protocol runtimes;
- `INodeStore` as an active-protocol contact projection;
- `IContactStore` as an active-protocol user-fact projection.

The node and contact ports are views of the same repository. They must never be
backed by independent ESP stores.

`ChatService`, `MessageLedger`, `ChatDeliveryEventProjector`, and UI
presentation remain protocol-independent business owners. Physical protocol
partitioning must not duplicate these services or their state machines.

## Authoritative Commit Rule

For an inbound message:

```text
decode
  -> append protocol message slot
  -> append RT LXMF dedup identity when applicable
  -> commit MessageLedger delivery
  -> publish model/event/notification
  -> append/rebuild catalog projection
```

The message slot and required RT dedup identity are authoritative. Catalog,
preview, unread count, and status indexes are projections.

If the message append fails, publication is deferred by `MessageLedger`.

If the message append succeeds but a catalog projection append fails, the
message is still committed and published. The catalog is marked dirty and is
rebuilt from message journals.

This rule specifically prevents the former MQTT failure mode where a downlink
reached `ChatService`, storage projection I/O failed, and the UI/notification
observer never saw the message.

## Chat Physical Layout

Each protocol owns independent chat files:

```text
/data/v2/<protocol>/chat/
  catalog.snapshot
  catalog.delta
  read.snapshot
  read.delta
  status.snapshot
  status.delta
  conversations/<conversation-key>/0000.msg
  conversations/<conversation-key>/0001.msg
```

RT additionally owns:

```text
/data/v2/rt/chat/seen.snapshot
/data/v2/rt/chat/seen.delta
```

Message segments are 128 KiB fixed-slot journals. Slots are protocol-specific:

- MT text capacity is 233 bytes and the MT slot is fixed-size;
- MC text capacity follows the MC wire/product limit;
- RT text and identity fields follow the LXMF product limit;
- only RT slots carry RT destination, identity, and LXMF hash fields.

Status changes are appended as status projection records. Existing message
slots are never rewritten to update delivery state.

Read state is a `last_read_sequence` projection. Receiving a message does not
rewrite the complete read-state file.

## Peer And Contact Physical Layout

Each protocol owns:

```text
/data/v2/<protocol>/
  peers.snapshot
  peers.delta
  contacts.snapshot
  contacts.delta
```

Peer slots contain only protocol identity, protocol facts, timestamps, source,
display facts, metrics, and position observations.

Contact slots contain only stable identity plus user-owned facts:

- alias;
- favorite;
- ignored;
- trusted;
- MC node-id projection hint where required by the current UI identity.

Alias and user flags must not be duplicated in peer slots.

## Identity Rules

MT stable identity is node ID. Its public key is a protocol fact.

MC stable identity is the 32-byte public key. A node-ID-only observation may be
persisted temporarily, but it cannot become a contact until a stable public key
is known. When the key arrives, the repository appends the stable record before
tombstoning the temporary identity.

RT stable identity is the destination hash. A node-ID-only observation may be
persisted temporarily. A later destination identity upgrades it using the same
stable-first ordering.

A manually verified MT key or verified MC key must not be replaced by a
conflicting unverified runtime observation. Other fresh node facts may still be
merged.

## Retention Rules

Nearby/discovered records are preemptible storage entries.

Per protocol, at most 2048 unprotected nearby records are retained. When that
budget is full, only the oldest unprotected nearby record may be tombstoned.

A peer is protected when either condition is true:

- it has a contact/user-fact projection;
- an existing chat conversation references it.

Protected peers do not consume the nearby eviction budget and must never be
selected as nearby eviction victims.

The repository accepts up to 4096 protected contact/user-fact projections in
total. Reaching that explicit limit rejects a new contact; it never evicts an
existing contact. This limit is independent of the 2048 nearby budget.

## Active Protocol Query Rule

Switching protocol changes the application query partition:

- Chat returns only conversations for the active protocol;
- Contacts returns only contacts for the active protocol;
- Nearby returns only peers for the active protocol.

The UI must not load all protocols and filter them locally.

## Memory Rule

Canonical chat projections, peer records, contact records, pending peer deltas,
and codec scratch ownership use the single platform `PsramAllocator`.

On ESP that allocator requests `MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT` and has no
internal-heap fallback. Allocation failure is explicit and fatal rather than a
silent migration of large protocol data to internal RAM.

Small RTOS objects and the existing `INodeStore` active UI projection may use
internal memory. They must not become an additional fact source.

## Scheduling Rule

Message append uses the storage service's non-blocking durable operation.
Contention or temporary device unavailability defers the message through the
ledger rather than blocking the UI.

Peer observation deltas use a non-blocking append. Failed appends enter a
PSRAM-backed ordered queue. The runtime drains at most four peer deltas per
service tick and does not drain during call realtime resource preemption.

Contact edits are explicit user transactions and use a bounded durable storage
operation. Memory is updated only after the contact delta is durable.

Large snapshot compaction runs during startup. Normal runtime flush only
retries a dirty chat projection at a throttled interval; it does not compact
healthy read/status journals on every UI loop.

## Recovery Rule

Every journal has a versioned descriptor and every slot has a CRC.

A partial tail exposes only complete slots. Authoritative message journals are
reconciled into catalog/read projections.

Snapshot replacement uses:

```text
temp complete
  -> final renamed to backup
  -> temp renamed to final
  -> backup removed
```

Startup restores the backup if power was lost between the two renames.

The RT seen ledger is a projection of authoritative RT messages. If it is
missing, incompatible, partially written, or contains a corrupt slot, it is
rebuilt by streaming RT message journals. This prevents propagation from
re-presenting already stored LXMF messages after index damage.

## Forbidden Changes

- Do not add a v1 reader or v2-to-v1 fallback.
- Do not reintroduce an ESP `NodeStore` or `ContactStore` beside the protocol
  repository.
- Do not make catalog/index success a prerequisite for publishing a durable
  message.
- Do not write contact aliases or flags into peer slots.
- Do not evict contacts to admit nearby observations.
- Do not rewrite complete index/read/contact files on every message.
- Do not compact large snapshots in a renderer, notification callback, packet
  callback, or ordinary UI tick.
- Do not use a second PSRAM STL allocator implementation.

## Verification

Required checks include:

- MT/MC/RT max-size message slot round trips and CRC rejection;
- temporary and stable MC/RT peer identity round trips;
- contact identity and MC node-hint round trips;
- peer slots do not round-trip contact flags or aliases;
- verified keys survive conflicting unverified updates;
- ESP source contract contains no registered v1 paths or old store classes;
- MQTT durable-before-projection ordering remains enforced;
- Pager PlatformIO build and ESP stack hygiene pass.
