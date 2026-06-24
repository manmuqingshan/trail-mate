# C6 Companion Architecture Specification

## Purpose

This specification defines the Trail Mate ESP32-C6 companion boundary for
ESP32-P4 targets such as Tab5 and T-Display-P4.

The companion exists so the P4 product can use BLE, ESP-NOW, and later Wi-Fi
surfaces without moving Trail Mate business truth onto the C6.

As of 2026-06-13, the active development slice is the first full C6 wireless
facade firmware followed by external C6 flashing and P4-C6 validation. The
phase is not P4-forced C6 recovery and not runtime slave OTA.

## Core Rule

```text
P4 owns meaning.
C6 owns wireless surfaces.
HostLink carries every P4-C6 message.
```

The C6 is not a second Trail Mate business host. It is a wireless facade and
transport peer.

## Ownership

P4 owns:

- LoRa, GPS, maps, storage, UI, MeshCore, Meshtastic, Team Mode, and app
  services.
- Business configuration, channel state, node state, message history, keys, and
  final team state.
- BLE/ESP-NOW/Wi-Fi runtime configuration decisions.
- The downgrade decision when the C6 is missing, unsupported, or running an
  incompatible protocol version.

C6 owns:

- BLE GAP/GATT surface mechanics.
- ESP-NOW send/receive mechanics.
- Wi-Fi management facade mechanics.
- Local wireless queues, connection handles, notification state, and short-term
  peer caches.
- HostLink session state.
- C6-local diagnostic settings that do not change Trail Mate business truth.

The first full facade firmware may implement:

- Meshtastic-compatible BLE GATT raw transport.
- MeshCore/NUS-compatible BLE GATT raw transport.
- Trail Mate private BLE GATT raw transport.
- ESP-NOW raw Team transport send/receive.
- Wi-Fi scan, STA connect/disconnect/get IP, and AP start/stop management.
- Config and diagnostic reports.

This does not grant C6 ownership of Meshtastic packets, MeshCore routing, Team
membership, keys, NodeDB, messages, LoRa, GPS, maps, storage, or UI.

## Forbidden C6 State

C6 firmware must not persist:

- Meshtastic channel keys.
- MeshCore private keys.
- Team keys.
- Trail Mate user identity keys.
- NodeDB.
- Message history.
- Map cache.
- LoRa configuration authority.

Any temporary key material needed by BLE or ESP-NOW must be supplied by P4 and
kept in RAM unless P4 explicitly authorizes persistence.

## HostLink C6 Protocol

The C6 HostLink protocol is distinct from the existing Data Exchange HostLink
USB/CDC protocol.

The shared C6 wire definitions live in:

```text
modules/core_hostlink/include/hostlink/c6/c6_protocol.h
modules/core_hostlink/include/hostlink/c6/c6_frame_codec.h
modules/core_hostlink/src/c6_frame_codec.cpp
```

The frame format is binary little-endian, uses magic `0x36434D54`, protocol
version `1`, a 20-byte header, and CRC32 over the header with the `crc32` field
zeroed plus the payload. The default maximum payload is 1024 bytes.

All upper-layer messages must preserve both `frame_type` and `channel` so P4
can route BLE Meshtastic, BLE MeshCore, BLE Trail Mate private, ESP-NOW Team,
Wi-Fi, and diagnostics explicitly.

Supported feature bits are compile-time and implementation claims. Enabled
feature bits are runtime configuration results and must only be reported after
P4 sends `CONFIG_SET` or asks for a configuration report. P4 must not treat
`requested_features` as C6 capability evidence.

## Phase 0 Contract

Phase 0 may provide only:

- Shared protocol definitions and codec tests.
- A P4-side C6 companion abstraction/stub.
- A C6 ESP-IDF project scaffold.
- A bootable C6 firmware shell that waits for P4.
- P4 behavior that survives a missing C6 and reports an explicit missing state.
- A `platform::ui::wireless_companion` diagnostic projection that exposes
  `unsupported`, `not_started`, `missing`, `transport_pending`, `present`, or
  `error` without making the shared UI depend on ESP-IDF C6 headers.
- A diagnostics surface on C6-capable P4 targets that can show
  `C6 transport pending` while SDIO/ESP-Hosted HostLink is not implemented.

Phase 0 must not pretend that BLE, ESP-NOW, Wi-Fi, or SDIO HostLink are already
operational.

## Phase 1 Boundary

Phase 1 HostLink bring-up starts only when both sides have a real transport
binding. Protocol definitions, HELLO/PING frame templates, and a bootable C6
firmware shell are necessary but not sufficient.

