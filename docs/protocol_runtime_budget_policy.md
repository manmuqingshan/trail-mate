# Protocol Runtime Budget Policy

Trail Mate treats mesh protocols as device-terminal runtimes, not desktop
routers. Protocol adapters must protect the UI, LoRa radio, serial logging, SD
card, and NVS budgets before trying to ingest background network traffic.

## Reticulum Runtime Model

Reticulum runs as a low-frequency terminal ingress runtime.

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
- Peer-name projection is not persistence. It may run while the screen is on,
  but it must be queue-backed and rate-limited; it must not trigger SD, TSV, or
  NVS writes.
- Peer cache persistence is dirty/coalesced. Code must not force
  `maybePersistPeers(true)` from RX paths, and non-forced peer dirty marking
  must not flush NVS from `mesh_task`.
- RX hot-path logging must be summary-first. Detailed logs are acceptable for
  local/realtime traffic and diagnostics, but public discovery should normally
  be represented by periodic counters.
- Reticulum runtime code executed by `mesh_task` must not allocate MTU-sized
  packet buffers or queued packet records as automatic locals. Carrier packet
  scratch storage belongs in adapter/interface members, and `mesh_task` needs
  enough stack headroom for Reticulum parsing and crypto call frames.

## Maintenance Windows

A maintenance window is available when the screen runtime reports that the
device is sleeping and the saver is not active. During maintenance windows the
Reticulum runtime may:

- Replay deferred discovery packets under the discovery sample budget.
- Let the Reticulum directory worker persist one coalesced announce or LXMF
  address record to SD per slice.
- Publish deferred peer projections to the contact store at the faster
  maintenance-window rate.

The maintenance window still has finite budgets. It must not drain unbounded
network queues in one cycle.

## UI Projection Budget

Contacts and Network are projections of Reticulum discovery state. They must not
mount every known announce as a live LVGL object on small ESP targets. Long
Contacts lists should render a visible window with spacer rows and preserve
scroll position across timer refreshes. Data snapshots may contain more records
than the visible UI window, preferably in PSRAM-backed storage when available.

## Regression Checks

`scripts/check_reticulum_runtime_budget_policy.py` enforces the highest-risk
boundaries:

- Reticulum polling APIs must not call the packet ingestion pump.
- Reticulum RX paths must not force peer persistence.
- Discovery budget names and helpers must not regress to Wi-Fi-only concepts.
- The Reticulum product adapter must forward the periodic runtime pump.
- Announce RX/TX hot paths must not allocate Reticulum packet buffers on the
  `mesh_task` stack.
