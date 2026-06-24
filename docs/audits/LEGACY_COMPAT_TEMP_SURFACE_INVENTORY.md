# Legacy / Compatibility / Temporary Surface Inventory

## Purpose

This inventory maps every non-final architecture surface that is still visible
in the repository. It does not authorize deletion or migration by itself. It
exists to prevent another cutover from guessing which legacy, compatibility,
transitional, fallback, archive, adapter, bridge, probe, smoke, or checker
surface is still carrying real responsibility.

Scanner input lives in `docs/audits/LEGACY_COMPAT_TEMP_SURFACE_SCAN.md`.
The scan is discovery only; this inventory is the disposition ledger.

## Categories Covered

- root legacy source roots
- compatibility shims
- deprecated aliases
- transitional descriptors
- fallback-only paths
- archive-only roots
- legacy build entries
- old build entrypoints
- legacy app shells
- adapter / bridge / facade layers
- probe / smoke-only runtime
- historical debt docs
- legacy-governing checkers
- legacy/transitional/compatibility/shim/fallback names

## Disposition Values

- Must Delete
- Must Rename
- Must Migrate
- Deleted
- Keep as Final Adapter
- Keep as Deprecated Alias Temporarily
- Test-only / Smoke-only

## Surface: root legacy/

Category:
- root legacy source roots

Current location:
- `legacy/`
- `legacy/README.md`
- `legacy/LEGACY_GOVERNANCE.md`

Current callers:
- docs and checkers reference the root as a governance boundary.
- `tools/architecture/check_post_refactor_final_ready.py` and
  `tools/architecture/check_legacy_app_roots_burndown_ready.py` still accept
  `legacy/app_implementations` as present.

Current responsibility:
- contains historical implementation roots and governance notes.

Is this final architecture?
- No.

Final owner:
- docs/archive for historical records, plus concrete final owners per root.

Disposition:
- Deleted.

Final status:
- Removed in Root Legacy Elimination.

Historical record:
- docs/archive/REMOVED_LEGACY_ROOTS.md

Delete condition:
- Satisfied by Batch 4 Root Legacy Elimination.

Risk:
- deleting before build and runtime ownership transfer would break ESP-IDF,
  PIO, Linux simulator, uConsole, RPI/Cardputer, and GAT562 workflows.

## Surface: legacy/app_implementations/esp_idf

Category:
- root legacy source roots

Current location:
- `legacy/app_implementations/esp_idf`
- `legacy/app_implementations/esp_idf/CMakeLists.txt`
- `legacy/app_implementations/esp_idf/idf_component.yml`
- `legacy/app_implementations/esp_idf/library.json`
- `legacy/app_implementations/esp_idf/targets/*`

Current callers:
- root `CMakeLists.txt`
- `builds/esp_idf/CMakeLists.txt`
- `apps/esp32_lvgl/CMakeLists.txt`
- ESP-IDF component registration under the root

Current responsibility:
- transitional ESP-IDF build/component root, target sdkconfig defaults, and
  ESP runtime entry glue.

Is this final architecture?
- No.

Final owner:
- `builds/esp_idf`, `apps/esp32_lvgl`, `platform/esp`, `boards/*`, and
  stable runtime modules.

Disposition:
- Deleted.

Final status:
- Removed in Root Legacy Elimination.

Historical record:
- docs/archive/REMOVED_LEGACY_ROOTS.md

Delete condition:
- Satisfied by Batch 4 Root Legacy Elimination.

Risk:
- high; this root is still an active build dependency.

## Surface: legacy/app_implementations/esp_pio

Category:
- root legacy source roots

Current location:
- `legacy/app_implementations/esp_pio`
- `legacy/app_implementations/esp_pio/library.json`
- `legacy/app_implementations/esp_pio/src/*`
- `legacy/app_implementations/esp_pio/include/*`

Current callers:
- `builds/pio_nrf52/platformio.ini`
- `builds/pio_nrf52/src/nrf52_node_wrapper_baseline.cpp`
- `apps/nrf52_node` transitional source strings

Current responsibility:
- PlatformIO/Arduino compatibility source layout and app runtime glue.

Is this final architecture?
- No.

Final owner:
- `builds/pio_nrf52`, `apps/nrf52_node`, `platform/nrf52`, `boards/*`, and
  stable modules.

Disposition:
- Deleted.

Final status:
- Removed in Root Legacy Elimination.

Historical record:
- docs/archive/REMOVED_LEGACY_ROOTS.md

Delete condition:
- Satisfied by Batch 4 Root Legacy Elimination.

Risk:
- high; PIO include flags and wrapper baseline still reference it.

## Surface: legacy/app_implementations/gat562_mesh_evb_pro

Category:
- root legacy source roots

Current location:
- `legacy/app_implementations/gat562_mesh_evb_pro`
- `legacy/app_implementations/gat562_mesh_evb_pro/library.json`
- `legacy/app_implementations/gat562_mesh_evb_pro/src/*`
- `legacy/app_implementations/gat562_mesh_evb_pro/include/*`

