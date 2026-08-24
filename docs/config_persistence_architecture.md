# Configuration Persistence Architecture

Status date: 2026-08-21

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
ESP Arduino, the editable working authority is the SD-card TMS document; the
Preferences/NVS sections are compatibility caches, not a competing authority.
The SD layer owns card access through the existing shared-storage arbitration;
the configuration model never names or bypasses an SPI bus.

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
6. The ESP backend writes the complete bounded TMS projection transactionally
   when a working document is available. Only after that transaction is durable
   are the corresponding Preferences/NVS compatibility keys updated.
7. On success, the in-flight snapshot becomes the new save baseline. Any
   pending snapshot is reconciled against that new baseline before another
   write is allowed. If the latest in-memory value reverted while the old
   write was in flight, the runtime schedules that latest value rather than
   replaying the old payload.
8. A present but invalid TMS file is retained for repair and blocks NVS
   fallback. A missing card or missing document permits the established NVS
   compatibility configuration to be materialized into a new TMS document;
   it does not make NVS supersede a present SD authority.

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
| `AppConfig::reticulumConfig().reticulum_groups` | AppConfig core TMS projection | Each group destination is persisted by the strict `rt.group.*` records in `config.tms`; legacy group storage is migration input only |
| `AppConfig::ble_enabled` | AppConfig core TMS projection | Persisted by the strict `device.ble_enabled` record and applied by the BLE runtime after configuration selection |
| `/trailmate/config.tms` working configuration | SD-first configuration owner | Complete persisted `AppConfig`, NVS-backed settings, saved Wi-Fi profiles, and supported cellular settings; NVS is a compatibility cache and a fallback only when the card or file is absent |

Adding a field to `AppConfig` does not make it persistent automatically. The
field must be assigned to a domain, included in change detection, and handled
by the owning adapter, or explicitly documented as runtime-only.

## SD Working Configuration (ESP)

`/trailmate/config.tms` is the only editable working configuration. It is a
bounded, line-streamed TMS document—not JSON and not a whole-file object. On
startup, a valid document is completely validated before it changes `AppConfig`
or any independent settings owner, then it is mirrored to NVS as a compatibility
cache. NVS is read only when the card is absent or the file is absent. A present
but invalid document is retained for repair and blocks an NVS fallback, so a
typo can never appear to be silently ignored.

| Concern | Active owner | Working-document behavior |
| --- | --- | --- |
| `AppConfig` domains | SD working document with Preferences/NVS cache | All persisted fields are read from a valid `config.tms` before NVS. A present invalid file is not replaced from NVS. |
| Device and presentation preferences outside `AppConfig` | `settings_store` / NVS mirror | The full supported set is written and validated under typed `ui.*`, `chat.*`, `debug.*`, and `power.*` keys. |
| Saved Wi-Fi credentials | Wi-Fi runtime / NVS mirror | An ordered exact set of zero through ten SSID/password profiles is validated as a whole before replacement. |
| A7682E settings | Cellular runtime / NVS mirror | The complete supported cellular block is emitted only on the A7682E product variant, and is mandatory and validated as a whole there before application. |
| Reticulum network/LXMF interface configuration | Reticulum network-config owner projected into TMS | The complete bounded `rt.net.*` block is validated with the working document. The older `/trailmate/reticulum/config.json` root is imported once for migration and retired only after a durable `TMSET7` write. |
| Reticulum group destinations | `AppConfig` core TMS projection | Group destinations are part of the strict core projection. Earlier group files are migration inputs only and are retired after the new document is durable. |

Every supported NVS-backed settings mutation crosses one synchronous durable
commit boundary. A multi-key owner (Wi-Fi or cellular) is coalesced only until
its scope exits, then the current call writes the SD document. There is no
pending flag, retry timer, foreground-loop service, or NVS metadata that can
make NVS supersede an existing valid SD file.

Writes stream a complete `TMSET7` document to `config.tms.new`, parse and
canonicalize it, record its tiny SD transaction digest in `config.tms.txn`, move
the previous document to `config.tms.bak`, and then promote the new file. If
power is lost while the primary is absent, boot restores the validated `.new`
candidate that matches the transaction, otherwise the validated `.bak` file.
The backup is a recovery generation, not a second configuration authority.

`TMSET2` through `TMSET6` remain migration inputs. `TMSET6` was an
unreleased transitional dialect that used `rt.net.*`, legacy group
`destination` records, and no BLE block; it is accepted only when that exact
complete layout validates, then is immediately rewritten. New writes always
emit strict `TMSET7`: every expected record must appear exactly once in
canonical order and an unknown key is rejected. This makes an SD edit failure
visible instead of leaving an old value in effect. `TMSET7` additionally owns
the Reticulum network/LXMF block, so new firmware does not recreate a separate
editable Reticulum JSON root.

### TMS memory budget

TMS is deliberately a streaming format. Each read and write share one 384-byte
line buffer in BSS; the document is never retained in RAM. The bounded settings
projection—needed to validate all ten Wi-Fi profiles, cellular settings, and
the Reticulum network block—lives in PSRAM only while a document is decoded or
emitted. Both first materialization and an existing-document rewrite pass
through that same projection and one record writer; a setting candidate is
merged once before output rather than creating a second field list. The
temporary 4,556-byte `AppConfig` validation object also prefers PSRAM and is
released immediately after the decision. No ESP task stack receives a whole
configuration object or JSON DOM.

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
| ESP Arduino | `AppContext::beginConfigEdit()` / `requestSaveConfig()` | `AppContext::updateCoreServices()` calls `takeDue()` and the SD-first TMS adapter, then mirrors its NVS compatibility cache |
| ESP IDF | `IdfAppFacadeRuntime::beginConfigEdit()` / `saveConfig()` | `IdfAppFacadeRuntime::updateCoreServices()` calls `takeDue()` and the full-blob adapter |
| Linux | `LinuxAppServices::beginConfigEdit()` / `saveConfig()` | `LinuxAppServices::tick()` calls `takeDue()` and the settings-store adapter |
| nRF52 | `AppConfigChangeSet` facade request | Board-owned deferred settings store; full snapshot is the explicit platform fallback |

`AppConfig` edits do not perform routine storage I/O in the callback that
submits the intent; their debounced snapshots are owned by the application
persistence runtime. Independently owned typed settings are deliberately
different: they synchronously commit the complete TMS document before updating
their NVS cache or applying the new runtime value. This is the same SD-first
authority transaction, not a second background worker. Critical
protocol-switch persistence may still use an immediate platform path where the
board contract requires it.

Reticulum group destinations are strict fields in the `AppConfig` TMS core
projection. A contacts flow edits its candidate through `AppConfigEdit` and
submits the appropriate change domain; it must not call a separate Reticulum
group storage owner. Earlier dedicated group files are migration input only and
are retired after the canonical `TMSET7` document is durable.

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