Before real SDIO or ESP-Hosted transport is bound, P4 must report:

```text
state = transport_pending
present = false
detail = sdio_transport_not_bound
```

When a real SDIO probe is attempted, P4 may use a more specific pending detail:

```text
state = transport_pending
present = false
detail = sdio_transport_probe_pending
```

`present` is reserved for a completed P4-C6 exchange:

```text
P4 sends HELLO
C6 returns HELLO_ACK
P4 sends PING
C6 returns PONG
P4 sends CONFIG_SET
C6 returns CONFIG_REPORT
P4 records protocol, firmware, features, enabled services, service states, and
heap values from C6
```

Diagnostics may show locally generated HELLO/PING templates in
`transport_pending`, but those templates are not evidence that C6 is present.
Before `HELLO_ACK` is decoded, P4 must keep C6 `supported_features` unknown
or zero. A P4 `requested_features` mask is only an outbound request and must
not be projected to UI or product logic as C6 capability evidence.

The first real Phase 1 implementation uses ESP-IDF SDIO packet transport:

```text
P4: esp_serial_slave_link over SDMMC slot 1
C6: esp_driver_sdio slave packet mode
```

The C6 firmware must decode and encode frames with the shared C HostLink codec.
The P4 firmware may use the C++ codec facade, but C/C++ cross-codec smoke tests
must prove that both sides compute the same CRC and preserve `frame_type`,
`channel`, `seq`, `ack`, flags, and payload bytes.

Startup must remain bounded. If SDIO probe, HELLO_ACK, or PONG fails, P4 must
continue booting the product and report either `missing` or `error` with a
specific detail. A failed probe must not block UI, GPS, SD card, LoRa, or other
P4-owned product surfaces. SDIO card probing may retry for a short bounded
window to let the C6 finish reset and slave startup, but it must not become an
unbounded board boot gate.

SDIO enumeration is only transport evidence. An SDIO card with ESP32-C6 I/O
functions and readable CCCR state does not prove that Trail Mate HostLink is
available. If ESSL function-ready or the later HELLO/PING/PONG exchange fails,
P4 must report the failure detail instead of promoting enumeration to
`present`.

For T-Display-P4, C6 SDIO startup runs before SD card startup because the board
family uses SDMMC slot 1 for C6 and SDMMC slot 0 for the SD card. Any later SD
card changes must preserve that order or explicitly implement shared SDMMC host
ownership. The T-Display-P4 XL9535 IO14 C6 control follows LilyGo's SDIO
examples: release high, assert low, release high before probing SDIO. For Tab5,
the recorded C6 reset GPIO remains unvalidated and must not be pulsed as a
recovery control.

## C6 Firmware Update Boundary

There are two different update meanings and they must not be collapsed:

```text
Runtime slave OTA:
  P4 and C6 are both running compatible firmware.
  The existing ESP-Hosted or HostLink transport is alive.
  P4 sends a C6 image to the C6 OTA partition through that transport.

Forced recovery / esptool-style flashing:
  C6 may be blank or broken.
  P4 must control the C6 download, reset, and boot straps, or an external
  programmer/USB path must be used.
```

Runtime slave OTA is allowed after Phase 1 transport is real and versioned.
Forced recovery must not be promised unless the board facts prove that P4 can
drive the required C6 reset/download/BOOT lines. Tab5 reset GPIO54 remains
unvalidated and overlaps Grove/Port A I2C SCL, so it must not be treated as a
validated C6 recovery control.

The current Phase 1 implementation does not yet implement C6 image transfer or
`host_performs_slave_ota` semantics. The `TM_C6_FEATURE_SLAVE_OTA` bit is
reserved in the protocol but must not be advertised in `requested_features` or
`supported_features` until an OTA frame sequence and image source are
implemented and validated. Product claims must distinguish:

```text
HostLink present:
  HELLO_ACK and PONG succeeded over real SDIO packet transport.

C6 runtime OTA available:
  HostLink or ESP-Hosted transport is present, an image source is selected,
  chunked write/verify/end semantics are implemented, and the C6 OTA partition
  activation path is tested.
```

## External First Flash Boundary

The first full C6 facade firmware is validated by external flashing.

For T-Display-P4-class hardware, the C6 UART0 pins exposed on a Qwiic-shaped
connector are UART pins, not an I2C Qwiic protocol surface. External flashing
requires a 3.3V USB-UART path wired to C6 UART RX/TX/GND and the board's
BOOT/EN controls. ESP32-C6 enters the ROM serial bootloader when GPIO9 is held
low during reset.