Current callers:
- `builds/pio_nrf52/platformio.ini`
- `apps/nrf52_node` board-specific transitional source strings

Current responsibility:
- concrete GAT562 Mesh EVB Pro historical app/runtime glue.

Is this final architecture?
- No.

Final owner:
- `apps/nrf52_node`, `builds/pio_nrf52`, `boards/gat562_mesh_evb_pro`, and
  `platform/nrf52`.

Disposition:
- Deleted.

Final status:
- Removed in Root Legacy Elimination.

Historical record:
- docs/archive/REMOVED_LEGACY_ROOTS.md

Delete condition:
- Satisfied by Batch 4 Root Legacy Elimination.

Risk:
- high; it is a concrete device path, not just documentation.

## Surface: legacy/app_implementations/linux_rpi

Category:
- root legacy source roots

Current location:
- `legacy/app_implementations/linux_rpi`
- `legacy/app_implementations/linux_rpi/CMakeLists.txt`
- `legacy/app_implementations/linux_rpi/SConstruct`
- `legacy/app_implementations/linux_rpi/main/*`
- `legacy/app_implementations/linux_rpi/scripts/*`

Current callers:
- `builds/linux_cmake/README.md` records it as a current transitional Linux
  app-local path.
- local root CMake/SCons/scripts may still be used by device workflows.

Current responsibility:
- historical Pi OS / Cardputer Zero Linux device bring-up.

Is this final architecture?
- No.

Final owner:
- future Linux device app shell, `builds/linux_cmake`, and `platform/linux`.

Disposition:
- Deleted.

Final status:
- Removed in Root Legacy Elimination.

Historical record:
- docs/archive/REMOVED_LEGACY_ROOTS.md

Delete condition:
- Satisfied by Batch 4 Root Legacy Elimination.

Risk:
- medium-high; no final RPI/Cardputer app shell currently replaces it.

## Surface: legacy/app_implementations/linux_sim

Category:
- root legacy source roots

Current location:
- `legacy/app_implementations/linux_sim`
- `legacy/app_implementations/linux_sim/CMakeLists.txt`
- `legacy/app_implementations/linux_sim/ARCHIVE.md`
- `legacy/app_implementations/linux_sim/archive/*`

Current callers:
- docs and checkers recognize it as archive-only.
- active app shells do not retain source-descriptor metadata for it.

Current responsibility:
- archive-only historical simulator source and scripts.

Is this final architecture?
- No.

Final owner:
- `apps/linux_sim_shell`, `modules/ui_ascii_runtime`, `platform/linux`, and
  docs/archive for history.

Disposition:
- Deleted.

Final status:
- Removed in Root Legacy Elimination.

Historical record:
- docs/archive/REMOVED_LEGACY_ROOTS.md

Delete condition:
- Satisfied by Batch 4 Root Legacy Elimination.

Risk:
- low-medium; source is archive-only and now referenced only by historical
  documentation and retirement checkers.

## Surface: legacy/app_implementations/linux_uconsole

Category:
- root legacy source roots

Current location:
- `legacy/app_implementations/linux_uconsole`
- `legacy/app_implementations/linux_uconsole/CMakeLists.txt`
- `legacy/app_implementations/linux_uconsole/ARCHIVE.md`
- `legacy/app_implementations/linux_uconsole/archive/*`

Current callers:
- docs and checkers recognize it as archive-only.
- active app shells do not retain source-descriptor metadata for it.

Current responsibility:
- archive-only historical uConsole GTK source, packaging files, scripts, and
  old widget/page code.

Is this final architecture?
- No.

Final owner:
- `apps/linux_uconsole_gtk`, `builds/linux_cmake`, `platform/linux/uconsole`,
  `modules/ui_gtk_runtime`, and docs/archive for history.

Disposition:
- Deleted.

Final status:
- Removed in Root Legacy Elimination.

Historical record:
- docs/archive/REMOVED_LEGACY_ROOTS.md

Delete condition:
- Satisfied by Batch 4 Root Legacy Elimination.

Risk:
- high; this archive still contains real business/page/packaging history that
  must be migrated or deliberately replaced before deletion, but no active app
  shell descriptor keeps it alive.

## Surface: legacy/app_implementations/linux_unoq

Category:
- root legacy source roots

Current location:
- `legacy/app_implementations/linux_unoq`
- `legacy/app_implementations/linux_unoq/README.md`
- `legacy/app_implementations/linux_unoq/TRANSITIONAL_IMPLEMENTATION_ROOT.md`

Current callers:
- `builds/linux_cmake/README.md` records it as a current transitional Linux
  path.
- `legacy/app_implementations/LEGACY_IMPLEMENTATION_INDEX.md`

Current responsibility:
- placeholder historical Linux UNO Q root.

Is this final architecture?
- No.

Final owner:
- future Linux UNO Q app shell or delete with no replacement if target is
  retired.

Disposition:
- Deleted.

Final status:
- Removed in Root Legacy Elimination.

Historical record:
- docs/archive/REMOVED_LEGACY_ROOTS.md

Delete condition:
- Satisfied by Batch 4 Root Legacy Elimination.

