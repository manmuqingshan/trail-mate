# Runtime Ownership Boundary Freeze

Status: normative

This document freezes ownership boundaries for key mechanisms of the Trail Mate runtime. It is not a stage plan or an accident review.
Subsequent modifications must first comply with the owner relationship here, and then consider local implementation. You can no longer use page patches, protocol bypasses,
storage double-write luck, or resource temporary judgments to bypass the main path.

If this document conflicts with older implementation specifications, this document shall prevail; the old document must be updated, rather than having the code continue to choose the
 more convenient bypass.

## One Rule

A fact can only have one authoritative owner.

Other modules can only submit intent, consume projection, or execute the effect given by owner. Any module that simultaneously
 "reads facts, rewrites facts, fails to interpret, and refreshes the UI" has crossed the line.

## Scope

This document freezes the following mechanisms:

1. The division of labor between UI and runtime.
2. MT / MC / RT three protocol message delivery status.
3. read / unread / badge status.
4. Reticulum direct / propagation deduplication and confirmation.
5. Reticulum Sideband/LXST call and realtime resource lease.
6. The air interface budget and UI non-blocking relationship between MQTT downlink to LoRa.
7. External fonts and language packs are loaded.
8. Contacts / Network projection classification.
9. Owner migration rules after God file disassembly.

## Non-Negotiable Boundaries

### UI

UI only allows:

1. Issuing user intent.
2. Show projection snapshot.
3. Display the pending / failure / progress clearly given by the runtime.
4. Manage page local selection, scroll position, focus and visible navigation.

UI not allowed:

1. Directly parse protocol packets, announce, LXMF envelope, Meshtastic protobuf or MeshCore frame.
2. Directly change read/unread, delivery, contact, path, link, call, font loaded status.
3. To solve display problems, load fonts privately or access SD font files.
4. Directly stop hardware resources for call, download, MQTT or LoRa.
5. Pretend that the business status has changed by hiding the badge, refreshing the list, deleting the item, etc.

### Settings

Settings only submits product intent.

Settings does not allow determining protocol internal wire profile, packet context, call fallback, resource lease or
font loading strategy. Settings can select active protocol, Wi-Fi profile, Reticulum gateway, notification policy,
volume, and locale, but these selections cannot be implemented as a private branch that bypasses the runtime owner.

### Protocol Adapters

MT / MC / RT adapter can have wire codec, platform IO adaptation, queue access and protocol runtime combination.

They are not allowed to have common business states. Message status, read/unread, conversation badge, contact projection,
send retry, and deduplication ledger must enter the shared owner and then be projected to the UI.

Protocol differences must be expressed as protocol-aware events, rather than falling back to bare msg_id,
node id or packet id by the UI or ChatService.

### Store And Index

Index, conversation list, message list cache, header mirror are all projection or cache. They can
speed up the presentation, but they cannot become the authority on business facts.

If a state should remain consistent after restarting, it must have an independent owner or ledger. Maintaining the
 status by successfully writing multiple files at the same time is an unconverged design.

## Authoritative Owners

