# ESP32 Display-First Startup Contract

## Purpose

The first user-visible boot state is a product contract, not a side effect of
the application runtime. The display must be powered, initialized, and able to
present a boot log before any slow or competing service is allowed to start.
This prevents a black screen from being mistaken for a dead device and gives
the user a visible explanation while storage and networking continue.

## Required order

The Arduino LVGL startup path is intentionally split into phases:

```text
reset / serial
    |
    v
display hardware: power, backlight, pins, controller, display SPI
    |
    v
LVGL + brightness
    |
    v
synchronous boot frame + boot log physical flush
    |
    v
board services: sensors, RTC, keyboard, LoRa, audio, rotary, power button
    |
    v
SD mount and debug-log setup
    |
    v
AppContext, Wi-Fi/MQTT, deferred storage, and other background tasks
```

`beginDisplayHardware()` is the only board entry point allowed before
`beginBootUi()`. `beginServices()` is called only after `beginBootUi()` has
completed its synchronous first-frame render. The display-first phase may
configure the power rails required by the display, but it must not initialize
LoRa, sensors, audio, input tasks, Wi-Fi, MQTT, or SD file contents.

The T-LoRa pager implements this contract with:

- `TLoRaPagerBoard::beginDisplayHardware()`
- `TLoRaPagerBoard::beginServices()`
- `platform::esp::boards::initializeBoardDisplayHardware()`
- `platform::esp::boards::initializeBoardServices()`

Other board targets currently retain their legacy board bootstrap behind the
same platform-level API until their board classes expose equivalent split
entry points. They must not add a second startup ordering mechanism; migration
is complete only when each target has a real display-only phase.

## Why this is an architectural fix

The old path called the complete board `begin()` before LVGL. LoRa probing,
sensor/RTC setup, audio initialization, rotary task creation, and other
operations could delay or contend with the first display transaction. A
timeout or an extra retry could not guarantee that the first frame was ever
visible.

The new contract makes the dependency explicit: the boot UI is the barrier
between hardware visibility and service startup. It also keeps the existing
SPI coordinator semantics intact: the first frame must complete as a physical
display transaction before shared-SPI users are started.

## Review invariants

1. The startup runtime must call `initializeBoardDisplayHardware()` before
   `display_runtime::initialize()`.
2. `beginBootUi()` must run before `initializeBoardServices()`.
3. SD initialization and `AppContext` initialization must remain after the
   first boot frame.
4. No board service may be started from the display-only entry point.
5. A target-specific migration must not reintroduce the removed legacy SPI
   lock/arbiter implementation.
