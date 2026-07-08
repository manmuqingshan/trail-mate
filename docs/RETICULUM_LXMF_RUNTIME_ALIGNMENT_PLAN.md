# Reticulum / LXMF Runtime Alignment Plan

## Purpose

This document defines the full Trail Mate adaptation target for device-side
Reticulum and LXMF support over the existing ESP RNode-compatible radio path.

Protocol conformance rules and reference-oracle boundaries are defined in
[RETICULUM_CONFORMANCE_BASELINE.md](RETICULUM_CONFORMANCE_BASELINE.md). This
runtime plan describes the target implementation shape; the conformance baseline
defines how we decide whether the implementation is still Reticulum-compatible.
User-facing Reticulum mode behavior and SD-card group TSV configuration are
documented in [RETICULUM_MODE_USER_GUIDE.md](RETICULUM_MODE_USER_GUIDE.md).

It replaces the earlier phase-oriented view that treated `LxmfAdapter` as a
single adapter with incremental feature add-ons. The runtime has now grown into
 transport, path discovery, link relay, resource transfer, propagation, and
 business-level LXMF delivery concerns. Continuing to extend it as one large
 class would make protocol alignment, testing, and field debugging increasingly
 fragile.

The goal is to land a complete embedded runtime architecture that is:

- protocol-accurate enough to interoperate with upstream Reticulum/LXMF nodes
- structured enough to reason about path, link, and resource lifecycles
- observable enough to debug delivery failures in real deployments
- persistent enough to survive reboot without forgetting core identity and peer
  recall

## Scope

This plan covers the device-side runtime used when Trail Mate is operating in
native `Reticulum` product-protocol mode. It does not redefine the old `RNode`
bridge concept, which remains only a historical host-controlled modem path for
an external Reticulum instance.

This plan is explicitly about:

- transport runtime behavior
- path discovery and path recovery
- link establishment and teardown
- resource transfer lifecycle
- LXMF direct and propagated delivery
- persistence and recovery rules
- test and validation criteria

This plan is not, by itself, a commitment to implement every upstream desktop
Reticulum feature such as management destinations or full shared-instance
service parity. The target is complete, system-level behavior for an embedded
device node.

## Device Role And Ingress Policy

Trail Mate's Reticulum runtime is a **low-frequency terminal ingest runtime**,
not a desktop Reticulum node, public router, propagation node, or shared-instance
service endpoint.

This distinction is part of the product specification. A Trail Mate device can
use LoRa and a Wi-Fi Reticulum TCP gateway at the same time, but the Wi-Fi
gateway must not make the device behave like a high-throughput desktop node.
The runtime must continuously fit the real device budget:

- small screen and keypad-first UI
- ESP-class CPU and task stacks
- bounded serial logging bandwidth
- scarce LoRa airtime
- SD-card write latency and wear

The runtime priority order is:

1. **Chat and local business delivery first.** Local LXMF delivery, configured
   group destinations, active local links/resources, proofs for local traffic,
   and Trail Mate appdata such as team position/track are foreground traffic.
2. **Public discovery as sampled background state.** Announces and path traffic
   learned from a Wi-Fi gateway are useful for Nearby, Network, and address-book
   freshness, but they are not a mandate to mirror the public Reticulum network.
   The runtime may sample, coalesce, summarize, bound, or age out this state.
3. **Path/announce noise skip-first.** Unknown destinations, unrelated path or
   cache requests, non-local/non-group packets, and remote transport chatter not
   tied to a live local session should be skipped or rate-limited before they
   cause heavy parse work, UI updates, SD writes, LoRa transmissions, or verbose
   serial logs.
4. **No implicit Wi-Fi-to-LoRa public relay.** Wi-Fi gateway background traffic
   must not be blindly rebroadcast over LoRa. Any future full transport/router
   behavior must be an explicit mode with its own acceptance criteria, power
   budget, and UI indication.

Required side-effect policy:

- Serial logs must prefer periodic summaries and counters over per-packet logs
  for background Wi-Fi traffic.