Risk:
- low-medium; deleting without product decision may lose target intent.

## Surface: esp_idf_legacy_implementation_adapter

Category:
- adapter / bridge / facade layers

Current location:
- `legacy/app_implementations/esp_idf/src/esp_idf_legacy_implementation_adapter.*`

Current callers:
- docs and inventory only after Batch 2.
- `builds/esp_idf/ESP_IDF_COMPONENT_SOURCES.cmake` owns migrated ESP-IDF
  source lists.

Current responsibility:
- historical adapter source retained under the ESP-IDF historical root.

Is this final architecture?
- No.

Final owner:
- `apps/esp32_lvgl` for app shell/runtime ownership and `builds/esp_idf` for
  build entrypoint wiring.

Disposition:
- Deleted.

Final status:
- Removed in Root Legacy Elimination.

Historical record:
- docs/archive/REMOVED_LEGACY_ROOTS.md

Delete condition:
- Satisfied by Batch 4 Root Legacy Elimination.

Risk:
- medium; source is retained but no final app shell or build entrypoint should
  compile it.

## Surface: nrf52_pio_legacy_implementation_adapter

Category:
- adapter / bridge / facade layers

Current location:
- `legacy/app_implementations/esp_pio/src/nrf52_pio_legacy_implementation_adapter.h`

Current callers:
- docs and inventory only after Batch 1.
- `builds/pio_nrf52/platformio.ini` no longer adds the legacy root include path.

Current responsibility:
- describes transitional PIO and board-specific roots for the nRF52 wrapper.

Is this final architecture?
- No.

Final owner:
- `apps/nrf52_node`, `builds/pio_nrf52`, and `boards/gat562_mesh_evb_pro`.

Disposition:
- Deleted.

Final status:
- Removed in Root Legacy Elimination.

Historical record:
- docs/archive/REMOVED_LEGACY_ROOTS.md

Delete condition:
- Satisfied by Batch 4 Root Legacy Elimination.

Risk:
- high; active PlatformIO wrapper includes it.

## Surface: linux_sim_legacy_implementation_adapter

Category:
- adapter / bridge / facade layers

Current location:
- deleted from `legacy/app_implementations/linux_sim/archive/adapters` in
  Batch 1.

Current callers:
- docs and checkers only; old adapter smoke source and archived adapter source
  are deleted.

Current responsibility:
- historical record only after final shell moved to its own historical source
  descriptor.

Is this final architecture?
- No.

Final owner:
- docs/archive or delete.

Disposition:
- Deleted.

Final status:
- Removed in Root Legacy Elimination.

Historical record:
- docs/archive/REMOVED_LEGACY_ROOTS.md

Delete condition:
- Satisfied by Batch 4 Root Legacy Elimination.

Risk:
- low; it is archive-only but still enforced by legacy root checker.

## Surface: uconsole_legacy_implementation_adapter

Category:
- adapter / bridge / facade layers

Current location:
- deleted from `legacy/app_implementations/linux_uconsole/archive/adapters` in
  Batch 1.

Current callers:
- docs and checkers only; old adapter smoke source and archived adapter source
  are deleted.

Current responsibility:
- historical record only after final shell moved to its own historical source
  descriptor.

Is this final architecture?
- No.

Final owner:
- docs/archive or delete after uConsole final ownership is complete.

Disposition:
- Deleted.

Final status:
- Removed in Root Legacy Elimination.

Historical record:
- docs/archive/REMOVED_LEGACY_ROOTS.md

Delete condition:
- Satisfied by Batch 4 Root Legacy Elimination.

Risk:
- low-medium; uConsole root still contains other real source that needs
  separate handling.

## Surface: linux_sim_historical_source_descriptor (formerly linux_sim_legacy_source_descriptor)

Category:
- retired descriptors

Current location:
- removed from active app source and tests.

Current callers:
- none from active app or build code.
- `tools/architecture/check_legacy_disposition_execution_ready.py` and
  `tools/architecture/check_no_root_legacy_ready.py` assert it stays retired.

Current responsibility:
- Historical descriptor retired from active app shell. Removed root history is
  documented only in `docs/archive/REMOVED_LEGACY_ROOTS.md`.

Is this final architecture?
- Yes.

Final owner:
- `apps/linux_sim_shell` owns the app shell; docs/archive owns removed root
  history.

Disposition:
- Deleted.

Final status:
- Historical descriptor retired from active app shell.

Delete condition:
- Satisfied once docs/archive records removed root history and active app/build
  code has no descriptor files, targets, includes, or fields.

Risk:
- low; app shell validation no longer reads this descriptor.

## Surface: linux_uconsole_gtk_historical_source_descriptor (formerly linux_uconsole_gtk_legacy_source_descriptor)

Category:
- retired descriptors

Current location:
- removed from active app source and tests.

Current callers:
- none from active app or build code.
- `tools/architecture/check_legacy_disposition_execution_ready.py` and
  `tools/architecture/check_no_root_legacy_ready.py` assert it stays retired.

Current responsibility:
- Historical descriptor retired from active app shell. Removed root history is
  documented only in `docs/archive/REMOVED_LEGACY_ROOTS.md`.