P4 must not promise esptool-style forced C6 flashing unless board facts prove
that P4 can drive all required C6 reset, UART, and BOOT strap signals. The
known T-Display-P4 schematic facts allow P4-side C6 reset control, but C6
GPIO9/BOOT is controlled by the physical boot switch and is not currently a
validated P4-controlled recovery signal.

Missing SD card media does not block P4-C6 SDIO. C6 uses the C6 SDIO link; the
SD card is a separate storage surface and must remain a P4-owned feature.

## First Full Facade Firmware

The first full C6 facade firmware is a wireless facade, not a second product
runtime.

It must:

- Start wireless surfaces only from P4 configuration.
- Preserve HostLink `frame_type` and `channel` on every uplink and downlink.
- Forward BLE and ESP-NOW payloads as raw bytes.
- Apply BLE pairing mode and passkey mechanics as wireless access control only.
- Report service state through `CONFIG_REPORT` and diagnostics.
- Keep Wi-Fi credentials in RAM unless P4 explicitly requests persistence.
- Keep the C6 app partition large enough for BLE, ESP-NOW, Wi-Fi, SDIO, and
  diagnostics.

It must not:

- Parse Meshtastic protobufs.
- Parse MeshCore business messages.
- Decide Team membership or final pairing state.
- Persist product keys or message history.
- Expose Wi-Fi data-plane product behavior unless a future specification grants
  that surface explicitly.

The current partition policy for the full facade firmware assumes 4MB C6 flash
and uses a single 3MB factory app partition. This is compatible with external
first flashing and intentionally does not reserve OTA slots for the current
phase.

BLE advertisement payloads must stay within legacy advertising size limits.
When multiple 128-bit profile services are enabled, C6 may rotate advertised
service UUIDs while keeping all enabled GATT services registered.

BLE pairing mode is P4 configuration, not C6 business state. In the first full
facade firmware, P4 may request fixed PIN, random PIN, or debug no-PIN mode.
C6 may enforce encrypted/authenticated GATT access and may keep bond material
in RAM while the firmware is running, but `CONFIG_BT_NIMBLE_NVS_PERSIST` remains
disabled for this phase. C6 must not log the configured PIN and must not persist
BLE pairing material as product identity, Team state, MeshCore keys, or
Meshtastic channel state.

## Functional Check Baseline

The in-repository C6 firmware is not considered functionally checked merely
because the ESP-IDF project configures. The minimum repository check must cover:

- `tm_services_init` starts from a disabled service state and exposes a bounded
  report.
- `tm_services_apply_config` enables and disables requested BLE, ESP-NOW, and
  Wi-Fi facade bits without claiming unsupported feature bits.
- Feature masks preserve the boundary between supported features, requested
  features, and enabled runtime services.
- Diagnostics report firmware version, protocol range, heap values, supported
  feature bits, enabled feature bits, and service state.
- BLE, ESP-NOW, and Wi-Fi components initialize as facade mechanics and remain
  disabled until P4 HostLink configuration arrives.
- HostLink startup is present and does not move Trail Mate business state onto
  C6.

The required evidence is both:

- host-side functional smoke tests for the service/config/report behavior; and
- a real ESP-IDF `esp32c6` build of `firmware/c6_companion`.

Current local repository evidence from 2026-06-13 satisfies that build baseline:

- `trailmate_c6_services_functional_smoke` covers service init, config apply,
  feature mask reporting, disabled-by-default service state, and diagnostics
  reporting.
- ESP-IDF built `firmware/c6_companion` for `esp32c6` and produced
  `.tmp/build.c6_companion_real/trail-mate-c6-companion.bin`; binary size
  `0x129560`, smallest app partition `0x300000`, free `0x1d6aa0` (61%).

This baseline still does not prove live RF behavior, SDIO HostLink exchange, or
on-device interoperability. Those require board flashing and P4-C6 runtime
validation.

## Firmware Layout

The C6 companion firmware is an in-repository peer project:

```text
firmware/c6_companion/
```

It is a companion firmware project, not an app shell and not a build entrypoint
for the P4 product firmware. It may consume shared protocol headers from
`modules/core_hostlink/include/hostlink/c6`.

C6 build, flash, and monitor tasks must remain separate from P4 build, flash,
and monitor tasks.

## Review Rules

New C6 work must answer:

- Is this wireless surface mechanics, or Trail Mate business meaning?
- Does every P4-C6 byte travel through the C6 HostLink frame?
- Is the HostLink channel preserved?
- Does P4 still own configuration and downgrade decisions?
- Does C6 avoid persisting business state and secrets?
- Does the implementation report missing/unsupported states explicitly?
