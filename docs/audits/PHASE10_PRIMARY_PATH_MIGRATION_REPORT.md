# Phase 10 Primary Path Migration Report

## Scope

Phase 10 changes the default path. Later LinuxSim/uConsole burn-down deletes
their hardcoded fallback branches. It still does not rewrite GTK widgets,
rewrite LVGL menu/page renderers, add UX packs, or let runtime entries choose
UX packs.

The migration rule is:

adoption descriptor path is primary; failed LinuxSim/uConsole/LVGL adoption is
unavailable-on-failure

## LinuxSim Primary Path

Previous status:

- LinuxSim hardcoded runtime routing was a contained fallback.
- `LinuxSimRuntimeEntry` consumed `LinuxSimRuntimeEntryAdoptionProbe`, but the
  primary/fallback source was not explicit.

Current primary path:

`LinuxSimRuntimeEntry -> LinuxSimRuntimeEntryAdoptionProbe -> AsciiRuntimeEntryAdoption -> AsciiRuntimeScreenGraphPresenter`

Current status:

- `LinuxSimRuntimeSource::ScreenGraphAdoption` is the source when adoption
  loads.
- `LinuxSimRuntimeSource::Unavailable` is used when adoption fails.
- `usingPrimaryScreenGraph()` and `runtimeSource()` expose the active path.

Fallback status:

deleted after LinuxSim/uConsole fallback burn-down

Deletion condition:

satisfied: simulator renderer no longer needs hardcoded routing.

## GTK Primary Path

Previous status:

- GTK hardcoded page registry was a contained fallback.
- `LinuxUConsoleGtkPageRegistryAdoption` exposed descriptors, but the
  primary/fallback source was not explicit.

Current primary path:

`LinuxUConsoleGtkPageRegistryAdoption -> GtkRuntimeEntryAdoption -> GtkUConsoleScreenGraphPresenter`

Current status:

- `LinuxUConsoleGtkPageRegistrySource::ScreenGraphAdoption` is the source when
  adoption loads.
- `LinuxUConsoleGtkPageRegistrySource::Unavailable` is used when adoption
  fails.
- `usingPrimaryScreenGraph()` and `registrySource()` expose the active path.

Fallback status:

deleted after LinuxSim/uConsole fallback burn-down

Deletion condition:

satisfied: GTK descriptor page registry is the only active page-registry source.

## LVGL Primary Descriptor Path

Previous status:

- LVGL hardcoded menu/page creation was a contained fallback.
- `LvglRuntimeAdoptionProbe` proved compatibility runtime descriptor
  consumption, but the descriptor runtime was not the explicit primary source.

Current primary descriptor path:

`LvglPrimaryScreenGraphRuntime -> LvglRuntimeEntryAdoption -> LvglRuntimeScreenGraphPresenter`

Current status:

- `LvglScreenGraphRuntimeSource::ScreenGraphAdoption` is the default source
  when adoption loads.
- `LvglScreenGraphRuntimeSource::Unavailable` is used when adoption fails.
- `usingPrimaryScreenGraph()` and `runtimeSource()` expose the active path.

Fallback status:

LVGL fallback deleted

Real widget migration:

deferred

Deletion condition:

satisfied: failed adoption no longer enters hardcoded LVGL menu/page creation.

## Not Done

- real GTK widget hierarchy rewrite
- real LVGL widget/menu rewrite
- full navigation stack replacement
- all screen/page migration

## Checker Status

`tools/architecture/check_phase10_primary_path_ready.py` verifies that
LinuxSim/uConsole primary paths fail closed with unavailable-on-failure, that
LVGL failed adoption is unavailable-on-failure, and that forbidden-token
guardrails remain in place.

## Phase 11 Recommendation

Phase 11 should target real renderer descriptor consumption:

- first consume ASCII descriptors as renderer input without hardcoded route
  lookup
- then let GTK page registry consume descriptors as the concrete page list
- then migrate LVGL device renderers from descriptor-primary runtime into real
  widget/menu construction
