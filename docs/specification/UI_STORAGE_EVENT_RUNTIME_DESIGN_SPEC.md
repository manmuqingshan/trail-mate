# UI / Storage / Event Runtime Design Spec

Status date: 2026-06-17

This document defines the design baseline for removing UI stalls caused by
blocking storage, shared-SPI contention, synchronous persistence, and page-owned
runtime side effects.

It complements `RUNTIME_CONCURRENCY_SPEC.md`. The concurrency spec states the
rules. This document explains the design that makes those rules implementable
and testable.

## Problem Statement

Trail Mate currently has several paths where renderer-owned code, runtime
services, and platform storage operations can enter the same slow resource path:

- LVGL timers and page callbacks can trigger SD or shared-SPI access.
- Map tile loading can perform file lookup, image object setup, and decode work
  from the UI owner context.
- GPS track recording can hold recorder state and shared-SPI access while it
  opens, writes, flushes, and closes files.
- Node and contact persistence can be reached from event dispatch paths.
- Feedback notices, protocol completion, and UI mutation can be coupled to the
  page that happened to start an operation.

The failure mode is not only a crash. A target can keep GPS, radio, timers, and
sleep tasks alive while the UI owner task is blocked or starved. When that
happens, touch input, key input, page navigation, display refresh, and wake
handling appear frozen even though background logs continue.

The design goal is therefore:

```text
UI owner context never waits for storage, shared-SPI, filesystem, decode,
protocol completion, or persistence.
```

After the ESP shared-SPI map freeze investigation, this goal is tightened:

```text
Moving work off the UI owner context is necessary but not sufficient.
No storage worker may hold, starve, or repeatedly reacquire a physical bus that
display flush depends on in a way that prevents frame progress, input-visible
feedback, wake rendering, or page navigation.
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
- `PersistenceWorker`
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
- SPI bus acquire/release
- LVGL image descriptor setup
- nRF flash write
- GTK idle invocation
- radio TX

Adapters must not own business rules, retry policy, tile priority, feedback
eligibility, chat delivery semantics, or tracker state transitions.

### Resource Topology

Resource topology describes the physical resource domains that can block each
other on a target:

- display flush bus / DMA path
- SD or flash storage bus
- radio SPI bus
- touch or NFC bus
- shared mutex or controller domain

Two features are in different business contexts but the same physical topology
when they share a controller, chip-select bus, DMA path, or lock. On ESP targets
such as T-Deck and T-LoRa Pager, display, SD, radio, and optional NFC can share
the same SPI domain. Therefore a storage operation that holds the shared bus can
freeze display progress even when it runs outside the UI owner task.

### Display Frame Critical Resource

Display flush, wake rendering, and input-visible feedback are frame-critical.
They are not ordinary bus consumers. Any storage or protocol worker using a
display-shared resource must yield to frame progress.

Four budgets are distinct:

| Budget | Meaning | Failure mode when missing |
| --- | --- | --- |
| `wait_budget` | Maximum time a caller waits to acquire a resource | Caller blocks before work starts |
| `hold_budget` | Maximum expected time a holder keeps the resource | Display frames are starved after acquisition |
| `burst_budget` | Maximum repeated acquisitions in a time window | Many short operations still freeze UI |
| `frame_budget` | Maximum time display can be denied progress | Screen appears frozen while logs continue |

Bounded waiting alone is not sufficient. A worker can acquire immediately and
still hold the display-shared bus for tens of milliseconds during SD open/read.
That violates this spec unless the operation is outside UI hot paths, explicitly
budgeted, and followed by cooldown/backpressure.

## Pattern Decision

The design uses a small set of patterns with explicit responsibilities.

| Pattern | Primary use | Why it is required | Must not become |
| --- | --- | --- | --- |
| Active Object | Storage, tile, track, persistence, protocol workers | Moves slow work behind queues so callers return immediately | A hidden synchronous service |
| Command | Runtime intents queued to workers | Separates request creation from execution and enables cancellation, priority, diagnostics | Wire-format payloads or platform calls |
| Observer / Event Bus | Completion, failure, state, feedback, UI effects | Reports asynchronous outcomes without coupling to the current page | Direct renderer mutation from background contexts |
| State | Track recording, tile loading, storage health, chat send lifecycle | Makes cross-time behavior explicit instead of encoded in locks or call stacks | Page-local boolean flags |
| Strategy | Bus access, tile loading, flush, persistence, retry policies | Keeps environment and mode differences explicit and swappable | Scattered `if target` branches |
| Bridge | Runtime rule side to platform execution side | Keeps shared business rules single-source while allowing ESP/nRF/Linux backends | Another adapter with business decisions |
| Adapter | Wrap platform APIs | Contains technical integration details | A second business implementation |
| Mediator / Arbiter | Shared resource scheduling | Replaces ad-hoc lock competition with observable scheduling | A new god object for product behavior |
| Unit of Work | Track flush, node/contact save, config persistence | Batches durable writes and reduces SD/SPI transaction count | An unbounded memory buffer |
| Circuit Breaker / Backpressure | Slow or failing SD/SPI/storage | Degrades features without freezing UI | Silent data loss |
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
- interfaces for queues, workers, bus/storage arbitration, platform adapters,
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

  class IBusArbiter {
    <<interface>>
    +acquire(policy, command_id)
    +release(token)
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
    +selectBusPolicy(command)
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
  IActiveWorker o-- IBusArbiter
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
    PersistWorker["PersistenceWorker"]
  end

  subgraph Platform["Platform Adapters"]
    Bus["Bus / Storage Arbiter"]
    Storage["Storage Adapter"]
    Decode["Decode Adapter"]
  end

  Page --> Facade
  Facade --> Commands
  Facade --> State
  Facade --> Policies
  Commands --> TileWorker
  Commands --> TrackWorker
  Commands --> PersistWorker
  TileWorker --> Bus
  TrackWorker --> Bus
  PersistWorker --> Bus
  Bus --> Storage
  TileWorker --> Decode
  TileWorker --> Events
  TrackWorker --> Events
  PersistWorker --> Events
  Events --> State
  Events --> UiDrain
  UiDrain --> Renderer
```

