# Phase 9 Final Readiness Report

## Scope

Phase 9.6 closes the Phase 9 architecture work. It does not add new presenter,
entry adoption, UX pack, renderer, or legacy compatibility layers.

The Phase 9 closeout distinction is:

- Phase 9 proves runtime adoption paths and burns down selected legacy
  adapters from main runtime ownership.
- Phase 10 makes one adopted path the primary runtime path.

## Runtime Adoption

| Runtime | Presenter status | Entry adoption status | Real entry status | Remaining fallback |
| --- | --- | --- | --- | --- |
| ASCII / LinuxSim | `AsciiRuntimeScreenGraphPresenter` done in `modules/ui_ascii_runtime` | `AsciiRuntimeEntryAdoption` done | `LinuxSimRuntimeEntry` consumes the adoption probe as the only active renderer source | deleted after LinuxSim/uConsole fallback burn-down |
| GTK / uConsole | `GtkUConsoleScreenGraphPresenter` done in `modules/ui_gtk_runtime` | `GtkRuntimeEntryAdoption` done | `LinuxUConsoleGtkPageRegistryAdoption` exposes descriptor data as the only active page-registry source | deleted after LinuxSim/uConsole fallback burn-down |
| LVGL | `LvglRuntimeScreenGraphPresenter` done in `modules/ui_lvgl_ux_packs` | `LvglRuntimeEntryAdoption` done | `LvglPrimaryScreenGraphRuntime` consumes descriptor adoption as the only active descriptor-runtime source | deleted after LVGL fallback burn-down |

The runtime adoption path is real enough to test:

`PresentationBundle -> RuntimeEntryAdoption -> RuntimeScreenGraphPresenter`

It is not yet the primary renderer path. That is the Phase 10 boundary.

Readiness summary:

- ASCII presenter: done; entry adoption done; hardcoded runtime fallback deleted.
- GTK presenter: done; entry adoption done; hardcoded page-registry fallback deleted.
- LVGL presenter: done; primary descriptor runtime done; real widget/menu
  migration remains.

## Legacy Burn-down

| Surface | Phase 9 status | Stable owner | Remaining compatibility |
| --- | --- | --- | --- |
| ChatDelivery | main runtime callers removed; burned down to formal adapters | `ChatDeliveryActionPortAdapter`, `ChatDeliveryEventProjectionAdapter`, `IChatDeliveryActionPort`, `IChatDeliveryEventPort` in `modules/ui_chat_runtime` | alias build include surface removed |
| KeyVerification | main runtime callers removed; burned down to formal source/sink/session adapter | `KeyVerificationPresentationSource`, `KeyVerificationActionSink`, `KeyVerificationSessionAdapter` in `modules/ui_key_verification_runtime` | alias build include surface removed |
| MapOverlay | main runtime callers removed; burned down to stable snapshot/projection adapters | `MapOverlaySnapshotSource`, `MapOverlayProjectionAdapter` in `modules/ui_map_runtime` | alias build include surface removed |

Legacy headers are not primary runtime APIs. The migrated alias headers have
been removed from the active build include surface.

## No Remaining Fallback

Current fallback status:

| Fallback | Owner | New path | Exit condition |
| --- | --- | --- | --- |
| LinuxSim hardcoded runtime routing | final LinuxSim app shell | `LinuxSimRuntimeEntry -> LinuxSimRuntimeEntryAdoptionProbe -> AsciiRuntimeEntryAdoption` | satisfied; fallback branch deleted |
| GTK hardcoded page registry | final GTK app shell page-registry adoption | `LinuxUConsoleGtkPageRegistryAdoption -> GtkRuntimeEntryAdoption -> GtkUConsoleScreenGraphPresenter` | satisfied; fallback branch deleted |
| LVGL hardcoded menu/page creation | `modules/ui_lvgl_ux_packs` descriptor runtime | `LvglPrimaryScreenGraphRuntime -> LvglRuntimeEntryAdoption -> LvglRuntimeScreenGraphPresenter` | satisfied; fallback branch deleted |

The fallback ledger remains the authoritative itemized tracker for fallback
owner, reason, new path, exit condition, and checker status.

## Consistency Baseline

Phase 9 final readiness requires the following documents to agree:

- `docs/audits/LEGACY_BURNDOWN_REGISTER.md`
- `docs/audits/PHASE9_LEGACY_BURNDOWN_REPORT.md`
- `docs/audits/PHASE9_FALLBACK_CONTAINMENT_LEDGER.md`

The shared status for ChatDelivery, KeyVerification, and MapOverlay is:

main runtime callers removed; alias build include surface removed

The shared status for LinuxSim and GTK hardcoded UI paths is:

deleted after LinuxSim/uConsole fallback burn-down

The shared status for the LVGL hardcoded UI path is:

deleted after LVGL fallback burn-down

## Phase 10 Entry Recommendation

First target:

LinuxSim / ASCII primary path

Completed Phase 10/11 burn-down follow-up:

LinuxSim hardcoded runtime routing -> `AsciiRuntimeEntryAdoption as primary source`
and unavailable-on-failure for failed adoption.

Why this is the right first cut:

- It avoids real device memory, input, and LVGL layout risk.
- It does not require GTK widget or page-registry replacement.
- It proves the ScreenGraphPresenter path can become primary before touching
  device renderers.
- Failure cost is low because invalid adoption now fails closed instead of
  selecting a second UI source.

Expected risk:

Low to medium. The risk is output parity and failed-adoption detection, not device
memory pressure, platform widget lifecycle, or board-specific layout behavior.

## Phase 9.6 Guardrail

No new Phase 9 work should add runtime ownership under `legacy/`, introduce a
new presenter/adoption layer, or rename remaining fallback as completed primary
path migration. Phase 10 starts only when one real runtime path makes the
adoption path primary.