Is this final architecture?
- Yes.

Final owner:
- `apps/linux_uconsole_gtk` owns the app shell; `builds/linux_cmake` owns
  build metadata; docs/archive owns removed root history.

Disposition:
- Deleted.

Final status:
- Historical descriptor retired from active app shell.

Delete condition:
- Satisfied once docs/archive records removed root history and active app/build
  code has no descriptor files, targets, includes, or fields.

Risk:
- medium; uConsole history is still important, but no descriptor keeps it in
  the active app shell.

## Surface: nrf52_historical_source_descriptor

Category:
- retired descriptors

Current location:
- removed from active app source and tests.

Current callers:
- none from active app or build code.
- `tools/architecture/check_legacy_disposition_execution_ready.py` and
  `tools/architecture/check_no_root_legacy_ready.py` assert it stays retired.

Current responsibility:
- Historical descriptor retired from active app shell. Removed root history is
  documented only in `docs/archive/REMOVED_LEGACY_ROOTS.md`.

Is this final architecture?
- Yes.

Final owner:
- `apps/nrf52_node`, `builds/pio_nrf52`, and
  `boards/gat562_mesh_evb_pro`; docs/archive owns removed root history.

Disposition:
- Deleted.

Final status:
- Historical descriptor retired from active app shell.

Delete condition:
- Satisfied once docs/archive records removed root history and active app/build
  code has no descriptor files, targets, includes, or fields.

Risk:
- low-medium; the wrapper no longer compiles or includes this descriptor.

## Surface: esp32_lvgl_historical_source_descriptor

Category:
- retired descriptors

Current location:
- removed from active app source and tests.

Current callers:
- none from active app or build code.
- `tools/architecture/check_legacy_disposition_execution_ready.py` and
  `tools/architecture/check_no_root_legacy_ready.py` assert it stays retired.

Current responsibility:
- Historical descriptor retired from active app shell. Removed root history is
  documented only in `docs/archive/REMOVED_LEGACY_ROOTS.md`.

Is this final architecture?
- Yes.

Final owner:
- `apps/esp32_lvgl` and `builds/esp_idf`; docs/archive owns removed root
  history.

Disposition:
- Deleted.

Final status:
- Historical descriptor retired from active app shell.

Delete condition:
- Satisfied once docs/archive records removed root history and active app/build
  code has no descriptor files, targets, includes, or fields.

Risk:
- medium; ESP-IDF remains an active build path, but descriptor metadata is no
  longer part of the app shell contract.

## Surface: ui_headless_runtime descriptor consumer

Category:
- adapter / bridge / facade layers

Current location:
- `modules/ui_headless_runtime/include/ui_headless_runtime/headless_descriptor_consumer.h`
- `modules/ui_headless_runtime/src/headless_descriptor_consumer.cpp`
- `modules/ui_headless_runtime/tests/test_headless_descriptor_consumer.cpp`

Current callers:
- tests and architecture checker only in Batch 1.

Current responsibility:
- final renderer-safe descriptor consumer for targets that need a non-widget
  descriptor path.

Is this final architecture?
- Yes as a final adapter.

Final owner:
- `modules/ui_headless_runtime`.

Disposition:
- Keep as Final Adapter.

Delete condition:
- none for the final adapter; delete only if a later final architecture chooses
  a different headless descriptor consumer and updates the checker.

Risk:
- low; it consumes DTO-style descriptors and does not choose UX, board, or
  build behavior.

## Surface: ui_shared compatibility shims

Category:
- compatibility shims

Current location:
- removed from active include and build surfaces.

Current callers:
- none from active source or build inputs.
- docs and checker policy assert the shim surface stays retired.

Current responsibility:
- historical record of removed forwarding shims and Linux compatibility
  translation units.

Is this final architecture?
- Yes.

Final owner:
- stable modules such as `ui_chat_runtime`, `ui_key_verification_runtime`,
  `ui_map_runtime`, `ui_presentation`, `platform/linux/common`, and
  docs/archive for history.

Disposition:
- Deleted.

Final status:
- Retired from active include and build surfaces.

Delete condition:
- Satisfied: no production include path uses the old `ui_shared` forwarding
  headers, and Linux `gps_shared_compat.cpp` / `mt_protocol_air_compat.cpp`
  have left `cmake/TrailMateLinuxSources.cmake`.

Risk:
- low-medium; downstream consumers must use the stable runtime/module headers
  directly.

## Surface: LegacyChatDeliveryActionBridge

Category:
- deprecated aliases

Current location:
- `modules/ui_legacy_adapters/include/ui_legacy_adapters/legacy_chat_delivery_action_bridge.h`
- `modules/ui_shared/include/ui/presentation_sources/legacy_chat_delivery_action_bridge.h`

Current callers:
- `modules/ui_legacy_adapters/tests/test_legacy_chat_delivery_action_bridge_legacy_alias.cpp`
- docs and checker policy

Current responsibility:
- deprecated alias to `ui_chat_runtime::ChatDeliveryActionPortAdapter`.

