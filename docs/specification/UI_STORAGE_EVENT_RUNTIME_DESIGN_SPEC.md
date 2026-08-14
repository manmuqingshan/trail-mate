# UI / Storage / Event Runtime Design Spec

Status date: 2026-06-17

This document defines the design baseline for removing UI stalls caused by
blocking storage, synchronous persistence, and page-owned runtime side
effects.

It complements `RUNTIME_CONCURRENCY_SPEC.md`. The concurrency spec states the
rules. This document explains the design that makes those rules implementable
and testable.

The physical shared-device mechanism is a technical concern owned by
`docs/specs/SPI_BUS_ARCHITECTURE_SPEC.md`. This document defines only UI/runtime ownership
and semantic storage behavior.

There is no generic `sys::runtime::PersistenceRuntime` in production. Storage
responsibilities are deliberately split:

- `StorageMaintenanceRuntime` owns SD-backed hydration, compaction, startup
  gating, and maintenance retry state.
- `ConfigPersistenceRuntime` owns application-config dirty tracking, debounce,
  immutable payloads, generations, critical flush, and config retry state.
- Domain stores own their domain state and domain-specific snapshot policy.

These owners use semantic storage adapters. They do not expose SPI tokens,
chip-select lines, filesystem sessions, mutexes, or RTOS task handles to
callers, and neither runtime is a universal storage service.

## Problem Statement

Trail Mate currently has several paths where renderer-owned code, runtime
services, and platform device operations can enter the same slow resource path:

- LVGL timers and page callbacks can trigger device I/O.
- Map tile loading can perform file lookup, image object setup, and decode work
  from the UI owner context.
- GPS track recording can hold recorder state while it opens, writes, flushes,
  and closes files.
- Node and contact persistence can be reached from event dispatch paths.
- Feedback notices, protocol completion, and UI mutation can be coupled to the
  page that happened to start an operation.

The failure mode is not only a crash. A target can keep GPS, radio, timers, and
sleep tasks alive while the UI owner task is blocked or starved. When that
happens, touch input, key input, page navigation, display refresh, and wake
handling appear frozen even though background logs continue.

The design goal is therefore:

```text
UI owner context never waits for device I/O, filesystem, decode, protocol
completion, or persistence.
```

After the map freeze investigation, this goal is tightened:

```text
Moving work off the UI owner context is necessary but not sufficient.
No device service may starve frame presentation or input-visible feedback in a
way that prevents wake rendering or page navigation.
```

Responsiveness is protected by ownership of time-critical resources, not only
by moving slow calls to another task.

## Core Distinctions

### UI Owner Context

The UI owner context is the only context allowed to mutate concrete renderer
objects:

- LVGL `lv_obj_*`
- GTK widgets
- terminal renderer output buffers
- target-specific display widgets

It owns rendering and input response. It does not own storage transactions,
protocol business rules, or persistence durability.

### Runtime Intent

A runtime intent is a user or service request expressed without platform IO:

- load these visible map tiles
- start a new track
- append this GPS point
- list available tracks
- save node store changes
- show transient feedback
- send this chat message

Intent expresses what should happen. It does not say how long to wait for SD,
which SPI mutex to take, which LVGL file callback to invoke, or which hardware
adapter performs IO.

### Command

A command is the queued, executable form of an intent. Commands are the boundary
between caller responsiveness and slow work. A caller submits a command and
returns immediately.

Commands must carry enough metadata for cancellation, deduplication, priority,
and diagnostics:

```text
command_id
kind
created_at
origin
priority
generation
deadline
dedupe_key
cancel_policy
```

### Worker / Active Object

A worker owns slow execution. It serializes or schedules commands and invokes
platform adapters. The worker is an Active Object: callers do not execute its
methods directly; they enqueue work.

Examples:

- `MapTileWorker`
- `TrackStorageWorker`
- `StorageMaintenanceRuntime` for SD-backed maintenance
- `ConfigPersistenceRuntime` for application-config persistence
- `ProtocolRuntimeWorker`
- `FeedbackDispatchWorker` when a target needs one

### Event

An event is the only way slow work reports completion, failure, cancellation, or
state change back to interested runtimes and UI projections.

Events must be safe to publish from non-UI contexts. Events that require UI
mutation must be consumed by the UI owner context before touching renderer
objects.

### Platform Adapter

A platform adapter wraps a technical API:

- SD file open/read/write/flush
- semantic device I/O
- LVGL image descriptor setup
- nRF flash write
- GTK idle invocation
- radio TX

Adapters must not own business rules, retry policy, tile priority, feedback
eligibility, chat delivery semantics, or tracker state transitions.

## Pattern Decision

The design uses a small set of patterns with explicit responsibilities.

| Pattern | Primary use | Why it is required | Must not become |
| --- | --- | --- | --- |
| Active Object | Storage, tile, track, persistence, protocol workers | Moves slow work behind queues so callers return immediately | A hidden synchronous service |
| Command | Runtime intents queued to workers | Separates request creation from execution and enables cancellation, priority, diagnostics | Wire-format payloads or platform calls |
| Observer / Event Bus | Completion, failure, state, feedback, UI effects | Reports asynchronous outcomes without coupling to the current page | Direct renderer mutation from background contexts |
| State | Track recording, tile loading, storage health, chat send lifecycle | Makes cross-time behavior explicit instead of encoded in locks or call stacks | Page-local boolean flags |
| Strategy | Device I/O, tile loading, flush, persistence, retry policies | Keeps environment and mode differences explicit and swappable | Scattered `if target` branches |
| Bridge | Runtime rule side to platform execution side | Keeps shared business rules single-source while allowing ESP/nRF/Linux backends | Another adapter with business decisions |
| Adapter | Wrap platform APIs | Contains technical integration details | A second business implementation |
| Device service | Technical I/O boundary | Replaces ad-hoc hardware calls with observable semantic results | A new god object for product behavior |
| Unit of Work | Track flush, node/contact save, config persistence | Batches durable writes and reduces device I/O calls | An unbounded memory buffer |
| Circuit Breaker / Backpressure | Slow or failing device I/O/storage | Degrades features without freezing UI | Silent data loss |
| Visitor / Effect Apply | Optional UI-effect application in owner context | Centralizes renderer-side effect application | Business decision engine |

