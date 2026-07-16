# Reticulum Client Runtime Architecture

Status: accepted for implementation

## Product boundary

Trail Mate is a Reticulum client. It is not a general-purpose Reticulum
transport node, propagation node, or service host. The supported product
capabilities are:

- LXMF direct delivery and propagation retrieval for person-to-person messages.
- Reticulum path discovery, identity recall, link lifecycle, packet proofs, and
  receipts required by those client operations.
- LXST telephony interoperability with Sideband on `lxst.telephony`.
- Nomad/Micron service discovery and browsing in Network.

MeshChat `call.audio` remains source-level compatibility code for development
and protocol study. It is not registered in the product runtime, is not exposed
in Settings, and is never an automatic fallback for LXST.

## Semantic objects

The runtime recognises the following protocol facts. UI pages and persisted
views are projections of these facts; they do not own protocol state.

| Fact | Owner | Invariant |
| --- | --- | --- |
| Identity | Identity registry | A public identity is accepted only after its hash and signature relationship have been verified. |
| Destination | Destination registry | A destination is keyed by full destination hash and aspect; projected `NodeId` values are never authoritative. |
| Path | Path manager | Freshness, replay rejection, request coalescing, expiry, and selected interface are decided in one place. |
| Link | Link manager | Link establishment, identification, keepalive, resource transfer, and closure have one lifecycle owner. |
| Message | Message ledger | LXMF hash is the durable idempotency key; UI status follows receipt/proof facts, not a successful local send call. |
| Propagation sync | Propagation client | Offers may repeat. A durable seen/ack ledger prevents duplicate user-visible delivery. |
| Call | LXST telephony client | Only one call session may own telephony state and media resources. Unsupported media profiles fail explicitly. |

## Runtime shape

The implementation follows ports-and-adapters with state-machine owners and
read projections:

```text
IMeshAdapter facade
  -> Reticulum client coordinator
       -> packet router
       -> identity/destination registry
       -> path manager
       -> link manager
       -> LXMF delivery client + message ledger
       -> propagation client + seen/ack ledger
       -> LXST telephony client
  -> platform ports
       -> Reticulum interfaces
       -> peer/directory storage
       -> call realtime leases
       -> audio backend
  -> projections
       -> Contacts: people and communicable identities only
       -> Network: Nomad/web/service destinations only
       -> Chat: conversation/message ledger projection
       -> Call Page: current LXST call projection
```

`LxmfAdapter` is a compatibility facade and scheduler shell. It may translate
`IMeshAdapter` calls and provide platform ports, but it must not independently
own path, link, message, propagation, call, Contacts, or Network truth.

## Packet ingress contract

```text
interface packet
  -> parse and packet-hash duplicate gate
  -> packet router
  -> announce | data | proof | link request | link packet
  -> the single owning runtime
  -> domain event
  -> ledger/projection persistence
```

No UI code, notification code, or product setting may parse Reticulum wire
bytes. No packet handler may update a UI projection while bypassing its owning
runtime.

## Message contract

- Direct and propagation delivery converge before LXMF envelope validation.
- The full LXMF message hash is the idempotency key across reboot and across
  delivery methods.
- A repeated propagation offer may update transport metadata but cannot create
  another chat item or unread count.
- `Sent` means queued or transmitted locally. `Delivered` requires a valid
  Reticulum/LXMF receipt or proof associated with the same message ledger entry.
- Propagation acknowledgement is emitted only after the delivery has been
  durably accepted into the local message ledger.

## Projection contract

Contacts contains identities that can represent a person in messaging or
telephony. A record is eligible when it has a verified LXMF delivery destination
or a verified LXST telephony destination that can be joined to an identity.
Propagation nodes, Nomad/web services, unknown announces, gateways, interfaces,
and path hops are excluded.

Network contains Nomad/Micron service destinations and their path/service
metadata. Propagation nodes are maintained by the propagation client and are
not presented as contacts. Gateways and interfaces are connection diagnostics,
not directory entries.

## LXST call state machine