Is this final architecture?
- No.

Final owner:
- `modules/ui_chat_runtime`.

Disposition:
- Keep as Deprecated Alias Temporarily.

Delete condition:
- no downstream includes of either alias header remain.

Risk:
- low-medium; alias may be used by external or untracked downstream code.

## Surface: LegacyKeyVerificationSource

Category:
- deprecated aliases

Current location:
- `modules/ui_legacy_adapters/include/ui_legacy_adapters/legacy_key_verification_source.h`
- `modules/ui_shared/include/ui/presentation_sources/legacy_key_verification_source.h`

Current callers:
- `modules/ui_legacy_adapters/tests/test_legacy_key_verification_adapters_legacy_alias.cpp`
- docs and checker policy

Current responsibility:
- deprecated alias to
  `ui_key_verification_runtime::KeyVerificationPresentationSource`.

Is this final architecture?
- No.

Final owner:
- `modules/ui_key_verification_runtime`.

Disposition:
- Keep as Deprecated Alias Temporarily.

Delete condition:
- no downstream includes of either alias header remain.

Risk:
- low-medium; alias may be used by external or untracked downstream code.

## Surface: LegacyKeyVerificationActionSink

Category:
- deprecated aliases

Current location:
- `modules/ui_legacy_adapters/include/ui_legacy_adapters/legacy_key_verification_action_sink.h`
- `modules/ui_shared/include/ui/presentation_sources/legacy_key_verification_action_sink.h`

Current callers:
- `modules/ui_legacy_adapters/tests/test_legacy_key_verification_adapters_legacy_alias.cpp`
- docs and checker policy

Current responsibility:
- deprecated alias to `ui_key_verification_runtime::KeyVerificationActionSink`.

Is this final architecture?
- No.

Final owner:
- `modules/ui_key_verification_runtime`.

Disposition:
- Keep as Deprecated Alias Temporarily.

Delete condition:
- no downstream includes of either alias header remain.

Risk:
- low-medium; alias may be used by external or untracked downstream code.

## Surface: LegacyKeyVerificationSession

Category:
- deprecated aliases

Current location:
- `modules/ui_legacy_adapters/include/ui_legacy_adapters/legacy_key_verification_session.h`
- `modules/ui_shared/include/ui/presentation_sources/legacy_key_verification_session.h`

Current callers:
- `modules/ui_legacy_adapters/tests/test_legacy_key_verification_adapters_legacy_alias.cpp`
- docs and checker policy

Current responsibility:
- deprecated alias to
  `ui_key_verification_runtime::KeyVerificationSessionAdapter`.

Is this final architecture?
- No.

Final owner:
- `modules/ui_key_verification_runtime`.

Disposition:
- Keep as Deprecated Alias Temporarily.

Delete condition:
- no downstream includes of either alias header remain.

Risk:
- low-medium; alias may be used by external or untracked downstream code.

## Surface: LegacyMapOverlaySource

Category:
- deprecated aliases

Current location:
- `modules/ui_legacy_adapters/include/ui_legacy_adapters/legacy_map_overlay_source.h`
- `modules/ui_shared/include/ui/presentation_sources/legacy_map_overlay_source.h`

Current callers:
- `modules/ui_legacy_adapters/tests/test_legacy_map_overlay_source_legacy_alias.cpp`
- docs and checker policy

Current responsibility:
- deprecated alias to `ui::map_overlay::MapOverlaySnapshotSource`.

Is this final architecture?
- No.

Final owner:
- `modules/ui_map_runtime` and map presentation/runtime modules.

Disposition:
- Keep as Deprecated Alias Temporarily.

Delete condition:
- no downstream includes remain and map renderers consume stable snapshots only.

Risk:
- low-medium; alias may be used by external or untracked downstream code.

## Surface: LinuxSim hardcoded runtime routing

Category:
- deleted fallback paths

Current location:
- `apps/linux_sim_shell/src/linux_sim_runtime_entry.*`
- `apps/linux_sim_shell/src/linux_sim_runtime_renderer.*`
- removed `apps/linux_sim_shell/tests/linux_sim_runtime_entry_fallback_smoke.cpp`

Current callers:
- LinuxSim runtime entry and renderer tests.
- Phase 10/11 checkers.

Current responsibility:
- burned-down fallback route; failed screen graph adoption is
  unavailable-on-failure.

Is this final architecture?
- No.

Final owner:
- `apps/linux_sim_shell` and `modules/ui_ascii_runtime`.

Disposition:
- Deleted.

Final status:
- Removed in LinuxSim/uConsole fallback burn-down.

Delete condition:
- Satisfied: real simulator workflows no longer need hardcoded route fallback
  and renderer smoke covers the failed-adoption false-return path.

Risk:
- medium; handled by explicit unavailable-on-failure assertions.

## Surface: GTK hardcoded page registry

Category:
- fallback-only paths