- SD persistence must be stream-friendly, bounded, and coalesced. Verified
  announces and LXMF address-book entries may be persisted, but background
  traffic must not create one SD write per observed packet.
- UI projections must stay bounded. Network discovery views should project a
  small latest-first window, currently the newest 100 announce entries, with
  search/filter support instead of loading the whole directory as screen state.
- Background maintenance belongs in screen-sleep windows. While the screen is
  awake, Reticulum must only preserve foreground service: receiving packets
  addressed to the local device or configured groups, processing proofs/link
  traffic for active local sessions, processing path responses for user-initiated
  sends, and sending user/business traffic. Public announce discovery, peer
  projection, and non-urgent persistence must wait until the screen is sleeping.
- LoRa TX must be reserved for local product behavior and explicitly throttled
  protocol needs. Public Wi-Fi background discovery must not consume LoRa airtime
  by default.

Current ESP policy constants such as a small Wi-Fi socket read budget, a limited
number of Reticulum ingress packets per poll, a multi-second Wi-Fi discovery
sample interval, and a long announce-rebroadcast cooldown are implementation
defaults for this low-frequency terminal mode. They are tuning points, not
Reticulum wire-conformance rules.

A Reticulum change is not acceptable if ordinary public Wi-Fi gateway traffic can
make chat noticeably laggy, flood serial output, churn SD storage, or turn the
device into an accidental LoRa relay.

## Current Baseline

The current `LxmfAdapter` already implements more than the original phase-1/2
documents describe:

- Reticulum announce validation and caching
- path request emission and path-response handling
- cached announce replay
- `HEADER_2` multi-hop forwarding
- reverse-path proof relay
- local link sessions
- link request relay
- resource advertise/request/hashmap/part/proof flows
- propagation offer/get request handling
- runtime ownership contract coverage for transport, link, resource, and
  propagation state objects

The main problem is not missing packet codecs. The main problem is that these
capabilities are still governed by a monolithic adapter with limited runtime
lifecycle control.

## Current Implementation Status

The codebase now includes the first full runtime-alignment landing for this
plan.

Implemented runtime behaviors:

- shared runtime state models for transport, link, and propagation ownership
- destination-keyed pending path request tracking and timeout cleanup
- path-resolution clearing when matching announces are learned
- link close reasons and teardown-driven state cleanup
- timeout-driven path expiry and rediscovery trigger on failed outbound links
- outbound link establishment for locally initiated large-payload delivery
- deferred link payload queuing until the link becomes active
- active-link keepalive, stale transition, and timeout teardown behavior
- split-resource segment assembly for inbound multi-segment resource delivery
- runtime ownership contract smoke coverage for transport, link, resource, and
  propagation state objects, keeping the current monolithic adapter's internal
  state split into explicit ownership tables while behavior-level conformance
  traces continue to grow
- transport runtime table operations are split into `lxmf_transport_runtime.*`
  for path lookup/upsert, duplicate packet filtering, reverse proof routes,
  pending path request lifecycle, link relay lookup/upsert, and TTL cleanup;
  `LxmfAdapter` now injects time and limits while keeping product-facing
  behavior unchanged
- link runtime lifecycle operations are split into `lxmf_link_runtime.*` for
  session lookup, bounded session insertion, close cleanup, stale/timeout
  decisions, request/resource culling, and expired-session removal. The adapter
  still owns side effects such as deferred payload flushing, keepalive sends,
  path expiry, and rediscovery triggers.

Implemented product-protocol convergence:

- user-facing protocol selection is Meshtastic / MeshCore / Reticulum; `RNode`
  and `LXMF` remain compatibility/runtime terms, not selector-level product
  protocols
- chat messages and conversation ids now carry Reticulum destination identity
  as a first-class peer fact while preserving `NodeId` as a compatibility
  projection
- Reticulum peer identity construction now lives in the core chat domain, so
  LXMF/RNode runtime code only supplies hashes and does not own the product
  identity model. UI and storage projections that only carry a destination hash
  now use an explicit destination-only identity constructor instead of
  pretending to reconstruct a full identity hash.
