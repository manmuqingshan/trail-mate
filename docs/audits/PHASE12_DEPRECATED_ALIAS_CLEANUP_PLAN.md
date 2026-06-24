# Phase 12 Deprecated Alias Cleanup Plan

## Purpose

Phase 9 burned selected `Legacy*` surfaces down to runtime-owned adapters.
The alias headers have now been retired from the active build include surface.

## Alias Ledger

| Alias | Former alias header path | Replacement header | Remaining includes | Build-visible? | Current condition |
| --- | --- | --- | --- | --- | --- |
| `LegacyChatDeliveryActionBridge` | `modules/ui_legacy_adapters/include/ui_legacy_adapters/legacy_chat_delivery_action_bridge.h` and `modules/ui_shared/include/ui/presentation_sources/legacy_chat_delivery_action_bridge.h` | `ui_chat_runtime/chat_delivery_action_port_adapter.h` | docs references only | No | retired from build include surface |
| `LegacyKeyVerificationSource` | `modules/ui_legacy_adapters/include/ui_legacy_adapters/legacy_key_verification_source.h` and `modules/ui_shared/include/ui/presentation_sources/legacy_key_verification_source.h` | `ui_key_verification_runtime/key_verification_presentation_source.h` | docs references only | No | retired from build include surface |
| `LegacyKeyVerificationActionSink` | `modules/ui_legacy_adapters/include/ui_legacy_adapters/legacy_key_verification_action_sink.h` and `modules/ui_shared/include/ui/presentation_sources/legacy_key_verification_action_sink.h` | `ui_key_verification_runtime/key_verification_action_sink.h` | docs references only | No | retired from build include surface |
| `LegacyKeyVerificationSession` | `modules/ui_legacy_adapters/include/ui_legacy_adapters/legacy_key_verification_session.h` and `modules/ui_shared/include/ui/presentation_sources/legacy_key_verification_session.h` | `ui_key_verification_runtime/key_verification_session_adapter.h` | docs references only | No | retired from build include surface |
| `LegacyMapOverlaySource` | `modules/ui_legacy_adapters/include/ui_legacy_adapters/legacy_map_overlay_source.h` and `modules/ui_shared/include/ui/presentation_sources/legacy_map_overlay_source.h` | `ui_map_runtime/map_overlay_snapshot_source.h` | docs references only | No | retired from build include surface |

## Rules

- Main code must not include deprecated alias headers.
- Compatibility tests must use runtime headers directly.
- Docs may mention historical alias names.
- Legacy alias headers are retired from build include surface.
- New `Legacy*` bridge names are forbidden for new runtime work.

## Cleanup Window

The active repo no longer carries these alias headers in build-visible include
roots. Downstream compatibility, if needed, must be handled outside the active
build surface or by migrating to the runtime headers above.