The primary axis is:

```text
Command -> Active Object -> Event -> State
```

`Strategy`, `Bridge`, and `Adapter` prevent the primary axis from drifting back
into target-specific duplicate implementations. `Mediator/Arbiter`,
`Unit of Work`, and `Backpressure` keep the system responsive on real hardware.

## Skeleton-First Implementation Protocol

Implementation must start with architecture skeletons before behavior is moved.
This is a hard rule for this refactor because the bug class is caused by weak
boundaries, not by one missing timeout.

The first code slice for a runtime area must add:

- value objects for intents, commands, events, state, priority, generation, and
  diagnostics
- interfaces for queues, workers, device services, platform adapters,
  event sinks, and UI-effect application
- fake implementations for clock, command queue, event bus, storage backend,
  bus arbiter, worker completion, and feedback presenter
- deterministic tests that simulate at least one success path, one timeout, one
  cancellation, and one stale completion
- explicit legacy containment comments or adapters for the old implementation
  that will be burned down in the same migration slice

No active-path migration may begin until the UML coverage gate below is
complete for that runtime area. The UML is a design artifact, not a decorative
diagram. It must be detailed enough to identify the business owner, runtime
owner, worker owner, platform adapter, event path, state transitions, testing
fakes, and legacy deletion condition.

The skeleton may return placeholder results, but it must compile and expose the
final ownership shape. Active product paths must not be rewired to a skeleton
until the relevant behavior and simulation tests are present.

After the skeleton exists, behavior moves one vertical slice at a time:

```text
write skeleton
add simulation test
move one runtime flow
delete or deactivate old direct path
verify no active dual implementation remains
repeat
```

At no point may an active feature keep two business-rule owners. A temporary
adapter may bridge old data into the new runtime, but the adapter must not
decide policy.

## UML Design Skeleton

The code skeleton must preserve the following ownership shape. Names may be
adapted to local module style, but any rename must keep the same roles and
relationships.

```mermaid
classDiagram
  class RuntimeFacade {
    +submit(intent)
    +tick(now_ms)
    +drainEvents()
  }

  class RuntimeIntent {
    <<value object>>
    +kind
    +origin
  }

  class RuntimeCommand {
    <<command>>
    +command_id
    +kind
    +priority
    +generation
    +deadline_ms
    +dedupe_key
    +cancel_policy
  }

  class RuntimeEvent {
    <<event>>
    +event_id
    +kind
    +command_id
    +timestamp_ms
  }

  class RuntimeState {
    <<state>>
    +status
    +last_error
  }

  class ICommandQueue {
    <<interface>>
    +enqueue(command)
    +cancel(dedupe_key)
    +popReady(now_ms)
  }

  class IActiveWorker {
    <<interface>>
    +tick(now_ms)
    +submit(command)
  }

  class IEventSink {
    <<interface>>
    +publish(event)
  }

  class IPlatformStorageAdapter {
    <<interface>>
    +read(request)
    +write(request)
    +list(request)
    +flush(handle)
  }

  class IUiEffectSink {
    <<interface>>
    +apply(effect)
  }

  class RuntimePolicyStrategy {
    <<strategy>>
    +selectPriority(intent)
    +selectRetry(command, result)
  }

  RuntimeFacade --> RuntimeIntent
  RuntimeFacade --> RuntimeCommand
  RuntimeFacade o-- ICommandQueue
  RuntimeFacade o-- IEventSink
  RuntimeFacade o-- RuntimeState
  RuntimeFacade o-- RuntimePolicyStrategy
  IActiveWorker --> RuntimeCommand
  IActiveWorker --> RuntimeEvent
  IActiveWorker o-- IPlatformStorageAdapter
  IEventSink --> RuntimeEvent
  IUiEffectSink --> RuntimeEvent
```

The important dependency direction is from stable runtime abstractions toward
ports. Platform adapters implement ports. Pages and widgets depend on facades or
presentation actions, not on adapters, workers, or concrete storage APIs.

## UML Component Boundary

```mermaid
flowchart TB
  subgraph Presentation["Presentation / UI Owner"]
    Page["Page / Widget"]
    UiDrain["UI Event Drain"]
    Renderer["LVGL / GTK / Mono Renderer"]
  end

  subgraph Runtime["Shared Runtime"]
    Facade["Runtime Facade"]
    State["State Machine"]
    Policies["Strategies"]
    Commands["Command Queue"]
    Events["Event Bus"]
  end

  subgraph Workers["Active Objects"]
    TileWorker["MapTileWorker"]
    TrackWorker["TrackStorageWorker"]
    ConfigPersist["ConfigPersistenceRuntime"]
    Maintenance["StorageMaintenanceRuntime"]
  end

  subgraph Platform["Platform Adapters"]
    DeviceIo["Device I/O Services"]
    Storage["Storage Adapter"]
    Decode["Decode Adapter"]
  end

  Page --> Facade
  Facade --> Commands
  Facade --> State
  Facade --> Policies
  Commands --> TileWorker
  Commands --> TrackWorker
  Commands --> ConfigPersist
  Commands --> Maintenance
  TileWorker --> DeviceIo
  TrackWorker --> DeviceIo
  ConfigPersist --> DeviceIo
  Maintenance --> DeviceIo
  DeviceIo --> Storage
  TileWorker --> Decode
  TileWorker --> Events
  TrackWorker --> Events
  ConfigPersist --> Events
  Maintenance --> Events
  Events --> State
  Events --> UiDrain
  UiDrain --> Renderer
```

Forbidden reverse dependencies:

- platform adapter to page/widget
- worker to concrete renderer
- page/widget to storage adapter
- page/widget to device I/O implementation
- adapter to product policy
- UI event drain to blocking worker execution

## UML Coverage Gate

Before coding an active-path migration, the design must include the following
views for the runtime area being changed:

| UML view | Required question it answers | Minimum content |
| --- | --- | --- |
| Business context | Which product actors and features participate? | user action, GPS/radio input, storage media, power/sleep, feedback |
| Bounded context | Which layer owns each concept? | presentation, runtime, worker, platform adapter, simulator |
| Static class model | What is the code skeleton shape? | intents, commands, events, state, policies, ports, adapters |
| Component dependency | Which dependencies are legal? | page to facade, facade to queue/state, worker to ports, event to UI drain |
| Thread/deployment | Which task/thread owns each operation? | UI owner, GPS task, radio task, worker tasks, storage/bus owner |
| State model | What long-running states exist? | command, storage health, tile, track, persistence, feedback |
| Sequence model | How does one scenario flow end-to-end? | success, timeout, cancellation, stale completion, page destruction |
| Simulation model | How is it tested without hardware? | fake clock, fake queue, fake storage, fake arbiter, fake UI drain |
| Legacy burn-down map | What old path is removed? | old active caller, new owner, deletion condition, guard test |

If one of these views is missing, the implementation is still in design debt and
must not proceed as a production migration.

## UML Business Context

This view shows the full product context for the UI/storage/event refactor.

```mermaid
flowchart LR
  User["User touch/key input"] --> UI["Presentation surfaces"]
  Gps["GPS receiver"] --> GpsRuntime["GPS runtime"]
  RadioPeer["Nearby radio peer"] --> Protocol["Protocol runtime"]
  Power["Power/sleep manager"] --> UI
  StorageMedia["SD / flash storage"] <--> StorageRuntime["Storage runtime"]

  UI --> MapUseCase["Map viewing / panning"]
  UI --> TrackerUseCase["Tracker start / stop / list"]
  UI --> ChatUseCase["Chat send / resend"]
  UI --> ContactsUseCase["Contacts / node detail"]

  GpsRuntime --> TrackerUseCase
  Protocol --> ChatUseCase
  Protocol --> ContactsUseCase
  Protocol --> MapUseCase

  MapUseCase --> Runtime["Shared runtime layer"]
  TrackerUseCase --> Runtime
  ChatUseCase --> Runtime
  ContactsUseCase --> Runtime

  Runtime --> Workers["Async workers"]
  Workers --> StorageRuntime
  Runtime --> Feedback["Feedback runtime"]
  Feedback --> UI
```

Business meaning:

- The user, GPS receiver, radio peer, storage media, and power manager are
  external stimuli.
- UI surfaces initiate product actions but do not own slow execution.
- Shared runtime owns business interpretation and state.
- Workers own slow operations.
- Feedback is global runtime output, not page-local rendering.

## UML Bounded Contexts

```mermaid
flowchart TB
  subgraph Presentation["Presentation Context"]
    Pages["Pages / widgets"]
    UiActions["Presentation actions"]
    UiDrain["UI event drain"]
    Renderer["Concrete renderer"]
  end

  subgraph RuntimeContext["Runtime Context"]
    Facades["Use-case facades"]
    StateMachines["State machines"]
    Policies["Policy strategies"]
    Events["Runtime events"]
    Commands["Runtime commands"]
  end

  subgraph WorkerContext["Worker Context"]
    TileWorker["Tile worker"]
    TrackWorker["Track worker"]
    ConfigPersist["Config persistence runtime"]
    Maintenance["Storage maintenance runtime"]
    ProtocolWorker["Protocol worker"]
  end

  subgraph PlatformContext["Platform Adapter Context"]
    StorageAdapter["Storage adapter"]
    DeviceIo["Device I/O adapter"]
    DecodeAdapter["Decode adapter"]
    ClockAdapter["Clock adapter"]
    RadioAdapter["Radio adapter"]
  end

  subgraph TestContext["Simulation Context"]
    FakeClock["Fake clock"]
    FakeStorage["Fake storage"]
    FakeDeviceIo["Fake device I/O"]
    FakeUi["Fake UI owner"]
    FakeEvents["Fake event bus"]
  end

  Pages --> UiActions
  UiActions --> Facades
  Facades --> Commands
  Facades --> StateMachines
  Facades --> Policies
  Commands --> TileWorker
  Commands --> TrackWorker
  Commands --> ConfigPersist
  Commands --> Maintenance
  Commands --> ProtocolWorker
  TileWorker --> StorageAdapter
  TrackWorker --> StorageAdapter
  ConfigPersist --> StorageAdapter
  Maintenance --> StorageAdapter
  TileWorker --> DecodeAdapter
  ProtocolWorker --> RadioAdapter
  WorkerContext --> Events
  Events --> StateMachines
  Events --> UiDrain
  UiDrain --> Renderer
  TestContext -.implements.-> PlatformContext
```

Boundary rules:

- Presentation may depend on runtime facades, not workers or adapters.
- Runtime may depend on ports and value objects, not concrete platform APIs.
- Workers may depend on platform ports, not pages or renderer objects.
- Platform adapters may depend on platform SDKs, not product policy.
- Simulation implements the same ports as platform adapters.

## UML Thread / Deployment View

```mermaid
flowchart LR
  subgraph UiTask["UI owner task/thread"]
    Input["Input processing"]
    UiTick["UI tick / lv_timer_handler / GTK loop"]
    UiDrain["Runtime UI event drain"]
  end

  subgraph GpsTask["GPS task"]
    UartRx["UART RX"]
    Parse["NMEA parse"]
    PointIntent["Append point intent"]
  end

  subgraph RadioTask["Radio task"]
    RadioRx["RX decode"]
    ProtocolIntent["Protocol incoming intent"]
  end

  subgraph WorkerTasks["Worker tasks"]
    CommandPump["Command pump"]
    TileWorker["Tile worker"]
    TrackWorker["Track worker"]
    ConfigPersist["Config persistence runtime"]
    Maintenance["Storage maintenance runtime"]
  end

  subgraph ResourceOwner["Resource owner"]
    DeviceIo["Device I/O service"]
    Storage["SD / flash adapter"]
    Decode["Decode adapter"]
  end

  Input --> UiTick
  UiTick --> UiDrain
  UiTick --> CommandPump
  UartRx --> Parse
  Parse --> PointIntent
  PointIntent --> CommandPump
  RadioRx --> ProtocolIntent
  ProtocolIntent --> CommandPump
  CommandPump --> TileWorker
  CommandPump --> TrackWorker
  CommandPump --> ConfigPersist
  CommandPump --> Maintenance
  TileWorker --> DeviceIo
  TrackWorker --> DeviceIo
  ConfigPersist --> DeviceIo
  Maintenance --> DeviceIo
  DeviceIo --> Storage
  TileWorker --> Decode
  TileWorker --> UiDrain
  TrackWorker --> UiDrain
  PersistWorker --> UiDrain
```