- Reticulum identity hash reads, writes, comparisons, and duplicate-window
  projections now pass through core domain helpers across NodeStore, ChatService,
  ContactService, Linux SQLite, ESP SD, and nRF52 InternalFS, keeping raw hash
  field access inside the identity model and low-level wire codecs.
- Linux SQLite chat persistence groups Reticulum conversations, unread counts,
  recent-message loads, and conversation clearing by destination hash when it
  is available
- SQLite persistence rejects duplicate incoming Reticulum messages by
  `{protocol, channel, destination_hash, msg_id}` so projected peer changes do
  not create duplicate persisted messages
- presentation conversation tokens preserve Reticulum destination hash across
  core/UI round trips, so UI selection and mark-read actions do not collapse
  back to `NodeId` projection semantics
- Node protocol labels and protocol-to-runtime projection now use shared core
  helpers, so legacy contact values such as `RNode` and `LXMF` display and route
  as Reticulum without each UI surface reinterpreting them independently
- Reticulum team/appdata support is exposed through unicast appdata and known
  peer fan-out. It deliberately does not claim native broadcast appdata
  semantics, because LXMF delivery remains destination-oriented.
- nRF52 internal-file chat persistence writes Reticulum destination and
  identity hashes in its v3 format, while still reading the legacy v2
  projection-only records
- ESP SD chat log persistence writes Reticulum destination and identity hashes
  in its v3 message/index formats and uses destination-hash keyed log filenames
  when a Reticulum destination is available, while still reading legacy v2
  projection-only logs/index entries
- core runtime selection now has a Reticulum slot, and legacy `RNode` raw enum
  values normalize to the Reticulum product runtime instead of selecting a raw
  RNode user protocol
- ESP adapter creation now enters Reticulum through a product-level
  `ReticulumAdapter` class. The current implementation behind that boundary is
  still the LXMF service over the RNode-compatible raw carrier, but factory code
  no longer selects those internal implementation names directly and the product
  boundary no longer collapses to a type alias.

Still intentionally narrower than full upstream desktop/service parity:

- no shared-instance service runtime parity
- no management destinations or tunnel handling
- no metadata/compressed resource handling yet
- legacy v2 Reticulum SD logs that only contain the `NodeId` projection are not
  automatically merged into newer destination-keyed conversations
- ESP SDStore still needs target-firmware build/runtime verification on actual
  SD-backed hardware
- no deep automated interoperability matrix inside CI

## End-State Runtime Architecture

The end state is a layered runtime with the following modules.

### 1. Carrier Layer

`ReticulumAdapter` is the ESP product-protocol adapter boundary. Today it
delegates to the existing LXMF service adapter while the runtime is still being
split by responsibility.

`RNodeAdapter` remains responsible for:

- raw LoRa send/receive
- fragmentation/reassembly
- radio parameter application
- RX metadata capture

The carrier layer is unaware of Reticulum destinations, links, or LXMF payload
semantics.

### 2. Reticulum Transport Runtime

The transport runtime owns all network-level state that is independent of a
particular link payload.

Responsibilities:

- packet duplicate filtering
- path learning from announces
- path request lifecycle management
- pending local path request tracking
- announce cache, replay, hold, and release policy
- reverse-path proof routing
- `HEADER_1` and `HEADER_2` forwarding decisions
- local destination lookup
- link relay discovery state
- stale state culling and rediscovery triggers

Required state tables:

- `path_table`
- `destinations_map`
- `announce_table`
- `held_announces`
- `path_requests`
- `pending_local_path_requests`
- `reverse_table`
- `link_table`
- `packet_filter`

Implementation note:

On ESP targets these tables can remain bounded containers instead of unbounded
 maps, but they must still behave like keyed runtime tables with explicit
 lookup, lifetime, and eviction rules.

### 3. Reticulum Link Runtime

The link runtime owns encrypted session establishment and lifecycle.

Responsibilities:

- link request handling
- handshake proof validation
- RTT exchange
- keepalive and stale detection
- teardown and close reason classification
- pending request tracking
- validated-state tracking
- link-level packet proof handling
- resource runtime coordination

