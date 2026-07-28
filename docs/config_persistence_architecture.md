# Configuration Persistence Architecture

Status date: 2026-07-27

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
2. `AppContext` initializes `ConfigPersistenceRuntime` with the loaded config as
   its persisted baseline. The runtime's snapshots live with the PSRAM-backed
   application context.
3. A writer opens `beginConfigEdit()`. The returned small token holds the
   configuration mutex while the caller makes one coherent update.
4. `commit(changes)` publishes the edit and replaces the pending snapshot with
   the current configuration. The token destructor cancels the edit without
   publishing it.
5. `ConfigPersistenceRuntime` owns debounce, pending/in-flight snapshots,
   generations, and retry state. The platform execution shell only takes an
   immutable work view from the runtime and invokes the adapter. A later edit
   can replace an older pending snapshot, including by reverting to the last
   persisted value.
6. The ESP backend maps domains to Preferences/NVS sections and writes only the
   required sections.
7. On success, the in-flight snapshot becomes the new save baseline. Any
   pending snapshot is reconciled against that new baseline before another
   write is allowed. If the latest in-memory value reverted while the old
   write was in flight, the runtime schedules that latest value rather than
   replaying the old payload.
8. On failure, the baseline remains the last successfully persisted snapshot.
   The failed payload's domains remain dirty and are retried; a newer pending
   snapshot merges those domains with its own changes without escalating to a
   full-domain rewrite.

The long-lived snapshots are PSRAM-backed on ESP targets. The execution shell
must not create large `AppConfig`, protocol config, or byte-buffer automatic
locals on ESP task stacks. Arduino runs the shell from the application service
owner; IDF runs it from the IDF application owner; neither path creates a
dedicated configuration-save task.

`ConfigPersistenceRuntime` is deterministic and platform-neutral. It shares
`sys::PersistenceGeneration` and `sys::PersistenceResultKind` with the
storage-maintenance foundation. FreeRTOS queues, task handles, Preferences,
NVS namespaces, and platform retry logging remain outside this module.

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
IDF and Linux submit an explicit full-snapshot adapter request to their
`ConfigPersistenceRuntime` and execute it from their application service tick.
nRF52 submits the same semantic request to its board-owned deferred settings
store, which currently persists a complete snapshot. These are platform
adapter choices, not alternate dirty-state machines.

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

## Config Persistence Runtime

The runtime is the sole owner of configuration persistence state. Its state is:

```text
Idle
  -> Debouncing
  -> InFlight
  -> Idle

InFlight
  -> Debouncing     (a newer snapshot arrived)
  -> Backoff        (the adapter failed)

Backoff
  -> InFlight       (retrying the latest immutable snapshot)
```

The runtime has three distinct snapshots:

```text
baseline  = last successfully persisted snapshot
pending   = newest requested snapshot not yet started
active    = immutable snapshot currently passed to the adapter
```

The adapter must never receive `AppConfig&` that can be changed by an edit
while the write is in progress. A completion is valid only when its generation
matches `active`; stale completions are ignored. A failed write keeps `baseline`
at the last successful snapshot and retries `active_changes`; any newer pending
changes are merged with those failed domains. A successful write reconciles
`pending` against the newly persisted baseline.

The platform execution shells are intentionally thin:

| Platform | Intent submission | Persistence owner execution |
| --- | --- | --- |
| ESP Arduino | `AppContext::beginConfigEdit()` / `requestSaveConfig()` | `AppContext::updateCoreServices()` calls `takeDue()` and the Preferences adapter |
| ESP IDF | `IdfAppFacadeRuntime::beginConfigEdit()` / `saveConfig()` | `IdfAppFacadeRuntime::updateCoreServices()` calls `takeDue()` and the full-blob adapter |
| Linux | `LinuxAppServices::beginConfigEdit()` / `saveConfig()` | `LinuxAppServices::tick()` calls `takeDue()` and the settings-store adapter |
| nRF52 | `AppConfigChangeSet` facade request | Board-owned deferred settings store; full snapshot is the explicit platform fallback |

No caller performs routine configuration I/O in the callback that submits the
intent. Critical protocol-switch persistence may still use an immediate
platform path where the board contract requires it, but that path is separate
from routine debounce/retry persistence.

Reticulum groups use the same ownership rule without pretending to be fields
owned by the AppConfig writer. Contacts edits a local candidate array, commits
the necessary runtime mirror through `AppConfigEdit`, and submits the candidate
to `platform::ui::reticulum_groups`. The Reticulum group owner performs the
physical SD write from the platform service tick. The legacy `save()` method is
retained only as the physical backend operation and is not a UI submission API.

Mesh peer directory hydration follows the same split on IDF: the facade binds
an empty, valid directory during construction, and the storage maintenance
owner hydrates its immutable blob during `Hydrate`. The IDF startup sequence
waits for the owner readiness event before starting protocol background tasks
or exposing the operational facade, so no consumer can race the directory
commit. This keeps SD file I/O out of facade initialization while preserving
the old invariant that protocol tasks only see a hydrated directory.

The SD maintenance adapter is a different contract from configuration
persistence. It owns hydration, bounded journal work, compaction, and the
repository persistence lease. Its `Persist` operation drains immutable
repository deltas in bounded batches; its `Compact` operation is demand-driven
by reset intents or journal growth and is admitted only after the idle gate is
stable. It does not receive configuration payloads and must not be used as a
generic configuration worker.
