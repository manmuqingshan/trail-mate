# Trail Mate Project Summary

<!-- praxis:project-overview:start -->

 - Project version: 0.1.30-alpha
- Git:34aad0bffa2f / main / dirty
 - Updated: 2026-06-25T09:10:47.794Z
 - Knowledge status: CANDIDATE; the current summary comes from project documentation and warehouse evidence, and has not been promoted to CONFIRMED project memory.

## Project Positioning

 A low-power, offline-first outdoor handheld navigation and communication device based on ESP32 hardware that supports LoRa text chat, offline map, SSTV reception and other functions, and is compatible with Meshtastic and MeshCore networks.

Trail Mate is specially designed for outdoor scenes without cellular network coverage. It provides simple self-positioning and direct LoRa text communication capabilities independent of smartphones, emphasizing stability, efficiency and interoperability.

The project focuses on two core requirements: offline GPS map display with a fixed north direction, and LoRa chat to send free-form messages in a Meshtastic or MeshCore network without a mobile phone.

The design philosophy pursues honest presentation of uncertainty, deterministic system behavior and long-term reliability, suitable for real outdoor environments with limited resources, rather than replacing smartphones.

## Current status

**Alpha under development** (high)

The project is in the active development stage. The latest released version is 0.1.30-alpha (2026-06-24). Functions continue to be added and the API may change. It has not yet reached the 1.0 stable version.

### Status evidence

- CHANGELOG.md#[0.1.30-alpha]
- README.md#Overview and design philosophy

## Key capabilities

### Offline GPS map

Offline map rendering in fixed north direction, supports OSM, terrain, satellite basemaps and contour overlay, with real-time location markers and breadcrumb trails.

### Evidence

- README.md#GPS Map

### LoRa Text Chat

 LoRa-based short message communication, compatible with Meshtastic and MeshCore protocols, supports Chinese text, and does not require central infrastructure.

### Evidence

- README.md#LoRa Chat

### SSTV image reception

Receives slow scan TV signals and decodes them into images on the device, suitable for low-power embedded environments.

### Evidence

- README.md#SSTV Receiver

### Team mode and location sharing

Through ESP-NOW close pairing, member list, location sharing and team chat can be realized on LoRa after exchanging team keys.

### Evidence

- README.md#Team Mode

### Track recording and route following

Supports track recording, storage, browsing, as well as KML route overlay and GPX export.

### Evidence

- README.md#Track Recording & Route Following

### Walkie Talkie Voice Intercom

Half-duplex voice communication based on FSK and Codec2, press to speak and release to answer.

### Evidence

- README.md#Walkie Talkie

### Protocol air interface parameter detection

Discover the complete LoRa
air profile carrying MeshCore, Meshtastic or Reticulum real protocol traffic, and obtain positive confirmation when the protocol allows; do not replace it with RSSI or low-noise recommendations
Protocol evidence.

### Evidence

- README.md#Protocol Probe

### GNSS sky map

Draws the azimuth and elevation angles of visible satellites in real time, and displays SNR and positioning status.

### Evidence

- README.md#GNSS Sky Plot

## Project entrance

- **PlatformIO build configuration**: platformio.ini (speculative). Manage multiple ESP32 device build targets such as tlora_pager_sx1262, tdeck, techo_lite, etc.
- **Firmware source code**: src/. Contains Trail Mate main logic, protocol adaptation, UI and other core codes.
- **Project documentation and resources**: docs/. Store project pictures, charts and other explanatory materials.
## Design and Architecture Entrance

- There is no clear entrance yet.

## Current progress

- **T-LoRa-Pager Firmware** (done): Currently the main verification device, supporting all core functions.
 - Evidence: README.md#Device Support Matrix
 - **T-Deck Firmware** (done): Keyboard and chat functions adapted, second primary verification platform.
 - Evidence: README.md#Device Support Table
- **LoRa Chat (Meshtastic/MeshCore)** (done): Text messaging, contact list, location sharing and other functions have been implemented, compatible with two mainstream protocols.
 - Evidence: README.md#LoRa Chat
 - Evidence: CHANGELOG.md#0.1.30-alpha
- **Offline map basic functions** (done): OSM, terrain, satellite basemap and contour overlay are available, zooming and layer switching are normal.
 - Evidence: README.md #GPS Map
 - **nRF52 class device preliminary support** (in_progress): MeshCore discovery portal and basic UI adaptation have been added, but full functionality (such as chat, map) is still under development.
 - Evidence: CHANGELOG.md#0.1.30-alpha
 - Evidence: README.md#Planned Supported Devices
- **Multi-language and localization** (in_progress): Extended language packs such as Chinese and Cyrillic have been introduced, but coverage and input method support still need to be improved.
 - Evidence: CHANGELOG.md#0.1.30-alpha
## Risks and gaps

### Lack of high-level architecture documentation

No independent system architecture or design documents have been found, which may increase the understanding cost for new contributors.

### Evidence

- Architecture/design class documents are missing in the document directory

### CHANGELOG readability issues

The current CHANGELOG file has encoding corruption, and some entries cannot be interpreted normally, affecting project history tracing.

### Evidence

- Garbled fragments in CHANGELOG.md

### Stability risks in the pre-release stage

The project has not yet reached 1.0, and the protocol and API may undergo breaking changes between versions.

### Evidence

- CHANGELOG.md states "breaking changes may occur between minor versions"

### Low-power device compatibility is uncertain

Full feature support for target devices such as nRF52 is still in the early stages, and there are challenges in performance and integration implementation.

### Evidence

- README.md#Planned Supported Devices
- CHANGELOG.md#0.1.30-alpha

## Questions to be confirmed

 - What is the planned release date for stable version 1.0?
- What is the complete feature roadmap for nRF52 and ultra-low power devices?
- Will there be a pre-installed tool or guide for SD card offline map tiles?
- Currently there is a lack of performance test and power consumption benchmark data. When will it be added?

## Next step

- Fix CHANGELOG encoding issue to ensure version history is clear and readable.
-Write an overview document of the project architecture to help contributors quickly understand the system modules and data flow.
- Promote the development of core functions such as LoRa chat and location sharing on nRF52 devices.
- Optimize the user experience of offline maps and consider providing one-click download or production tools for map tiles.
- Expand device compatibility testing, gradually converge API, and move towards the 1.0 stable version.

## Source document

- README.md
- CHANGELOG.md
- AGENTS.md

<!-- praxis:project-overview:end -->