Required lifecycle:

- `Pending`
- `Handshake`
- `Active`
- `Stale`
- `Closed`

Every exit path from a live link must converge on a single teardown routine that
clears pending requests, cancels resources, shuts down channels, and erases
session secrets.

### 4. Reticulum Resource Runtime

The resource runtime owns large-payload delivery over links.

Responsibilities:

- advertisement decode result initialisation
- incoming/outgoing transfer bookkeeping
- windowed part request scheduling
- hashmap segment tracking
- split-resource continuation bookkeeping
- cancel lookup/erase semantics
- proof completion semantics
- resource TTL culling

The target behavior is not the current "single-segment encrypted resource only"
subset. The runtime must be able to reason about:

- encrypted resources
- split resources
- resource proof timing
- in-flight cancellation
- partial hashmap knowledge

Current split status: transfer initialisation, hashmap window construction,
hashmap update application, part receipt bookkeeping, split assembly,
proof-completion marking, resource lookup/erase helpers, and resource TTL
culling now live in `lxmf_resource_runtime.*`. Wire encode/decode, packet
encryption/decryption, proof payload construction, and product dispatch remain
in `LxmfAdapter`.

### 4b. Reticulum Propagation Runtime

The propagation runtime owns store-and-forward bookkeeping for LXMF propagation
nodes, but not LXMF wire encoding or local message delivery.

Responsibilities:

- held propagation entry lookup and bounded retention
- transient duplicate tracking and delivered-state merging
- propagation peer lookup, insertion, seen time, and counters
- offer wanted-id selection from known entries/transients
- get-response message selection and served counters
- propagation entry/transient/peer TTL culling

Current split status: entry/transient/peer bookkeeping, wanted-id selection,
message selection with transfer-limit accounting, served/incoming counters, and
TTL culling now live in `lxmf_propagation_runtime.*`. `/offer` and `/get`
request planning, peering-key validation, wanted-id response selection, held-id
listing, get-message response selection, served counters, propagation batch
decode/gating, remote propagation peer bookkeeping, and propagated message
acceptance decisions now live in `lxmf_propagation_service_runtime.*`. The
propagation service runtime decides whether a propagation batch is admissible
for the current offer state and whether each propagated LXMF message is
rejected, already known, destined for local delivery, or stored for another
destination. Link response sending, propagated delivery decryption, and local
envelope acceptance remain in `LxmfAdapter`.

### 4c. LXMF Delivery Runtime

The delivery runtime owns product ingress classification and materialisation
after LXMF envelope verification has already succeeded. It does not parse
Reticulum packets, validate outer LXMF envelope signatures, decrypt payloads, or
push/drop adapter queues.

Responsibilities:

- classify verified LXMF packed payload bytes as Trail Mate app-data or LXMF
  text
- map LXMF text payloads into `MeshIncomingText`
- map LXMF app-data payloads into `MeshIncomingData`
- preserve Reticulum destination identity for product conversation grouping
- preserve RX metadata captured by the adapter
- keep local-node, peer-node, message-id, timestamp, and channel projection
  explicit

Current split status: verified packed-payload classification and text/app-data
materialisation now live in `lxmf_delivery_runtime.*`, and `IncomingTextQueue`
preserves Reticulum identity through push/pop. `LxmfAdapter` still owns message
envelope unpacking, signature verification, queue pressure policy, Serial
logging, and propagation/local delivery orchestration.

### 5. LXMF Service Layer

The LXMF service layer sits above Reticulum transport/link/resource machinery
and converts protocol behavior into product behavior.

Responsibilities:

- announce-derived contact updates
- opportunistic delivery
- direct link-based delivery
- propagation offer/get flows
- local chat/app-data injection and queue/drop side effects
- proof/delivery result translation to UI/service events

This layer should not own route discovery or link teardown policy directly.

### 6. Persistence Layer

Persistence rules must be explicit.

Persisted state:

- local identity keypairs
- known peer identities and display names
- known destinations needed for recall
- durable propagation storage entries that survive reboot if product policy
  requires it