Forbidden reverse dependencies:

- platform adapter to page/widget
- worker to concrete renderer
- page/widget to storage adapter
- page/widget to shared-SPI lock
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
    PersistWorker["Persistence worker"]
    ProtocolWorker["Protocol worker"]
  end

  subgraph PlatformContext["Platform Adapter Context"]
    StorageAdapter["Storage adapter"]
    BusAdapter["Bus / SPI adapter"]
    DecodeAdapter["Decode adapter"]
    ClockAdapter["Clock adapter"]
    RadioAdapter["Radio adapter"]
  end

  subgraph TestContext["Simulation Context"]
    FakeClock["Fake clock"]
    FakeStorage["Fake storage"]
    FakeBus["Fake bus arbiter"]
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
  Commands --> PersistWorker
  Commands --> ProtocolWorker
  TileWorker --> StorageAdapter
  TrackWorker --> StorageAdapter
  PersistWorker --> StorageAdapter
  TileWorker --> BusAdapter
  TrackWorker --> BusAdapter
  PersistWorker --> BusAdapter
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
    PersistWorker["Persistence worker"]
  end

  subgraph ResourceOwner["Resource owner"]
    Arbiter["Storage / bus arbiter"]
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
  CommandPump --> PersistWorker
  TileWorker --> Arbiter
  TrackWorker --> Arbiter
  PersistWorker --> Arbiter
  Arbiter --> Storage
  TileWorker --> Decode
  TileWorker --> UiDrain
  TrackWorker --> UiDrain
  PersistWorker --> UiDrain
