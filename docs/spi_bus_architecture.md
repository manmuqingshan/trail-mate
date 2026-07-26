# Shared SPI Bus Architecture

## Document status

This is the single authoritative specification for the shared-SPI mechanism.
It describes a technical resource boundary, not any product feature. Map,
chat, contacts, localization, tracking, team, package, and protocol documents
must not repeat its lock, token, priority, or deadline rules.

The implementation is reviewed against the invariants in this document, not
against individual timeout values or feature-local conventions.

The mechanism is selected from the board's physical topology. It applies to
boards where the display, SD card, LoRa radio, external fonts, GPS/track
persistence, USB mass-storage, or other peripherals share one physical SPI
controller. Boards without a shared SPI controller use their native independent
bus or SDMMC driver and do not enter this coordinator. A no-op backend is valid
only for an API-compatible device path that is physically independent; it must
not hide a second locking model.

The current board profiles are intentionally different:

- T-LoRa Pager, T-Deck, and T-Deck Pro place display, SD, and LoRa on the same
  Arduino SPI bus. Their board configuration enables the SdFat shared-SPI
  adapter.
- T-Display P4 uses SDMMC for the SD card and a separate SPI path for LoRa.
  SDMMC must not acquire the shared-SPI coordinator.
- Future boards must declare one coordinator per actual physical bus. A board
  may have no coordinator, one coordinator, or several independent
  coordinators; the business layer remains unaware of that topology.

## Why the old model failed

The old implementation had one physical mutex but several independent policy
wrappers:

```text
display ───────────────┐
SD runtime guard ──────┤
radio arbiter ─────────┤
font arbiter ──────────┤── one physical mutex
track/USB/team arbiter ┤
legacy direct unlock ──┘
```

Those wrappers did not share a request queue, priority order, deadline view, or
ownership token. They only added different labels and timeout calculations
around the same mutex. Background hydration therefore competed with display
flushes even when the display had a frame deadline.

The previous display path also treated a failed SPI acquisition as a successful
LVGL flush. `pushColors()` could return after a timeout and the LVGL callback
would still call `lv_display_flush_ready()`. That silently dropped the frame
and made a lost first frame look like a black screen or a stuck startup page.

The refactor must remove both failure modes. Changing a timeout, task priority,
or retry delay without changing these semantics is not an architecture fix.

## Goals and non-goals

### Goals

The new design must:

1. Have one owner of the physical shared-SPI resource.
2. Give display and radio requests a globally visible priority and deadline.
3. Make SD, hydration, compaction, font, track, team, and USB work
   preemptible at transaction boundaries.
4. Use a strict acquisition token that can be released exactly once by the
   acquiring task.
5. Report acquisition, hold, release, timeout, and display-flush outcomes with
   enough information to diagnose starvation.
6. Make display flush failure explicit. A failed transaction must not be
   reported as a successful LVGL flush.
7. Make the first boot frame and wake redraw observable as completed physical
   transactions, not merely as LVGL function calls.
8. Preserve synchronous public APIs where they are required by existing
   hardware drivers, while moving arbitration and ownership into one common
   implementation.
9. Allow a no-op backend for targets that do not physically share SPI.

### Non-goals

This design does not:

- make SD file-system operations interruptible in the middle of a FATFS call;
- make a radio transaction safe to interrupt between chip-select and the end of
  a command;
- guarantee that a caller waiting forever will eventually acquire the bus;
- use a second per-board or per-feature mutex as a hidden SPI arbiter;
- solve unrelated Wi-Fi, MQTT, LVGL object lifetime, or screen-power bugs.

The unit of scheduling is a complete hardware-safe SPI transaction. Long
software work such as parsing, object construction, compression, and protobuf
decoding must occur outside the transaction.

## Core model

The implementation provides one `SharedSpiCoordinator` per physical shared
bus. The coordinator is the single serialized ownership gate and owns all
waiter, token, and ownership metadata. The critical section inside the
coordinator protects that metadata; it is not a second feature-level SPI
mutex. Code must not bypass the coordinator to touch a shared peripheral.

```text
request(resource, class, deadline, owner)
                 │
                 ▼
        SharedSpiCoordinator
        ┌──────────────────────┐
        │ one ownership gate   │
        │ one waiter state     │
        │ one owner token      │
        │ one diagnostics set  │
        └─────────┬────────────┘
                  │
       ┌──────────┼──────────┐
       ▼          ▼          ▼
    display      radio       storage
    deadline     timing      background
```

