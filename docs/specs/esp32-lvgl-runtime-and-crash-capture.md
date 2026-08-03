# ESP32 LVGL runtime and crash-capture contract

Status: authoritative for every ESP32 build that uses the shared LVGL shell.

This specification defines the boundary between an LVGL frame, foreground
operation feedback, external-font storage I/O, Reticulum work, screen sleep,
and persistent coredump capture. It supersedes any earlier advice that permits
a component to make its own immediate LVGL pass.

## 1. The failure this contract prevents

The `v0.1.39-alpha` T-LoRa Pager SX1262 coredumps include four matching
`LoadProhibited` exceptions in `lv_timer_handler()` on `loopTask`. The program
counter is in the timer-executor continuation immediately after an LVGL timer
callback. Two adjacent persistent LVGL timer addresses occur across the four
dumps, but the coredump does not capture those heap objects; therefore it does
not identify one callback with certainty.

The previous foreground-operation path was nevertheless a direct violation of
LVGL timer ownership: `OverlayImmediate` could invoke `lv_timer_handler()` and
`lv_refr_now()` from an input callback or from an existing LVGL timer callback.
The firmware-update polling timer exercised that path repeatedly. Reticulum
Ping completion and font-loading feedback could exercise it as well.

The correction is architectural, not a callback-specific workaround: no
operational UI component may begin a nested handler pass or force a refresh.

## 2. LVGL frame ownership

For Arduino ESP32 products, including the Pager, the sole normal owner of
`lv_timer_handler()` is
`platform/esp/arduino_common/src/display_runtime.cpp:tickIfDue`.

The normal order is:

```text
root lv_timer_handler()
  -> returns
  -> i18n post-frame work (at most one external-font transaction)
  -> loop-shell application/runtime work
  -> later root frame
```

Rules:

- UI object changes, invalidation, and snapshot selection stay on the LVGL
  owner context.
- Only the root display runtime may call `lv_timer_handler()` in normal
  operation.
- Operational callbacks, LVGL timer callbacks, event handlers, and overlay
  presenters must not call `lv_timer_handler()` or `lv_refr_now()`.
- An operational presenter requests a future frame by invalidating its root or
  top layer. A 20 ms frame interval is the intentional latency budget.
- The boot-only synchronized presentation in `startup_shell.cpp` and
  `startup_ui_shell.cpp` is a narrowly scoped exception: it runs before the
  normal loop is entered and cannot be called by a runtime callback.

The rule applies equally when an operation is initiated by an LVGL input
device. Being on the same FreeRTOS task is not permission to re-enter LVGL.

## 3. Foreground operation state

`ui::widgets::foreground_operation` is a snapshot arbiter. It selects one
slot by priority and updates `ProgressOverlayPresenter`; it is not a rendering
loop. Its policy set is intentionally limited to `Hidden`, `PageOnly`, and
`Overlay`. `OverlayImmediate` is forbidden.

An operation may request a present after a selection changes, but that request
only invalidates the top layer. The next root frame performs layout, refresh,
and timer scheduling together.

This covers firmware update progress, settings actions, package work, route
images, I18n font feedback, and Reticulum Ping. In particular, a periodic LVGL
timer may publish a changed firmware status, but it can never recurse into the
timer executor.

## 4. Post-frame external-font work

External SD-backed font activation may be slow and shares resources with the
display. It has two requirements:

1. the loading overlay must get a normal frame before the I/O begins; and
2. no external font I/O may run in an input/render callback or inside
   `lv_timer_handler()`.

The shared I18n registry records pending locale and content-supplement work
with a not-before frame number. A request made during frame `N` runs only after
the root handler has completed frame `N + 1`; its work therefore begins after
the subsequent handler return. `on_lvgl_frame_completed()` runs at most one
such transaction per frame.

The existing external-font filesystem scope remains the final display-bus
guard. If it reports that the bus is busy, the registry records a transient
failure and its retry timer only queues another post-frame attempt. The retry
timer never performs the file read itself.

Rebuilding the locale registry cancels any queued locale request before it
releases pack records. The completion receives `changed=false`, so a settings
page can rebuild its prior selection without retaining a stale pack pointer.

Locale selection follows this state machine:

```text
settings input
  -> request_locale_by_index() + Overlay snapshot
  -> one normal frame presents the overlay
  -> post-frame locale/font activation
  -> completion on LVGL owner context
  -> refresh localized menu and rebuild active settings page
```

Content text follows the same deferred path whenever it needs an external
supplement. Built-in and bounded flash-resident supplements remain allowed in
the render path.

## 5. Reticulum Ping ownership

The Chat and Contacts button callbacks may validate a Ping request and display
its `Overlay` snapshot. They must not do peer lookup, path request, packet
encryption, or radio transmission.

`LxmfAdapter::pingReticulumDestination()` now records the request in its
bounded `PingService`. `LxmfAdapter::processRuntime()`, which runs after the
root display frame through the normal application lifecycle, owns peer lookup,
path requests, encryption, radio TX, retries, and expiry. A delivered/timeout
result returns through `EventBus` and is rendered by the UI event dispatcher
on its owner context.

The queue is bounded, de-duplicates destinations, and retains its existing
timeout and retry policy. It is therefore an intent boundary, not an
unbounded background job list.

## 6. Screen sleep and saver

Screen-sleep state effects may run from an LVGL timer, but showing the saver
only changes its object state and calls `request_present()`. It does not force
`lv_refr_now()`. Input is still queued through the screen runtime; the normal
root frame makes the resulting visual state visible.

## 7. Persistent coredump capture

The Pager also produced a separate reboot-time fault in
`esp_core_dump_get_summary()` while exporting the prior raw coredump to SD.
The parser dereferenced the recorded exception task control block, so this
second failure happened after the raw payload had been copied but before the
flash coredump was erased.

The export transaction is therefore:

```text
validate flash coredump
  -> copy raw ELF payload to SD and flush
  -> write and flush generic sidecar metadata (erase_state=pending)
  -> erase flash coredump
  -> append erase result to sidecar
```

`esp_core_dump_get_summary()` is prohibited on the normal boot/export path.
The sidecar deliberately records `summary=deferred_to_offline_decoder`; use
the raw `.elf` together with the matching release symbol ELF on a host to
obtain a backtrace. If payload copy or the first metadata flush fails, the
flash coredump is retained for a later boot.

## 8. Verification obligations

- The ESP32 LVGL font-hot-path contract test rejects `OverlayImmediate`,
  presenter-side `lv_timer_handler()`/`lv_refr_now()`, and async font I/O from
  the LVGL handler. It verifies the post-frame ordering and Ping intent
  boundary.
- The coredump contract test rejects boot-time summary parsing and verifies
  that durable metadata is written before erasing flash.
- Pager builds must be compiled for `tlora_pager_sx1262`. Flashing is followed
  by a bounded serial capture to check boot, coredump export, and LVGL timing
  diagnostics.
