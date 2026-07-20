# Chat Delivery Runtime Specification

## Purpose

Define structured runtime ownership for chat delivery, pending, and failure
state.

Phase 7.1 makes delivery state explicit without making the presentation model or
renderer responsible for pending queues.

## Core Rule

Delivery state is runtime state, not UI state.

```text
send result / ACK / failure event
  -> ChatDeliveryEventProjector
    -> ChatDeliveryReadModel
      -> ChatPresentationSource
        -> MessageRow delivery/failure
          -> Renderer
```

Final user-visible send feedback is also runtime feedback. It must flow
through the delivery feedback mechanism defined in
`CHAT_DELIVERY_FEEDBACK_SPEC.md`, not through a page-local compose widget.

Delivery state and read state are separate ledgers:

```text
Protocol send / ACK / proof / receipt
  -> MessageLedger / ChatDeliveryEventProjector
  -> delivery projection

User opens or marks a conversation read
  -> ReadStateLedger
  -> unread projection
```

`ChatDeliveryReadModel` may project outgoing status, but it must not own
read/unread, app badge counts, or conversation read watermarks. Those facts
belong to `ReadStateLedger` as defined in
`RUNTIME_OWNERSHIP_BOUNDARY_FREEZE.md`.

## Types

Phase 7.1 introduces:

- `ChatDeliveryRef`
- `DeliveryState`
- `DeliveryFailureKind`
- `ChatDeliveryRecord`
- `ChatDeliveryReadModel`
- `ChatDeliveryEventProjector`

Phase 7.3 adds:

- `ChatDeliveryEvent`
- `IChatDeliveryEventPort`
- `ProjectingChatDeliveryEventPort`
- `ChatDeliverySendResultProjection`
- `ChatDeliveryEventProjectionAdapter`
- `ChatDeliveryMessageProjection`

## Ownership

`ChatDeliveryReadModel` owns UI-readable delivery records.

`ChatDeliveryEventProjector` updates the read model from send/delivery events.

`IChatDeliveryEventPort` is the runtime event sink port for delivery events.

`ProjectingChatDeliveryEventPort` adapts that port to
`ChatDeliveryEventProjector`.

`ChatDeliveryEventProjectionAdapter` maps protocol-aware send result events and
ACK timeout hooks into the delivery event port. New active paths must not reduce
send results to `msg_id + bool`; they must preserve protocol and failure kind.

`ChatDeliveryMessageProjection` maps existing coarse `ChatMessage::status`
into delivery records.

`ChatDeliverySendResultProjection` maps send-result success/failure facts into
`ChatDeliveryEvent`.

`ChatDeliveryFeedbackController` observes delivery result facts and emits
platform feedback through `IChatDeliveryFeedbackPort`.

`ChatPresentationSource` reads the delivery read model and projects state
into `MessageRow`.

`ChatMessageLedger` also owns transient persistence pressure. A persistent
store reports authoritative append success through `appendDurably(...)`.
Transient failure enters a bounded pending-write queue owned by the ledger;
the presentation source reads a merged ledger page, never a page-local fallback.

Incoming publication is a two-phase boundary:

```text
decode -> durable append/defer -> delivery commit -> model/event/notification
```

`appendIncomingDurably(...)` failure must not drop the message and must not
publish it early. The runtime retries with a bounded per-tick budget. Once the
append succeeds, observers are invoked exactly once. Queue exhaustion is an
explicit rejection, not a silent loss.

On ESP, the physical commit and recovery rules are frozen by
`PROTOCOL_PARTITIONED_STORAGE_V2_SPEC.md`. A protocol message slot is
authoritative; catalog/read/status are projections. Once an inbound message and
its required RT LXMF dedup identity are durable, catalog failure must not block
observer, UI, or notification publication.

On ESP shared-SPI targets, chat append and status transactions use a
non-blocking outer SPI lease. Failure to obtain the lease produces `Deferred`;
the hot path must not multiply the storage runtime's per-operation wait or run
a synchronous full index rebuild.