| Fact | Owner | Projection | Hard invariant |
| --- | --- | --- | --- |
| Outgoing/incoming message identity | `MessageLedger` | Chat message rows | MT/MC/RT must have protocol-aware identity, and must not rely solely on bare `msg_id` |
| Delivery state | `MessageLedger` + `ChatDeliveryEventProjector` | Message badge, feedback | `Delivered` must come from ACK/proof/receipt or protocol equivalent fact |
| Read/unread state | `ReadStateLedger` | Conversation badge, unread budget, app badge | Index/header can only be mirrored, not authoritative |
| Conversation list | `ConversationProjectionStore` | Chat workspace snapshot | Can be rebuilt, cannot be reversed ledger |
| UI chat state | `ChatWorkspaceModel` | Renderer | Only saves selection/offset, not business status |
| Runtime events | `ChatPageRuntimeEventPump` | UI refresh sink | Event pump routing events, no rendering, no inference of business results |
| Reticulum destination | `DestinationRegistry` | Contacts/Network row | Full destination hash/aspect is authoritative, projected node id is not |
| Reticulum path | `PathManager` | Path diagnostics, send eligibility | Freshness/replay/coalescing/expiry can only be adjudicated in one place |
| Reticulum link | `LinkManager` | Call/link status | Link open/identify/keepalive/close There is only one lifecycle owner |
| Reticulum announce ingest | `AnnounceIngestor` | Contacts/Network/propagation metadata | Signature verification, identity/destination association, path observation are completed uniformly |
| Reticulum packet routing | `ReticulumPacketRouter` | Domain events | The route from Packet type/context to owner has only one entry |
| Propagation sync | `PropagationClient` + propagation seen/ack ledger | Chat projection | Repeated offer cannot generate repeated messages or repeated unread |
| Reticulum call | `LxstTelephonyClient` | Call Page projection | Product call path only supports Sideband/LXST |
| Call resources | Call realtime leases + `WifiAccessRuntime` | Call Page progress/failure | UI does not directly preempt Wi-Fi/LoRa/GPS/audio |
| Audio hardware | Platform audio adapter | Ring/call volume projection | ES8311/I2S/mic/speaker setup teardown only one owner |
| Notification policy | Notification policy runtime | Tone/vibration/notice intents | Message prompts, contact prompts, mute/vibration/volume only consume business projection |
| Font loading | `FontRuntimeCoordinator` + `ResourcePackRegistry` | Loading page/modal + refreshed font chain | Missing fonts must not be permanently blocked by active locale or hot-path |
| MQTT downlink relay | Meshtastic runtime TX queue / air-time budget owner | Send/deferred/drop state | UI does not wait for LoRa TX, MQTT burst does not directly fill up UI tick |

## Runtime Overview Design

The outline design is fixed to four runtime-oriented product combinations, rather than page patch combinations:

```text
Product intent
  -> Protocol facade
  -> Domain owner
  -> Ledger / queue / lease
  -> Projection
  -> UI renderer
```

1. Reticulum call has the protocol fact owned by `LxstTelephonyClient`, and has the Wi-Fi/LoRa/GPS/sleep/audio resource fact by Call realtime leases
, which is displayed by Call Page projection.
2. Notification is owned by the Notification runtime and the product policy fact is owned by the platform audio adapter.
 ES8311/I2S/speaker/microphone hardware fact is owned by the platform audio adapter. Message events, contact events and Settings previews can only be submitted
   notification intent.
3. Contacts only consumes person/contact projection. Network consumption service/relay/web/unknown
 projection. Neither parses Reticulum announce.
4. LoRa TX is owned by the TX scheduler of the protocol adapter. The business layer can only enqueue.
 Success of `sendAppData()` means entering the scheduler, but does not mean that the air interface has been occupied and the transmission is completed.
5. `LxmfAdapter` can only exist as Reticulum facade/coordinator shell. New functions must first fall on
   DestinationRegistry、PathManager、LinkManager、AnnounceIngestor、ReticulumPacketRouter、
 PropagationClient, PingService, NetworkPageClient or LxstTelephonyClient.
6. The independent scheduling status, RX statistical status, and deferred discovery queue are not allowed to be reintroduced in the Adapter
 or MTU scratch array; these facts belong to `RuntimeBudget`, `RawRxTelemetry`,
 `DeferredDiscoveryQueue` and `AdapterScratchBuffers` respectively.

## Runtime Detailed Design

### Reticulum Call

Detailed design:

1. The product call profile is fixed to Sideband-compatible `lxst.telephony`.
2. The user actively dials out and enters hard preempt directly, because the user has explicitly submitted the call intent.
3. Incoming calls LinkRequest can enter the identifying/ringing resource stage; it will not report that it has been connected before answering.
4. To answer the call, you must first obtain a hard realtime lease and then start the media session. If any step fails, it will enter clear failure and
 will not answer automatically.
5. Only LXST audio RX/TX of the current `link_id` is allowed during the call.
6. Hang-up/remote shutdown/media failure/timeout must all enter Closing and then release the lease.
7. MeshChat `call.audio` is only allowed as a source code compatible adapter that is not registered by default. It is not allowed to enter product
 Settings, automatic fallback is not allowed, and the main LXST path branch is not allowed to depend on it.

