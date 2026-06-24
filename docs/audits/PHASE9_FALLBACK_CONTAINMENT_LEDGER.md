# Phase 9 Fallback Containment Ledger

Phase 9.3 originally kept hardcoded routing alive as contained fallback while
real runtime entries began consuming the presentation graph. Current active
guardrails now distinguish historical fallback records from fallback paths
that are still allowed in code.

| Fallback | Current owner | Why fallback remains | New path | Exit condition | Checker status |
| --- | --- | --- | --- | --- | --- |
| LinuxSim hardcoded runtime routing | final `apps/linux_sim_shell` runtime entry | no active fallback remains; failed adoption is unavailable-on-failure | `LinuxSimRuntimeEntry -> LinuxSimRuntimeEntryAdoptionProbe -> AsciiRuntimeEntryAdoption` | satisfied: simulator renderer no longer needs hardcoded routing | deleted after LinuxSim/uConsole fallback burn-down |
| GTK hardcoded page registry | final `apps/linux_uconsole_gtk` page registry adoption | no active fallback remains; failed adoption is unavailable-on-failure | `LinuxUConsoleGtkPageRegistryAdoption -> GtkRuntimeEntryAdoption -> GtkUConsoleScreenGraphPresenter` | satisfied: descriptor page registry is the only active page-registry source | deleted after LinuxSim/uConsole fallback burn-down |
| LVGL hardcoded menu/page creation | `modules/ui_lvgl_ux_packs` descriptor runtime | no active fallback remains; failed adoption is unavailable-on-failure | `LvglPrimaryScreenGraphRuntime -> LvglRuntimeEntryAdoption -> LvglRuntimeScreenGraphPresenter` | satisfied: descriptor runtime failure no longer enters hardcoded menu/page creation | deleted after LVGL fallback burn-down |
| Chat LegacyDelivery bridges | alias build include surface removed | runtime modules own all active includes | `ChatDeliveryActionPortAdapter` and `ChatDeliveryEventProjectionAdapter` in `modules/ui_chat_runtime` | keep runtime headers as the only build-visible API | retired from build include surface |
| KeyVerification legacy source/sink | alias build include surface removed | runtime modules own all active includes | `KeyVerificationPresentationSource`, `KeyVerificationActionSink`, and `KeyVerificationSessionAdapter` in `modules/ui_key_verification_runtime` | keep runtime headers as the only build-visible API | retired from build include surface |
| MapOverlay legacy source | alias build include surface removed | runtime modules own all active includes | `MapOverlaySnapshotSource` and `MapOverlayProjectionAdapter` in `modules/ui_map_runtime` | keep runtime headers as the only build-visible API | retired from build include surface |

## Phase 9.6 Final Readiness Alignment

The runtime fallback ledger after LinuxSim/uConsole burn-down is:

- LinuxSim hardcoded runtime routing: exit condition satisfied; fallback branch
  and fallback smoke target deleted.
- GTK hardcoded page registry: exit condition satisfied; fallback branch and
  fallback smoke target deleted.
- LVGL hardcoded menu/page creation: exit condition satisfied; fallback branch
  deleted after LVGL fallback burn-down.

The legacy adapter rows are not primary UI fallback rows anymore:

- Chat LegacyDelivery bridges: main runtime callers removed; alias build include
  surface removed.
- KeyVerification legacy source/sink: main runtime callers removed; alias build
  include surface removed.
- MapOverlay legacy source: main runtime callers removed; alias build include
  surface removed.
