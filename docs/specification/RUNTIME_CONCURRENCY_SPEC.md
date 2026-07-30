# Runtime and Concurrency Specification

This specification defines the concurrency baseline for Trail Mate targets. It
exists because ESP32, nRF52, Linux, and tests have different runtime mechanics
but must preserve the same ownership model.

The detailed design patterns, event simulation requirements, and burn-down
guidance for UI/storage responsiveness are defined in
`UI_STORAGE_EVENT_RUNTIME_DESIGN_SPEC.md`.

Active-path migrations that touch UI/storage/event responsiveness must satisfy
that document's UML coverage gate before implementation begins.

Meshtastic Android App BLE connection is the concrete phone-BLE protocol case for
the `ble_write` rule below. Its full connection sequence, GATT drain semantics,
config snapshot state machine, response-drain-before-save rule, and forbidden
bypasses are specified in `MESHTASTIC_ANDROID_BLE_CONNECTION_SPEC.md`.

## Sources of Concurrency

Trail Mate targets may receive work from:

```text
Radio IRQ
Radio RX task or poll loop
Radio TX completion
GPS UART RX
GPS parser task
BLE stack callback
BLE notify queue
UI event loop
LVGL tick/input
GTK main loop
ASCII terminal input loop
HostLink USB/serial RX
Storage write
Config update
Power/sleep/wake
Timer/retry/ACK timeout
```

## Mandatory Rules

ISR code may only defer.

BLE callbacks must not directly mutate app services.

Radio IRQ handlers must not run protocol or business logic.

GPS tasks must not directly update UI.

UI objects may only be updated on the UI owner thread/task.

Storage backends must declare their concurrency model.

Mutable app-service state must have a single owner context.

The UI owner context must not wait for blocking storage, device I/O, filesystem,
decode, or persistence work. UI paths may submit commands, consume
ready events, or attempt explicitly non-blocking work that can be abandoned
within the frame budget.

Cross-thread and cross-task interaction must use one of:

```text
event queue
command queue
immutable snapshot
declared mutex-protected store
```

## Canonical Event Paths

```yaml
radio_rx:
  path: "radio_irq -> radio_task -> mesh_event_queue -> app_task"
  rule: "IRQ defers; radio_task frames bytes; protocol/app processing runs outside IRQ."

gps_rx:
  path: "uart_irq -> gps_task -> location_event_queue -> app_task"
  rule: "GPS task may parse/normalize but may not directly mutate UI."

ble_write:
  path: "ble_callback -> phone_command_queue -> app_task"
  rule: "BLE stack callback owns transport timing only."

ui_input:
  path: "ui_thread -> presentation_action -> app_command_queue"
  rule: "Renderer translates input into UI actions; app owner executes business mutations."
```

## Forbidden Paths

```text
ble_callback -> ChatService
radio_irq -> MeshSession
gps_task -> lvgl
gtk_worker -> GtkWidget
ui_thread -> blocking storage write
ui_thread -> blocking device I/O wait
ui_thread -> filesystem open/read/write/list
ui_thread -> image decode from storage
ui_thread -> track file create/flush/list
ui_thread -> node/contact store synchronous save
ui_renderer -> direct radio access
ui_renderer -> direct GPS driver access
platform_driver -> direct message policy
```

## UI Thread Only

LVGL:

```text
lv_obj_* may only be called from the LVGL owner task/thread.
Other contexts post UI commands or publish snapshots.
```

GTK:

```text
GtkWidget mutation may only happen on the GTK main loop.
Workers use main-context invocation, channels, or idle callbacks.
```

ASCII/TUI:

```text
Terminal output has one renderer owner.
Input and refresh paths must not concurrently write stdout.
```

Headless:

```text
No renderer owner exists; state is exposed through logs, API, or snapshots.
```

## ISR Policy

ISR code may:

```text
clear interrupt status
record minimal flags
post lightweight ISR-safe events
```

ISR code must not:

```text
malloc/free
write storage
notify BLE
update UI
encode/decode protobuf
send direct messages
parse GPS sentences
perform crypto
```

## Storage Concurrency

Every storage backend must declare:

```text
single writer or multiple writers
reader model
transaction support
async write support
erase/write blocking behavior
required owner context or mutex
UI-owner behavior
queue/backpressure behavior
diagnostic fields for slow waits
```

Examples:

```text
ESP32 NVS: blocking, write-limited, usually mutex protected.
nRF52 flash: erase/write expensive, often async, callback-sensitive.
SQLite: transactional with file locks; must avoid UI-thread blocking.
```

## Mutable State Ownership

Every mutable service must have one owner:

```text
ChatService -> app service context
MeshSession -> mesh/app context declared by target
GpsService -> gps/app context declared by target
ConfigService -> app service context
DeviceStatusService -> app service context
UI State -> UI context or presentation owner
```

Other contexts may send commands, publish events, or consume snapshots.

## Slow Work Ownership

Slow work must have an explicit owner. Page widgets, LVGL timers, GTK callbacks,
and input handlers are not valid owners for durable storage, filesystem walking,
tile decode, device I/O waits, protocol retries, or persistence flushes.

Valid slow-work owners include:

```text
command worker
protocol runtime worker
map tile worker
track storage worker
StorageMaintenanceRuntime
ConfigPersistenceRuntime
domain store owner
declared platform service task
```

Slow-work owners communicate completion through events or immutable snapshots.
They must not mutate concrete UI objects directly.

## Simulation Requirement

Any runtime that introduces asynchronous command/event behavior must be
testable with deterministic simulated events. Tests must be able to script:

```text
command enqueue
event publish
worker completion
timeout
cancellation
storage delay
storage failure
bus arbitration delay
UI event drain
```

The simulator must assert that UI owner code does not execute blocking
storage/device-I/O/filesystem calls and that background code does not execute
concrete renderer calls.

## Storage Maintenance Owner

SD-backed maintenance is one active object, not one task per operation. The
owner task is created once, consumes a bounded command queue, and remains
blocked after maintenance reaches `Done`. A FreeRTOS task handle is an
implementation detail and must not be used as the business state.

The maintenance owner publishes an immutable `StorageRuntimeSnapshot` with a
monotonic `StorageOperationGeneration`. Every operation completion carries the
operation and generation that produced it. A completion for an older
generation is ignored and cannot move the current state backward.

The maintenance state machine is limited to:

```text
Dormant
  -> WaitingStartupGate
  -> Hydrating(generation)
  -> Ready
  -> WaitingIdle
  -> Persisting(generation)
  -> Compacting(generation)
  -> Done

Hydrating / Persisting / Compacting -> Backoff -> the same operation

Normal hydration, persistence, and compaction completion returns to `Ready`.
`Done` is reserved for an explicit stop or an exhausted retry policy. The
owner receives a latest-state maintenance demand rather than an unbounded
timer: pending immutable deltas request `Persist`, while reset intents or
compaction thresholds request `Compact`. When both are pending, active
foreground work is allowed to drain bounded persistence steps first; after
the idle gate is stable, compaction takes precedence because its snapshot
already includes the newest in-memory projections.
```

Foreground contexts may enqueue ticks and consume snapshots or one-shot
hydration-ready events. They may not mutate maintenance state or call the
maintenance backend directly. The owner sees only `ISemanticStorageAdapter`;
SPI tokens, chip-select pins, filesystem sessions, mutexes, and task handles
must remain behind that adapter.

Maintenance readiness must not stop the foreground UI, input, or event loop.
Foreground presentation reads observe a semantic not-ready result while the
authoritative projection is being installed, and retain a retryable view
state instead of treating an empty projection as valid data. Device sessions,
bus ownership, and lock details remain inside the storage adapters.

The adapter contract is intentionally incremental:

```text
begin(operation, generation)
step(operation, generation, budget)
cancelAtStepBoundary(operation, generation)
```

`StorageOperationBudget` is expressed in logical work items. A backend must
perform filesystem/device work outside its logical state lock, then take a
short bounded lock only to apply the decoded result or commit a generation.
For SD-backed repositories, a separate persistence lease serializes the
multi-step physical writer; foreground mutations enqueue immutable projections
or reset intents while that lease is held.

Retry semantics are part of the adapter contract, not an implementation
convention:

- `begin(operation, generation)` with a new generation initializes a new
  operation cursor.
- `begin(operation, generation)` with the same non-terminal operation and
  generation resumes the existing cursor after `RetryLater`, `StateBusy`,
  `DeviceUnavailable`, or another retryable result.
- The repository's logical maintenance-ownership lease remains held across
  those retries. The physical filesystem/device transaction lease may be
  released at the end of a bounded step, so the next `begin` resumes under
  the existing ownership without competing with foreground persistence.
- Exhausting the owner's retry policy is a terminal cancellation boundary.
  Before publishing `Done`, the owner must call
  `cancelAtStepBoundary(operation, generation)` so the adapter discards the
  cursor and releases every retained logical maintenance lease.
- A different generation, an explicit cancellation, or a terminal backend
  phase is the only valid reason to discard that cursor.

The three concurrency boundaries remain distinct:

```text
logical repository state lock
physical filesystem/device session
shared SPI transaction
```

Hydration and compaction must acquire the logical state lock with a bounded
wait, release it between protocol/journal units where the backend permits, and
never use `portMAX_DELAY` across SD I/O. Shared-SPI boards must wait for a real
display transaction completion before hydration. SDMMC or independent-SPI
boards must use an already-satisfied gate and must not inherit a display delay.

The board runtime exposes storage topology capabilities rather than making
the storage runtime maintain a board-name macro list. The capability
distinguishes shared-display SPI, dedicated SPI, and SDMMC.

## Configuration Persistence Owner

Configuration persistence is a separate owner from SD maintenance. It owns
configuration dirty state, debounce, immutable payloads, generation tracking,
critical flush requests, and retry decisions. It does not own SD hydration,
compaction, map tile reads, or repository maintenance.

The configuration owner exposes only semantic work:

```text
submit(snapshot, change_set, urgency)
takeDue(now_ms, work)
complete(generation, result)
```

The platform execution shell owns only task scheduling and adapter invocation.
It must not reconstruct the pending/in-flight state machine in a second set of
flags. `AppContext` facade methods are compatibility entry points; they submit
an edit or a persistence intent to the configuration owner.

The owner keeps three snapshots:

```text
baseline = last successful persisted snapshot
pending  = newest requested snapshot not yet started
active   = immutable snapshot currently being persisted
```

The following invariants are mandatory:

- `active` is never mutated while an adapter call is in flight.
- A completion with a stale generation cannot change owner state.
- A successful completion advances `baseline` before reconciling `pending`.
- If the latest configuration reverted while an older write was in flight, the
  latest value remains pending until it is persisted or explicitly cancelled.
- A failed completion invalidates `baseline` and retries conservatively.
- Configuration snapshots and protocol payloads must not be automatic locals on
  ESP task stacks.

Storage maintenance and configuration persistence may share result kinds,
generation semantics, retry policy conventions, and semantic adapter patterns,
but they must not share one giant state machine or business snapshot. Their
owners, payloads, and lifecycle remain independent.