### Notification And Audio

Detailed design:

1. `ChatNewMessageEvent`, `NodeInfoUpdateEvent`, and Settings volume preview must be called
   Notification runtime.
2. Notification runtime reads message alerts, contact alerts, vibration, tone volume, etc.
 product policy, and outputs tone/vibration intent.
3. Notification runtime is not allowed to parse the message protocol, change unread, and bypass platform audio
   adapter.
4. Call ring and call media are still controlled by the Call realtime/audio owner; Notification runtime
 Call audio must not be preempted in `ActiveCall`.
5. Platform audio adapter is the only hardware owner, responsible for ES8311/I2S/mic/speaker session open,
   volume、gain、mute、teardown.

### Contacts / Network

Detailed design:

1. Contacts uses `ReticulumContactProjectionPolicy`, only projects valid LXMF address/person records:
 favorite/manual/import is Contact, runtime announce is Announced, ignored means Ignored.
2. Contacts does not display propagation, Nomad/web/service, unknown, gateway, interface or path hop.
3. Network uses `ReticulumNetworkProjectionPolicy` to project non-contact announce:
 `lxmf.propagation` is Message Relay, `nomadnetwork.node` is Web/Service,
 `lxst.telephony`/legacy `call.audio` is Telephony Service, unknown is Unknown Service.
4. PropagationClient can maintain relay metadata in the background; whether the UI displays relay is determined by Network projection
 policy and cannot be bypassed through Contacts.
5. Destination hash and projected node id are only address/search metadata, not contact identity authority.
6. `PeerDirectoryService` owns the read-write, hot-loading and projection queues of the Reticulum peer directory;
 The adapter only publishes the final NodeInfo/Protocol update event as `IPeerProjectionSink`.

### Reticulum Runtime Owners

Detailed design:

1. `RuntimeBudget` is the only scheduling strategy output in the call/nomad/sleep/saver/P4 screen stage.
 The adapter can only provide input facts and cannot copy stage determinations.
2. `AnnounceScheduler` has native announce pending, retry, interval and rebroadcast
 Throttle status. The adapter only performs signing, packaging and actual TX.
3. `DeferredDiscoveryQueue` has public discovery bounded queue, drop-oldest and
 packet-hash deduplication. The adapter only determines whether to defer and how to replay.
4. `RawRxTelemetry` has RX summary counters, LoRa discovery detail suppression and LoRa
 ignored announce suppression. The adapter does not save these counters.
5. `AdapterScratchBuffers` is the long-term owner of MTU-level packet scratch. Added MTU buffer
 cannot litter the adapter with bare fields.

### LoRa TX Scheduler

Detailed design:

1. All transmissions that will occupy the LoRa air interface must enter the same scheduler tick.
2. `sendText()`、`sendAppData()`、key verification、runtime protocol effects、MQTT
 Neither downlink relay nor ACK retry can directly and synchronously block radio TX from the UI/event/RX path.
3. Each tick holds `kLoRaAirTxBudgetPerTick`. This budget is consumed when protocol actions, ACK retry, ordinary messages, and MQTT
 downlink successfully enqueue radio TX.
4. `min_tx_interval_ms_` is a shared throttling across TX owners, not a local judgment of a queue itself.
5. MQTT downlink maintains the official gateway relay semantics, but it must be deduplicated according to `from + id + channel`,
 join the queue, and drain according to the budget; when the queue is full, a drop/deferred reason must be generated instead of a stuck UI.
6. The UI can only display Queued/Sending/Sent/Delivered/Failed or deferred/drop projection, and cannot wait for
 LoRa TX to complete before continuing to render.

## Notification Policy Contract

Notification policy is product policy, not a side effect of the message store, protocol adapter, or audio driver.

Settings can be configured:

1. message alerts enabled/disabled.
2. contact alerts: none / contacts only / all discovered people, or equivalent user-understandable options.
3. vibration enabled/disabled.
4. message tone volume.
5. call ring volume.