```

The UI owner task is never deployed as the resource owner. If a target has only
one physical thread, the same ownership model still applies cooperatively:
slow work must be incremental, budgeted, and represented as commands/events
rather than blocking UI execution.

When storage and display share a physical SPI domain, the storage worker is also
not the resource owner. It owns command state. A bus/storage scheduler owns
permission to occupy the display-shared resource and must apply wait, hold,
burst, and frame budgets.

## UML Bus Arbitration Class Model

```mermaid
classDiagram
  class BusAccessPolicy {
    <<enum>>
    DisplayFrameCritical
    UiNeverBlock
    InteractiveWorkerBounded
    BackgroundWorkerBounded
    DurableCommit
    RecoveryExclusive
  }

  class BusAcquireRequest {
    +resource
    +policy
    +command_id
    +deadline_ms
    +origin
  }

  class BusAccessToken {
    +resource
    +owner
    +acquired_ms
    +valid
  }

  class BusAcquireResult {
    +status
    +token
    +diagnostics
  }

  class BusDiagnostics {
    +resource
    +owner_task
    +wait_ms
    +hold_ms
    +policy
    +command_id
  }

  class IBusArbiter {
    <<interface>>
    +acquire(request) BusAcquireResult
    +release(token)
    +health() StorageHealthState
  }

  class IBusAdapter {
    <<interface>>
    +tryAcquire(timeout_ms)
    +release()
  }

  class BusPolicyStrategy {
    <<strategy>>
    +select(command) BusAccessPolicy
    +timeoutFor(policy)
  }

  class StorageHealthState {
    <<state>>
    +status
    +last_error
    +last_transition_ms
  }

  class StorageBusArbiter {
    +acquire(request)
    +release(token)
    +health()
  }

  IBusArbiter <|.. StorageBusArbiter
  StorageBusArbiter o-- IBusAdapter
  StorageBusArbiter o-- BusPolicyStrategy
  StorageBusArbiter o-- StorageHealthState
  BusAcquireRequest --> BusAccessPolicy
  BusAcquireResult --> BusAccessToken
  BusAcquireResult --> BusDiagnostics
```

## UML ESP Shared-SPI Lock Mechanism

This section is the normative specification for the ESP shared-SPI lock
mechanism that protects UI responsiveness when display refresh, SD-backed LVGL
FS, map tile IO, track persistence, radio, NFC, USB, and other peripherals share
one physical SPI controller.

The mechanism is intentionally specified in `docs/specification`, not in
historical best-practice notes. Any future change to the lock behavior must
update this section and the code bindings below in the same commit.

### Lock Mechanism Distinctions

| Concept | Meaning | Current code binding |
| --- | --- | --- |
| Physical shared bus | The real contested resource: board-level SPI controller and chip-select domain | `platform/esp/common/include/platform/esp/common/shared_spi_lock.h` |
| Frame-critical display consumer | Display flush, wake rendering, and input-visible feedback | `platform/esp/boards/src/display/DisplayInterface.cpp` |
| Worker-domain storage consumer | Map tile SD reads, track persistence, node/team/config stores, pack IO | Platform/runtime adapters that use `SharedSpiLockGuard` or `IBusArbiter` |
| LVGL FS adapter | Technical adapter allowing LVGL to read SD/flash paths; not a scheduler and not a business owner | `platform/esp/arduino_common/src/LV_Helper_v9.cpp` |
| Runtime bus arbiter | Policy boundary that turns commands into bounded bus acquire/release attempts | `modules/core_sys/include/sys/runtime_async.h`, ESP map binding in `platform/esp/arduino_common/src/ui/widgets/map/map_tiles.cpp` |
| Display pressure signal | A recent display lock timeout that tells worker-domain storage to cool down | `note_display_spi_timeout()`, `display_spi_recently_timed_out()` |
| Legacy display-lock alias | Old naming that implied the lock belonged to display instead of the bus | Burned down; `display_spi_lock()` / `display_spi_unlock()` must not exist in active headers |

The most important distinction is this:

```text
The lock belongs to the physical shared SPI bus.
Display is the frame-critical client of that bus, not the owner of the concept.
```

Therefore code outside the display driver must not name the mechanism as a
display-private lock. New code must use runtime bus ports, `shared_spi_lock*`,
or `SharedSpiLockGuard` depending on layer.

### Why LVGL FS Does Not Solve This

LVGL can register file-system callbacks. That gives LVGL a path to call open,
read, write, seek, tell, and dir operations. It does not give LVGL knowledge of:

- which board peripherals share one physical SPI controller
- which consumer is frame-critical
- whether a display flush just timed out
- how many storage commands may run in one burst
- whether a tile load is stale after a map drag
- whether a track flush is durable or background

For that reason `init_sd_fs_driver()` is only a platform adapter registration.
It does not make SD and display contention safe by itself. The callbacks in
`LV_Helper_v9.cpp` must remain short, bounded, and allowed to fail with busy or
failed-open semantics. Product/UI code must not use LVGL FS as a synchronous
storage probe from renderer hot paths.

### Static Class Binding

```mermaid
classDiagram
  class SharedSpiLockPort {
    <<platform port>>
    +shared_spi_lock(wait_ticks)
    +shared_spi_lock_with_owner(wait_ticks, owner)
    +shared_spi_unlock()
    +note_display_spi_timeout(now_ms)
    +display_spi_recently_timed_out(now_ms, window_ms)
  }

  class SharedSpiLockGuard {
    <<RAII adapter>>
    +SharedSpiLockGuard(wait_ticks, owner)
    +locked()
  }

  class LilyGoDispArduinoSPI {
    <<frame-critical adapter>>
    +pushColors(area)
    +writeCommand(cmd)
    +lock(wait_ticks, owner)
    +unlock()
    +lockOwnerLabel()
    +lastLockHeldMs()
  }

  class LvglSdFsAdapter {
    <<platform adapter>>
    +sd_fs_open(path, mode)
    +sd_fs_read(file, buffer, bytes)
    +sd_fs_write(file, buffer, bytes)
    +sd_fs_dir_open(path)
  }

  class RuntimeBusAbstractions {
    <<runtime contract>>
    +BusAcquireRequest
    +BusAccessToken
    +BusAcquireResult
    +IBusArbiter
    +StorageHealthState
  }

  class EspMapTileBusArbiter {
    <<ESP adapter>>
    +acquire(request)
    +release(token)
    +health()
  }

  class MapTileWorker {
    <<active object>>
    +execute(command, now_ms)
  }

  class SdMapTileFileSystem {
    <<storage adapter>>
    +exists(path)
    +isDirectory(path)
    +readFile(path, buffer, capacity)
  }

  SharedSpiLockGuard --> SharedSpiLockPort
  LilyGoDispArduinoSPI --> SharedSpiLockPort : publishes pressure
  LvglSdFsAdapter --> SharedSpiLockGuard
  RuntimeBusAbstractions <|.. EspMapTileBusArbiter
  EspMapTileBusArbiter --> SharedSpiLockPort
  MapTileWorker --> RuntimeBusAbstractions
  MapTileWorker --> SdMapTileFileSystem
  SdMapTileFileSystem --> SharedSpiLockPort : chunk yield
