# Phase 9 Runtime Entry Adoption Report

## Rule Correction

Phase 9.2 does not add new runtime ownership under
`legacy/app_implementations`. That tree is a burn-down target. If a runtime
entry adoption change would require editing a legacy implementation root, the
correct move is to move or replace that responsibility in a stable module or a
final app-shell probe.

`legacy/app_implementations remains burn-down`; it is not the home for
`AsciiRuntimeEntryAdoption`, `GtkRuntimeEntryAdoption`, or future entry
adoption helpers.

## LinuxSim

Adopted entry/probe path:

- `modules/ui_ascii_runtime/src/ascii_runtime_entry_adoption.cpp`
- `modules/ui_ascii_runtime/include/ui_ascii_runtime/ascii_runtime_entry_adoption.h`
- `apps/linux_sim_shell/src/linux_sim_runtime_entry_adoption_probe.*`
- `apps/linux_sim_shell/tests/linux_sim_runtime_entry_adoption_probe_smoke.cpp`

The module helper consumes `PresentationBundle` through
`AsciiRuntimeScreenGraphPresenter`. The app-shell probe supplies the
transitional shell-selected UX pack, builds the compatibility presentation
graph, and loads `AsciiRuntimeEntryAdoption` without touching
`legacy/app_implementations`.

Failure behavior:

- adoption failure is unavailable-on-failure; the entry reports
  `LinuxSimRuntimeSource::Unavailable`.
- the hardcoded simulator fallback branch and fallback smoke target are
  deleted from the active app.

Exit condition:

- satisfied: the real simulator entry consumes `AsciiRuntimeEntryAdoption`
  descriptors directly, and old hardcoded routing is no longer an active
  fallback source.

## uConsole GTK

Adopted entry/probe path:

- `modules/ui_gtk_runtime/src/gtk_runtime_entry_adoption.cpp`
- `modules/ui_gtk_runtime/include/ui_gtk_runtime/gtk_runtime_entry_adoption.h`
- `apps/linux_uconsole_gtk/src/linux_uconsole_gtk_runtime_entry_adoption_probe.*`
- `apps/linux_uconsole_gtk/tests/linux_uconsole_gtk_runtime_entry_adoption_probe_smoke.cpp`

The module helper consumes `PresentationBundle` through
`GtkUConsoleScreenGraphPresenter`. The final GTK app-shell probe loads
`GtkRuntimeEntryAdoption` from a compatibility presentation graph and keeps the
historical GTK page registry untouched.

Failure behavior:

- adoption failure is unavailable-on-failure; page-registry adoption reports
  `LinuxUConsoleGtkPageRegistrySource::Unavailable`.
- the hardcoded GTK page-registry fallback branch and fallback smoke target are
  deleted from the active app.

Exit condition:

- satisfied: GTK page-registry adoption consumes `GtkRuntimeEntryAdoption`
  descriptors directly, and the hardcoded page registry is no longer an active
  fallback source.

## LVGL

Adopted probe path:

- `modules/ui_lvgl_ux_packs/src/runtime/lvgl_runtime_entry_adoption.cpp`
- `modules/ui_lvgl_ux_packs/include/ui_lvgl_ux_packs/runtime/lvgl_runtime_entry_adoption.h`
- `modules/ui_lvgl_ux_packs/tests/test_lvgl_runtime_entry_adoption.cpp`

Failure behavior:

- LVGL failed adoption is unavailable-on-failure; descriptor runtime reports
  `LvglScreenGraphRuntimeSource::Unavailable`.
- no hardcoded LVGL menu/page fallback branch remains in the descriptor
  runtime.

Exit condition:

- satisfied: `LvglPrimaryScreenGraphRuntime` consumes
  `LvglRuntimeEntryAdoption` descriptors and failed adoption stops as
  unavailable.

## Guardrail

Phase 9.2 proves real/probe runtime entry adoption, not full navigation
migration. Entry adoption helpers do not select UX packs, construct
`MenuModel`, create GTK/LVGL widgets, or instantiate app services. Final
app-shell probes may adapt the selected UX pack into a temporary
`PresentationBundle`, but that is transitional probe glue, not new product
composition ownership.

## Phase 9.3 Real Entry Adoption

Phase 9.3 adds entry-facing consumers above the Phase 9.2 probes:

- `LinuxSimRuntimeEntry` consumes `AsciiRuntimeEntryAdoption` through
  `LinuxSimRuntimeEntryAdoptionProbe`.
- `LinuxUConsoleGtkPageRegistryAdoption` consumes `GtkRuntimeEntryAdoption`
  and exposes GTK menu/screen descriptors for page-registry adoption.
- `LvglRuntimeAdoptionProbe` consumes `LvglRuntimeEntryAdoption` as the LVGL
  compatibility runtime path.

These are real/probe runtime entry paths rather than plain app-shell smoke
tests. LinuxSim, GTK, and LVGL failed adoption now stop at
unavailable-on-failure. LVGL hardcoded menu/page creation is reported as burned
down in `docs/audits/PHASE9_FALLBACK_CONTAINMENT_LEDGER.md`.

`legacy/app_implementations remains burn-down`: Phase 9.3 does not add
`LinuxSimRuntimeAdoptionBridge`, `GtkRuntimeEntryAdoption`, or screen graph
bridge files under legacy implementation roots.
