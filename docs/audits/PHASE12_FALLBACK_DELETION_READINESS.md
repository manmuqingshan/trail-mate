# Phase 12 Fallback Deletion Readiness

## Purpose

Phase 12 records the fallback deletion guardrail. LinuxSim/uConsole/LVGL
follow-up work deletes the fallback surfaces whose exit conditions are now
satisfied, while the ledger records how those deletions must stay enforced.

## Readiness Ledger

| Surface | Current status | Primary path replacement | Safe to delete now? | Why / why not | Deletion condition | Owner | Checker status |
| --- | --- | --- | --- | --- | --- | --- | --- |
| LinuxSim hardcoded runtime routing | deleted | `LinuxSimRuntimeRenderer -> AsciiDescriptorRenderer -> AsciiRuntimeEntryAdoption` | Yes | exit condition satisfied; fallback smoke removed and failed adoption is unavailable-on-failure | keep `HardcodedFallback`, `fallbackUsed`, and `renderFallback` absent from `apps/linux_sim_shell` | `apps/linux_sim_shell` | `check_phase10_primary_path_ready.py`, `check_phase11_renderer_consumption_ready.py` |
| GTK hardcoded page registry | deleted | `LinuxUConsoleGtkPageRegistryRenderer -> GtkDescriptorPageRegistry -> GtkRuntimeEntryAdoption` | Yes | exit condition satisfied; fallback smoke removed and failed adoption is unavailable-on-failure | keep `HardcodedFallback`, `fallbackUsed`, and `renderFallback` absent from `apps/linux_uconsole_gtk` | `apps/linux_uconsole_gtk` | `check_phase10_primary_path_ready.py`, `check_phase11_renderer_consumption_ready.py` |
| LVGL hardcoded menu/page creation | deleted | `LvglDescriptorRendererProbe -> LvglDescriptorMenuModel -> LvglPrimaryScreenGraphRuntime` | Yes | exit condition satisfied; descriptor runtime failure is unavailable-on-failure | keep `HardcodedFallback`, `fallbackUsed`, `usedFallback`, and `loadFallback` absent from `modules/ui_lvgl_ux_packs` | `modules/ui_lvgl_ux_packs` | `check_phase10_primary_path_ready.py`, `check_phase11_renderer_consumption_ready.py` |
| Chat legacy alias headers | alias build include surface removed | `ChatDeliveryActionPortAdapter` | Yes | runtime headers are the only build-visible API | keep alias headers absent from active build roots | `modules/ui_chat_runtime` | `check_phase9_legacy_burndown_ready.py` |
| KeyVerification legacy alias headers | alias build include surface removed | `KeyVerificationPresentationSource`, `KeyVerificationActionSink`, `KeyVerificationSessionAdapter` | Yes | runtime headers are the only build-visible API | keep alias headers absent from active build roots | `modules/ui_key_verification_runtime` | `check_phase9_legacy_burndown_ready.py` |
| MapOverlay legacy alias header | alias build include surface removed | `MapOverlaySnapshotSource`, `MapOverlayProjectionAdapter` | Yes | runtime headers are the only build-visible API | keep alias headers absent from active build roots | `modules/ui_map_runtime` | `check_phase9_legacy_burndown_ready.py` |
| `ui_shared` legacy presentation forwarding shims | alias build include surface removed | stable runtime module headers | Yes | runtime headers are the only build-visible API | keep `ui/presentation_sources/legacy_*` absent from active build roots | `modules/ui_shared` | `check_phase9_legacy_burndown_ready.py` |
| `legacy/app_implementations` roots | historical implementation roots only | final app shells plus stable modules | No | roots still provide transitional build/runtime compatibility | final app shells and stable modules own all runtime behavior and builds no longer need historical roots | `legacy/app_implementations` | `check_phase8_layout_ready.py`, `check_post_refactor_final_ready.py` |

## Deletion Rule

Fallback deletion must be a targeted debt task, not a new architecture phase.
Every deletion needs:

- primary path evidence
- fallback smoke update or removal
- repo/downstream include search
- checker update
- concrete owner

## Current Decision

LinuxSim, GTK, and LVGL runtime fallbacks are deleted after their primary
descriptor paths became the only active app/runtime sources. The migrated
deprecated alias headers have left the build include surface, and Phase 12
records the guardrails that prevent deleted fallback from becoming a primary
path again.