The UI owner task is never deployed as the resource owner. If a target has only
one physical thread, the same ownership model still applies cooperatively:
slow work must be incremental, budgeted, and represented as commands/events
rather than blocking UI execution.

The storage runtimes are not the device owner. They own operation state and
call semantic device services. Those services own the physical transaction
mechanism described in `docs/specs/SPI_BUS_ARCHITECTURE_SPEC.md`.

## Device I/O Boundary

Workers call semantic device services for storage, display, and radio work.
Those services own all physical transactions. The runtime layer receives only
semantic results. The complete shared-device mechanism is specified only in
`docs/specs/SPI_BUS_ARCHITECTURE_SPEC.md`.

### Long-Running Progress Overlay Boundary

`busy_overlay` is the single visual component for modal long-running progress.
`foreground_operation_overlay` is the only ownership boundary for code that
publishes user-visible long-running operation state. `ProgressOverlayPresenter`
is only the renderer adapter behind that coordinator; page code and runtime
status sync code must not own a presenter directly.

Foreground operation snapshots are fixed-size UI projections. They describe:

- operation slot (`FirmwareUpdate`, `PackageInstall`, `I18nFontLoad`,
  `RouteImage`, or `SettingsAction`)
- presentation policy (`Hidden`, `PageOnly`, `Overlay`, or
  `OverlayImmediate`)
- priority
- title, detail, optional result, and optional `progress_percent`

They are not task executors, storage transactions, JSON payloads, or business
state owners. The underlying runtimes continue to own their real state.

The foreground operation runtime is used by:

- external font loading, where the presenter flushes the modal before the
  device font service begins its foreground load
- package/language-pack installation, where download status may publish
  `progress_percent` while SD writes stay in the storage runtime/file adapter
- firmware update, where OTA status publishes `progress_percent` and the
  settings page only presents the status
- route image download/cache, where user-triggered KML image download publishes
  a foreground operation while route preview and GPS map pages keep their own
  page-local route/image context widgets

The illegal shortcut is to treat every progress bar as a device transaction.
Progress UI describes user-visible operation state; device services own
physical storage, display, and radio work.

Route image tasks are explicitly split by presentation semantics:

- `UserVisible` route image tasks may publish a global overlay.
- `PageOnly` route image tasks may update page-local status such as route image
  strips, counters, and map loader pause state, but must not steal the global
  overlay.
- `Hidden` route image tasks are status-only.

Route image HTTP downloads use `wifi_access` with the `RouteStorage` client.
They must not make an HTTP transfer a storage transaction. Cache/build stages
call the storage service for bounded semantic operations; they must not own
device transaction state.

### Device I/O Delegation

UI/runtime specifications describe commands, worker ownership, semantic
results, and event delivery. The concrete display, radio, and storage
services perform physical I/O behind the device boundary. Their shared-device
transaction rules are defined only in
`docs/specs/SPI_BUS_ARCHITECTURE_SPEC.md`.

## UML Map Tile Class Model

```mermaid
classDiagram
  class MapTileRuntime {
    +requestVisibleTiles(plan)
    +cancelGeneration(generation)
    +handle(event)
  }

  class MapViewportPlan {
    +generation
    +tiles
    +interaction_mode
  }

  class LoadTileCommand {
    +command_id
    +tile_ref
    +generation
    +priority
    +deadline_ms
  }

  class MapTileWorker {
    +submit(command)
    +tick(now_ms)
  }

  class IMapTileSource {
    <<interface>>
    +read(tile_ref)
  }

  class IMapTileDecoder {
    <<interface>>
    +decode(payload)
  }

  class MapTileEvent {
    +kind
    +tile_ref
    +generation
    +image_ref
    +error
  }

  class MapTileStateMachine {
    +transition(event)
    +snapshot()
  }

  class IMapTileUiSink {
    <<interface>>
    +applyTile(event)
  }

  MapTileRuntime --> MapViewportPlan
  MapTileRuntime --> LoadTileCommand
  MapTileRuntime o-- MapTileStateMachine
  MapTileWorker --> LoadTileCommand
  MapTileWorker o-- IMapTileSource
  MapTileWorker o-- IMapTileDecoder
  MapTileWorker --> MapTileEvent
  MapTileRuntime --> MapTileEvent
  IMapTileUiSink --> MapTileEvent
```

## UML Track Recording Class Model

```mermaid
classDiagram
  class TrackRuntime {
    +startNewTrack()
    +stopTrack()
    +appendPoint(point)
    +listTracks()
    +handle(event)
  }

  class TrackCommand {
    +command_id
    +kind
    +track_id
    +point_batch
    +deadline_ms
  }

  class TrackPointBuffer {
    +append(point)
    +takeBatch(policy)
    +size()
  }

  class TrackFlushPolicy {
    <<strategy>>
    +shouldFlush(buffer, now_ms)
    +isCritical(command)
  }

  class TrackStateMachine {
    +state()
    +transition(event)
  }

  class TrackStorageWorker {
    +submit(command)
    +tick(now_ms)
  }

  class ITrackFileAdapter {
    <<interface>>
    +open(track_id)
    +append(points)
    +flush(track_id)
    +close(track_id)
    +list()
  }

  class TrackEvent {
    +kind
    +command_id
    +track_id
    +error
  }

  TrackRuntime --> TrackCommand
  TrackRuntime o-- TrackPointBuffer
  TrackRuntime o-- TrackStateMachine
  TrackRuntime o-- TrackFlushPolicy
  TrackStorageWorker --> TrackCommand
  TrackStorageWorker o-- ITrackFileAdapter
  TrackStorageWorker --> TrackEvent
  TrackRuntime --> TrackEvent
```

