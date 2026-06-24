# Key Verification / Map Overlay Legacy Burn-down Audit

## Purpose

Phase 9.5 burns down the remaining key verification and map overlay legacy
presentation adapters from main runtime ownership. The goal is not to move these
surfaces under `legacy/`; the goal is to make stable runtime modules the real
owners and remove old `Legacy*` headers from the active build include surface.

Audit fields intentionally include current callers, main runtime callers, test callers,
docs references, replacement, and migration decision for each legacy surface.

## LegacyKeyVerificationSession

| Field | Value |
| --- | --- |
| Current compatibility header | removed from active build include surface |
| Former compatibility header | `modules/ui_shared/include/ui/presentation_sources/legacy_key_verification_session.h` |
| Current implementation source | none; runtime session adapter owns the state |
| Former implementation source | session state was embedded in the legacy key verification adapter implementation |
| Current callers | documentation references only |
| Main runtime callers | none |
| Test callers | runtime adapter tests only |
| Docs references | historical Phase 7/8 audits and this burn-down audit |
| Replacement | `ui_key_verification_runtime::KeyVerificationSessionAdapter` |
| Migration decision | main runtime owns the stable session adapter name directly; old headers are not build-visible |

## LegacyKeyVerificationSource

| Field | Value |
| --- | --- |
| Current compatibility header | removed from active build include surface |
| Former compatibility header | `modules/ui_shared/include/ui/presentation_sources/legacy_key_verification_source.h` |
| Current implementation source | `modules/ui_key_verification_runtime/src/key_verification_presentation_source.cpp` |
| Former implementation source | `modules/ui_legacy_adapters/src/legacy_key_verification_source.cpp` |
| Current callers | main runtime includes `ui_key_verification_runtime/key_verification_presentation_source.h`; docs may mention the old name historically |
| Main runtime callers | `modules/ui_shared/src/ui/screens/chat/chat_page_runtime.cpp` and `modules/ui_chat_runtime/src/chat_page_runtime_event_pump.cpp` use `KeyVerificationPresentationSource` |
| Test callers | `modules/ui_key_verification_runtime/tests/test_key_verification_runtime_adapters.cpp` |
| Docs references | historical Phase 7/8 audits and this burn-down audit |
| Replacement | `ui_key_verification_runtime::KeyVerificationPresentationSource` implementing `IKeyVerificationPresentationSource` |
| Migration decision | event pump and chat runtime consume the stable presentation source; old header is not build-visible |

## LegacyKeyVerificationActionSink

| Field | Value |
| --- | --- |
| Current compatibility header | removed from active build include surface |
| Former compatibility header | `modules/ui_shared/include/ui/presentation_sources/legacy_key_verification_action_sink.h` |
| Current implementation source | `modules/ui_key_verification_runtime/src/key_verification_action_sink.cpp` |
| Former implementation source | `modules/ui_legacy_adapters/src/legacy_key_verification_action_sink.cpp` |
| Current callers | main runtime includes `ui_key_verification_runtime/key_verification_action_sink.h`; docs may mention the old name historically |
| Main runtime callers | `modules/ui_shared/src/ui/screens/chat/chat_page_runtime.cpp` uses `KeyVerificationActionSink` |
| Test callers | `modules/ui_key_verification_runtime/tests/test_key_verification_runtime_adapters.cpp` |
| Docs references | historical Phase 7/8 audits and this burn-down audit |
| Replacement | `ui_key_verification_runtime::KeyVerificationActionSink` implementing `IKeyVerificationActionSink` |
| Migration decision | key verification actions go through the stable command sink; old header is not build-visible |

## LegacyMapOverlaySource

| Field | Value |
| --- | --- |
| Current compatibility header | removed from active build include surface |
| Former compatibility header | `modules/ui_shared/include/ui/presentation_sources/legacy_map_overlay_source.h` |
| Current implementation source | `modules/ui_map_runtime/src/map_overlay_snapshot_source.cpp` |
| Former implementation source | `modules/ui_legacy_adapters/src/legacy_map_overlay_source.cpp` |
| Current callers | main runtime includes `ui_map_runtime/map_overlay_snapshot_source.h`; docs may mention the old name historically |
| Main runtime callers | `modules/ui_shared/src/ui/screens/gps/gps_page_runtime.cpp` uses `MapOverlaySnapshotSource` |
| Test callers | `modules/ui_map_runtime/tests/test_map_overlay_snapshot_source.cpp` |
| Docs references | historical Phase 7/8 audits and this burn-down audit |
| Replacement | `ui::map_overlay::MapOverlaySnapshotSource` and `ui::map_overlay::MapOverlayProjectionAdapter` |
| Migration decision | GPS/map runtime consumes the stable snapshot source; old header is not build-visible |

## Caller Classification

Main runtime callers were migrated to stable owners:

- `chat_page_runtime.cpp` uses `KeyVerificationSessionAdapter`, `KeyVerificationPresentationSource`, and `KeyVerificationActionSink`.
- `chat_page_runtime_event_pump.*` uses `KeyVerificationPresentationSource`.
- `gps_page_runtime.cpp` uses `MapOverlaySnapshotSource`.

Compatibility callers are not build-visible. The old names are historical doc
references only; tests use runtime headers directly.

## Burn-down Decision

The old `LegacyKeyVerification*` and `LegacyMapOverlaySource` names are no
longer runtime owners. Their alias build include surface is removed; the main
runtime graph uses stable module names.

## Checker Decision

`check_phase9_legacy_burndown_ready.py` must reject:

- main-code includes of the old key verification and map overlay legacy headers
- old key verification or map overlay legacy implementation sources in build lists
- legacy alias headers or tests returning to the build include surface

It may allow:

- historical docs references
