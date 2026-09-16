# Wio Tracker L2 architecture review

Reviewed on 2026-09-10 against the working tree based on main f84c4654.

## Scope and evidence order

The product requirement is the existing Trail Mate feature set, T-Deck-like
320 x 240 presentation, and touch-only input with a full text keyboard.
This port does not authorize another repository-wide architecture migration.

The normative ownership freeze takes precedence over older phase plans.
Current build manifests and executable startup sources establish which paths
actually run. Historical README plans, generated C4 candidates and a successful
link alone do not establish correct ownership or active product routing.

Reviewed baselines:

- specification/POST_REFACTOR_ARCHITECTURE_FREEZE.md
- specification/RUNTIME_OWNERSHIP_BOUNDARY_FREEZE.md
- specification/REPOSITORY_LAYOUT_ARCHITECTURE_SPEC.md
- specification/CROSS_PLATFORM_PRODUCT_ARCHITECTURE_SPEC.md
- specification/APP_SHELL_ARCHITECTURE_SPEC.md
- specification/PRODUCT_COMPOSITION_ARCHITECTURE_SPEC.md
- specification/DEVICE_UX_PACK_ARCHITECTURE_SPEC.md
- specification/UI_PRESENTATION_ARCHITECTURE_SPEC.md
- specification/BOARD_PACKAGE_SPEC.md
- specification/TARGET_MANIFEST_SPEC.md
- specification/RUNTIME_CONCURRENCY_SPEC.md
- specification/PROTOCOL_RUNTIME_DESIGN_SPEC.md
- decisions/ADR_BUILD_ENTRYPOINTS.md
- audits/TARGET_UI_FINAL_OWNERSHIP_REPORT.md
- tools/architecture/KNOWN_PRODUCT_ARCHITECTURE_VIOLATIONS.md
- active app/library/CMake manifests, platform dispatchers and module READMEs

## Repository responsibilities

| Directory | Responsibility | Boundary |
| --- | --- | --- |
| apps/ | Five product shells: ESP LVGL, nRF52 node, Linux simulator, uConsole GTK, Cardputer Zero | Select target/UX and hand off lifecycle; no hidden driver or domain-service implementation |
| builds/ | IDF component assets/defaults, nRF52 PIO wrapper, Linux CMake entry | Invoke shells; do not decide UI or create service graphs |
| root CMakeLists.txt | Executable ESP-IDF project | Consumes builds/esp_idf; do not create a second IDF project there |
| root platformio.ini, variants/ | Active Arduino PIO configurations and pin variants | Build/board selection only; existing Arduino route is explicitly covered by the restoration checker |
| boards/ | Hardware metadata, pins, bus/electrical facts | New L2 SDK objects, tasks and UI bindings will not live here |
| platform/shared/ | Shared hardware-facing contracts | No concrete SDK ownership |
| platform/esp/ | Arduino/IDF adapters, device drivers, transport/storage/audio execution and FreeRTOS ownership | Do not reinterpret protocol or business state |
| platform/nrf52/ | nRF52 Arduino/Bluefruit/runtime adapters | Reuse shared protocol and service contracts |
| platform/linux/ | Linux device, storage, serial, network and renderer host integration | Separate desktop/device/simulator execution from shared meaning |
| modules/ | Shared cores, composition, presentation and renderer/runtime modules | No dependency on apps; respect each module's narrower role |
| firmware/c6_companion/ | Separately built wireless peer firmware | C6 provides wireless mechanics; it does not own P4 business state |
| packs/ | Language, fonts and IME resources | Font/resource runtime owns loading and readiness |
| scripts/, tools/, cmake/, .github/ | Build support, validation, packaging and CI | No application policy disguised as tooling |
| site/ | Documentation/download/web flasher presentation | Consumes release metadata/artifacts |
| third_party/, generated sources | External implementations and generated protocol code | Not a place for board-specific product policy |
| docs/ | Specifications, decisions, manifests and audit evidence | Documents do not execute policy |

## Module map

| Module family | Authority / responsibility |
| --- | --- |
| core_chat | Shared messages, channels, conversation and protocol/domain behavior; ledgers remain authoritative |
| core_mesh | Packet-radio and identity/key ports, mesh strategies and use cases |
| core_phone | Shared Meshtastic/MeshCore phone protocol sessions; BLE mechanics remain in platform hosts |
| core_gps | Location/time facts, parsers, policies and service ports |
| core_team | Team membership, pairing, encrypted coordination and track semantics |
| core_hostlink | Portable codecs/session contracts; C6 HostLink and USB Data Exchange remain distinct protocols |
| core_device | Capability, device state and authority vocabulary |
| core_sys | Shared clocks, queues/ownership contracts and app facade contracts; historical platform/ui headers are recorded containment |
| product_composition | TargetProfile, TargetBuildBinding and TargetUxBinding; explicit product selection |
| ui_presentation | Toolkit-independent workspace models, snapshots, actions, page manifests and layout DTOs |
| chat_presentation_adapters | Domain/runtime-to-presentation adaptation |
| ui_chat_runtime, ui_map_runtime, ui_gps_runtime, ui_key_verification_runtime | Event pumps, caches, queues, scheduling and stable runtime adapters; no widget-tree ownership |
| ui_lvgl_core | Technical LVGL primitives and hosts |
| ui_lvgl_ux_packs | UX screen/input choices and common concrete renderers/modals |
| ui_ascii_runtime, ui_gtk_runtime, ui_headless_runtime | Renderer-family consumption and host adaptation |
| ui_mono | Existing compact/monochrome presentation surfaces |
| ui_shared | Existing shared LVGL screen/widget umbrella and compatibility surfaces; do not use it as an unbounded home for new device-specific UI |