## UML Configuration Persistence Class Model

```mermaid
classDiagram
  class ConfigPersistenceRuntime {
    +initialize(baseline)
    +submit(desired, changes, now_ms, urgency)
    +takeDue(now_ms, work)
    +complete(generation, result, now_ms)
  }

  class AppConfigEdit {
    +config()
    +commit(change_set)
    +cancel()
  }

  class AppConfigChangeSet {
    +domains
    +generation
  }

  class ConfigPersistenceWork {
    +snapshot
    +changes
    +generation
  }

  class PersistenceGeneration {
    <<value>>
    +value
  }

  class PersistenceResultKind {
    <<enumeration>>
    Completed
    InProgress
    StateBusy
    DeviceUnavailable
    RetryLater
    IoError
    Cancelled
    StaleGeneration
  }

  class ISemanticStorageAdapter {
    <<interface>>
    +begin(operation, generation)
    +step(operation, generation, budget)
    +cancelAtStepBoundary(operation, generation)
  }

  ConfigPersistenceRuntime o-- ConfigPersistenceWork
  ConfigPersistenceRuntime --> AppConfigChangeSet
  ConfigPersistenceWork --> PersistenceGeneration
  ConfigPersistenceRuntime --> PersistenceResultKind
  AppConfigEdit --> AppConfigChangeSet
```

`ConfigPersistenceRuntime` is the only owner of configuration persistence
state. `AppConfigEdit` creates an immutable intent from a caller-owned edit;
the runtime snapshots the authoritative configuration and owns the pending and
in-flight payloads. Domain stores such as contacts, peers, maps, and tracks do
not enter this model merely because they also use SD or flash.

## UML Feedback Class Model

```mermaid
classDiagram
  class FeedbackRuntime {
    +post(intent)
    +handle(event)
    +drainToUi()
  }

  class NoticeIntent {
    +notice_id
    +category
    +severity
    +message
    +duration_ms
    +dedupe_key
  }

  class FeedbackPolicy {
    <<strategy>>
    +shouldShow(intent)
    +dedupe(intent)
    +duration(intent)
  }

  class FeedbackEvent {
    +kind
    +notice_id
    +error
  }

  class IFeedbackPresenter {
    <<interface>>
    +present(intent)
  }

  class FeedbackQueue {
    +enqueue(intent)
    +popReady(now_ms)
    +dedupe(intent)
  }

  FeedbackRuntime --> NoticeIntent
  FeedbackRuntime o-- FeedbackQueue
  FeedbackRuntime o-- FeedbackPolicy
  FeedbackRuntime o-- IFeedbackPresenter
  FeedbackRuntime --> FeedbackEvent
```

## UML State Models

### Command Lifecycle

```mermaid
stateDiagram-v2
  [*] --> Created
  Created --> Queued
  Queued --> Coalesced: duplicate accepted
  Queued --> Cancelled: stale or user cancelled
  Queued --> Running
  Running --> WaitingForResource
  WaitingForResource --> Running: acquired
  WaitingForResource --> Retrying: timed out
  Running --> Completed
  Running --> Failed
  Retrying --> Queued: retry allowed
  Retrying --> Failed: retry exhausted
  Completed --> [*]
  Failed --> [*]
  Cancelled --> [*]
  Coalesced --> [*]
```

### Storage Health

```mermaid
stateDiagram-v2
  [*] --> Healthy
  Healthy --> Slow: wait or hold threshold exceeded
  Slow --> Healthy: recovery window clean
  Slow --> Degraded: repeated timeout
  Degraded --> Unavailable: open/read/write unavailable
  Unavailable --> Recovering: recovery probe scheduled
  Recovering --> Healthy: probe succeeds
  Recovering --> Unavailable: probe fails
```

### Map Tile

```mermaid
stateDiagram-v2
  [*] --> Missing
  Missing --> Requested
  Requested --> Loading
  Requested --> Cancelled: stale generation
  Loading --> Ready
  Loading --> Failed
  Loading --> Cancelled: stale generation
  Ready --> BoundToRenderer
  Ready --> Evicted
  Failed --> Requested: retry
  BoundToRenderer --> Evicted
```

### Track Recorder

```mermaid
stateDiagram-v2
  [*] --> Idle
  Idle --> Starting
  Starting --> Recording
  Starting --> Error
  Recording --> Flushing
  Flushing --> Recording
  Recording --> Stopping
  Stopping --> Stopped
  Stopped --> Idle
  Recording --> Error
  Flushing --> Error
  Stopping --> Error
  Error --> Recovering
  Recovering --> Recording
  Recovering --> Stopped
```

### Persistence Save

```mermaid
stateDiagram-v2
  [*] --> Clean
  Clean --> Dirty: markDirty
  Dirty --> SavePending: debounce elapsed
  SavePending --> Saving
  Saving --> Clean: save ok
  Saving --> Dirty: save failed, retry allowed
  Saving --> Degraded: repeated failure
  Degraded --> SavePending: recovery retry
```

### Feedback Notice

```mermaid
stateDiagram-v2
  [*] --> Posted
  Posted --> Deduped: same dedupe key
  Posted --> Queued
  Queued --> Presenting
  Presenting --> Visible
  Presenting --> Dropped: presenter unavailable
  Visible --> Expiring
  Expiring --> Completed
  Deduped --> [*]
  Dropped --> [*]
  Completed --> [*]
```

## UML Interaction Set

These sequences are required design coverage. They may be split into separate
documents during implementation, but each active-path migration must keep the
relevant sequence current.

### Mixed Map Drag and GPS Track Burst

