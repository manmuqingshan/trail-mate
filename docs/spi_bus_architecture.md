# Shared SPI Bus Architecture

## Document status

This document is the design contract for the ESP shared-SPI refactor. It is
written before the implementation migration so that the implementation can be
reviewed against explicit invariants instead of being judged by individual
timeout values.

The affected devices include boards where the display, SD card, LoRa radio,
external fonts, GPS/track persistence, USB mass-storage, or other peripherals
share one physical SPI controller. Boards without a shared SPI controller may
use the same API with a no-op backend, but they must not reintroduce a second
locking model.

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

The coordinator is not a collection of independent `StorageBusArbiter`
instances. Feature code receives a typed `SharedSpiAccess` handle or uses the
coordinator's transaction helper. The handle contains no independent mutex and
cannot release another handle's acquisition.

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

- A waiting `DisplayFrame` blocks new `Background` acquisitions.
- A waiting `RadioTiming` blocks new `Background` acquisitions unless the
  radio transaction has explicitly declared that it can be deferred.
- Within a class, the oldest request wins.
- A background request is never allowed to acquire repeatedly while an older
  foreground request is waiting.
- The coordinator may finish the transaction that already owns the bus, but it
  must not grant the next transaction to a lower class while a higher class is
  waiting.

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

Same-task nesting is allowed only through the coordinator and increments a
coordinator-owned depth counter. A legacy direct `unlock()` must not be able to
participate in nesting.

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
frames. The boot state machine may report `physical_present=true` only after a
completed display transaction.

### Radio

Radio calls hold the coordinator only across the chip-select command and the
hardware response. Packet parsing, queue manipulation, retries, and logging
occur after release. A radio API that cannot split those phases must declare its
maximum hold budget and is rejected in debug builds when it exceeds that
budget.

### SD and background storage

Every SD file or directory call is one transaction. Hydration and compaction:

- acquire for one filesystem operation;
- release;
- parse/copy/build application state outside the bus;
- re-check the foreground/display gate before the next operation.

No background worker may hold the bus while performing snapshot parsing,
repository updates, compression, or allocation.

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
   `shared_spi_lock_with_owner`, or direct `shared_spi_unlock()` outside the
   coordinator implementation and its compatibility tests. The generic
   `sys::runtime::StorageBusArbiter` may remain for non-SPI host/runtime tests,
   but it must not be instantiated as a shared-SPI implementation.
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
