# Legacy Burn-down Register

## Purpose

This register tracks legacy surfaces that are now contained by Phase 7 runtime ownership boundaries.

A legacy surface may remain temporarily only if it has:

- a new owner
- remaining caller list
- deletion or rename condition
- target removal phase
- checker status

## Chat / Team Legacy Surfaces

| Legacy surface | New owner | Remaining callers | Removal condition | Target phase | Status |
| --- | --- | --- | --- | --- | --- |
| `LegacyChatDeliveryActionBridge` | `ChatDeliveryActionPortAdapter` / `IChatDeliveryActionPort` | no main runtime callers; alias headers and alias tests removed from build include surface | keep runtime headers as the only build-visible API | 9.4 | retired from build include surface |
| `LegacyTeamActionBridge` | `TeamActionRuntimeSink` / `TeamActionRequest` / `ITeamActionSink` / Team runtime command port | none; Chat runtime and Contacts compose own `TeamActionRuntimeSink` instances through composition roots | legacy bridge file/class/test removed and renderers/controllers only submit `TeamActionRequest` | Team legacy burn-down | burned-down |
| `LegacyKeyVerificationSource` | `KeyVerificationPresentationSource` / `IKeyVerificationPresentationSource` in `modules/ui_key_verification_runtime` | no main runtime callers; alias headers and alias tests removed from build include surface | keep runtime headers as the only build-visible API | 9.5 | retired from build include surface |
| `LegacyKeyVerificationActionSink` | `KeyVerificationActionSink` / `IKeyVerificationActionSink` in `modules/ui_key_verification_runtime` | no main runtime callers; alias headers and alias tests removed from build include surface | keep runtime headers as the only build-visible API | 9.5 | retired from build include surface |
| `ChatUiController` key verification modal rendering | `KeyVerificationModalRenderer` helper consumes `KeyVerificationSnapshot` | `ChatUiController` opens/closes modal and forwards submit/trust callbacks | Full modal view object owns widget lifecycle and controller keeps only workflow routing | 7.x | burned-down |
| `ChatUiController` Team payload/action send special case | `ChatTeamWorkflow` / `TeamActionRuntimeSink` | None expected for send path; controller delegates Team text/location actions through workflow | Checker forbids `TeamActionRequest`, `sendTeamAction`, payload encoders, and raw `TeamChatMessage` send encoding in controller | Team legacy burn-down | burned-down |
| `ChatUiController` delivery mutation | `ChatDeliveryReadModel` / `ChatDeliveryActionService` | None expected; event pump forwards send-result events to `ChatDeliveryEventProjectionAdapter` | Checker forbids direct `ChatDeliveryReadModel`, `ChatDeliveryEventProjector`, and `ChatDeliveryActionService` ownership in controller | 7.6 / 7.7 | burned-down |
| `ChatUiController` runtime event pump | `ChatPageRuntimeEventPump` / `ChatPageRuntimeFacade` | None expected; app facade registers runtime facade instead of controller | Checker forbids `ChatUiController::onChatEvent(...)`, `processIncoming()`, `flushStore()`, and key verification source projection calls in controller | 7.7 | burned-down |
| `ChatUiController` Team rich payload formatting | `TeamRichPayloadProjector` / `TeamChatPresentationSource` | None in `ChatUiController`; Team chat rows consume projected summaries from `team_chat_model_.snapshot()` | Checker forbids `format_team_chat_entry(...)`, `decodeTeamChatLocation(...)`, `decodeTeamChatCommand(...)`, `TeamChatLocation`, and `TeamChatCommand` in controller | 7.8 | burned-down |
| `ChatUiController` Team position picker renderer | `TeamPositionPickerRenderer` | `ChatUiController` calls open/close/updateHint and handles selected/cancel workflow only | UX pack-specific picker replaces shared LVGL renderer or renderer is renamed as official chat picker view | 7.9 | burned-down |
| `TeamPage` create-team command handler | `TeamPageCreateTeamAction` / `TeamPageRuntimePort` / `TeamPageKeyEventLog` | `team_page_components.cpp` adapts generated random bytes, page state, failure notifications, save, and navigation only | Checker requires action seam and forbids create-team runtime/key-event orchestration from returning to controller helpers | Team legacy burn-down | burned-down |
| `TeamPage` pairing command handlers | `TeamPagePairingCommandAction` / `TeamPageRuntimePort` / `TeamPageCommandReducer` | `team_page_components.cpp` passes requested role and translates failure notifications only | Checker requires pairing action seam and forbids role-specific runtime start orchestration in controller helpers | Team legacy burn-down | burned-down |
| `TeamPage` pairing/keydist/status event side effects | `TeamPageEventEffectSink` / runtime/key-log/deferred/notifier seams | `team_page_components.cpp` reduces accepted events and applies returned page/navigation requests | Checker requires event-effect sink and forbids event effect handling from re-owning reducer mutation or LVGL/rendering | Team legacy burn-down | burned-down |
| `TeamPage` request-keys handler | `TeamPageRequestKeysAction` / `TeamPageKeyRequestAction` / Team KeyRequest wire protocol | `team_page_components.cpp` adapts current state and routes outgoing/incoming key requests through action seams | Key refresh request stays outside LVGL handlers and leader-side KeyDist response remains owned by `TeamPageKeyRequestAction` | Team legacy burn-down | burned-down |
| `TeamPage` navigation flow helpers | `TeamPageFlowController` | `team_page_components.cpp` adapts page-local controller context, calls render/transition, and exits to menu | Checker requires flow controller and forbids local nav-stack/pairing-active logic from returning | Team legacy burn-down | burned-down |
| `TeamPage` LVGL render helpers | `TeamPageLvglRenderer` | `team_page_components.cpp` builds renderer input/context/handlers only | Checker forbids page render helper functions, label/button/list helpers, and member chip rendering in the controller | Team legacy burn-down | burned-down |
| `TeamPage` kick-confirm command handler | `TeamPageKickConfirmAction` / `TeamPageRuntimePort` / `TeamPageDeferredDispatchQueue` | `team_page_components.cpp` adapts LVGL/page state, random source, deferred enqueue, notifications, save, and navigation only | Checker forbids Kick/KeyDist construction, direct `reduceKickConfirmed`, local key rotation, and immediate status send orchestration in `handle_kick_confirm` | Team legacy burn-down | burned-down |
| `Hostlink` Team shadow state | `ITeamUiSnapshotStore` / `ui::team_presence` / Team shell event processing | Hostlink still forwards Team wire payloads as `EvAppData`; `EvTeamState` is emitted after Team shell saves canonical snapshot | Checker forbids `s_runtime_team_state`, local member touch/update helpers, and liveness from `member.online` in Hostlink bridge | Team legacy burn-down | burned-down |
| `GPS` Team overlay Team Page state read | `TeamMapOverlaySource` over `ITeamUiSnapshotStore` and Team posring | ESP GPS map overlay still owns page-local marker widgets, but selected-member position data comes from the Team map source seam | Checker forbids `team_state.h`, `g_team_state`, and direct `team_ui_posring_load_latest` reads in GPS/map/dashboard consumers | Team legacy burn-down | burned-down |
| `Contacts` Team Page state read | `ContactsTeamSnapshotSource` over `ITeamUiSnapshotStore` | Contacts composes Team chat/action sinks and reads Team display/id through a Contacts-local projection only | Checker forbids `team_state.h` and `g_team_state` in Contacts page code | Team legacy burn-down | burned-down |
| `Team rich message row structured payload` | `TeamRichPayloadDisplay` / `TeamMessageRichPayload` / Team row renderer | `TeamChatPresentationSource` maps projected rich payloads into structured `MessageRow` fields and renderers consume those fields with text fallback | Checker requires `has_team_rich_payload` / `team_rich_payload` and forbids controller-side Team rich decode/formatting | Team legacy burn-down | burned-down |
| `Map tile path/cache legacy runtime` | `MapTileResolver` / `FilesystemMapTileSource` / map tile cache owner | Platform LVGL map tile runtimes call source/resolver; ESP decoded cache, Linux downloader cache, and uConsole path fields remain contained | Renderer consumes tile refs/source without direct path/cache ownership, and decoded/downloader caches are moved behind stable runtime adapters | 7.10 / 7.x | contained |
| `Map tile visible plan in platform renderer` | `MapTileRenderQueue` | Platform map tile runtimes still populate queue from legacy `MapTile` records | Dedicated map runtime computes visible queue and renderer consumes queue rows without mutating plan state | 7.11 / 7.x | contained |
| `ESP decoded LVGL tile cache` | `LvglDecodedTileCache` / `IMapTileDecoderCache` | ESP map tile runtime still asks the LVGL cache adapter for decoded handles | Runtime-owned decoder cache is injected into a dedicated map tile renderer | 7.11 / 7.x | contained |
| `Map overlay current/team marker projection` | `MapOverlaySnapshotSource` / `MapOverlayProjectionAdapter` / `MapOverlayProjector` in `modules/ui_map_runtime` | no main runtime callers of `LegacyMapOverlaySource`; alias headers and alias tests removed from build include surface | keep runtime map overlay headers as the only build-visible API | 9.5 | retired from build include surface |
| `Map route/tracker overlay projection` | deferred route/tracker presentation source | ESP route/tracker draw callbacks still own route/tracker widget drawing | Route/tracker stores expose presentation sources and renderer consumes overlay snapshot rows | 7.12 / 7.x | contained |
| `GPS page refresh cadence` | `GpsPageRuntimePump` / `IGpsUiRefreshSink` | ESP and Linux GPS page timers tick `GpsPageRuntimePump`; page-local adapters preserve legacy refresh behavior | Page-local adapters are replaced by GPS presentation refresh models and renderers consume snapshots only | 7.13 / 7.x | contained |