Notification runtime can only consume:

1. message projection.
2. contact/person projection.
3. read/unread projection.
4. user notification policy.
5. active interruption/call state.

Notification runtime can output:

1. play message tone intent.
2. start/stop call ring intent.
3. vibrate intent.
4. on-screen notice intent.

It does not allow:

1. Determine the message delivered yourself.
2. Clear unread by yourself.
3. Directly parse the protocol packet.
4. Bypass the platform audio adapter to play sound.
5. Start non-call audio when call active/exclusive.

Message tone, incoming call ringtone, Settings volume preview, and call playback must go through the same platform audio owner.
If the audio owner is unavailable, the notification runtime can only get an explicit failure or deferred result, and cannot swallow the sound silently.

## Message State Contract

The message state is an abstract business state, and the protocol adapter is only responsible for mapping the protocol facts into it.

Allowed business status:

1. `Queued`: has entered the local outbox or is waiting for a runtime opportunity.
2. `Sending`: Sending or waiting for protocol receipt.
3. `Sent`: Sent but the protocol does not have or does not promise end-to-end delivery proof.
4. `Delivered`: ACK, proof, receipt or equivalent delivery fact defined by the protocol has been received.
5. `Failed`: Sending refused, wireless sending failed, ACK timeout, resource unavailable or protocol not supported.

Rules:

1. MT direct and requires ACK: `Queued -> Sending -> Delivered/Failed`.
2. MT broadcast/group or ackless success: `Queued -> Sending -> Sent`.
3. MC app ACK completed: Enter `Delivered`.
4. MC app ACK timeout: enter `Failed(AckTimeout)`.
5. RT LXMF proof/receipt completed: Enter `Delivered`.
6. Successful RT propagation local reception does not equal remote delivery; it only proves that the local machine is durable accepted.
7. When the same bare `msg_id` appears in MT/MC/RT, only the message ref matching the protocol can be updated.
8. UI badge can only display simplified text, but the status source must be ledger/projection.

Forbidden:

1. Continue to send the final business event with only `msg_id + bool` as the new path.
2. Let the renderer show that it has been delivered based on "send function returns true".
3. Discard the protocol field in retry, delivery action, and presentation lookup.
4. Because the message cannot be found, create another outgoing item with the same content.

### Message Persistence And Publication

Message content, sending status and UI/notification events must go through the same ledger main path:

```text
decoded incoming
  -> MessageLedger durable append or bounded deferred queue
  -> incoming delivery commit
  -> ChatModel / EventBus / notification
  -> conversation projection

outgoing protocol acceptance
  -> MessageLedger durable append or bounded deferred queue
  -> protocol-aware delivery event
  -> conversation projection
```

Hard constraints:

1. incoming only occurs when authoritative message record, dedup identity and read-state commit
 Publish `ChatNewMessageEvent`, notification, vibration or sound only after success.
2. When SD/SPI is temporarily unavailable, the incoming message enters the fixed-depth deferred queue; after a successful retry, it is only submitted and published
 once. When the queue is full, a clear rejected/drop reason must be output, and failure must be returned to the protocol that supports two-phase commit.
3. When the outgoing record or subsequent status writing fails, the bounded pending write is retained by `MessageLedger`;
 The message page, conversation page and lookup must merge the pending state, and cannot falsely report `stored`,
 nor allow the UI to create temporary messages.
4. pending write is only allowed to execute a limited budget per runtime tick. ESP chat store must call the non-blocking semantic interface of storage
 service; deferred immediately when the device is temporarily unavailable, and cannot wait for multiple
 250ms SD operations in the same tick.
5. conversation index, header mirror and UI cache are still just projections. Projection write failure can keep the
 ledger operation pending, but it must not trigger the synchronous full disk `rebuildIndex()` in the sending and receiving hot path.
6. message/status retry must be idempotent. The conversation log has been written, but subsequent ledger/projection writing failed
Message, only unfinished submissions can be completed when retrying, and a second identical record cannot be appended.
7. The latest page of the chat workspace is fixed at 10 items; turning pages continues to use the same ledger page API. runtime event pair
The current session can trigger snapshot reload at most once, and the auxiliary function and the caller are not allowed to do a full rebuild each.