Ephemeral runtime state:

- packet filter entries
- reverse proof routes
- live link sessions
- in-flight resource transfers
- held announces
- pending path requests

## Functional Requirements

### Transport / Path

- Support direct and multi-hop announce learning.
- Track outstanding path requests per destination.
- Coalesce duplicate path requests for the same destination.
- Expire stale pending path requests.
- Release held announces when a pending request completes or times out.
- Rediscover paths after failed link establishment where policy allows.
- Route proofs using reverse-path state with explicit expiry.

### Link

- Support local link establishment for delivery and propagation destinations.
- Relay remote link traffic with explicit relay/link table state.
- Track link validation state separately from mere session presence.
- Tear down links deterministically on timeout, close packet, or local error.
- Cancel all live resources during teardown.
- Avoid leaving half-closed sessions until TTL cleanup.

### Resource

- Support outbound and inbound resource lifecycle management.
- Track segment and hashmap state explicitly.
- Support cancel semantics that also stop in-flight transfers.
- Associate resource state with link lifecycle.
- Reject unsupported resource forms only when policy explicitly says so.
- Prefer protocol-correct support over long-term permanent rejection.

### LXMF Delivery

- Opportunistic text/app-data delivery must remain supported.
- Link-based delivery should be the preferred path when a validated link exists.
- Propagation sync must use the same resource runtime rather than a side
  protocol path.
- Delivery success semantics must distinguish:
  - raw packet send
  - proof receipt
  - link response readiness
  - business-level response intent

## Implementation Strategy

The implementation proceeds in these layers, but each layer is expected to be
carried to a coherent, usable state before moving on.

### Stage 1: Runtime Decomposition

- Move path/transport state into a dedicated transport state container.
- Move link relay/session state into a dedicated link state container.
- Move propagation stores into a dedicated propagation state container.
- Move shared state types into a dedicated runtime state header.
- Keep the public `LxmfAdapter` API stable.

### Stage 2: Transport Lifecycle Completion

- Add destination-keyed pending path request tracking.
- Add explicit completion/timeout cleanup for path requests.
- Make path learning resolve pending requests.
- Add hooks for link-failure-driven rediscovery.
- Prepare local destination lookup as a first-class runtime concern.

### Stage 3: Link Lifecycle Completion

- Replace mark-only close behavior with real teardown.
- Introduce close reason tracking and stale-state transitions.
- Ensure timeout cleanup routes through the same teardown path.
- Make resource cancellation part of teardown.

### Stage 4: Resource Lifecycle Completion

- Normalize inbound and outbound transfer state machines.
- Add stronger cancellation semantics.
- Add split-resource support and proper segment continuation.
- Align proof timing and cancellation behavior with upstream expectations.

### Stage 5: Service and Interop Hardening

- Remove duplicated business logic across opportunistic/link/propagation paths.
- Add structured debug logging and counters.
- Add cross-validation against `.tmp/Reticulum` runtime behavior.

## Validation Matrix

The work is not complete until the following categories have been exercised.

- Direct announce discovery and opportunistic text delivery
- Multi-hop path request, path response, and proof return
- Link establish, keepalive, close, and timeout recovery
- Large link resource transfer with proof completion
- Resource cancellation while transfer is still active
- Propagation offer/get with mixed payload sizes
- Reboot recovery with persisted identity and peer recall intact

## Acceptance Criteria

The adaptation is considered complete when all of the following are true:

- The runtime is no longer governed by a single monolithic state blob.
- Transport, link, and resource lifecycle rules are explicit in code.
- Path requests and link teardown have dedicated runtime bookkeeping.
- Resource cleanup is tied to link lifecycle rather than passive TTL alone.
- The implementation can explain delivery success and failure at each stage.
- The architecture document and codebase describe the same system.

## Relationship To Existing Documents

- `RETICULUM_LXMF_DEVICE_MODE.md` remains useful as historical background and
  for earlier phase context.
- This document is the authoritative implementation target for the current
  alignment work.