```mermaid
sequenceDiagram
  participant UI as UI Owner
  participant Map as MapTileRuntime
  participant GPS as GPS Task
  participant Track as TrackRuntime
  participant Queue as Command Queue
  participant Worker as Workers
  participant DeviceIo as Device I/O service
  participant Events as Event Bus

  UI->>Map: requestVisibleTiles(generation=42, interactive)
  Map->>Queue: enqueue LoadTileCommand priority=interactive
  GPS->>Track: appendPoint(point)
  Track->>Queue: enqueue/buffer AppendTrackPointCommand
  Queue->>Worker: dispatch tile before idle/background work
  Worker->>DeviceIo: execute tile read
  DeviceIo-->>Worker: ready or retry later
  Worker->>Events: MapTileReady(generation=42)
  Queue->>Worker: dispatch batched track write
  Worker->>DeviceIo: execute track write
  DeviceIo-->>Worker: completed or deferred
  Worker->>Events: TrackFlushSucceeded or Backpressure
  Events->>UI: drain UI-safe events
```

### NodeInfo Storm With Domain-Owned Maintenance

```mermaid
sequenceDiagram
  participant Radio as Radio Task
  participant Runtime as Protocol Runtime
  participant Contacts as Contact/Node Runtime
  participant StoreOwner as Contact/Node Store Owner
  participant Maintenance as StorageMaintenanceRuntime
  participant Adapter as Semantic Storage Adapter
  participant UI as UI Owner

  loop many packets
    Radio->>Runtime: incoming NodeInfo/Position
    Runtime->>Contacts: update in-memory projection
    Contacts->>StoreOwner: apply domain update
    Contacts->>UI: publish projection update
  end
  StoreOwner->>StoreOwner: coalesce domain changes
  StoreOwner->>Maintenance: request maintenance when policy is due
  Maintenance->>Adapter: hydrate or compact store
  Adapter-->>Maintenance: result(generation)
  Maintenance-->>StoreOwner: completion or retry state
  StoreOwner->>UI: optional storage feedback event
```

### Chat Send Result While Page Changes

```mermaid
sequenceDiagram
  participant ChatPage as Conversation Page
  participant Chat as Chat Runtime
  participant Protocol as Protocol Facade
  participant Feedback as Feedback Runtime
  participant ContactsPage as Contacts Page
  participant UI as UI Owner

  ChatPage->>Chat: sendText(peer, text)
  Chat->>Protocol: sendText intent
  Protocol-->>Chat: message_id pending
  ChatPage->>ContactsPage: navigate away
  Protocol->>Chat: SendSucceeded or SendFailed
  Chat->>Feedback: post delivery notice intent
  Chat->>UI: publish message state update
  UI->>Feedback: drain notice on current UI owner context
```

### Sleep / Wake While Storage Command Is Pending

```mermaid
sequenceDiagram
  participant UI as UI Owner
  participant Power as Power Manager
  participant Runtime as Runtime Facade
  participant Worker as Storage Worker
  participant DeviceIo as Device I/O service

  UI->>Runtime: submit command
  Runtime->>Worker: enqueue slow storage work
  Worker->>DeviceIo: execute storage command
  Power->>UI: sleep timeout event
  UI->>UI: render sleep transition
  Power->>UI: wake input
  UI->>UI: process wake input
  DeviceIo-->>Worker: storage complete later
  Worker->>UI: publish completion event
```

## UML Simulation Harness

```mermaid
classDiagram
  class RuntimeHarness {
    +advance(ms)
    +drainUi()
    +runUntilIdle()
  }

  class FakeClock {
    +now()
    +advance(ms)
  }

  class FakeCommandQueue {
    +enqueue(command)
    +popReady(now_ms)
    +size()
  }

  class FakeEventBus {
    +publish(event)
    +drain()
  }

  class FakeSemanticStorageAdapter {
    +scriptDelay(operation, ms)
    +scriptFailure(operation, error)
    +execute(operation)
    +result()
  }

  class FakeDeviceIo {
    +scriptResult(result)
    +execute(request)
    +diagnostics()
  }

  class FakeUiOwner {
    +assertNoBlockingCalls()
    +apply(effect)
    +tick()
  }

  class FakeFeedbackPresenter {
    +present(intent)
    +captured()
  }

  RuntimeHarness o-- FakeClock
  RuntimeHarness o-- FakeCommandQueue
  RuntimeHarness o-- FakeEventBus
  RuntimeHarness o-- FakeSemanticStorageAdapter
  RuntimeHarness o-- FakeDeviceIo
  RuntimeHarness o-- FakeUiOwner
  RuntimeHarness o-- FakeFeedbackPresenter
```

The simulation harness is part of the design, not only test utility code. It
proves that runtime behavior is event-driven and not dependent on real SD,
SPI, LVGL, GTK, or radio hardware.

## Canonical Flow

```mermaid
flowchart LR
  Caller["UI / GPS / Protocol / App Service"] --> Intent["Runtime Intent"]
  Intent --> Facade["Runtime Facade"]
  Facade --> CommandQueue["Command Queue"]
  CommandQueue --> Worker["Active Object Worker"]
  Worker --> DeviceIo["Device I/O service"]
  DeviceIo --> Adapter["Platform Adapter"]
  Adapter --> Completion["Completion Result"]
  Completion --> EventBus["Event Bus"]
  EventBus --> State["Runtime State Projection"]
  EventBus --> UiDrain["UI Owner Event Drain"]
  UiDrain --> Renderer["Concrete Renderer Objects"]
```

The caller may be the UI owner context, GPS task, radio task, protocol runtime,
or app service. The rule is the same: submit intent or command, then return.

The UI event drain is the only step in this flow that may touch concrete UI
objects.

## Device I/O Delegation

Workers submit semantic operations to device services. Device services own
physical arbitration and return semantic completion, retry, unavailable, or
failure results. The shared-device mechanism is specified only in
`docs/specs/SPI_BUS_ARCHITECTURE_SPEC.md`.

## Map Tile Runtime Design

Map rendering must be split into request planning, slow tile work, and UI
application.

