# Phase 7.6 Legacy Burn-down Report

## Decision

Phase 7.6 reduced Chat / Team / key-verification legacy ownership surfaces without introducing new runtime models.

The pass burned down legacy paths already covered by Phase 7 owners and documented exit conditions for bridges that must remain temporarily.

## Burned Down

| Surface | Result |
| --- | --- |
| `ChatUiController` direct Team payload encoding | forbidden; controller submits `TeamActionRequest` for location marker sends |
| `ChatUiController` direct key verification runtime calls | forbidden; controller calls `KeyVerificationModel` only |
| `ChatUiController` direct delivery ownership | forbidden; controller does not own read model, projector, or action service |
| Key verification modal rendering | extracted to `KeyVerificationModalRenderer` helper consuming `KeyVerificationSnapshot` |
| Legacy key verification API token in controller | controller UI method renamed to avoid `submitKeyVerificationNumber` ownership token |

## Still Contained

| Surface | Reason | Exit condition |
| --- | --- | --- |
| `LegacyChatDeliveryEventBridge` | Closed by later runtime event pump / delivery adapter work | Removed after `ChatDeliveryEventProjectionAdapter` became the only active owner |
| `LegacyChatDeliveryActionBridge` | Closed by later delivery action adapter work | See `LEGACY_BURNDOWN_REGISTER.md` for current status |
| `LegacyTeamActionBridge` | Closed by later Team burn-down; active bridge file/class/test removed | See `LEGACY_BURNDOWN_REGISTER.md` for current owner |
| `LegacyKeyVerificationSource` / `LegacyKeyVerificationActionSink` | MeshCore / Meshtastic verification APIs still differ behind adapter | Split or rename protocol-specific adapters |
| `ChatUiController` Team ownership | Closed by later Team workflow/rich payload burn-down | Controller no longer owns Team payload formatting or Team action send construction |

## Checker Changes

- Added `LEGACY_BURNDOWN_REGISTER.md`, `CHAT_UI_CONTROLLER_BURNDOWN_AUDIT.md`, and this closeout report as required Phase 7 files.
- Added burn-down register token checks for remaining callers, removal condition, target phase, and status.
- Required key verification modal renderer helper files.
- Forbid direct Team send payload encoding tokens in `ChatUiController`.
- Forbid direct key verification runtime API tokens in `ChatUiController`.
- Forbid direct delivery read model / projector / action service ownership in `ChatUiController`.
- Narrowed legacy API token survival to legacy adapter/runtime paths.

## Remaining Work

| Work | Owner direction |
| --- | --- |
| Event pump extraction | `chat_page_runtime.cpp` or app shell should forward runtime events before controller refresh |
| Team rich payload rendering | Closed by `TeamRichPayloadProjector`, `TeamChatPresentationSource`, and `MessageRow::team_rich_payload` |
| Key verification modal full view split | A fuller LVGL view object can own modal lifecycle beyond helper functions |
| ChatUiController thinning | Conversation cache and store flush should move to runtime/app shell owners |

## Later Team Closeout

This Phase 7.6 report is historical. Later Team burn-down passes removed the
active `LegacyTeamActionBridge`, moved Team send workflow through
`TeamActionRuntimeSink`, moved rich Team row data into structured presentation
fields, and closed Team Page command/event/navigation/render seams. Current
Team status is tracked by `LEGACY_BURNDOWN_REGISTER.md`.