## Actual execution paths

The active Arduino route is:

    variants environment -> src/main.cpp
      -> apps/esp32_lvgl/arduino_entry::setup/loop
      -> arduino_startup_runtime / arduino_loop_runtime
      -> platform board/display/storage adapters
      -> facade/configuration and shared runtime owners
      -> startup shell with explicitly selected UX pack
      -> UX menu/screen descriptors and existing renderers

The IDF route starts at root CMakeLists.txt and builds/esp_idf/main/idf_entry.cpp,
then invokes the IDF app startup/loop sources. It is a different compiled source
set from the Arduino route, even though both belong to apps/esp32_lvgl.

nRF52 uses builds/pio_nrf52 and apps/nrf52_node. Linux uses builds/linux_cmake
and the corresponding Linux shell. Their native input/transport responsibilities
must not be inferred from ESP widgets; Cardputer Zero uses its Linux IME path.

## Runtime facts and effects

MessageLedger and ReadStateLedger own delivery/read truth. ChatWorkspaceModel
owns view selection, not delivery outcomes. Reticulum destination/path/link/call
owners, the protocol TX queue, configuration persistence owner, storage
maintenance owner, audio adapter and FontRuntimeCoordinator retain their existing
responsibilities. The L2 port changes providers and input affordances, not those
authorities.

The SDMMC device remains below the existing SdFat file and storage-maintenance
contracts. It must not inherit shared-display-SPI startup gating. Display QSPI,
radio SPI and SDMMC are separate physical resources. LVGL calls stay on the UI
owner; hardware I2C and audio operations retain explicit single owners/locks.

## Findings in the first implementation and required corrections

1. The initial TargetProfile/config additions did not reach Arduino startup:
   apps/esp32_lvgl/library.json omitted esp32_lvgl_runtime_config.cpp, and the
   Arduino initializeShell function left Hooks.ux_pack_id unset. Native config
   tests and firmware compilation did not establish this missing connection.
2. Selecting the T-Deck compatibility UX binding as the L2 default expanded a
   historical containment path. L2 needs a registered touch UX pack, while reusing
   the established T-Deck page set and rendering conventions.
3. The new board class mixed hardware description with SDK handles, bus mutexes,
   FreeRTOS audio tasks and UI runtime binding. The new L2 implementation belongs
   in a dedicated ESP platform adapter package. Existing board packages are not
   moved as incidental cleanup.
4. The new text-editor modal belongs with common UX-pack renderers. Existing IME
   behavior can be reused through its existing contract without creating a second
   input engine or moving old screens wholesale.
5. Compact touch layout is a selected UX property. A new L2 board-name branch in
   a shared renderer/profile resolver is not the product selection authority.

These corrections are limited to the new L2 route and necessary shared contracts.
No new compatibility layer, app shell, service locator, protocol owner or file API
is required. Checks must cover the real Arduino UX handoff, pack membership/input
bindings, keyboard interaction/lifetime, both firmware builds and existing storage
and stack invariants. Hardware validation remains separate from these checks.

## Resolution and validation

- Hardware facts and upload metadata remain in boards/wio_tracker_l2; SDK
  objects, driver execution, locks, audio tasks and board-runtime binding now
  live in platform/esp/wio_tracker_l2.
- The new modal and input-layout consumption live in ui_lvgl_ux_packs/common.
  The shared page-profile resolver has an app-selected, static-lived profile
  override and no new L2 detection branch.
- The real Arduino library compiles runtime configuration and resolves
  TargetUxBinding. Its startup validates deck_touch, supplies the selected pack
  to the startup shell, and configures input layout from DeviceUxProfile.
- deck_touch builds its screen membership from the existing deck_full_manifest.
  The existing App Catalog remains the source of live provider availability and
  optional tools; it is not copied into a second board-specific app list.
- The new pack and concrete common widget sources are present in the relevant
  Arduino, IDF and Linux source-list owners. The lightweight host descriptor
  library compiles only the portable pack code.
- A clean L2 firmware build and the real LVGL interaction/lifetime test passed.
  ELF inspection confirms esp32LvglRuntimeUxBinding, DeckTouchUxPack and
  configureInputLayout are linked into the firmware.
- The board-facts boundary checker passes. The broad Phase 1 product-architecture
  report has 714 findings across the repository. Its new-route matches are test
  target references and legitimate platform-to-board/factory includes; these
  were reviewed against the normative dependency rules, not silenced by
  weakening the checker. The entire repository is not claimed violation-free.
- Native tests verify the target binding, touch-only profile, no UX fallback,
  manifest membership and keyboard operation without a board macro.
- The aggregate post-refactor checker does not fully pass in this workspace:
  it scans old ignored release copies under .codex-build, expects historical
  uConsole source-list tokens, and expects ui_mono text in root platformio.ini.
  The latter two tracked files are unchanged from HEAD. These failures were
  recorded rather than changing unrelated targets or relaxing the checks.

The earlier firmware/config-only validation did not establish the real Arduino
UX handoff. It is superseded by this source-level correction and link evidence.