## Phase 9 Runtime Adoption Burn-down

| Legacy surface | New owner | Remaining callers | Removal condition | Target phase | Status |
| --- | --- | --- | --- | --- | --- |
| `legacy/app_implementations/linux_sim` ASCII descriptor adapters | `modules/ui_ascii_runtime` | moved out of legacy; final LinuxSim app consumes module-owned sources | real simulator entry consumes `AsciiRuntimeScreenGraphPresenter` and old hardcoded routing is deleted | 9.2 / LinuxSim-uConsole fallback burn-down | burned-down |
| `legacy/app_implementations/linux_uconsole` GTK descriptor adapters | `modules/ui_gtk_runtime` | moved out of legacy; final uConsole app consumes module-owned sources | real GTK page-registry path consumes `GtkUConsoleScreenGraphPresenter` and old hardcoded routing is deleted | 9.2 / LinuxSim-uConsole fallback burn-down | burned-down |
| Runtime entry adoption helpers under `legacy/app_implementations` | `modules/ui_ascii_runtime`, `modules/ui_gtk_runtime`, `modules/ui_lvgl_ux_packs`, final app-shell probes | none; Phase 9.2 keeps entry adoption helpers out of legacy | checker forbids `*RuntimeEntryAdoption` files and tokens under `legacy/app_implementations` | 9.2 | burned-down |
| Runtime entry bridges under `legacy/app_implementations` | final app-shell runtime entry/page-registry adoption and `modules/ui_lvgl_ux_packs` | none; Phase 9.3 keeps real entry adoption out of legacy | checker forbids Phase 9 runtime adoption bridge files under `legacy/app_implementations` | 9.3 | burned-down |
| Chat delivery legacy bridge pair | `modules/ui_chat_runtime` formal ports and adapters | no main runtime callers; alias build include surface removed | keep runtime headers as the only build-visible API | 9.4 | retired from build include surface |
| Key verification legacy source/sink/session | `modules/ui_key_verification_runtime` stable `KeyVerificationPresentationSource`, `KeyVerificationActionSink`, and `KeyVerificationSessionAdapter` | no main runtime callers; alias build include surface removed | keep runtime headers as the only build-visible API | 9.5 | retired from build include surface |
| Map overlay legacy source | `modules/ui_map_runtime` stable snapshot source and projection adapter | no main runtime callers; alias build include surface removed | keep runtime headers as the only build-visible API | 9.5 | retired from build include surface |

