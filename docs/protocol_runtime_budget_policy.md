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
- Public discovery persistence must be deferred. `record_announce()`,
  `record_lxmf_address()`, and peer cache persistence must not run from the
  screen-on RX hot path for ordinary public announces.
- Peer cache persistence is dirty/coalesced. Code must not force
  `maybePersistPeers(true)` from RX paths. Maintenance windows decide when the
  dirty peer cache is written.
- RX hot-path logging must be summary-first. Detailed logs are acceptable for
  local/realtime traffic and diagnostics, but public discovery should normally
  be represented by periodic counters.

## Maintenance Windows

A maintenance window is available when the screen runtime reports that the
device is sleeping and the saver is not active. During maintenance windows the
Reticulum runtime may:

- Replay deferred discovery packets under the discovery sample budget.
- Persist Reticulum announces and LXMF address records to SD.
- Flush dirty peer cache state to NVS.
- Publish deferred peer projections to the contact store.

The maintenance window still has finite budgets. It must not drain unbounded
network queues in one cycle.

## Regression Checks

`scripts/check_reticulum_runtime_budget_policy.py` enforces the highest-risk
boundaries:

- Reticulum polling APIs must not call the packet ingestion pump.
- Reticulum RX paths must not force peer persistence.
- Discovery budget names and helpers must not regress to Wi-Fi-only concepts.
- The Reticulum product adapter must forward the periodic runtime pump.