Current location:
- `apps/linux_uconsole_gtk/src/linux_uconsole_gtk_page_registry_adoption.*`
- `apps/linux_uconsole_gtk/src/linux_uconsole_gtk_page_registry_renderer.*`
- removed `apps/linux_uconsole_gtk/tests/linux_uconsole_gtk_page_registry_fallback_smoke.cpp`
- archived old page registry under `legacy/app_implementations/linux_uconsole/archive/gtk/gtk/gtk_uconsole_pages.*`

Current callers:
- uConsole GTK page registry tests.
- Phase 10/11 checkers.

Current responsibility:
- burned-down fallback registry; failed descriptor adoption is
  unavailable-on-failure.

Is this final architecture?
- No.

Final owner:
- `apps/linux_uconsole_gtk` and `modules/ui_gtk_runtime`.

Disposition:
- Deleted.

Final status:
- Removed in LinuxSim/uConsole fallback burn-down.

Delete condition:
- Satisfied for the page-registry adoption path: `GtkDescriptorPageRegistry`
  is the only active page-registry source and fallback smoke is removed.

Risk:
- medium-high; real GTK widget rewrite is still separate debt, but this active
  page-registry fallback no longer carries it.

## Surface: LVGL hardcoded menu/page creation

Category:
- fallback-only paths

Current location:
- `modules/ui_lvgl_ux_packs/src/runtime/lvgl_primary_screen_graph_runtime.cpp`
- `modules/ui_lvgl_ux_packs/src/runtime/lvgl_descriptor_renderer_probe.cpp`
- `modules/ui_lvgl_ux_packs/tests/test_lvgl_primary_screen_graph_runtime.cpp`
- `modules/ui_lvgl_ux_packs/tests/test_lvgl_descriptor_renderer_probe.cpp`

Current callers:
- LVGL runtime/probe tests.
- Phase 10/11 checkers.

Current responsibility:
- historical record of the deleted LVGL hardcoded menu/page fallback. Failed
  adoption is unavailable-on-failure.

Is this final architecture?
- Yes for fallback deletion; real widget/menu migration remains separate debt.

Final owner:
- `modules/ui_lvgl_ux_packs` real renderer path.

Disposition:
- Deleted.

Final status:
- Removed in LVGL fallback burn-down.

Delete condition:
- Satisfied for descriptor runtime failure: no `HardcodedFallback`,
  `fallbackUsed`, `usedFallback`, or `loadFallback` remains in
  `modules/ui_lvgl_ux_packs`.

Risk:
- medium; real LVGL widget/menu migration remains, but failed descriptor
  adoption no longer selects a second hardcoded UI source.

## Surface: legacy/app_implementations/linux_sim/archive

Category:
- archive-only roots

Current location:
- `legacy/app_implementations/linux_sim/archive/*`

Current callers:
- docs and `tools/architecture/check_legacy_app_roots_burndown_ready.py`
  require selected archive paths.

Current responsibility:
- retains historical simulator composition, adapters, scripts, and smoke/probe
  source.

Is this final architecture?
- No.

Final owner:
- delete source after docs/archive summary, with active workflows in
  `apps/linux_sim_shell` and `platform/linux`.

Disposition:
- Deleted.

Final status:
- Removed in Root Legacy Elimination.

Historical record:
- docs/archive/REMOVED_LEGACY_ROOTS.md

Delete condition:
- Satisfied by Batch 4 Root Legacy Elimination.

Risk:
- medium; archive still contains useful workflow history.

## Surface: legacy/app_implementations/linux_uconsole/archive

Category:
- archive-only roots

Current location:
- `legacy/app_implementations/linux_uconsole/archive/*`

Current callers:
- docs and `tools/architecture/check_legacy_app_roots_burndown_ready.py`
  require selected archive paths.

Current responsibility:
- retains historical uConsole composition, adapters, GTK pages, scripts,
  packaging metadata, and old smoke tests.

Is this final architecture?
- No.

Final owner:
- `apps/linux_uconsole_gtk`, `builds/linux_cmake`, `platform/linux/uconsole`,
  `modules/ui_gtk_runtime`, and docs/archive for historical notes.

Disposition:
- Deleted.

Final status:
- Removed in Root Legacy Elimination.

Historical record:
- docs/archive/REMOVED_LEGACY_ROOTS.md

Delete condition:
- Satisfied by Batch 4 Root Legacy Elimination.

Risk:
- high; deleting this archive without migration loses real GTK and packaging
  material.

## Surface: legacy build CMake and library manifests

Category:
- legacy build entries

Current location:
- `legacy/app_implementations/esp_idf/CMakeLists.txt`
- `legacy/app_implementations/esp_idf/library.json`
- `legacy/app_implementations/esp_pio/library.json`
- `legacy/app_implementations/gat562_mesh_evb_pro/library.json`
- `legacy/app_implementations/linux_rpi/CMakeLists.txt`
- `legacy/app_implementations/linux_rpi/SConstruct`
- `legacy/app_implementations/linux_sim/CMakeLists.txt`
- `legacy/app_implementations/linux_uconsole/CMakeLists.txt`

Current callers:
- `builds/*` wrappers, root CMake, PlatformIO, docs, and checkers.

Current responsibility:
- keeps old build ownership alive or documents archive-only local roots.