Forbidden:

1. `continue` directly after `appendIncomingDurably(...) == false` and discard the message.
2. Unconditionally record `stored` after the call returns `void append(...)`.
3. The UI monitors raw MQTT/LoRa packets, or synthesizes message bubbles by itself when persistence fails.
4. In order to fix notifications, bypass durable commit and call notification/audio directly.
5. After the index writing fails, all conversation logs are scanned synchronously within the message sending and receiving tick.

## Read And Unread Contract

`ReadStateLedger` is the only authority for read/unread.

It must express:

1. protocol.
2. conversation identity.
3. last read durable cursor or equivalent read watermark.
4. commit status.
5. pending/failed mark-read results when necessary.

Reading rules:

1. unread count is derived from `MessageLedger + ReadStateLedger`.
2. conversation index, SD header, app badge, screen badge are all projections.
3. The same unread result must be restored from the ledger after restarting.
4. The projection can lag behind, but cannot conflict with the ledger for a long time.

Writing rules:

1. `ChatWorkspaceModel::markRead(...)` is just a UI intent.
2. `IChatActionSink` hands the intent to the app/runtime service.
3. app/runtime service submits `ReadStateLedger`.
4. The projection store refreshes the badge after receiving the committed or pending fact.
5. When durable commit fails, explainable failure or pending must be retained instead of UI false success.

Forbidden:

1. Only change the index/header but not the ledger.
2. Hide the unread badge only in the UI.
3. The read status is based on whether a page is open as the authority.
4. Reticulum direct and propagation paths each increase unread.

## Reticulum Client Contract

Trail Mate is a Reticulum client, not a general transport node, propagation node, gateway, or service host.

The product capabilities are fixed as:

1. LXMF direct delivery.
2. LXMF propagation retrieval.
3. Path discovery, identity recall, link lifecycle, proof/receipt required by the client.
4. Sideband-compatible `lxst.telephony` call.
5. Nomad/Micron service discovery and browsing, projected to Network.

Reticulum main path must comply with:

1. `ReticulumPacketRouter` is the only entry.
2. `AnnounceIngestor` uniformly completes announce signature verification, identity/destination association and path observation.
3. `DestinationRegistry` has destination truth.
4. `PathManager` owns path truth.
5. `LinkManager` owns link truth.
6. `MessageLedger` has LXMF idempotency.
7. `PropagationClient` owns propagation offer/ack/seen.
8. `LxstTelephonyClient` has call truth.

Forbidden:

1. UI, notification, Settings or Contacts parse Reticulum wire bytes.
2. `LxmfAdapter` once again has the main states of path, link, message, propagation and call.
3. Add the MeshChat `call.audio` fallback branch to the main LXST call path.
4. Display the call protocol selector in product Settings.

MeshChat `call.audio` can be retained as a source code compatibility/protocol research adapter, but it will not be registered by default, will not enter the product image,
 will not fallback automatically, and will not be an optional configuration for users.

## Reticulum Propagation Contract

Duplicate offers for Propagation are part of the Reticulum/LXMF network behavior; showing them repeatedly to users is not acceptable behavior.

Rules:

1. Direct and propagation must converge to the same message ledger before or during LXMF envelope validation.
2. The complete LXMF message hash is the idempotency key across restarts and direct/propagation.
3. Repeated offers can update transport metadata, last seen, and source path, but cannot create new messages.
4. This machine only sends propagation acknowledgment after the message durable accepted.
5. The ack/seen ledger must be able to prevent repeated user-visible delivery across restarts.

Forbidden:

1. propagation appends chat records every time it is pulled.
2. ack is issued before durable message commit.
3. Use sender + timestamp + text as a weak key instead of LXMF hash.
4. Direct and propagation each maintain duplicate detection.

## Call Realtime Contract

The product call path is Sideband-compatible LXST. The answering experience is Call Page, not UI modal.

Status:

1. `Idle`
2. `IncomingIdentifying`
3. `IncomingRinging`
4. `PathResolving`
5. `LinkConnecting`
6. `ResourceAcquiring`
7. `MediaPreparing`
8. `Active`
9. `Closing`