## Boundaries

`ChatDeliveryReadModel` may:

- store bounded delivery records
- return records by `ChatDeliveryRef`
- clear records

`ChatDeliveryReadModel` must not:

- send messages
- retry messages
- inspect renderer state
- include LVGL/GTK
- include `ui_presentation`
- own radio or mesh adapters

`ChatDeliveryEventProjector` may:

- project queued/sending/sent/delivered/failed/received events
- map send failure kinds to delivery failure kinds

`ChatDeliveryEventProjector` must not:

- send packets
- retry messages
- build UI snapshots
- mutate ChatService storage directly
- render UI

`ChatDeliveryEventProjectionAdapter` may:

- consume send result events
- look up the message needed to build `ChatDeliveryRef`
- publish delivery events through `IChatDeliveryEventPort`
- expose an ACK timeout projection hook

`ChatDeliveryEventProjectionAdapter` must not:

- send packets
- retry messages
- build UI snapshots
- include LVGL/GTK
- mutate renderer state
- show final send success/failure prompts

`ChatPresentationSource` may:

- read delivery records
- enrich `MessageRow.delivery`
- enrich `MessageRow.failure`

`ChatPresentationSource` must not:

- maintain pending queues
- receive send result events
- call the projector
- infer failure from renderer state

## Message Reference

`ChatDeliveryRef` must identify a message inside one protocol namespace.
Bare `msg_id` is not sufficient for active MT / MC / RT delivery paths.

Phase 7.1 introduced `ChatDeliveryRef` as a compatibility reference:

- `local_id`
- `protocol_id`
- `nonce_or_seq`

Existing `ChatMessage::msg_id` may map to `protocol_id` first for compatibility,
but active send-result, retry, and presentation lookup paths must carry the
message protocol alongside the protocol id.

Required mapping intent:

- Meshtastic: protocol + from/to + packet id + channel when available.
- MeshCore: protocol + frame/app ACK identity or route/control identity.
- Reticulum: protocol + LXMF hash and destination identity.

The UI may render a small status badge, but the badge must be a projection of
this protocol-aware reference. A renderer must not look up or retry messages by
bare `msg_id`.

## Failure Kinds

Phase 7.1 recognizes:

- `PeerKeyMissing`
- `LocalIdentityMissing`
- `RadioSendFailed`
- `AckTimeout`
- `UnsupportedProtocol`
- `Rejected`
- `Unknown`

New protocol adapters must map failures before they reach the delivery
projector. `Unknown` is a compatibility fallback, not a normal design target.

Examples:

- ACK wait expired: `AckTimeout`
- radio or transport rejected TX: `RadioSendFailed`
- peer or local identity missing: `PeerKeyMissing` / `LocalIdentityMissing`
- active protocol cannot send this conversation: `UnsupportedProtocol`
- runtime policy rejected the operation: `Rejected`

## Non-Goals

Phase 7.1 does not implement a full retry engine.

The bounded persistence retry owned by `ChatMessageLedger` is not a protocol
retransmission engine. It only completes local authoritative message/status
commit after transient storage contention.

Phase 7.1 does not change radio packet format.

Phase 7.1 does not move delivery business ownership into the physical store.
`SdStore` may own protocol-partitioned journals, but delivery meaning remains in
MessageLedger and the delivery projector.

Phase 7.1 does not make `ChatWorkspaceModel` own delivery state.

Phase 7.1 does not make renderers infer pending/failure.

Phase 7.1 does not resolve Team rich payload delivery semantics.

## Relationship To Runtime Ownership Freeze

`RUNTIME_OWNERSHIP_BOUNDARY_FREEZE.md` is the higher-level boundary document for
message, delivery, retry, read/unread, and projection ownership. If a future
delivery change needs a new owner, update that document first, then this spec.