Is this final architecture?
- No.

Final owner:
- `builds/esp_idf`, `builds/pio_nrf52`, `builds/linux_cmake`, final app
  shells, and platform modules.

Disposition:
- Deleted.

Final status:
- Removed in Root Legacy Elimination.

Historical record:
- docs/archive/REMOVED_LEGACY_ROOTS.md

Delete condition:
- Satisfied by Batch 4 Root Legacy Elimination.

Risk:
- high; build files are sensitive and may be active.

## Surface: old build entrypoints

Category:
- old build entrypoints

Current location:
- root `CMakeLists.txt`
- root `platformio.ini`
- `legacy/app_implementations/*/CMakeLists.txt`
- `legacy/app_implementations/*/library.json`
- `docs/audits/TRANSITIONAL_BUILD_ENTRYPOINTS.md`

Current callers:
- current developer workflows and phase checkers.

Current responsibility:
- records and preserves historical build entrypoint behavior while
  `builds/*` wrappers mature.

Is this final architecture?
- No.

Final owner:
- `builds/esp_idf`, `builds/pio_nrf52`, and `builds/linux_cmake`.

Disposition:
- Deleted.

Final status:
- Removed in Root Legacy Elimination.

Historical record:
- docs/archive/REMOVED_LEGACY_ROOTS.md

Delete condition:
- Satisfied by Batch 4 Root Legacy Elimination.

Risk:
- high; this can break all build families if moved without proof.

## Surface: legacy app shell roots

Category:
- legacy app shells

Current location:
- `legacy/app_implementations/linux_sim`
- `legacy/app_implementations/linux_uconsole`
- `legacy/app_implementations/linux_rpi`
- `legacy/app_implementations/linux_unoq`
- `legacy/app_implementations/gat562_mesh_evb_pro`
- `legacy/app_implementations/esp_idf`
- `legacy/app_implementations/esp_pio`

Current callers:
- app shell docs, build docs, legacy root checker, and transitional wrappers.

Current responsibility:
- historical app shell / implementation root identity.

Is this final architecture?
- No.

Final owner:
- `apps/*` final app shells, `builds/*`, `boards/*`, `platform/*`, and stable
  modules.

Disposition:
- Deleted.

Final status:
- Removed in Root Legacy Elimination.

Historical record:
- docs/archive/REMOVED_LEGACY_ROOTS.md

Delete condition:
- Satisfied by Batch 4 Root Legacy Elimination.

Risk:
- high; these roots mix build, app, target, runtime, scripts, and archive
  responsibilities.

## Surface: adapter / bridge / facade naming surface

Category:
- adapter / bridge / facade layers

Current location:
- `modules/ui_ascii_runtime/*adapter*`
- `modules/ui_gtk_runtime/*adapter*`
- `modules/ui_lvgl_ux_packs/*adapter*`
- `modules/ui_chat_runtime/*adapter*`
- `modules/ui_map_runtime/*adapter*`
- `platform/*/*bridge*`
- `legacy/app_implementations/*/*facade*`

Current callers:
- runtime modules, platform modules, CMake source lists, tests, and docs.

Current responsibility:
- mixed: some are final runtime boundary adapters; others are historical
  anti-corruption or legacy facade surfaces.

Is this final architecture?
- Mixed.

Final owner:
- stable adapter modules for real runtime boundaries; delete or migrate legacy
  bridge/facade layers by concrete surface.

Disposition:
- Keep as Final Adapter.

Delete condition:
- final adapter names stay only where they adapt real runtime/rendering
  boundaries; legacy migration wrappers are separately deleted or renamed.

Risk:
- medium; treating all adapters as debt would incorrectly delete final
  architecture adapters.

## Surface: probe and smoke runtime surface

Category:
- probe / smoke-only runtime

Current location:
- `apps/*/tests/*smoke.cpp`
- `apps/*/src/*adoption_probe.*`
- `modules/*/tests/*smoke.cpp`
- `modules/ui_lvgl_ux_packs/src/runtime/*probe.*`
- `modules/ui_presentation/src/workspace/presentation_workspace_probe.cpp`
- `legacy/app_implementations/*/archive/tests/*`

Current callers:
- test targets, architecture checkers, and CMake test sections.

Current responsibility:
- validates migration paths, fallback paths, descriptor consumers, and
  presentation snapshots.

Is this final architecture?
- No for product runtime; yes only as tests or diagnostic probes.

Final owner:
- tests, architecture tooling, or final diagnostic modules when explicitly
  retained.

Disposition:
- Test-only / Smoke-only.

Delete condition:
- remove or relocate when covered by stable integration tests; no app runtime
  path should depend on smoke-only code.

Risk:
- medium; probes are useful guardrails but can become accidental product paths.

## Surface: historical debt docs

Category:
- historical debt docs

