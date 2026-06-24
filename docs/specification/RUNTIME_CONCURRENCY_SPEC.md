# Runtime and Concurrency Specification

This specification defines the concurrency baseline for Trail Mate targets. It
exists because ESP32, nRF52, Linux, and tests have different runtime mechanics
but must preserve the same ownership model.

The detailed design patterns, event simulation requirements, and burn-down
guidance for UI/storage responsiveness are defined in
`UI_STORAGE_EVENT_RUNTIME_DESIGN_SPEC.md`.

Active-path migrations that touch UI/storage/event responsiveness must satisfy
that document's UML coverage gate before implementation begins.

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

The UI owner context must not wait for blocking storage, shared-SPI,
filesystem, decode, or persistence work. UI paths may submit commands, consume
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
ui_thread -> blocking shared-SPI wait
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
tile decode, shared-SPI waits, protocol retries, or persistence flushes.

Valid slow-work owners include:

```text
command worker
storage worker
protocol runtime worker
map tile worker
track storage worker
persistence worker
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
storage/shared-SPI/filesystem calls and that background code does not execute
concrete renderer calls.
