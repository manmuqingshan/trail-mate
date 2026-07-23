# uConsole desktop feature parity

This document records the uConsole desktop feature audit against the ESP32
application catalog. The desktop shell reuses the existing Trail Mate domain,
protocol, radio, GPS, tracker, map, and package runtimes. It does not introduce
or depend on `meshtasticd`; Meshtastic traffic continues through Trail Mate's
native implementation.

## Interaction model

- Target geometry: 1280 x 720 landscape with keyboard and pointer.
- A 168 px navigation rail that can be collapsed with `\`, task-focused desktop
  workspaces, and a persistent 30 px keyboard/status bar.
- T-Deck visual language is retained through the embedded cream, amber, brown,
  gold-border, and blue/green status palette, while compact cards and strong
  active-state contrast are adapted to uConsole density.
- `F1`/`H` opens a Pager/T-Deck-style shortcut reference; direct workspace
  letters and `[`/`]` navigation keep every feature reachable without touch.
- Related low-frequency ESP screens are combined where a desktop workbench is
  more efficient. Energy Sweep, SSTV, and Walkie share **Radio tools** while
  keeping their independent runtimes and controls.

## Feature matrix

| ESP32 capability | uConsole desktop surface | Backend reuse | Status |
| --- | --- | --- | --- |
| Dashboard | Overview | uConsole dashboard model | Implemented |
| Chat | Chat workspace | Native Trail Mate Meshtastic/MeshCore/Reticulum services | Implemented |
| Contacts and nearby nodes | Contacts & nodes | Chat workspace model and node actions | Implemented |
| Map | Map workspace | Linux map cache, tile fetcher, contour store | Implemented |
| Automatic map download | Global background map service and Data & maps status | Existing asynchronous tile cache/fetch runtime | Implemented; desktop-specific behavior retained |
| GPS and Sky Plot | GPS & sky plot | GPS runtime and GNSS sky-plot presenter | Implemented |
| Team | Team operations | Dashboard team snapshot and chat model | Implemented |
| Tracker | Tracker | Linux GPX/CSV/binary tracker runtime | Implemented; saved settings now drive runtime |
| Energy Sweep | Radio tools | Existing LoRa runtime | Implemented; scan restores the active radio configuration |
| SSTV | Radio tools | Existing SSTV runtime | Implemented; capability state identifies simulated backends |
| Walkie | Radio tools | Existing Walkie runtime | Implemented; capability state identifies simulated backends |
| Extensions | Extensions | Existing pack repository runtime | Implemented |
| Settings | Settings | Existing app config and service apply paths | Implemented |
| Network | Settings and global status | Existing MQTT/network configuration | Implemented as desktop-integrated settings |
| USB disk | Data & maps / native Linux filesystem | Linux storage paths | Desktop equivalent; ESP USB mass-storage mode is not applicable |
| Poweroff | Native window/session lifecycle | GTK application shutdown | Desktop equivalent |
| Hardware diagnostics | Hardware | uConsole hardware probe | Desktop extension |
| Packet diagnostics | Logs | Linux packet log runtime | Desktop extension |

## Capability honesty

Desktop UI availability does not imply physical hardware availability. Radio
tools display `Available`, `Simulated`, `Degraded`, `Unsupported`, or `Error`
from the shared capability contract. This is especially important for the
current Linux Walkie and SSTV development backends, which must not be presented
as real audio/radio hardware.

## Verification boundary

The shared uConsole shell and UX pack are covered by the Windows CMake smoke
suite. Modified GTK translation units are syntax-checked in a Linux compiler
environment. The 1280 x 720 LVGL shell is also built and exercised through the
real SDL3 presenter, including renderer readback screenshots for Overview,
Chat, Map, GPS, Radio tools, and Extensions.

See the [uConsole SDL visual review](uconsole-sdl-visual-review.md) for the
screenshots, acceptance checklist, and reproduction command. SDL validates the
shared composition and interaction model; final GTK pixel metrics still require
a GTK4-capable target because GTK remains a separate widget renderer.