Current location:
- `docs/audits/LEGACY_BURNDOWN_REGISTER.md`
- `docs/audits/LEGACY_APP_IMPLEMENTATION_ROOT_BURN_DOWN_AUDIT.md`
- `docs/audits/LEGACY_LINUX_APP_ADAPTER_BURN_DOWN_AUDIT.md`
- `docs/audits/LEGACY_LINUX_LOCAL_ROOT_COLLAPSE_AUDIT.md`
- `docs/audits/PHASE12_FALLBACK_DELETION_READINESS.md`
- `docs/audits/PHASE12_DEPRECATED_ALIAS_CLEANUP_PLAN.md`
- `docs/audits/POST_REFACTOR_FINAL_CLOSEOUT_REPORT.md`
- `docs/audits/TRANSITIONAL_BUILD_ENTRYPOINTS.md`

Current callers:
- checkers and human review.

Current responsibility:
- records why non-final surfaces remain and what conditions delete them.

Is this final architecture?
- No; they are debt ledgers.

Final owner:
- docs/archive once the debts are resolved; active specs/checkers only for
  current guardrails.

Disposition:
- Must Migrate.

Delete condition:
- historical reports move to docs/archive or are superseded by final guardrail
  specs that no longer accept the legacy surface.

Risk:
- low-medium; deleting docs early loses deletion rationale.

## Surface: legacy-governing checker surface

Category:
- legacy-governing checkers

Current location:
- `tools/architecture/check_legacy_app_roots_burndown_ready.py`
- `tools/architecture/check_phase8_layout_ready.py`
- `tools/architecture/check_phase9_legacy_burndown_ready.py`
- `tools/architecture/check_post_refactor_final_ready.py`
- phase 5/6 legacy baseline JSON and historical checkers

Current callers:
- `tools/architecture/check_post_refactor_final_ready.py`
- direct developer/CI invocations

Current responsibility:
- proves legacy is contained, not absent.

Is this final architecture?
- No, except as historical checkers.

Final owner:
- active final guardrail checker that forbids legacy root reintroduction, plus
  archived historical checkers if still useful.

Disposition:
- Must Migrate.

Delete condition:
- final no-root-legacy checker exists and active final checker no longer
  requires `legacy/app_implementations` to exist.

Risk:
- medium; retiring too soon removes guardrails before final cutover.

## Surface: legacy/transitional/compatibility/shim/fallback names

Category:
- legacy/transitional/compatibility/shim/fallback names

Current location:
- filenames and APIs containing `legacy`, `transitional`, `compatibility`,
  `compat`, `shim`, `fallback`, `adapter`, `bridge`, `probe`, or `smoke` across
  apps, modules, platform, legacy, docs, and tools.

Current callers:
- discovered by `tools/architecture/scan_legacy_compat_temp_surfaces.py`.

Current responsibility:
- mixed: some names describe real final adapters, some describe temporary debt,
  and some are tests/docs only.

Is this final architecture?
- Mixed.

Final owner:
- name-specific final owner; inventory and scan report must be used before
  renaming or deleting.

Disposition:
- Must Rename.

Delete condition:
- each name is either proven final and allowed, renamed to accurate final
  language, moved to tests/docs, or deleted.

Risk:
- medium; blanket keyword deletion would remove valid final adapters.

## Surface: compatibility UX pack and compatibility screen factory

Category:
- compatibility shims

Current location:
- `modules/ui_lvgl_ux_packs/include/ui_lvgl_ux_packs/packs/compatibility_ux_pack.h`
- `modules/ui_lvgl_ux_packs/src/packs/compatibility_ux_pack.cpp`
- `modules/ui_lvgl_ux_packs/include/ui_lvgl_ux_packs/runtime/compatibility_screen_factory.h`
- `modules/ui_lvgl_ux_packs/src/runtime/compatibility_screen_factory.cpp`

Current callers:
- `cmake/TrailMateUxPacks.cmake`
- `cmake/TrailMateLinuxSources.cmake`
- LVGL UX pack registry and tests

Current responsibility:
- compatibility LVGL UX and screen descriptor factory.

Is this final architecture?
- Not as a product UX; may remain only as contained compatibility runtime.

Final owner:
- `modules/ui_lvgl_ux_packs`.

Disposition:
- Must Migrate.

Delete condition:
- concrete target UX packs and renderer descriptor consumers replace the
  compatibility pack for supported product paths.

Risk:
- medium; many tests and compatibility paths still depend on it.

## Surface: ui_legacy_adapters module

Category:
- compatibility shims

Current location:
- `modules/ui_legacy_adapters`
- `modules/ui_legacy_adapters/library.json`
- `modules/ui_legacy_adapters/include/ui_legacy_adapters/*`
- `modules/ui_legacy_adapters/tests/*`

Current callers:
- `cmake/TrailMateLinuxSources.cmake`
- deprecated alias tests
- docs and checkers

Current responsibility:
- bounded compatibility adapter and alias module split out from `ui_shared`.

Is this final architecture?
- No as a long-term module name; yes only as temporary compatibility
  containment.

Final owner:
- stable runtime modules or deletion after downstream include burn-down.

Disposition:
- Keep as Deprecated Alias Temporarily.

Delete condition:
- all aliases either deleted or renamed to final non-legacy adapter APIs.

Risk:
- medium; deleting early breaks compatibility alias tests and possible
  downstream callers.