Resource rules:

1. Incoming identifying/ringing has Call Page and soft-preempt Wi-Fi.
2. Incoming ringing suspends LoRa and GPS. BLE is not compiled in the ESP product firmware and there is no runtime lease.
3. The user actively dials out and enters hard preempt directly.
4. After answering, the user first obtains the hard preempt and audio session, and then reports that the answer is successful.
5. The Active/Closing phase only allows audio traffic of the current call link.
6. Non-interruptible critical operation causes the answer/dialout to fail and the call will not be answered automatically.
7. Incoming calls at the same time or during a call must fail quickly and cannot be queued into another UI call.
8. Closing holds an exclusive lease until LinkClose is issued/observed or bounded cleanup is completed.

UI rules:

1. Call Page displays caller, identifying/connecting/active/closing/failure.
2. Answer, reject, hang up, and volume shortcut keys are page actions.
3. The bottom of the page displays the shortcut keys available during the call.
4. Call Page does not directly stop Wi-Fi, LoRa, GPS, audio or MQTT.

Audio rules:

1. Platform audio adapter has ES8311/I2S/mic/speaker setup/teardown.
2. Ring tone, message tone, settings tone, and call playback must all enter the same audio owner.
3. After answering, the default speaker volume can be increased to the maximum safe volume of the call profile.
4. RX decode/playback and TX capture/encode cannot block each other.
5. Any echo suppression, gain, and jitter buffer changes must belong to the media session and must not be scattered in the UI.

## MQTT Downlink And LoRa Air-Time Contract

Meshtastic MQTT downlink can maintain gateway semantics, but must go through a unified air interface budget.

Rules:

1. MQTT downlink enters projection/ingest first and does not directly synchronize LoRa TX in MQTT callback.
2. LoRa TX enters the unified TX queue and air-time budget.
3. Downlink relay must press `from + packet id + channel` to force deduplication.
4. Limit the number of downlink drains per tick.
5. UI only consumes projection and does not wait for LoRa TX to complete.
6. When the queue is full or the budget is insufficient, the message status enters queued/deferred/drop reason instead of blocking the UI.
7. LoRa air interface long packets, repeated bursts, and route floods cannot occupy the display/input wake path.

Forbidden:

1. MQTT callback directly sends LoRa in a loop.
2. To prevent stuck, permanently disable downlink-to-LoRa official semantics.
3. Let UI tick bear relay flush.
4. The same downlink burst is sent to the air interface multiple times without deduplication.

## Network Page Cache Worker Contract

Nomad page cache reading is background storage work, and the Network page only submits requests and reads projections.

Rules:

1. The page body, completion status and request status must be uniquely held by `PageCacheLoadState`; large blocks of state must be placed in
 PSRAM, and silent fallback to internal heap is not allowed.
2. The stack/TCB of the PageCache worker must have definite memory ownership before entering the Network, and the task stack cannot be dynamically allocated repeatedly in the page
 tick.
3. Workers only have two startup paths: `not-started -> running` or `not-started -> unavailable`; startup failure
 is an observable termination status, and the creation of mutex/queue cannot be mistaken for worker availability.
4. `request_cached_page_load()` and `poll_cached_page_load()` must quickly return
 to a clear status when the worker is unavailable, and cannot continue to queue, wait for SD, or recreate the task in each frame.
5. cache read/write must go through the PageCache storage service; the UI does not wait for the device storage transaction to complete.

Forbidden:

1. After `xTaskCreate()` fails, a queue that appears to be available but actually has no consumer is retained.
2. Retry task creation frame by frame in the Network render/timer path and flush the log.
3. Freely roll back PageCache state or unsuitable task stack to PSRAM to resolve internal heap fragmentation.
4. After task creation fails, it returns to the UI thread and reads the Nomad page cache synchronously.

## Font And Localization Runtime Contract

The font is a runtime resource, not a page private repair point.

Core rules:

1. The glyph requirements of content text such as CJK/Japanese/Korean/Arabic are not determined by the active display locale.
2. When `active_locale=en` is set, if Chinese content appears on the chat, Network, Contacts or Nomad page, the installed and available
 `zh-hans-core` and other content supplements must still allow loading and joining the content font chain.
3. Missing word detection can occur in the content path, but the loading decision must be handed over to `FontRuntimeCoordinator` /
   `ResourcePackRegistry`.
4. Synchronous external font loading is allowed, but only as a user-visible foreground operation:
 Display the loading/progress/busy page or modal, flush to the screen, and then hand it to the font device service for loading.
5. Ordinary render/list/timer paths must not block SD IO silently without an owner.
6. Bus busy, insufficient memory, and file corruption must form interpretable diagnostics and retry/failure status, and cannot be permanently hard skipped.
7. Pages must not directly deny font loading due to `ui_hot_path`, `active_locale`, or `content_supplement` tags.

Forbidden:

1. Page/widget reads `font.bin` directly.
2. Replace the font chain in the renderer with the bypass of "cut CJK fonts if they are not ASCII".
3. Block Chinese content fonts on the grounds that the active locale is not Chinese.
4. Let Chinese tofu boxes be permanently displayed on the grounds of protecting UI.
5. Create LVGL modal but enter `lv_binfont_create()` without flushing.

## Contacts And Network Projection Contract

Contacts literally only displays people or identities that can be communicated with.

Allow entry to Contacts:

1. verified LXMF person destination.
2. verified LXST telephony destination.
3. Names, short names, addresses and communication destinations that can be associated with the same identity.

No access to Contacts:

1. propagation node.
2. gateway/interface/path hop.
3. Nomad/web/service.
4. unknown announce.
5. relay-only or message infrastructure.

Network displays network capabilities and services:

1. Nomad/Micron service.
2. web/service destination.
3. propagation node status.
4. gateway/interface diagnostics.
5. path/interface health.

Contacts and Network can only consume projection and are not allowed to read raw announce or directly maintain protocol truth.

## God File Burn-Down Contract

Removing the God file is not a physical removal of the file, but an owner migration.

Each fact must be migrated once:

1. Create owner.
2. Migration status and invariants.
3. Adapter calls owner.
4. Delete the old state and mutation in the Adapter.
5. Add or update contract testing.

Facadeization cannot be declared before completion. `LxmfAdapter` is a facade only when the following conditions are met:

1. External methods only forward use-case.
2. path/link/destination/message/propagation/call state is not written directly in the adapter.
3. UI and Settings do not read adapter internal protocol details.
4. The old mutation has been removed instead of being left as fallback.
5. The compatibility code is isolated from the product graph.

## Protocol-Partitioned Storage V2 Contract

Chat/Peer/Contact persistence during ESP Arduino product runtime is only allowed to use:

```text
/data/v2/mt
/data/v2/mc
/data/v2/rt
```

Authority rules:

1. The message journal is the message fact; the catalog/read/status is the rebuildable projection.
2. RT message journal is the reconstruction source of LXMF seen ledger.
3. `SdProtocolPeerRepository` is the sole owner of peer facts and contact user facts.
4. `INodeStore` and `IContactStore` are repository views, not independent stores.
5. contact alias/favorite/ignored/trusted only writes the contact journal, and does not write the peer slot repeatedly.
6. Active protocol must be filtered at the application query boundary, and the UI does not read all three protocols before filtering.
7. nearby can only eliminate unprotected peers; contact and conversation reference are always protected.
8. Snapshot can only be replaced atomically through temp/backup/final; only v2 backup can be restored after power failure.
9. For large projection/peer/contact/pending buffers, the only strict PSRAM allocator is preferred.
10. Bounded compaction can be done during the startup phase; ordinary UI ticks must not repeatedly scan or rewrite the complete projection.

ESP product graph re-registration is prohibited:

```text
/chat/*
/nodes.bin
/contacts.dat
/mesh/peers.bin
```

Reading the old format after v2 fails in the name of "compatibility" is prohibited. For complete rules, see
`PROTOCOL_PARTITIONED_STORAGE_V2_SPEC.md`.

