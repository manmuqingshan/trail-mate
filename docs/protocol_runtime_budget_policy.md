# Protocol Runtime Budget Policy

Trail Mate treats mesh protocols as device-terminal runtimes, not desktop
routers. Protocol adapters must protect the UI, LoRa radio, serial logging, SD
card, and NVS budgets before trying to ingest background network traffic.

## Reticulum Runtime Model

Reticulum runs as a low-frequency terminal ingress runtime.

Reticulum `All` carrier policy is a Wi-Fi-preferred auto policy, not a
desktop-style dual-interface router policy. When the Reticulum Wi-Fi gateway is
configured and ready, normal Reticulum TX, RX, and raw LoRa packet ingestion use
the Wi-Fi gateway only. LoRa is the fallback carrier only while the Wi-Fi
gateway is not ready. Explicit `LoRaOnly` and `WifiGatewayOnly` policies keep
their literal meanings.

When the screen is on, realtime processing is limited to:

- LXMF direct traffic addressed to this device.
- LXMF traffic addressed to configured Reticulum groups.
- Path, proof, link, and cache traffic that this device explicitly requested or
  that is required by an active local session.
- Outbound user actions such as sending text, app data, and self announces.

Ordinary public announces and public discovery traffic are not realtime while
the user is interacting with the device. They may be delayed for seconds or
tens of seconds and replayed during an idle or screen-off maintenance window.
The exception is lightweight peer-name projection: verified LXMF delivery
announces may publish one coalesced contact-store update at a low awake-screen
rate so Contacts and Chat can show the sender's display name without waiting
for SD or NVS persistence.

## MeshChat-Compatible Discovery Projection

Trail Mate's Reticulum UI projections follow the same product-facing discovery
split as Reticulum MeshChat:

- Contacts and Chat consume `lxmf.delivery` announces as chat peers. The peer
  display name is decoded from LXMF announce app data using the upstream shape:
  raw UTF-8 display names and msgpack arrays whose first element is the display
  name are both valid. Current upstream LXMF arrays may contain additional
  fields such as stamp cost and supported functionality; supported parsers must
  ignore trailing fields they do not need.
- Network consumes `nomadnetwork.node` announces as Nomad nodes. Their display
  name is decoded from text app data and is separate from LXMF peer naming.
- `lxmf.propagation`, `call.audio`, and unknown announces may be stored for
  routing, call, or diagnostic use, but they must not be promoted into Contacts
  as ordinary chat peers unless a Trail Mate business flow explicitly needs
  them.
- Destination and identity hashes remain address/search metadata. They must not
  be used as the default display name. If a Reticulum peer has no display name,
  the UI uses `Anonymous Peer`; if a Nomad node has no display name, the UI uses
  `Anonymous Node`.
- The UI must not infer display names from hash shape, old local records,
  destination prefixes, or other historical artefacts. If a stored record
  contains an incorrect name, that is stored data to fix or replace, not a
  runtime display-name fallback.

## Required Boundaries

- `pollIncomingText()` and `pollIncomingData()` may only return already
  materialised Trail Mate business messages. They must not parse Reticulum
  packets, verify announces, write SD files, persist peer caches, or send
  announces.
- Reticulum packet ingestion must run from a periodic runtime pump with an
  explicit budget. On ESP Arduino this is currently carried by the adapter
  `processSendQueue()` hook because it already runs outside the LVGL loop.
- LoRa and Wi-Fi Reticulum ingress must share the same public-discovery budget
  rules. Wi-Fi-only discovery throttles are not sufficient.
- Public discovery persistence must be deferred and coalesced.
  `record_announce()` and `record_lxmf_address()` on ESP Arduino are queueing
  APIs for runtime RX callers; they must not perform TSV upserts or other SD
  file I/O on the RX caller task.
- Explicit user address-book actions are the exception to RX-path deferral.
  Adding or removing a Reticulum contact may synchronously update
  `lxmf_addresses.tsv` because the user is already waiting for that action to
  complete. These paths must stay bounded, must not run from packet RX, and
  should use a blocking UI affordance if real hardware shows visible SD delay.
- Peer-name projection is not persistence. It may run while the screen is on,
  but it must be queue-backed and rate-limited; it must not trigger SD, TSV, or
  NVS writes.
- Peer cache persistence is dirty/coalesced. Code must not force
  `maybePersistPeers(true)` from RX paths, and non-forced peer dirty marking
  must not flush NVS from `mesh_task`.
- RX hot-path logging must be summary-first. Detailed logs are acceptable for
  local/realtime traffic and diagnostics, but public discovery should normally
  be represented by periodic counters.
- Shared LoRa RX task logging must also be summary-first. Normal IRQ/RX_DONE
  packets must not emit per-packet serial lines on ESP devices, because short
  packet bursts can stall UI-visible work even before packets reach Reticulum.
  If the mesh queue is full, the radio task must drop and count the packet
  instead of blocking on queue send.
- Reticulum runtime code executed by `mesh_task` must not allocate MTU-sized
  packet buffers or queued packet records as automatic locals. Carrier packet
  scratch storage belongs in adapter/interface members, and `mesh_task` needs
  enough stack headroom for Reticulum parsing and crypto call frames.

## Maintenance Windows

A maintenance window is available only after the screen runtime has reported
that the device is sleeping and the saver is not active for a stable grace
period. A single instantaneous sleep-state check is not sufficient for SD
persistence, because wake/saver/app transitions can otherwise let background
Reticulum writes overlap foreground Contacts or Map SD reads. During stable
maintenance windows the Reticulum runtime may:

- Replay deferred discovery packets under the discovery sample budget.
- Let the Reticulum directory worker persist one coalesced announce or LXMF
  address record to SD per slice.
- Publish deferred peer projections to the contact store at the faster
  maintenance-window rate.

The maintenance window still has finite budgets. It must not drain unbounded
network queues in one cycle.

Reticulum directory persistence is a cancellable maintenance transaction on
ESP-class devices. The directory worker must re-check the maintenance gate
before heavy SD operations such as TSV scans, temp-file writes, remove, and
rename. If the gate closes, it must stop the transaction, leave the original
directory file intact, requeue the coalesced record, and wait for the next
stable maintenance window. Foreground user actions such as manually adding a
contact may still perform bounded synchronous address-book writes, but runtime
announce/address discovery must not write or remove
`/trailmate/reticulum/announces.tsv` or
`/trailmate/reticulum/lxmf_addresses.tsv` while Contacts, Network, Map, Chat,
or other normal UI pages are foregrounded.

## UI Projection Budget

Contacts and Network are projections of Reticulum discovery state. They must not
mount every known announce as a live LVGL object on small ESP targets. Long
Contacts lists should render a visible window with spacer rows and preserve
scroll position across timer refreshes. Data snapshots may contain more records
than the visible UI window, preferably in PSRAM-backed storage when available.
Reticulum Contacts search may stream over the SD address book, but the UI must
still cap the number of projected rows.

## Regression Checks

`scripts/check_reticulum_runtime_budget_policy.py` enforces the highest-risk
boundaries:

- Reticulum polling APIs must not call the packet ingestion pump.
- Reticulum RX paths must not force peer persistence.
- Discovery budget names and helpers must not regress to Wi-Fi-only concepts.
- The Reticulum product adapter must forward the periodic runtime pump.
- Announce RX/TX hot paths must not allocate Reticulum packet buffers on the
  `mesh_task` stack.