```

Static ownership rules:

- `LilyGoDispArduinoSPI` is allowed to own the concrete mutex because it is the
  platform object that initializes the shared display SPI implementation on the
  Arduino ESP boards.
- The public platform name remains `shared_spi_*`; callers must not use or
  reintroduce `display_spi_*` aliases.
- Runtime workers should depend on `IBusArbiter`, not directly on the physical
  lock. A temporary ESP adapter may call `shared_spi_*` while it implements the
  arbiter port.
- LVGL FS callbacks are adapters. They must not contain product policy, retry
  strategy, tile priority, or page state.

### Component Deployment

```mermaid
flowchart TB
  subgraph UiDomain["UI realtime domain"]
    LvglTick["lv_timer_handler / input drain"]
    DisplayFlush["Display flush callback"]
    WakeRender["Wake/sleep visual transition"]
  end

  subgraph RuntimeDomain["Runtime command domain"]
    TileRuntime["MapTileAsyncRuntime"]
    TrackRuntime["Track runtime"]
    PersistRuntime["Persistence runtime"]
    CommandQueue["Command queues"]
  end

  subgraph WorkerDomain["Worker / adapter domain"]
    TileWorker["MapTileWorker"]
    TrackWorker["TrackStorageWorker"]
    PersistWorker["PersistenceWorker"]
    LvglFs["LVGL SD FS adapter"]
  end

  subgraph BusDomain["Physical shared-SPI domain"]
    DisplayLock["LilyGoDispArduinoSPI mutex"]
    SharedPort["shared_spi_* port"]
    Pressure["display pressure timestamp"]
  end

  subgraph StorageDomain["Storage media"]
    SdRuntime["SdRuntimeFile / SdRuntimeDir"]
    SdCard["SD card"]
  end

  LvglTick --> DisplayFlush
  DisplayFlush --> DisplayLock
  DisplayLock --> Pressure
  TileRuntime --> CommandQueue
  TrackRuntime --> CommandQueue
  PersistRuntime --> CommandQueue
  CommandQueue --> TileWorker
  CommandQueue --> TrackWorker
  CommandQueue --> PersistWorker
  TileWorker --> SharedPort
  TrackWorker --> SharedPort
  PersistWorker --> SharedPort
  LvglFs --> SharedPort
  SharedPort --> DisplayLock
  SharedPort --> SdRuntime
  SdRuntime --> SdCard
  Pressure --> TileWorker