## Checker Status

| Surface | Checker rule |
| --- | --- |
| Team send payload encoding in controller | forbidden |
| Direct key verification runtime API in controller | forbidden |
| Direct delivery read/action ownership in controller | forbidden |
| Key verification modal helper | required |
| Runtime event pump in controller | forbidden |
| Team rich payload formatting in controller | forbidden |
| Team position picker widget refs in controller | forbidden |
| Team Page create-team orchestration in controller | forbidden |
| Team Page pairing command orchestration in controller | forbidden |
| Team Page event-effect ownership in controller | forbidden |
| Team Page flow/navigation ownership in controller | forbidden |
| Team Page LVGL render helper ownership in controller | forbidden |
| Team Page kick-confirm orchestration in controller | forbidden |
| Hostlink Team shadow state | forbidden |
| GPS Team overlay `g_team_state` read | forbidden |
| Contacts Team Page `g_team_state` read | forbidden |
| Map tile path policy in viewport/renderer | forbidden |
| Map tile render queue boundary | required |
| ESP decoded tile cache owner | required |
| Map overlay source boundary | required |
| GPS runtime scheduling pump | required |
| Legacy bridges without removal condition | forbidden by register token check |
| Key verification stable adapter names | required by Phase 9.5 checker |
| Map overlay stable adapter names | required by Phase 9.5 checker |

## Phase 9.6 Final Readiness Alignment

Phase 9.6 does not add new legacy surfaces. It aligns the register with the
Phase 9 final readiness report:

- Chat delivery legacy bridge pair: main runtime callers removed; alias build
  include surface removed; stable owner is `ChatDeliveryActionPortAdapter` and
  `ChatDeliveryEventProjectionAdapter`.
- Key verification legacy source/sink/session: main runtime callers removed;
  alias build include surface removed; stable owner is `KeyVerificationPresentationSource`,
  `KeyVerificationActionSink`, and `KeyVerificationSessionAdapter`.
- Map overlay legacy source: main runtime callers removed; alias build include
  surface removed; stable owner is `MapOverlaySnapshotSource` and
  `MapOverlayProjectionAdapter`.

LinuxSim, GTK, and LVGL hardcoded UI fallbacks have been burned down. LVGL is
still pending real widget/menu migration, but failed descriptor adoption no
longer selects a second hardcoded UI source.