```mermaid
sequenceDiagram
  participant UI as UI Owner / MapViewport
  participant Runtime as MapTileRuntime
  participant Worker as MapTileWorker
  participant Store as Tile Storage Adapter
  participant DeviceIo as Device I/O service
  participant Events as Event Bus

  UI->>Runtime: requestVisibleTiles(viewport, generation)
  Runtime->>Worker: enqueue LoadTileCommand(ref, generation, priority)
  UI-->>UI: return to input/render loop
  Worker->>DeviceIo: execute tile read
  DeviceIo-->>Worker: ready or retry later
  Worker->>Store: read/decode tile payload
  Worker->>Events: MapTileReady/Failed(ref, generation, image_ref)
  Events->>UI: drain event on UI owner context
  UI->>UI: ignore stale generation or bind image to renderer object
```

Mandatory behavior:

- LVGL timers and input callbacks must not open tile files.
- LVGL timers and input callbacks must not wait for device I/O.
- Tile file lookup/read/decode must not be performed from drag, timer, input,
  or page render callbacks.
- Tile requests carry a viewport generation.
- Completion for stale generations must be ignored or released without UI work.
- Dragging uses interactive priority and cancels stale pending requests.
- Idle prefetch uses lower priority and must yield to current viewport tiles.
- Missing, loading, ready, failed, and cancelled are explicit tile states.
- Unknown tile availability is also state. The UI owner may render a pending or
  placeholder state, but it must not probe the SD card to discover whether a
  missing file exists.
- Negative tile probes must be cached or cooled down. Repeated `SD.open()` miss
  probes for adjacent tiles are forbidden in UI-visible paths.
- ESP active tile loading is not conforming until a slow/missing SD simulation
  proves UI ticks continue while storage work is deferred, marked busy, or
  completed later.

ESP active loader rules:

- `tile_loader_step()` is a UI owner path because it is reached from LVGL timers
  and map viewport callbacks.
- `tile_loader_step()` may calculate visible tile refs, move existing renderer
  objects, apply completed in-memory tile events, and submit/cancel runtime
  events.
- `tile_loader_step()` must not call `lv_fs_open`, SD file APIs, or a device
  I/O transaction.
- The ESP worker adapter uses the SD storage service. It may return
  `RetryLater` when the device cannot complete the read yet; the service owns
  all arbitration and transaction backpressure.
- `MapTileAsyncEvent` carries a copied `MapTilePayload` to the UI owner drain.
  The payload must be released even when stale.
- The ESP UI drain is non-blocking on command/event queues. A busy queue means
  the tile remains pending, not that the UI waits.
- The ESP UI drain applies at most one tile event per drain pass to avoid a
  burst of completed worker events monopolising the LVGL timer tick.

## Track Recording Runtime Design

Track recording must not perform durable storage in the GPS task or UI page
callback.

```mermaid
sequenceDiagram
  participant GPS as GPS Task
  participant UI as Tracker Page
  participant Runtime as TrackRuntime
  participant Worker as TrackStorageWorker
  participant Store as Track Storage Adapter
  participant Events as Event Bus

  UI->>Runtime: startNewTrack()
  Runtime->>Worker: enqueue StartTrackCommand
  UI-->>UI: render Starting state
  Worker->>Store: create/open file
  Worker->>Events: TrackStarted/TrackStartFailed
  Events->>UI: update page state

  GPS->>Runtime: appendPoint(point)
  Runtime->>Worker: enqueue/buffer point
  GPS-->>GPS: continue parsing
  Worker->>Store: batch write points
  Worker->>Events: TrackFlushSucceeded/Failed
```

Mandatory behavior:

- GPS parsing must not block on SD writes.
- UI actions such as New, Stop, Resume, List, Delete, and Export submit
  commands and render pending state.
- Track points are buffered and flushed by policy.
- `flush every point` is not allowed as the normal policy.
- Stop/close performs a durable final flush through a command completion event.
- The runtime state machine owns `Idle`, `Starting`, `Recording`, `Flushing`,
  `Stopping`, `Stopped`, `Error`, and `Recovering`.

## Configuration Persistence Design

Configuration persistence must be decoupled from event dispatch. Node/contact,
map, and track persistence remain domain-owned and do not share this runtime's
business state.

```text
Caller creates an AppConfigEdit.
The edit commits an AppConfigChangeSet.
ConfigPersistenceRuntime snapshots the authoritative configuration.
The runtime debounces, writes an immutable payload, and publishes the result.
```

Required configuration semantics:

- Debounce ordinary changes and coalesce them by configuration generation.
- Use an immediate critical flush only for explicit user settings or
  shutdown-critical state.
- Keep pending and in-flight payloads immutable and independently owned.
- Retry the failed generation without overwriting a newer generation.
- Treat stale completions as observations, never as permission to mutate the
  current runtime state.

Event dispatch may submit a configuration intent. It must not synchronously
write storage from the UI owner context.

## Domain Store Maintenance Design

Domain stores own their state and decide when their snapshot is durable. The
maintenance runtime provides the shared lifecycle for SD-backed hydration,
compaction, startup gating, and retry, but it does not become a universal
`save(store_key, bytes)` service. A domain owner submits a semantic operation
and consumes a completion tagged with the corresponding generation.

## Feedback Runtime Design

Feedback is an event-driven runtime concern.

```text
Feature/runtime detects a user-visible fact
  -> publishes feedback intent or domain completion event
  -> FeedbackRuntime applies eligibility/dedup policy
  -> UI owner event drain presents through platform presenter
```

Pages may request feedback, but pages do not own global feedback delivery.
Protocol completion, chat send result, storage degraded state, and tracker
failure events must be able to produce feedback regardless of the current page.

## State Machines

State machines must replace hidden call-stack state for cross-time flows.

### Storage Health

```text
Healthy -> Slow -> Degraded -> Unavailable
Unavailable -> Recovering -> Healthy
```

Entering `Slow`, `Degraded`, or `Unavailable` publishes an event. UI can show
reduced functionality without blocking.

### Map Tile

```text
Missing -> Requested -> Loading -> Ready -> BoundToRenderer
Requested -> Cancelled
Loading -> Cancelled
Loading -> Failed
Ready -> Evicted
```

### Track Recorder