```

The UI realtime domain may be denied a single frame, but it must never wait
indefinitely. Worker-domain storage may be delayed, cancelled, retried, or
marked busy when display pressure exists.

### Display Flush Sequence

```mermaid
sequenceDiagram
  participant UI as UI owner / LVGL
  participant Display as LilyGoDispArduinoSPI
  participant Lock as Shared SPI mutex
  participant Pressure as Display pressure signal

  UI->>Display: pushColorsArea()
  Display->>Lock: lock(wait=frame budget, owner="display")
  alt acquired
    Lock-->>Display: token
    Display->>Display: SPI transaction
    Display->>Lock: unlock()
    Display-->>UI: flush complete
  else timed out
    Display->>Pressure: note_display_spi_timeout(now_ms)
    Display-->>UI: return without blocking
  end
```

Display timeout is not a normal success path. It is a pressure signal that
storage workers must observe. Returning from the flush is still required because
blocking inside display refresh prevents input, wake rendering, and page
navigation from recovering.

### LVGL SD FS Callback Sequence

```mermaid
sequenceDiagram
  participant LVGL as LVGL file consumer
  participant Fs as LVGL SD FS adapter
  participant Guard as SharedSpiLockGuard
  participant SD as SdRuntimeFile

  LVGL->>Fs: sd_fs_open/read/write/dir()
  Fs->>Guard: acquire(short bounded wait)
  alt acquired
    Fs->>SD: perform one storage operation
    Fs->>Guard: release on scope exit
    Fs-->>LVGL: ok/result
  else busy
    Fs-->>LVGL: LV_FS_RES_BUSY or failed open
  end
```

The adapter must not retry in a loop. A busy result is valid and lets the caller
or runtime decide whether to defer, cancel, or show pending state. This keeps
legacy LVGL FS callers from monopolising the UI owner task.

### Map Tile Worker Sequence With Display Pressure

```mermaid
sequenceDiagram
  participant UI as Map UI owner
  participant Runtime as MapTileAsyncRuntime
  participant Worker as MapTileWorker
  participant Arbiter as EspMapTileBusArbiter
  participant Bus as shared_spi_* port
  participant TileStore as SdMapTileFileSystem
  participant Events as MapTileEventSink

  UI->>Runtime: requestVisibleTiles(plan, generation)
  Runtime->>Worker: enqueue LoadTileCommand
  UI-->>UI: return to input/render loop
  Worker->>Arbiter: acquire(command policy)
  Arbiter->>Bus: display_spi_recently_timed_out()
  alt recent display pressure or cooldown
    Arbiter-->>Worker: Busy
    Worker->>Events: ResourceBusy(generation, tile)
  else acquired
    Arbiter->>Bus: shared_spi_lock_with_owner("map_tile_sd")
    Bus-->>Arbiter: token
    Worker->>TileStore: readFile()
    loop between tile chunks
      TileStore->>Bus: shared_spi_unlock()
      TileStore->>Bus: bounded reacquire
    end
    Worker->>Arbiter: release(token)
    Arbiter->>Bus: shared_spi_unlock()
    Arbiter->>Arbiter: cooldown based on hold/display pressure
    Worker->>Events: Ready or Failed
  end
  Events-->>UI: drain at most one tile event per UI pass
```

This is the current ESP Arduino map binding. It is conforming only because:

- the UI owner submits a command and returns
- the worker owns the SD read
- `EspMapTileBusArbiter` checks display pressure before acquiring
- tile reads release the bus between chunks
- completed events are drained with a UI budget

Moving any of these operations back into a page callback, LVGL timer, input
handler, or renderer mutation path is a regression.

Display pressure is a storage-worker backpressure signal, not permission to
stop UI-domain map state work. While pressure is recent, the UI owner may still
calculate visible tile refs, update anchors, move already-created LVGL tile
objects, rebuild render queues, and drain a bounded number of already-delivered
tile events. The pressure signal must reduce or pause new SD-backed worker
requests; it must not make viewport/layout calculation return early.

### Lock Health State

```mermaid
stateDiagram-v2
  [*] --> Healthy
  Healthy --> DisplayPressure: display lock timeout
  DisplayPressure --> WorkerCooldown: worker observes pressure
  WorkerCooldown --> Healthy: cooldown elapsed without new timeout
  DisplayPressure --> Slow: repeated busy/timed out acquire
  Slow --> Degraded: consecutive worker acquire failures >= threshold
  Degraded --> Recovering: recovery/backoff window starts
  Recovering --> Healthy: acquired and released within budget
  Recovering --> Degraded: pressure continues