The coordinator is not a collection of independent storage arbiters. The
coordinator API is an infrastructure API. It is callable only
from device and platform I/O owners, never from business modules or UI
feature/runtime code.

Device owners translate semantic requests into private hardware transactions:

```text
business/runtime request
        -> device service
        -> device transaction executor
        -> SharedSpiCoordinator
        -> peripheral driver
```

The device executor may carry policy, command identity, deadlines, owner
labels, and tokens internally. None of those details cross the device service
boundary. A map tile request is a tile request; it is not a bus request. A
message persistence request is a storage request; it is not a token request.

### Memory and DMA boundary

Shared-SPI correctness includes the memory used around a transaction. ESP
hardware has limited internal RAM, so large protocol, file, packet, decoded
payload, and runtime scratch objects must follow these rules:

- Prefer PSRAM for long-lived device objects and reusable scratch storage.
  `heap_caps_malloc_prefer` must request `MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT`
  first and use internal 8-bit heap only as an explicit fallback.
- Do not create protocol-sized automatic locals in ESP hot paths. Use a member
  scratch slot, a fixed-depth ring slot with clear ownership, or caller-owned
  output storage. A scratch slot may be reused only after the previous
  operation has completed.
- Keep DMA, cache, semaphore, and driver objects in internal memory when the
  hardware or SDK requires it. PSRAM is not a universal replacement for
  DMA-capable storage.
- A physical bus transaction must not retain a large application buffer or
  protobuf object longer than necessary. Encode, decode, parse, and allocate
  outside the physical coordinator ownership window.

The memory placement decision belongs to the device/platform owner. Business
code does not select heap capabilities or carry SPI scratch buffers.

### Visibility boundary

The following concepts are implementation details of the device/platform
layer and must not appear in business or UI-facing headers:

- `SharedSpiCoordinator`
- `BusAccessPolicy`
- `BusAcquireRequest`, `BusAcquireResult`, and `BusAcquireStatus`
- `BusAccessToken`, `ScopedBusAccessToken`, and direct release operations
- the private SD logical-filesystem session guard and SdFat bus hook
- `PersistenceBusGate` or any equivalent outer admission gate
- shared-SPI resource identifiers and owner labels

Business code receives semantic operation results such as `Completed`,
`RetryLater`, `Missing`, `Unavailable`, or `Failed`. The device owner decides
whether a result came from arbitration, a peripheral timeout, media removal,
or an I/O error.

## Request classes and ordering

Requests are ordered by class, deadline, and age:

1. `DisplayFrame`: a frame already handed to the display flush callback.
2. `RadioTiming`: a transaction required to complete an active radio command or
   IRQ service.
3. `Interactive`: a foreground user action that is not frame-critical.
4. `DurableCommit`: a bounded user-requested save or stop/flush operation.
5. `Background`: hydration, discovery persistence, track samples, compaction,
   font prefetch, and other maintenance.

The ordering rules are:

- The device adapter selects the request class from the semantic operation. A
  file read needed for content currently visible to the user is `Interactive`;
  preload, hydration, compaction, and maintenance remain `Background`. The
  business-facing API does not expose the bus policy used for that selection.
- A waiting `DisplayFrame` blocks new `Background` acquisitions.
- A waiting `RadioTiming` blocks new `Background` acquisitions unless the
  radio transaction has explicitly declared that it can be deferred.
- Within a class, the oldest request wins.
- A background request is never allowed to acquire repeatedly while an older
  foreground request is waiting.
- The coordinator may finish the transaction that already owns the bus, but it
  must not grant the next transaction to a lower class while a higher class is
  waiting.
- A slow transaction updates coordinator health diagnostics and may emit a
  warning, but it does not create a cross-business cooldown or reject a later
  radio, display, or storage request. Fairness comes from request class,
  deadline, and age ordering. The coordinator never treats map, radio,
  display, and storage as one worker class.

The coordinator does not interrupt an active hardware-safe transaction. This is
why all callers must keep the transaction small and must release the bus before
parsing or constructing application objects.

## Ownership contract

Every successful acquisition returns a `SharedSpiToken` containing:

- bus generation;
- acquiring task handle;
- nesting depth (same-task acquisitions must be released LIFO);
- owner label;
- request class;
- acquisition timestamp;
- one-shot validity bit.

Release is valid only when:

1. the token is valid;
2. the current task is the acquiring task;
3. the token generation matches the coordinator's current owner;
4. the token has not already been released.