## Prohibited Patch Patterns

The following modifications are prohibited from entering the mainline:

1. Page-level special judgment to fix protocol or storage issues.
2. `if (reticulum_call::realtime_mode_active()) return;` This type of scattered resource judgment.
3. Bypass `ChatDeliveryEventProjector` to update message status.
4. Bypass `ReadStateLedger` to update unread.
5. Bypass `MessageLedger` to receive direct/propagation messages.
6. Bypass `FontRuntimeCoordinator` to load or reject fonts.
7. Bypass `WifiAccessRuntime` to seize Wi-Fi.
8. Expose compatible/experimental protocol branches that have not yet formed a product closed loop in Settings.
9. In order to let the current case pass, the main path will never hit.
10. Add a new owner but do not delete the old owner.

## Change Gate

Before modifying the implementation, you must answer:

1. Who is the owner of this change?
2. Where does the intent enter?
3. Who executes the effect?
4. Where does projection come from?
5. Where is durable state submitted?
6. How to recover after restarting?
7. Does the protocol field retain protocol-aware identity?
8. Does the UI only look at snapshot/projection?
9. Is the resource lease applied for by the runtime owner?
10. Are there old bypasses that can still be hit?

If there is no answer to any question, first add owner/spec/test, and then change the implementation.

## Required Regression Contracts

Subsequent related modifications need to cover at least the following contracts:

1. When `active_locale=en`, Chinese chat and Network/Nomad content can trigger controlled font loading and eventually use content font.
2. The user can see the loading/progress/busy status before the font is loaded, and it is not silent SD blocking.
3. Mark-read durable commit will not revive after restarting unread.
4. Failure of mark-read commit will not let the UI pretend to be successful.
5. Direct is the same as propagation. LXMF hash only generates one message and one unread transition.
6. MT/MC/RT with the same naked ID will only update the delivery/read status of the corresponding protocol.
7. ackless send will not permanently display Sending.
8. failed send has protocol-aware failure kind.
9. MQTT downlink burst does not block UI wake/render.
10. MQTT downlink relay via LoRa queue/air-time budget and deduplication.
11. The Incoming/active call resource lease phase is consistent with the UI Call Page status.
12. Incoming calls quickly fail during a call.
13. The call ring, message tone, settings tone and call playback all pass through the same audio owner.
14. The message alerts/contact alerts/vibration/audio volume policy does not change the delivery or unread facts.
15. Propagation, service, gateway/interface, and unknown announce do not appear in Contacts.
16. Network can present services and network infrastructure without polluting Contacts.
17. MQTT/LoRa/RT durable messages will not be displayed or notified due to catalog projection failure.
18. Peer refresh will not overwrite contact alias/flags or verified key.
19. Contact or existing conversation peers will not be eliminated when nearby reaches capacity.
20. After the RT seen journal is damaged, it will be rebuilt from the authoritative RT message journal, and historical messages will not be re-projected.

## Relationship To Existing Specs

This document is a total boundary frozen document. Relevant details continue to be maintained by the following documents:

1. `docs/specification/CHAT_DELIVERY_RUNTIME_SPEC.md`
2. `docs/specification/CHAT_WORKSPACE_MODEL_SPEC.md`
3. `docs/specification/CHAT_PRESENTATION_IDENTITY_SPEC.md`
4. `docs/specification/LOCALIZATION_SPEC.md`
5. `docs/specification/PROTOCOL_RUNTIME_DESIGN_SPEC.md`
6. `docs/reticulum_client_architecture.md`
7. `docs/wifi_access_resource_policy.md`
8. `docs/MULTI_PROTOCOL_SUPPORT.md`
9. `docs/specification/PROTOCOL_PARTITIONED_STORAGE_V2_SPEC.md`
10. `docs/design/PROTOCOL_PARTITIONED_STORAGE_V2_OVERVIEW.md`
11. `docs/design/PROTOCOL_PARTITIONED_STORAGE_V2_DETAILED_DESIGN.md`

When the implementation conflicts with this document, it cannot be solved by partial code patch; it must go back to the owner boundary and correct the main path.