```

The state is intentionally driven by observable resource events, not by page
state. A map page, contacts page, tracker page, chat page, or boot UI can all be
affected by the same physical contention.

### Code Binding Table

| Role | File | Required behavior |
| --- | --- | --- |
| Runtime lock contract | `modules/core_sys/include/sys/runtime_async.h` | Owns `BusAccessPolicy`, `BusAcquireRequest`, `BusAccessToken`, `BusAcquireResult`, `IBusArbiter`, `StorageHealthState`. Shared business/runtime code depends on these abstractions. |
| Shared SPI port | `platform/esp/common/include/platform/esp/common/shared_spi_lock.h` | Names the physical bus. Must not expose `display_spi_lock` aliases. |
| Arduino display mutex implementation | `platform/esp/boards/src/display/DisplayInterface.cpp` | Uses bounded display waits; logs and records pressure; never waits forever in `pushColors*`. |
| IDF no-contention implementation | `platform/esp/idf_common/src/shared_spi_lock.cpp` | Provides a no-op conforming implementation for ESP IDF targets that do not use the Arduino shared bus path. |
| LVGL SD FS adapter | `platform/esp/arduino_common/src/LV_Helper_v9.cpp` | Uses short bounded acquisitions and returns busy/failed-open instead of retrying. |
| Map runtime worker contract | `modules/ui_map_runtime/src/map_tiles/map_tile_async_runtime.cpp` | Executes tile commands through `IBusArbiter` and publishes ready/busy/failed events. |
| ESP map bus arbiter | `platform/esp/arduino_common/src/ui/widgets/map/map_tiles.cpp` | Checks display pressure, applies cooldown, maps runtime policy to short waits, and releases bus after each tile command. |
| ESP map tile SD adapter | `platform/esp/arduino_common/src/ui/widgets/map/map_tiles.cpp` | Reads tile payload in worker domain and yields the bus between chunks. Only confirmed not-found results may enter missing-tile memory; retryable read failures remain requestable after short backoff. |

### Forbidden Bypasses

The following are non-conforming in active UI-visible paths:

- adding `display_spi_lock()` / `display_spi_unlock()` aliases back to public
  headers
- page/widget code calling `shared_spi_lock*`
- page/widget code calling Arduino `SD.open`, `SdRuntimeFile`, or LVGL SD FS to
  probe whether content exists
- LVGL timer callbacks performing synchronous SD open/read/list operations
- storage adapters spinning until the bus becomes available
- worker loops draining many SD operations without cooldown after display
  pressure
- `lv_refr_now()` or forced flush used as feedback while storage owns the
  display-shared bus

Allowed exceptions must be explicit platform adapter code and must document
their wait, hold, burst, and frame budgets.

### Legacy Burn-Down Status

| Legacy path | Status | Deletion/containment rule |
| --- | --- | --- |
| `display_spi_lock` / `display_spi_unlock` public aliases | Burned down | No active declaration or inline alias may remain. New code must use `shared_spi_*` or runtime bus ports. |
| `display_spi_lock.cpp` source filename | Burned down | Platform implementations must be named after `shared_spi_lock`, not display-private terminology. |
| ESP map UI source synchronous storage behavior | Burned down | UI map source is path/planning only; worker source performs SD reads. |
| LVGL SD FS adapter reachable from legacy resource paths | Contained | Adapter remains but uses short bounded lock attempts and must not be used as a UI hot-path storage probe. |
| ESP map worker direct physical lock calls | Contained adapter | Allowed only inside `EspMapTileBusArbiter` and tile SD adapter until all ESP storage paths use a common `IBusArbiter` implementation. |
| Team UI store direct SD persistence | Remaining legacy | Must move behind team/storage runtime worker in a separate migration. |
| Route/track file load from GPS page | Remaining legacy | Must move behind route/track runtime worker in a separate migration. |
| Pack repository direct file operations | Remaining legacy | Must move behind pack repository commands/events. |

### Simulation Requirements

Every future change to this mechanism must have a hardware-free simulation or
host test that covers:

- display acquire timeout while storage owns the bus
- worker receiving `Busy` because display pressure was recent
- worker cooldown ending and a later command succeeding
- stale map generation completion being ignored
- LVGL SD FS callback returning busy rather than blocking
- deletion guard proving no `display_spi_lock` alias or UI-page direct SD probe
  was reintroduced

The simulation may use fake `IBusArbiter`, fake clock, fake event sink, fake UI
drain, and fake storage backend. It must not require real SD, LVGL, display SPI,
or radio hardware.

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
  MapTileWorker o-- IBusArbiter
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
  TrackStorageWorker o-- IBusArbiter
  TrackStorageWorker --> TrackEvent
  TrackRuntime --> TrackEvent
```