Any mismatch is a diagnostic failure and does not clear the coordinator's
ownership. A double release must never be able to unlock another task's
transaction.

Same-task nesting is allowed only inside one device transaction executor and
increments a coordinator-owned depth counter. A business caller must never see
the nesting or participate in it. A legacy direct `unlock()` must not be able
to participate in nesting.

## Transaction boundary rules

### Display

The display transaction includes address-window setup and pixel transfer. It
also includes multi-command control sequences such as rotation changes. It must
not include LVGL layout, font lookup, buffer allocation, or application state
updates.

The display flush API returns a transaction result:

- `Completed`: pixels were sent and the caller may complete the LVGL flush.
- `Busy`: the transaction was not started; the frame remains pending for a
  bounded retry.
- `Unavailable`: the bus or display is not initialized; the frame remains
  pending and a diagnostic is emitted.
- `Failed`: the transaction started but the driver reported failure; the
  caller completes the failure path and requests a full redraw.

`lv_display_flush_ready()` is called only after `Completed` or after the
explicit recovery path has recorded a dropped/invalidated frame. A lock timeout
must not be silently presented as success.

The first boot frame and the first wake redraw use the same path as normal
frames. A completed display transaction means only that the coordinator granted
the bus and the driver returned after issuing the SPI write. On a write-only
panel interface this is not a physical pixel acknowledgement: without a panel
readback, TE signal, or an external probe, software cannot prove that the
controller rendered the pixels. The boot state machine must therefore expose
this as `display_transaction_completed`, never as `physical_present`.

This distinction is an architectural invariant, not just a logging preference:

| Evidence | What it proves | What it cannot prove |
| --- | --- | --- |
| Coordinator token acquired | This task was granted the shared SPI resource | CS/DC wiring, controller state, or pixel visibility |
| `pushColorsResult()` returned `true` | The driver issued the SPI write and released the token | That the panel accepted the command or rendered it |
| LVGL flush completed | LVGL's software frame lifecycle advanced | That the panel contains the expected pixels |
| Backlight is on | The backlight power path is active | Panel reset, initialization, address window, or pixel transfer |

Any boot retry or health decision must use the strongest evidence actually
available. A write-only display path must not synthesize a physical
acknowledgement from a software counter.

### Radio

Radio calls hold the coordinator only across the chip-select command and the
hardware response. Packet parsing, queue manipulation, retries, and logging
occur after release. A radio API that cannot split those phases must declare its
maximum hold budget and is rejected in debug builds when it exceeds that
budget.

### SD and background storage

The SD adapter has two distinct scopes:

- A logical filesystem session serializes SdFat object access. It belongs to
  the SD device adapter and is invisible to business code.
- The physical shared-SPI ownership is acquired by the SdFat driver at its
  `activate`/`deactivate` transaction boundary. On shared-SPI boards, payload
  reads and writes are sliced to at most one 512-byte sector so the display or
  radio can be granted between physical transactions. On SDMMC or independent
  buses, this hook is not used.

The logical session may remain open while an interactive read-only `FsFile`
object is alive, but it must not hold the physical coordinator across sectors.
The session must not include parsing, UI work, repository updates, compression,
or application-object construction. Background hydration and compaction must:

- perform one bounded adapter operation;
- release the adapter session and physical bus;
- parse/copy/build application state outside the device layer;
- re-check the foreground/display state before the next operation.

Once a file has been opened, every return path must close it through the SD
device adapter before reporting `Ready`, `RetryLater`, or `Failed`. A
filesystem call itself remains non-interruptible; only the lower-level physical
SPI transaction boundaries are schedulable.

### Font, track, team, and USB

These paths use the same coordinator and request class as their actual work.
They do not construct a local `StorageBusArbiter`, local mutex, or direct
`shared_spi_unlock()` call.

## Display failure semantics

The display path must expose an explicit result to the LVGL integration. The
following sequence is forbidden:

```text
SPI acquisition failed
  -> return from pushColors()
  -> call lv_display_flush_ready()
```

The supported sequence is:

```text
flush requested
  -> coordinator grants DisplayFrame
  -> transfer completes
  -> release token
  -> lv_display_flush_ready()
```

If the coordinator cannot grant the frame before the bounded retry budget:

```text
flush requested
  -> coordinator reports Busy
  -> frame is marked pending
  -> lower-priority work is not granted while pending
  -> retry is scheduled
  -> LVGL is completed only by the defined recovery path
```