```text
Idle
  outgoing command -> PathResolving -> LinkConnecting
  incoming valid LinkRequest -> IncomingIdentifying

IncomingIdentifying
  link active -> send AVAILABLE
  valid LinkIdentify -> IncomingRinging + Call Page + ringing lease
  invalid/blocked/busy/timeout -> send BUSY or REJECTED -> Closing

IncomingRinging
  accept -> ResourceAcquiring -> send CONNECTING
  reject/timeout -> send REJECTED -> Closing

OutgoingRinging
  remote CONNECTING -> MediaPreparing

ResourceAcquiring / MediaPreparing
  exclusive lease + audio open + supported profile -> send/accept ESTABLISHED
  any failure -> explicit failure -> Closing

Active
  only matching link audio RX/TX is accepted
  hangup/link close/media failure -> Closing

Closing
  keep exclusive lease until LinkClose is sent/observed or timeout completes
  -> release media, realtime leases, and Call Page -> Idle
```

An incoming telephony LinkRequest may wake the display and navigate to the Call
Page in an identifying state, but it is not shown as an identified caller until
LinkIdentify succeeds. User acceptance cannot report success before the hard
preemption lease and audio session are both ready.

## Realtime resource contract

- Incoming identification/ringing owns the Call Page and soft-preempts Wi-Fi.
- On incoming ringing, LoRa and GPS work are paused; BLE has no compiled ESP
  product runtime and therefore has no lease to revoke.
- Outgoing call starts at hard preemption because it is an explicit user action.
- Accepted call hard-preempts all non-call Wi-Fi leases.
- A non-preemptible critical operation makes acceptance fail visibly.
- Closing retains call exclusivity until LinkClose completion or bounded timeout.
- UI never revokes hardware or network work directly; it only issues call
  commands to the call runtime.

## Audio contract

The selected embedded interoperability profile is LXST Bandwidth Low, Codec2
3200 at 8 kHz mono. Profile selection is explicit: unsupported incoming profile
requests are answered with the supported preference before media starts.

Capture/encode and receive/decode/playback run on independent clocks. Both are
children of one media session and stop together. Playback has a bounded jitter
buffer with underflow recovery and overflow accounting; capture cannot block
speaker cadence. The platform audio adapter owns ES8311/I2S setup, microphone
gain, speaker volume, and teardown.

## UI interruption contract

The call experience is a page, not a modal. A global interruption navigator
temporarily exits and remembers the current app, enters the Call Page as the
active app, blocks ordinary back/menu navigation while a call phase is active,
and restores the prior app or menu when the call returns to Idle.

The page shows caller identity when known, explicit identifying/connecting/
active/closing/failure states, answer/reject/hang-up actions, RX/TX health, and
bottom-aligned volume shortcuts. It does not own call or resource state.

## Migration and deletion rules

1. Introduce a runtime owner and tests for a fact.
2. Redirect the adapter or UI caller to the owner.
3. Delete the previous state mutation and compatibility branch in the same
   stage.
4. Keep `call.audio` code behind an unregistered development adapter; no
   product configuration value selects it.
5. A stage is incomplete while two modules can independently transition the
   same path, link, message, propagation, or call state.

## Acceptance matrix

- Settings contains no call protocol selector and persisted legacy values do
  not alter product call routing.
- Product call destination is always `lxst.telephony`.
- A valid incoming LXST link reaches the Call Page and ring path; LinkIdentify
  enriches the caller instead of being the first UI trigger.
- Busy or concurrent incoming calls receive a prompt protocol-level failure.
- Accept, reject, remote hangup, local hangup, timeout, media failure, and link
  failure all return resources and UI to Idle.
- RX and TX audio use the agreed profile; microphone work cannot stall speaker
  cadence.
- Direct and propagation copies of one LXMF hash create one chat item and one
  unread transition across reboot.
- Contacts excludes propagation, service, unknown, gateway, and interface
  entries; Network exposes Nomad services.
- Message delivery status is proof/receipt-backed.
- `LxmfAdapter` no longer owns the main Reticulum fact state and is reduced to a
  facade/coordinator shell.