## UML Persistence Class Model

```mermaid
classDiagram
  class PersistenceRuntime {
    +markDirty(store_key)
    +requestSave(store_key, policy)
    +handle(event)
  }

  class PersistenceCommand {
    +command_id
    +store_key
    +policy
    +deadline_ms
  }

  class PersistencePolicy {
    <<strategy>>
    DebouncedSave
    BatchSave
    ImmediateCriticalSave
    DropDuplicateSave
  }

  class DirtyStoreRegistry {
    +markDirty(store_key)
    +takeDue(now_ms)
    +hasPending(store_key)
  }

  class PersistenceWorker {
    +submit(command)
    +tick(now_ms)
  }

  class IStoreSnapshotProvider {
    <<interface>>
    +snapshot(store_key)
  }

  class IStoreStorageAdapter {
    <<interface>>
    +write(store_key, bytes)
    +read(store_key)
  }

  class PersistenceEvent {
    +kind
    +store_key
    +command_id
    +error
  }

  PersistenceRuntime o-- DirtyStoreRegistry
  PersistenceRuntime --> PersistenceCommand
  PersistenceCommand --> PersistencePolicy
  PersistenceWorker --> PersistenceCommand
  PersistenceWorker o-- IStoreSnapshotProvider
  PersistenceWorker o-- IStoreStorageAdapter
  PersistenceWorker o-- IBusArbiter
  PersistenceWorker --> PersistenceEvent
  PersistenceRuntime --> PersistenceEvent
```

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
  participant Bus as Bus Arbiter
  participant Events as Event Bus

  UI->>Map: requestVisibleTiles(generation=42, interactive)
  Map->>Queue: enqueue LoadTileCommand priority=interactive
  GPS->>Track: appendPoint(point)
  Track->>Queue: enqueue/buffer AppendTrackPointCommand
  Queue->>Worker: dispatch tile before idle/background work
  Worker->>Bus: acquire InteractiveWorkerBounded
  Bus-->>Worker: acquired
  Worker->>Events: MapTileReady(generation=42)
  Queue->>Worker: dispatch batched track write
  Worker->>Bus: acquire BackgroundWorkerBounded
  Bus-->>Worker: delayed or acquired
  Worker->>Events: TrackFlushSucceeded or Backpressure
  Events->>UI: drain UI-safe events
