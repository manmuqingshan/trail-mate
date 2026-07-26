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
or locks.

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
3. A save request enqueues a small `AppConfigChangeSet`.
4. Existing zero-argument `saveConfig()` and `requestSaveConfig()` calls are
   supported by detecting changed domains against the current baseline.
5. The async save worker debounces requests and persists the latest queued
   snapshot.
6. The ESP backend maps domains to Preferences/NVS sections and writes only the
   required sections.
7. On success, the in-flight snapshot becomes the new save baseline.
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

Routine business code should call scoped persistence when it knows the changed
domain, for example `requestSaveConfig(AppConfigChangeSet::map())`.

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