The implementation must maintain counters for:

- frame requests;
- completed frames;
- busy retries;
- failed transfers;
- invalidated/dropped frames;
- maximum frame wait;
- maximum frame hold;
- current owner and waiter class.

## Startup and wake ordering

For shared-SPI boards the startup sequence is:

```text
hardware/display controller init
  -> create boot UI
  -> submit first boot frame
  -> wait for physical present acknowledgement
  -> mount SD and initialize application context
  -> start deferred hydration
  -> enter normal UI loop
```

The wake sequence is:

```text
wake request
  -> power state machine enables panel
  -> submit redraw through coordinator
  -> wait for physical present acknowledgement
  -> expose interactive input
```

The boot UI must not perform synchronous LVGL refresh calls that bypass the
display transaction result. `lv_timer_handler()` and `lv_refr_now()` may be
used to schedule a frame, but they are not physical-present acknowledgement.

## Self-consistency assessment

The design is internally consistent under the following assumptions:

1. Every shared-SPI caller is migrated to the coordinator.
2. No caller retains a direct physical mutex handle.
3. Every hardware transaction can be bounded and split from software work.
4. LVGL flush completion is coupled to a real transaction result.
5. Hydration and compaction re-check the foreground gate between operations.
6. The coordinator is initialized before any display, SD, or radio request.

Under those assumptions, the design addresses the observed failures:

| Observed failure | Mechanism in this design |
| --- | --- |
| Display timeout while hydration is active | Display waiter is globally visible and blocks new background grants |
| `unlock_skip` / owner mismatch | One coordinator-owned token and strict release validation |
| Black boot screen | First frame requires physical-present acknowledgement |
| LVGL believes a dropped frame succeeded | Flush completion is driven by explicit transaction result |
| Background compaction hurts UI | Compaction is sliced at transaction boundaries and gate-checked |
| Radio and display starve one another | Global class ordering and age within class |
| More local arbiters make behavior less predictable | Feature-local arbiters are removed |
| Future AI reintroduces direct locking | Documented invariant and forbidden API surface |

The design cannot guarantee success if a hardware driver blocks inside one SPI
transaction for an unbounded time. That is an explicit remaining hardware
constraint. The implementation therefore adds hold-time diagnostics and tests
that fail when a transaction exceeds its declared budget.

The first implementation review has also validated the migration boundary
against the current repository:

- the coordinator is compiled by the `tdeck` and `tlora_pager_sx1262` Arduino
  environments;
- both environments link successfully after removing the old physical mutex
  and local SPI arbiters;
- the direct-call contract script passes with no legacy lock names in
  production source;
- the IDF generated source list no longer references the deleted legacy
  implementation.

These are compile and structural proofs, not hardware proofs. A device-level
test with SD hydration and radio activity is still required before the change
can be declared operationally complete.

## Acceptance criteria before declaring the migration complete

The refactor is complete only when all of the following are true:

1. `git grep` finds no production use of `SharedSpiBusAdapter`,
   `shared_spi_lock_with_owner`, `StorageBusArbiter`, or direct
   `shared_spi_unlock()` outside the coordinator implementation and its
   compatibility tests.
2. Display flush has an explicit success/failure result and never reports a
   lock timeout as a successful transfer.
3. Unit tests cover priority ordering, FIFO within a class, strict release,
   same-task nesting, timeout, retry, and background slicing.
4. A host test covers the startup and wake state transitions with a mocked
   display transaction.
5. ESP builds pass for at least `tdeck` and `tlora_pager_sx1262`.
6. Runtime logs expose the request/grant/complete/release counters described
   above.
7. Under a synthetic background-SD load, the first boot frame and wake redraw
   complete without a display timeout.
8. `detect_changes()` shows only the intended SPI, display, storage, radio,
   startup, tests, and documentation flows.

## Migration and removal policy

The migration is intentionally one package. A compatibility layer may exist
temporarily inside the coordinator implementation while callers are migrated,
but it must not remain as a public second API. After all callers move:

- delete the old physical-lock header and implementation;
- delete `StorageBusArbiter`, `SharedSpiBusAdapter`, and fixed local policies;
- delete board-local radio arbiter instances;
- delete direct display mutex ownership fields;
- delete timeout-based display-drop behavior;
- delete compatibility wrappers that allow direct unlock.

Leaving the old implementation reachable would allow the next change to
reintroduce the exact split-brain ownership model this document is designed to
prevent.