```

### NodeInfo Storm With Debounced Persistence

```mermaid
sequenceDiagram
  participant Radio as Radio Task
  participant Runtime as Protocol Runtime
  participant Contacts as Contact/Node Runtime
  participant Persist as PersistenceRuntime
  participant Worker as PersistenceWorker
  participant Store as Storage Adapter
  participant UI as UI Owner

  loop many packets
    Radio->>Runtime: incoming NodeInfo/Position
    Runtime->>Contacts: update in-memory projection
    Contacts->>Persist: markDirty(nodes)
    Contacts->>UI: publish projection update
  end
  Persist->>Persist: coalesce dirty notifications
  Persist->>Worker: enqueue SaveStoreCommand after debounce
  Worker->>Store: write snapshot
  Worker->>Persist: PersistenceSaved/PersistenceFailed
  Persist->>UI: optional storage feedback event
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
  participant Bus as Bus Arbiter

  UI->>Runtime: submit command
  Runtime->>Worker: enqueue slow storage work
  Worker->>Bus: acquire BackgroundWorkerBounded
  Power->>UI: sleep timeout event
  UI->>UI: render sleep transition
  Power->>UI: wake input
  UI->>UI: process wake input
  Bus-->>Worker: storage complete later
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

  class FakeStorageBackend {
    +scriptDelay(operation, ms)
    +scriptFailure(operation, error)
    +read(request)
    +write(request)
  }

  class FakeBusArbiter {
    +scriptAcquire(result)
    +acquire(request)
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
  RuntimeHarness o-- FakeStorageBackend
  RuntimeHarness o-- FakeBusArbiter
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
  Worker --> Arbiter["Storage / Bus Arbiter"]
  Arbiter --> Adapter["Platform Adapter"]
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

## Shared Resource Arbitration

Direct shared-SPI locking from feature code is legacy. The target design is a
bus/storage arbiter with explicit policies:

| Policy | Intended callers | Wait behavior | Failure behavior |
| --- | --- | --- | --- |
| `DisplayFrameCritical` | display flush, wake render, input-visible feedback | frame-budget bounded, highest priority | skip/defer non-critical storage |
| `UiNeverBlock` | UI event/timer/input paths | no wait or frame-budget-only try | defer, cancel, or render pending state |
| `InteractiveWorkerBounded` | map tile worker during drag | short bounded wait | cancel stale tile or retry later |
| `BackgroundWorkerBounded` | track append, node save, prefetch | bounded wait with backpressure | reschedule, batch, or enter degraded state |
| `DurableCommit` | explicit stop/close/final flush | bounded but higher priority | report durable failure event |
| `RecoveryExclusive` | storage remount or card recovery | exclusive, never from UI | publish degraded/unavailable state |

Every acquisition must be diagnosable:

```text
resource
owner_task_or_thread
command_id
wait_start_ms
acquired_ms
released_ms
hold_ms
wait_ms
policy
```

The arbiter expresses scheduling intent. A mutex only expresses exclusion. Code
that uses a bare blocking mutex to coordinate UI, SD, and display refresh is not
conformant.

For display-shared SPI, storage work must use a try-lock or bounded acquisition
and must enter cooldown/backpressure after every slow hold. The scheduler may
drop, defer, or mark commands `ResourceBusy`; it must not spin on the lock or
drain a burst of SD operations while display is trying to render.

## Map Tile Runtime Design

Map rendering must be split into request planning, slow tile work, and UI
application.

```mermaid
sequenceDiagram
  participant UI as UI Owner / MapViewport
  participant Runtime as MapTileRuntime
  participant Worker as MapTileWorker
  participant Store as Tile Storage Adapter
  participant Bus as Storage/Bus Arbiter
  participant Events as Event Bus

  UI->>Runtime: requestVisibleTiles(viewport, generation)
  Runtime->>Worker: enqueue LoadTileCommand(ref, generation, priority)
  UI-->>UI: return to input/render loop
  Worker->>Bus: acquire(policy)
  Bus-->>Worker: acquired or retry later
  Worker->>Store: read/decode tile payload
  Worker->>Events: MapTileReady/Failed(ref, generation, image_ref)
  Events->>UI: drain event on UI owner context
  UI->>UI: ignore stale generation or bind image to renderer object
```

Mandatory behavior:

- LVGL timers and input callbacks must not open tile files.
- LVGL timers and input callbacks must not wait for shared-SPI.
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
- `tile_loader_step()` must not call `lv_fs_open`, SD file APIs, or a
  shared-SPI lock.
- The ESP worker adapter uses the SD runtime file adapter and acquires
  display-shared SPI only through the bus/storage scheduler. For visible tiles
  it must use try-lock or tightly bounded acquisition, publish `ResourceBusy`
  when the bus is not immediately available, and apply cooldown after any slow
  hold.
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

## Persistence Runtime Design

Node/contact/config persistence must be decoupled from event dispatch.

```text
Runtime event updates in-memory state.
Persistence intent is recorded.
PersistenceWorker batches/debounces writes.
Persistence result is published as an event.
```

Required strategies:

- `DebouncedSave` for node/contact store updates.
- `ImmediateCriticalSave` only for explicit user settings or shutdown-critical
  state.
- `BatchSave` for high-frequency updates.
- `DropDuplicateSave` for repeated dirty notifications while one save is
  already pending.

Event dispatch may mark a store dirty. It must not synchronously write storage
from the UI owner context.

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
| `FakeStorageBackend` | scripted read/write/list/flush delay and failure |
| `FakeBusArbiter` | scripted acquisition delay, timeout, owner diagnostics |
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

- UI owner context does not call blocking storage, blocking shared-SPI, or
  filesystem APIs.
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
5. Move node/contact persistence to a debounced persistence worker.
6. Replace direct shared-SPI lock calls in UI-facing code with arbiter policies.
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
