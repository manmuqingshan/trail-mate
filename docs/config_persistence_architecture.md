# Configuration Persistence Architecture

Status date: 2026-07-26

This document describes only the technical mechanism for persisting `AppConfig`.
Business specifications such as map behavior, protocol behavior, language packs,
contacts, and messages must not duplicate storage-locking or platform storage
details.

## Distinctions

`AppConfig` runtime state is the in-memory configuration used by application
code.

`AppConfigChangeSet` is the persistence intent: it says which configuration
domain changed. It does not name NVS namespaces, files, SD card paths, SPI buses,
or locks. A domain selects a persistence section, not an individual key. For
example, a `Map` request permits the ESP adapter to rewrite the map keys in the
`settings` section; it is not a promise that only `map_source` is written.

The platform persistence backend maps change domains to concrete storage. On
ESP Arduino today this storage is Preferences/NVS, not SD card and not shared
SPI.

Runtime apply is separate from persistence. Applying LoRa, GPS, privacy, or map
runtime changes must not imply a storage implementation detail, and saving a
configuration value must not directly operate any hardware bus.

## Change Domains

The core mechanism exposes stable domains:

| Domain | Meaning |
| --- | --- |
| `Identity` | Long and short device name |
| `Mesh` | Protocol selection, radio profile, protocol MQTT and bearer options |
| `Channels` | Meshtastic and MeshCore channel visibility/key settings |
| `Gps` | GPS and motion sampling configuration |
| `Map` | Map source, contour, coordinate and track display defaults |
| `ChatUi` | UI chat defaults such as active channel |
| `Network` | Network duty-cycle and utilization limits |
| `Privacy` | Privacy and encryption mode settings |
| `Route` | Tracker route defaults |
| `Aprs` | APRS/iGate configuration |

Business code may request persistence for one or more domains. It must not
request a platform namespace such as `chat`, `settings`, or `gps`.

## ESP Persistence Flow

1. Startup loads `AppConfig` from the platform backend.
2. `AppContext` copies the loaded config into its PSRAM-backed save baseline.
3. A writer opens `beginConfigEdit()`. The returned small token holds the
   configuration mutex while the caller makes one coherent update.
4. `commit(changes)` publishes the edit and replaces the pending snapshot with
   the current configuration. The token destructor cancels the edit without
   publishing it.
5. The async save worker debounces requests and persists the latest queued
   snapshot. A later edit can replace an older pending snapshot, including by
   reverting to the last persisted value.
6. The ESP backend maps domains to Preferences/NVS sections and writes only the
   required sections.
7. On success, the in-flight snapshot becomes the new save baseline. Any
   pending snapshot is reconciled against that new baseline before another
   write is allowed.
8. On failure, the baseline is invalidated and retry conservatively persists all
   persisted domains until a save succeeds.

The queue still uses PSRAM-backed `AppContext` member snapshots to keep the
worker's save input stable. It must not create large `AppConfig`, protocol
config, or byte-buffer automatic locals on ESP task stacks.

## ESP Domain-To-Store Mapping

| Domains | ESP Preferences section |
| --- | --- |
| `Identity`, `Mesh`, `Channels` | `chat` |
| `Gps` | `gps` |
| `Map`, `ChatUi`, `Network`, `Privacy`, `Route` | `settings` |
| `Aprs` | `aprs` |

This mapping is an adapter concern. It may change per platform without changing
business code.

## Configuration Ownership

`AppConfig` is still a compatibility aggregate, but every field must have one
clear persistence owner. The following fields are deliberately not all handled
by the same mechanism:

| State | Owner | `saveConfig()` meaning |
| --- | --- | --- |
| `AppConfig::chat_policy.max_channels` | AppConfig / `chat` section | Persisted as part of `Channels` |
| `AppConfig::reticulumConfig().reticulum_groups` | Reticulum group storage | Runtime mirror only; loaded and saved by its own SD-backed owner |
| `AppConfig::ble_enabled` | BLE runtime policy | Not an AppConfig persistence field on ESP Arduino; the backend currently keeps it disabled |
| Settings backup JSON | Settings backup service | Separate full backup/restore format, not the async NVS section writer |

Adding a field to `AppConfig` does not make it persistent automatically. The
field must be assigned to a domain, included in change detection, and handled
by the owning adapter, or explicitly documented as runtime-only.

## Edit Boundary And Platform Semantics

`beginConfigEdit()` is the ownership seam between business code and the
configuration aggregate. New code must use it for writes so that mutation and
snapshot creation happen under one synchronization boundary. The mutable
`getConfig()` overload remains only as deprecated source compatibility for
legacy call sites; it must not be used for new writes and will be removed after
the remaining callers migrate.

The scoped `saveConfig(AppConfigChangeSet)` contract is implemented explicitly
by every facade. ESP Arduino uses the domains to select Preferences sections.
IDF, Linux, and nRF52 currently use full-blob or full-snapshot persistence, so
they intentionally expand the request to a complete save until their adapters
gain section-level writers. This fallback is visible in each implementation and
is not a silent default in the shared interface.

## Non-Goals

This mechanism does not define map tile rendering, SD tile storage, contact
storage, message storage, language-pack loading, or shared SPI arbitration.

This mechanism does not make `AppConfig` the long-term settings schema. It only
keeps the existing aggregate persistable while reducing save granularity and
removing full old-config load-before-save.

This mechanism does not introduce a business-facing key/value persistence API.
Adding `save("settings", "map_source")` style calls would leak adapter details
and is not allowed.

## Implementation Rules

Routine business code should use an edit transaction and commit the domain it
changed, for example:

```cpp
auto edit = config_api.beginConfigEdit();
if (edit)
{
    edit.config().map_source = source;
    edit.commit(AppConfigChangeSet::map());
}
```

Code that only has a notification after an already-owned update may call
`requestSaveConfig(AppConfigChangeSet::map())`. A no-argument save remains a
compatibility path and is reconciled against the current snapshot; it must not
be used to hide an unbounded configuration mutation.

Legacy zero-argument saves are tolerated only because `AppContext` detects
changed domains. New code should not rely on zero-argument saves when the domain
is known.

ESP save/load code must avoid large automatic locals. Long-lived snapshots must
be member-owned or explicitly PSRAM-preferred.

The ESP backend must not reload a full previous `AppConfig` before every save
just to compute deltas. Delta detection belongs to the application persistence
mechanism, not to the storage adapter.

Configuration persistence logs should show the change-set and concrete store
sections touched. A map source change should touch the `settings` section only.