```text
Idle -> Starting -> Recording -> Flushing -> Stopping -> Stopped
Recording -> Error
Error -> Recovering -> Recording
Error -> Stopped
```

### Chat Send Feedback

```text
Queued -> SubmittedToRadio -> ObservedAck -> Succeeded
SubmittedToRadio -> Timeout -> Failed
SubmittedToRadio -> Rejected -> Failed
```

The feedback runtime observes facts from this state machine. It does not infer
delivery success from the current page.

## Event Simulation and Robustness Testing

The refactor is not complete until runtime behavior can be simulated without
real hardware. The test harness must be able to drive events, command queues,
worker completions, delays, failures, and UI drains deterministically.

### Simulator Components

The simulator must provide:

| Component | Responsibility |
| --- | --- |
| `FakeClock` | deterministic time, timers, deadlines |
| `FakeUiThread` | records UI ticks and asserts no blocking operation runs on UI |
| `FakeEventBus` | publishes and drains runtime events deterministically |
| `FakeCommandQueue` | bounded queue, priorities, cancellation, dedupe |
| `FakeSemanticStorageAdapter` | scripted semantic storage results, delay, and failure |
| `FakeDeviceIo` | scripted device result, delay, and diagnostic outcome |
| `FakeMapTileWorker` | completes tile commands in controlled order |
| `FakeTrackStorageWorker` | batches points and emits track events |
| `FakeFeedbackPresenter` | captures notices without concrete renderer objects |

The same simulator style should be usable by unit tests for ESP-shared runtime
code and host/linux tests.

### Required Scenarios

#### Map Drag Under Slow SD

Script:

```text
UI requests viewport generation 1.
Storage delays tile reads by 200 ms.
UI requests viewport generation 2 before generation 1 completes.
Generation 1 completes after generation 2 is active.
```

Assertions:

- UI ticks continue.
- No UI-side blocking storage call occurs.
- Generation 1 results are ignored or released.
- Generation 2 visible tiles are applied when ready.
- Queue does not grow without bound.

#### GPS Track Burst While Map Is Dragging

Script:

```text
Generate 100 GPS points.
Generate map drag requests every frame.
Make storage arbiter slow for 2 seconds.
```

Assertions:

- GPS producer returns without waiting for SD.
- Track writer batches points.
- Map interactive requests keep higher priority than idle prefetch.
- UI event drain remains responsive.
- Backpressure is reported instead of freezing.

#### Tracker New / Stop / List While Storage Busy

Script:

```text
Hold storage busy.
Click New.
Click Back or navigate away.
Release storage.
Complete StartTrackCommand.
Request listTracks.
```

Assertions:

- UI renders pending state and remains navigable.
- Completion does not dereference destroyed page objects.
- Track runtime state is correct after navigation.
- List results are delivered through events.

#### NodeInfo / Position Storm

Script:

```text
Publish many NodeInfo and Position events.
Mark node/contact store dirty repeatedly.
Advance fake time under debounce threshold.
Advance past threshold.
```

Assertions:

- Memory projection updates are visible.
- Persistence writes are debounced and deduplicated.
- UI dispatch does not perform synchronous storage writes.
- Save failure produces a storage event and optional feedback intent.

#### Chat Send Result From Any Page

Script:

```text
Send chat message from conversation page.
Navigate to contacts page.
Deliver ack timeout or success event.
```

Assertions:

- The original message is updated, not duplicated.
- Feedback notice is captured by feedback runtime.
- Notice does not depend on current page.
- No page-owned `SystemNotification` call is required.

#### Sleep / Wake During Pending Storage

Script:

```text
Start long storage command.
Trigger screen sleep timer.
Inject wake input.
Complete storage command.
```

Assertions:

- Wake input is processed by UI owner context.
- Pending storage does not block wake path.
- UI state after completion is consistent.

#### Storage Circuit Breaker

Script:

```text
Make storage time out repeatedly.
Submit tile, track, and persistence commands.
Advance clock through retry windows.
Recover storage.
```

Assertions:

- Storage health transitions to Slow/Degraded/Unavailable.
- UI receives degraded state without freezing.
- Non-critical work is dropped, cancelled, or delayed by policy.
- Critical durable commands report failure or recover explicitly.

### Test Invariants

All runtime tests must assert:

- UI owner context does not call blocking device I/O or filesystem APIs.
- Background workers do not call concrete renderer APIs.
- Every command completes, fails, is cancelled, or remains pending for an
  explained reason.
- Queue bounds are enforced.
- Stale map tile generations cannot mutate the current viewport.
- Store saves are batched or debounced when high-frequency events arrive.
- Feedback capture does not require a live page object.
- Resource owner, wait time, and hold time diagnostics are emitted for slow
  storage/bus operations.

## Migration Order

The burn-down should proceed in slices that each leave the system shippable.

1. Add diagnostics around UI tick, storage/bus wait, command enqueue, command
   completion, and event drain.
2. Introduce command/event simulator tests for feedback and chat send result.
3. Move map tile file access out of LVGL timer/input paths.
4. Move track start/stop/list/append/flush into an asynchronous track storage
   worker.
5. Consolidate node/contact maintenance under domain store owners and
   `StorageMaintenanceRuntime`; keep it separate from configuration persistence.
6. Replace direct hardware access in UI-facing code with device service calls.
7. Burn down adapter-owned business decisions and route them through shared
   runtimes/facades.
8. Turn remaining synchronous storage calls in page/widget code into compile or
   test failures.

## Conformance Checklist

A change conforms to this design only when:

- UI code submits commands or intents instead of executing slow work.
- Slow work has an owning worker or declared active object.
- Cross-context results are events.
- UI mutation happens only in the UI owner drain.
- Platform adapters contain no business policy.
- Shared resource access is policy-driven and diagnosable.
- State machines describe visible long-running operation state.
- Simulation tests can trigger success, failure, timeout, cancellation,
  backpressure, and stale completion.
- The same business rule is implemented once in runtime code and reused by
  ESP32, nRF52, Linux LVGL, and Linux GTK targets through bridges/adapters.
