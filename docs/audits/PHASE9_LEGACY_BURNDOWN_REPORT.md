# Phase 9 Legacy Burn-down Report

## Burned Down In Phase 9.1

The Phase 8 ASCII and GTK descriptor adapters were initially located under
`legacy/app_implementations`. Phase 9 moves them to module-owned runtime
surfaces:

| Surface | Previous owner | New owner | Status |
| --- | --- | --- | --- |
| ASCII menu runtime adapter | `legacy/app_implementations/linux_sim` | `modules/ui_ascii_runtime` | moved out of legacy |
| ASCII screen host adapter | `legacy/app_implementations/linux_sim` | `modules/ui_ascii_runtime` | moved out of legacy |
| ASCII screen graph bridge | `legacy/app_implementations/linux_sim` | `modules/ui_ascii_runtime` | moved out of legacy |
| GTK menu runtime adapter | `legacy/app_implementations/linux_uconsole` | `modules/ui_gtk_runtime` | moved out of legacy |
| GTK screen host adapter | `legacy/app_implementations/linux_uconsole` | `modules/ui_gtk_runtime` | moved out of legacy |
| GTK screen graph bridge | `legacy/app_implementations/linux_uconsole` | `modules/ui_gtk_runtime` | moved out of legacy |

## Burned Down In Phase 9.4

Phase 9.4 burns down the Chat delivery bridge pair from real implementation
ownership into stable `ui_chat_runtime` ports and adapters. The event-side
compatibility alias has since been deleted completely.

| Surface | Previous owner | New owner | Status |
| --- | --- | --- | --- |
| `LegacyChatDeliveryActionBridge` | `modules/ui_legacy_adapters` real bridge implementation | `ChatDeliveryActionPortAdapter` and `IChatDeliveryActionPort` in `modules/ui_chat_runtime` | main runtime callers removed; alias build include surface removed |
| `LegacyChatDeliveryEventBridge` | `modules/ui_legacy_adapters` real bridge implementation | `ChatDeliveryEventProjectionAdapter` and `IChatDeliveryEventPort` in `modules/ui_chat_runtime` | implementation, forwarding headers, and alias tests removed |

The former `modules/ui_legacy_adapters/src/legacy_chat_delivery_*_bridge.cpp`
implementation files are removed from the build. The action compatibility
headers have also been removed from the active build include surface; the event
compatibility headers were already removed.

## Burned Down In Phase 9.5

Phase 9.5 burns down the KeyVerification and MapOverlay legacy presentation
adapters from main runtime ownership into stable runtime modules.

| Surface | Previous owner | New owner | Status |
| --- | --- | --- | --- |
| `LegacyKeyVerificationSession` | `modules/ui_legacy_adapters` compatibility session state | `KeyVerificationSessionAdapter` in `modules/ui_key_verification_runtime` | main runtime callers removed; alias build include surface removed |
| `LegacyKeyVerificationSource` | `modules/ui_legacy_adapters` real presentation source implementation | `KeyVerificationPresentationSource` in `modules/ui_key_verification_runtime` | main runtime callers removed; alias build include surface removed |
| `LegacyKeyVerificationActionSink` | `modules/ui_legacy_adapters` real action sink implementation | `KeyVerificationActionSink` in `modules/ui_key_verification_runtime` | main runtime callers removed; alias build include surface removed |
| `LegacyMapOverlaySource` | `modules/ui_legacy_adapters` real map overlay source implementation | `MapOverlaySnapshotSource` and `MapOverlayProjectionAdapter` in `modules/ui_map_runtime` | main runtime callers removed; alias build include surface removed |

The former `modules/ui_legacy_adapters/src/legacy_key_verification_*` and
`modules/ui_legacy_adapters/src/legacy_map_overlay_source.cpp` implementation
files are removed from the build. Compatibility headers and alias smoke tests
are removed from the active build include surface.

## Still Contained

These code-level legacy adapters remain governed by
`docs/audits/LEGACY_BURNDOWN_REGISTER.md`:

- map tile compatibility surfaces
- route/tracker overlay fallback surfaces

`LegacyTeamActionBridge` has since been removed from active code by the Team
burn-down and is no longer a remaining Phase 9 target.

## Next Burn-down Target

The next Phase 9 legacy burn-down target should be selected from the remaining
non-Chat, non-KeyVerification, non-MapOverlay compatibility surfaces in the
register. Chat delivery, KeyVerification, and MapOverlay are no longer main
runtime fallbacks; the active build now consumes the runtime headers directly.

## Rule

Phase 9 does not add new runtime ownership under `legacy/`. Compilation
failures in a legacy root should be treated as a signal to move the runtime
surface into a stable module or to replace the legacy adapter.

## Phase 9.2 Correction

Runtime entry adoption helpers are not maintained under
`legacy/app_implementations`. The Phase 9.2 entry adoption surfaces live in
`modules/ui_ascii_runtime`, `modules/ui_gtk_runtime`, and
`modules/ui_lvgl_ux_packs`; final app-shell probes live under
`apps/linux_sim_shell` and `apps/linux_uconsole_gtk`.

If a future Phase 9 task needs behavior that currently sits in
`legacy/app_implementations`, the task should burn down that behavior into a
stable owner instead of adding another compatibility helper inside the legacy
tree.

## Phase 9.3 Correction

Real runtime entry adoption also stays out of `legacy/app_implementations`.
The LinuxSim runtime entry and GTK page-registry adoption live under final
app-shell directories, while the LVGL compatibility runtime adoption probe
lives under `modules/ui_lvgl_ux_packs`.

Legacy implementation roots may continue to exist as historical fallback, but
new Phase 9 adoption code must either live in a stable module or in the final
app-shell surface that owns product startup semantics.

## Phase 9.6 Final Readiness Alignment

Phase 9.6 keeps the legacy burn-down status aligned with
`docs/audits/PHASE9_FINAL_READINESS_REPORT.md`:

| Surface | Final Phase 9 status | Stable owner | Deletion window |
| --- | --- | --- | --- |
| ChatDelivery legacy bridges | main runtime callers removed; alias build include surface removed | `ChatDeliveryActionPortAdapter` and `ChatDeliveryEventProjectionAdapter` | keep runtime headers as the only build-visible API |
| KeyVerification legacy source/sink/session | main runtime callers removed; alias build include surface removed | `KeyVerificationPresentationSource`, `KeyVerificationActionSink`, and `KeyVerificationSessionAdapter` | keep runtime headers as the only build-visible API |
| MapOverlay legacy source | main runtime callers removed; alias build include surface removed | `MapOverlaySnapshotSource` and `MapOverlayProjectionAdapter` | keep runtime headers as the only build-visible API |

LinuxSim, GTK, and LVGL hardcoded runtime paths are now reported as burned down:
their fallback branches and fallback smoke targets are deleted, and failed
adoption is unavailable-on-failure. LVGL is still pending real widget/menu
migration, but failed descriptor adoption no longer selects a second hardcoded
UI source.
