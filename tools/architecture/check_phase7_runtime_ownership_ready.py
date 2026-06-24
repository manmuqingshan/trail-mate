#!/usr/bin/env python3
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[2]

PATH_ALIASES = {
    "modules/ui_shared/src/ui/presentation_sources/legacy_chat_delivery_action_bridge.cpp":
        "modules/ui_chat_runtime/src/chat_delivery_action_port_adapter.cpp",
    "modules/ui_shared/tests/test_legacy_chat_delivery_action_bridge.cpp":
        "modules/ui_chat_runtime/tests/test_chat_delivery_action_port_adapter.cpp",
    "modules/ui_shared/src/ui/presentation_sources/legacy_chat_delivery_event_bridge.cpp":
        "modules/ui_chat_runtime/src/chat_delivery_event_projection_adapter.cpp",
    "modules/ui_shared/tests/test_legacy_chat_delivery_event_bridge.cpp":
        "modules/ui_chat_runtime/tests/test_chat_delivery_event_projection_adapter.cpp",
    "modules/ui_shared/src/ui/presentation_sources/legacy_key_verification_source.cpp":
        "modules/ui_key_verification_runtime/src/key_verification_presentation_source.cpp",
    "modules/ui_shared/src/ui/presentation_sources/legacy_key_verification_action_sink.cpp":
        "modules/ui_key_verification_runtime/src/key_verification_action_sink.cpp",
    "modules/ui_shared/tests/test_legacy_key_verification_adapters.cpp":
        "modules/ui_key_verification_runtime/tests/test_key_verification_runtime_adapters.cpp",
    "modules/ui_shared/src/ui/presentation_sources/legacy_map_overlay_source.cpp":
        "modules/ui_map_runtime/src/map_overlay_snapshot_source.cpp",
    "modules/ui_shared/tests/test_legacy_map_overlay_source.cpp":
        "modules/ui_map_runtime/tests/test_map_overlay_snapshot_source.cpp",
    "modules/ui_shared/include/ui/screens/chat/key_verification_modal_renderer.h":
        "modules/ui_lvgl_ux_packs/include/ui_lvgl_ux_packs/common/key_verification_modal_renderer.h",
    "modules/ui_shared/src/ui/screens/chat/key_verification_modal_renderer.cpp":
        "modules/ui_lvgl_ux_packs/src/common/key_verification_modal_renderer.cpp",
    "modules/ui_shared/include/ui/screens/chat/team_position_picker_renderer.h":
        "modules/ui_lvgl_ux_packs/include/ui_lvgl_ux_packs/common/team_position_picker_renderer.h",
    "modules/ui_shared/src/ui/screens/chat/team_position_picker_renderer.cpp":
        "modules/ui_lvgl_ux_packs/src/common/team_position_picker_renderer.cpp",
    "modules/ui_shared/include/ui/map_tiles/map_tile_types.h":
        "modules/ui_map_runtime/include/ui_map_runtime/map_tiles/map_tile_types.h",
    "modules/ui_shared/include/ui/map_tiles/map_tile_resolver.h":
        "modules/ui_map_runtime/include/ui_map_runtime/map_tiles/map_tile_resolver.h",
    "modules/ui_shared/include/ui/map_tiles/map_tile_source.h":
        "modules/ui_map_runtime/include/ui_map_runtime/map_tiles/map_tile_source.h",
    "modules/ui_shared/include/ui/map_tiles/map_tile_cache.h":
        "modules/ui_map_runtime/include/ui_map_runtime/map_tiles/map_tile_cache.h",
    "modules/ui_shared/include/ui/map_tiles/map_tile_decoder_cache.h":
        "modules/ui_map_runtime/include/ui_map_runtime/map_tiles/map_tile_decoder_cache.h",
    "modules/ui_shared/include/ui/map_tiles/map_tile_render_queue.h":
        "modules/ui_map_runtime/include/ui_map_runtime/map_tiles/map_tile_render_queue.h",
    "modules/ui_shared/src/ui/map_tiles/map_tile_render_queue.cpp":
        "modules/ui_map_runtime/src/map_tiles/map_tile_render_queue.cpp",
    "modules/ui_shared/src/ui/map_tiles/map_tile_resolver.cpp":
        "modules/ui_map_runtime/src/map_tiles/map_tile_resolver.cpp",
    "modules/ui_shared/tests/test_map_tile_render_queue.cpp":
        "modules/ui_map_runtime/tests/test_map_tile_render_queue.cpp",
    "modules/ui_shared/tests/test_map_tile_resolver.cpp":
        "modules/ui_map_runtime/tests/test_map_tile_resolver.cpp",
    "modules/ui_shared/include/ui/map_overlay/map_overlay_projector.h":
        "modules/ui_map_runtime/include/ui_map_runtime/map_overlay/map_overlay_projector.h",
    "modules/ui_shared/src/ui/map_overlay/map_overlay_projector.cpp":
        "modules/ui_map_runtime/src/map_overlay/map_overlay_projector.cpp",
    "modules/ui_shared/tests/test_map_overlay_projector.cpp":
        "modules/ui_map_runtime/tests/test_map_overlay_projector.cpp",
    "modules/ui_shared/include/ui/screens/gps/gps_ui_refresh_sink.h":
        "modules/ui_gps_runtime/include/ui_gps_runtime/gps_ui_refresh_sink.h",
    "modules/ui_shared/include/ui/screens/gps/gps_page_runtime_pump.h":
        "modules/ui_gps_runtime/include/ui_gps_runtime/gps_page_runtime_pump.h",
    "modules/ui_shared/src/ui/screens/gps/gps_page_runtime_pump.cpp":
        "modules/ui_gps_runtime/src/gps_page_runtime_pump.cpp",
    "modules/ui_shared/tests/test_gps_page_runtime_pump.cpp":
        "modules/ui_gps_runtime/tests/test_gps_page_runtime_pump.cpp",
    "modules/ui_shared/include/ui/screens/chat/chat_ui_refresh_sink.h":
        "modules/ui_chat_runtime/include/ui_chat_runtime/chat_ui_refresh_sink.h",
    "modules/ui_shared/include/ui/screens/chat/chat_page_runtime_event_pump.h":
        "modules/ui_chat_runtime/include/ui_chat_runtime/chat_page_runtime_event_pump.h",
    "modules/ui_shared/src/ui/screens/chat/chat_page_runtime_event_pump.cpp":
        "modules/ui_chat_runtime/src/chat_page_runtime_event_pump.cpp",
}


def resolve_path(path: str) -> Path:
    direct = ROOT / path
    if direct.exists() and path not in PATH_ALIASES:
        return direct
    alias = PATH_ALIASES.get(path)
    if alias:
        return ROOT / alias
    return direct


def exists(path: str) -> bool:
    return resolve_path(path).exists()


def read_text(path: str) -> str:
    return resolve_path(path).read_text(encoding="utf-8", errors="ignore")


def strip_cpp_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    return re.sub(r"//.*", "", text)


def fail(message: str) -> int:
    print(f"[phase7-runtime-ready] FAIL: {message}")
    return 1


def iter_code_files(*roots: Path):
    for root in roots:
        if not root.exists():
            continue
        for path in root.rglob("*"):
            if path.is_file() and path.suffix in {".h", ".hpp", ".cpp", ".cc", ".cxx"}:
                yield path


def check_required_files() -> int:
    required = [
        "docs/audits/CHAT_DELIVERY_OWNERSHIP_AUDIT.md",
        "docs/audits/CHAT_DELIVERY_EVENT_PROJECTION_AUDIT.md",
        "docs/audits/CHAT_DELIVERY_ACTION_OWNERSHIP_AUDIT.md",
        "docs/audits/KEY_VERIFICATION_OWNERSHIP_AUDIT.md",
        "docs/audits/CHAT_RUNTIME_EVENT_PUMP_AUDIT.md",
        "docs/audits/TEAM_RICH_PAYLOAD_PRESENTATION_AUDIT.md",
        "docs/audits/TEAM_POSITION_PICKER_RENDERER_AUDIT.md",
        "docs/audits/LEGACY_BURNDOWN_REGISTER.md",
        "docs/audits/CHAT_UI_CONTROLLER_BURNDOWN_AUDIT.md",
        "docs/audits/PHASE7_6_LEGACY_BURNDOWN_REPORT.md",
        "docs/audits/PHASE7_7_EVENT_PUMP_BURNDOWN_REPORT.md",
        "docs/audits/PHASE7_8_TEAM_RICH_PAYLOAD_BURNDOWN_REPORT.md",
        "docs/audits/PHASE7_9_TEAM_POSITION_PICKER_BURNDOWN_REPORT.md",
        "docs/audits/MAP_TILE_SOURCE_CACHE_OWNERSHIP_AUDIT.md",
        "docs/audits/PHASE7_10_MAP_TILE_SOURCE_CACHE_BURNDOWN_REPORT.md",
        "docs/audits/MAP_TILE_RENDER_QUEUE_CACHE_AUDIT.md",
        "docs/audits/PHASE7_11_MAP_TILE_RENDER_QUEUE_CACHE_REPORT.md",
        "docs/audits/MAP_OVERLAY_ROUTE_TRACKER_OWNERSHIP_AUDIT.md",
        "docs/audits/PHASE7_12_MAP_OVERLAY_ROUTE_TRACKER_REPORT.md",
        "docs/audits/GPS_RUNTIME_SCHEDULING_OWNERSHIP_AUDIT.md",
        "docs/audits/PHASE7_13_GPS_RUNTIME_SCHEDULING_REPORT.md",
        "docs/audits/PHASE7_FINAL_RUNTIME_OWNERSHIP_READINESS_REPORT.md",
        "docs/audits/PHASE7_FINAL_LEGACY_SURFACE_MATRIX.md",
        "docs/specification/CHAT_DELIVERY_RUNTIME_SPEC.md",
        "docs/specification/CHAT_DELIVERY_ACTION_RUNTIME_SPEC.md",
        "docs/specification/KEY_VERIFICATION_RUNTIME_SPEC.md",
        "docs/specification/CHAT_RUNTIME_EVENT_PUMP_SPEC.md",
        "docs/specification/TEAM_RICH_PAYLOAD_PRESENTATION_SPEC.md",
        "docs/specification/TEAM_POSITION_PICKER_RENDERER_SPEC.md",
        "docs/specification/MAP_TILE_SOURCE_CACHE_RUNTIME_SPEC.md",
        "docs/specification/MAP_TILE_RENDER_QUEUE_CACHE_SPEC.md",
        "docs/specification/MAP_OVERLAY_ROUTE_TRACKER_RUNTIME_SPEC.md",
        "docs/specification/GPS_RUNTIME_SCHEDULING_SPEC.md",
        "docs/audits/TEAM_ACTION_OWNERSHIP_AUDIT.md",
        "docs/specification/TEAM_ACTION_RUNTIME_SPEC.md",
        "docs/audits/PHASE7_RUNTIME_OWNERSHIP_REGISTER.md",
        "modules/core_chat/include/chat/delivery/chat_delivery_types.h",
        "modules/core_chat/include/chat/delivery/chat_delivery_read_model.h",
        "modules/core_chat/include/chat/delivery/chat_delivery_event_projector.h",
        "modules/core_chat/include/chat/delivery/chat_delivery_event_port.h",
        "modules/core_chat/include/chat/delivery/chat_delivery_action_types.h",
        "modules/core_chat/include/chat/delivery/chat_delivery_action_sink.h",
        "modules/core_chat/include/chat/delivery/chat_delivery_action_service.h",
        "modules/core_chat/include/chat/delivery/chat_delivery_message_projection.h",
        "modules/core_chat/include/chat/delivery/chat_delivery_send_result_projection.h",
        "modules/core_chat/src/delivery/chat_delivery_read_model.cpp",
        "modules/core_chat/src/delivery/chat_delivery_event_projector.cpp",
        "modules/core_chat/src/delivery/chat_delivery_event_port.cpp",
        "modules/core_chat/src/delivery/chat_delivery_action_service.cpp",
        "modules/core_chat/src/delivery/chat_delivery_message_projection.cpp",
        "modules/core_chat/src/delivery/chat_delivery_send_result_projection.cpp",
        "modules/core_chat/tests/test_chat_delivery_read_model.cpp",
        "modules/core_chat/tests/test_chat_delivery_event_projector.cpp",
        "modules/core_chat/tests/test_chat_delivery_event_port.cpp",
        "modules/core_chat/tests/test_chat_delivery_action_service.cpp",
        "modules/core_chat/tests/test_chat_delivery_message_projection.cpp",
        "modules/core_chat/tests/test_chat_delivery_send_result_projection.cpp",
        "modules/ui_shared/src/ui/presentation_sources/legacy_chat_delivery_action_bridge.cpp",
        "modules/ui_shared/tests/test_legacy_chat_delivery_action_bridge.cpp",
        "modules/ui_shared/include/ui/team_actions/team_action_types.h",
        "modules/ui_shared/include/ui/team_actions/team_action_sink.h",
        "modules/ui_shared/include/ui/team_actions/team_action_runtime_sink.h",
        "modules/ui_shared/src/ui/team_actions/team_action_runtime_sink.cpp",
        "modules/ui_shared/include/ui/team_actions/team_runtime_adapters.h",
        "modules/ui_shared/src/ui/team_actions/team_runtime_adapters.cpp",
        "modules/ui_shared/tests/test_team_action_types.cpp",
        "modules/ui_shared/tests/test_team_action_runtime_sink.cpp",
        "modules/ui_shared/include/ui/team_presentation/team_rich_payload_display.h",
        "modules/ui_shared/include/ui/team_presentation/team_rich_payload_projector.h",
        "modules/ui_shared/src/ui/team_presentation/team_rich_payload_projector.cpp",
        "modules/ui_shared/tests/test_team_rich_payload_projector.cpp",
        "modules/ui_shared/include/ui/presentation_sources/team_map_overlay_source.h",
        "modules/ui_shared/src/ui/presentation_sources/team_map_overlay_source.cpp",
        "modules/ui_shared/tests/test_team_map_overlay_source.cpp",
        "modules/ui_presentation/include/ui_presentation/key_verification/key_verification_snapshot.h",
        "modules/ui_presentation/include/ui_presentation/key_verification/key_verification_source.h",
        "modules/ui_presentation/include/ui_presentation/key_verification/key_verification_action_sink.h",
        "modules/ui_presentation/include/ui_presentation/key_verification/key_verification_model.h",
        "modules/ui_presentation/src/key_verification/key_verification_model.cpp",
        "modules/ui_presentation/tests/test_key_verification_model.cpp",
        "modules/ui_shared/src/ui/presentation_sources/legacy_key_verification_source.cpp",
        "modules/ui_shared/src/ui/presentation_sources/legacy_key_verification_action_sink.cpp",
        "modules/ui_shared/tests/test_legacy_key_verification_adapters.cpp",
        "modules/ui_shared/include/ui/screens/chat/key_verification_modal_renderer.h",
        "modules/ui_shared/src/ui/screens/chat/key_verification_modal_renderer.cpp",
        "modules/ui_shared/include/ui/screens/chat/team_position_picker_renderer.h",
        "modules/ui_shared/src/ui/screens/chat/team_position_picker_renderer.cpp",
        "modules/ui_shared/include/ui/map_tiles/map_tile_types.h",
        "modules/ui_shared/include/ui/map_tiles/map_tile_resolver.h",
        "modules/ui_shared/include/ui/map_tiles/map_tile_source.h",
        "modules/ui_shared/include/ui/map_tiles/map_tile_cache.h",
        "modules/ui_shared/include/ui/map_tiles/map_tile_decoder_cache.h",
        "modules/ui_shared/include/ui/map_tiles/map_tile_render_queue.h",
        "modules/ui_shared/src/ui/map_tiles/map_tile_render_queue.cpp",
        "modules/ui_shared/src/ui/map_tiles/map_tile_resolver.cpp",
        "modules/ui_shared/tests/test_map_tile_render_queue.cpp",
        "modules/ui_shared/tests/test_map_tile_resolver.cpp",
        "modules/ui_presentation/include/ui_presentation/map/map_overlay_snapshot.h",
        "modules/ui_presentation/include/ui_presentation/map/map_overlay_source.h",
        "modules/ui_shared/include/ui/map_overlay/map_overlay_projector.h",
        "modules/ui_shared/src/ui/map_overlay/map_overlay_projector.cpp",
        "modules/ui_shared/src/ui/presentation_sources/legacy_map_overlay_source.cpp",
        "modules/ui_shared/tests/test_map_overlay_projector.cpp",
        "modules/ui_shared/tests/test_legacy_map_overlay_source.cpp",
        "modules/ui_shared/include/ui/screens/gps/gps_ui_refresh_sink.h",
        "modules/ui_shared/include/ui/screens/gps/gps_page_runtime_pump.h",
        "modules/ui_shared/src/ui/screens/gps/gps_page_runtime_pump.cpp",
        "modules/ui_shared/tests/test_gps_page_runtime_pump.cpp",
        "modules/ui_shared/include/ui/screens/chat/chat_ui_refresh_sink.h",
        "modules/ui_shared/include/ui/screens/chat/chat_page_runtime_event_pump.h",
        "modules/ui_shared/src/ui/screens/chat/chat_page_runtime_event_pump.cpp",
        "modules/ui_shared/include/ui/screens/chat/chat_team_workflow.h",
        "modules/ui_shared/src/ui/screens/chat/chat_team_workflow.cpp",
        "modules/ui_shared/tests/test_chat_team_workflow.cpp",
        "modules/ui_shared/include/ui/screens/team/team_page_read_model.h",
        "modules/ui_shared/src/ui/screens/team/team_page_read_model.cpp",
        "modules/ui_shared/tests/test_team_page_read_model.cpp",
        "modules/ui_shared/include/ui/screens/team/team_page_types.h",
        "modules/ui_shared/include/ui/screens/team/team_page_flow_controller.h",
        "modules/ui_shared/src/ui/screens/team/team_page_flow_controller.cpp",
        "modules/ui_shared/tests/test_team_page_flow_controller.cpp",
        "modules/ui_shared/include/ui/screens/team/team_page_lvgl_renderer.h",
        "modules/ui_shared/src/ui/screens/team/team_page_lvgl_renderer.cpp",
        "modules/ui_shared/include/ui/screens/team/team_page_command_reducer.h",
        "modules/ui_shared/src/ui/screens/team/team_page_command_reducer.cpp",
        "modules/ui_shared/tests/test_team_page_command_reducer.cpp",
        "modules/ui_shared/include/ui/screens/team/team_page_create_team_action.h",
        "modules/ui_shared/src/ui/screens/team/team_page_create_team_action.cpp",
        "modules/ui_shared/tests/test_team_page_create_team_action.cpp",
        "modules/ui_shared/include/ui/screens/team/team_page_pairing_command_action.h",
        "modules/ui_shared/src/ui/screens/team/team_page_pairing_command_action.cpp",
        "modules/ui_shared/tests/test_team_page_pairing_command_action.cpp",
        "modules/ui_shared/include/ui/screens/team/team_page_request_keys_action.h",
        "modules/ui_shared/src/ui/screens/team/team_page_request_keys_action.cpp",
        "modules/ui_shared/tests/test_team_page_request_keys_action.cpp",
        "modules/ui_shared/include/ui/screens/team/team_page_event_reducer.h",
        "modules/ui_shared/src/ui/screens/team/team_page_event_reducer.cpp",
        "modules/ui_shared/tests/test_team_page_event_reducer.cpp",
        "modules/ui_shared/include/ui/screens/team/team_page_event_effect_sink.h",
        "modules/ui_shared/src/ui/screens/team/team_page_event_effect_sink.cpp",
        "modules/ui_shared/tests/test_team_page_event_effect_sink.cpp",
        "modules/ui_shared/include/ui/screens/team/team_page_key_event_log.h",
        "modules/ui_shared/src/ui/screens/team/team_page_key_event_log.cpp",
        "modules/ui_shared/tests/test_team_page_key_event_log.cpp",
        "modules/ui_shared/include/ui/screens/team/team_page_runtime_port.h",
        "modules/ui_shared/src/ui/screens/team/team_page_runtime_adapters.cpp",
        "modules/ui_shared/src/ui/screens/team/team_page_runtime_port.cpp",
        "modules/ui_shared/tests/test_team_page_runtime_port.cpp",
        "modules/ui_shared/include/ui/screens/team/team_page_activity_sink.h",
        "modules/ui_shared/src/ui/screens/team/team_page_activity_adapters.cpp",
        "modules/ui_shared/src/ui/screens/team/team_page_activity_sink.cpp",
        "modules/ui_shared/tests/test_team_page_activity_sink.cpp",
        "modules/ui_shared/include/ui/screens/team/team_page_deferred_dispatch.h",
        "modules/ui_shared/src/ui/screens/team/team_page_deferred_dispatch.cpp",
        "modules/ui_shared/src/ui/screens/team/team_page_deferred_dispatch_adapters.cpp",
        "modules/ui_shared/tests/test_team_page_deferred_dispatch.cpp",
        "modules/ui_shared/include/ui/screens/team/team_page_kick_confirm_action.h",
        "modules/ui_shared/src/ui/screens/team/team_page_kick_confirm_action.cpp",
        "modules/ui_shared/tests/test_team_page_kick_confirm_action.cpp",
        "modules/ui_shared/include/ui/screens/team/team_page_transfer_leader_action.h",
        "modules/ui_shared/src/ui/screens/team/team_page_transfer_leader_action.cpp",
        "modules/ui_shared/tests/test_team_page_transfer_leader_action.cpp",
        "modules/ui_shared/include/ui/screens/team/team_page_state_store.h",
        "modules/ui_shared/src/ui/screens/team/team_page_state_store.cpp",
        "modules/ui_shared/tests/test_team_page_state_store.cpp",
        "tools/architecture/check_phase7_runtime_ownership_ready.py",
    ]

    failures = 0
    for path in required:
        if not exists(path):
            failures += fail(f"missing required file: {path}")
    return failures


def check_removed_chat_read_projection_not_reintroduced() -> int:
    legacy_type = "Legacy" + "ChatPresentationSource"
    legacy_file_stem = "legacy_chat" + "_presentation_source"
    removed_paths = [
        "modules/ui_shared/include/ui/presentation_sources/" + legacy_file_stem + ".h",
        "modules/ui_shared/src/ui/presentation_sources/" + legacy_file_stem + ".cpp",
        "modules/ui_shared/tests/test_" + legacy_file_stem + ".cpp",
    ]

    failures = 0
    for path in removed_paths:
        if exists(path):
            failures += fail(f"removed chat presentation source path still exists: {path}")

    search_roots = [
        ROOT / "apps",
        ROOT / "cmake",
        ROOT / "docs",
        ROOT / "modules",
        ROOT / "tools" / "architecture",
    ]
    text_suffixes = {
        ".c",
        ".cc",
        ".cpp",
        ".cxx",
        ".h",
        ".hpp",
        ".cmake",
        ".ini",
        ".json",
        ".md",
        ".py",
        ".txt",
    }
    this_file = Path(__file__).resolve()
    for root in search_roots:
        if not root.exists():
            continue
        for path in root.rglob("*"):
            if not path.is_file() or path.resolve() == this_file:
                continue
            if path.suffix not in text_suffixes and path.name != "CMakeLists.txt":
                continue
            text = path.read_text(encoding="utf-8", errors="ignore")
            if legacy_type in text:
                failures += fail(
                    f"{path.relative_to(ROOT)} references removed {legacy_type}"
                )
            if legacy_file_stem in text:
                failures += fail(
                    f"{path.relative_to(ROOT)} references removed {legacy_file_stem}"
                )
    return failures


def check_docs() -> int:
    docs = {
        "docs/audits/CHAT_DELIVERY_OWNERSHIP_AUDIT.md": [
            "`ChatWorkspaceModel` must not own pending/failure",
            "Renderer must not infer delivery/failure",
            "ChatDeliveryReadModel / ChatDeliveryStateStore",
            "dedicated delivery read model",
        ],
        "docs/specification/CHAT_DELIVERY_RUNTIME_SPEC.md": [
            "Delivery state is runtime state, not UI state",
            "ChatDeliveryEventProjector",
            "ChatDeliveryReadModel",
            "IChatDeliveryEventPort",
            "ProjectingChatDeliveryEventPort",
            "ChatDeliveryEventProjectionAdapter",
            "ChatPresentationSource",
            "Phase 7.1 does not make `ChatWorkspaceModel` own delivery state",
        ],
        "docs/audits/CHAT_DELIVERY_ACTION_OWNERSHIP_AUDIT.md": [
            "retry",
            "cancel pending",
            "clear failure",
            "ChatDeliveryActionRequest",
            "ChatDeliveryActionService",
            "Renderers and controllers must not directly clear delivery records",
        ],
        "docs/specification/CHAT_DELIVERY_ACTION_RUNTIME_SPEC.md": [
            "Delivery actions belong to runtime/action sinks",
            "ChatDeliveryActionRequest",
            "IChatDeliveryActionSink",
            "IRetryChatMessagePort",
            "ChatDeliveryActionService",
            "LegacyChatDeliveryActionBridge",
            "`ChatWorkspaceModel` must not own retry, cancel, or clear-failure state",
        ],
        "docs/audits/CHAT_DELIVERY_EVENT_PROJECTION_AUDIT.md": [
            "ChatSendResultEvent",
            "`msg_id`",
            "`success`",
            "success=false",
            "AckTimeout",
            "ChatDeliveryEventProjectionAdapter",
            "ChatDeliverySendResultProjection",
        ],
        "docs/audits/PHASE7_RUNTIME_OWNERSHIP_REGISTER.md": [
            "Chat delivery / pending / failure",
            "runtime state",
            "not owned by",
            "composition-root ownership",
            "Team location/command action ownership",
            "TeamActionRuntimeSink",
            "Team rich payload display ownership",
            "TeamRichPayloadProjector",
            "Chat retry/cancel/clear failure actions",
            "ChatDeliveryActionService",
            "key verification workflow",
            "KeyVerificationModel",
            "Map tile/cache ownership",
            "MapTileResolver",
            "FilesystemMapTileSource",
        ],
        "docs/audits/KEY_VERIFICATION_OWNERSHIP_AUDIT.md": [
            "Key verification is a standalone runtime/presentation workflow",
            "`ChatWorkspaceModel`",
            "`MessageRow`",
            "renderer-local modal state",
            "`IMeshAdapter::submitKeyVerificationNumber(...)`",
            "`ContactService::setNodeKeyManuallyVerified(...)`",
            "LegacyKeyVerificationSource",
            "LegacyKeyVerificationActionSink",
        ],
        "docs/specification/KEY_VERIFICATION_RUNTIME_SPEC.md": [
            "Key verification is an independent runtime/presentation workflow",
            "KeyVerificationSnapshot",
            "IKeyVerificationPresentationSource",
            "IKeyVerificationActionSink",
            "KeyVerificationModel",
            "LegacyKeyVerificationSource",
            "LegacyKeyVerificationActionSink",
            "Phase 7.5 does not add key verification to `ChatWorkspaceModel` or",
        ],
        "docs/audits/TEAM_ACTION_OWNERSHIP_AUDIT.md": [
            "Team actions are command ownership",
            "Renderer/controller code may collect user input",
            "must not",
            "encode Team payloads",
            "TeamActionRuntimeSink",
        ],
        "docs/specification/TEAM_ACTION_RUNTIME_SPEC.md": [
            "Team action payload encoding belongs to Team action/runtime adapters",
            "TeamActionRequest",
            "ITeamActionSink",
            "ITeamLocationSource",
            "TeamActionRuntimeSink",
            "Phase 7.2 does not turn Team into DirectPeer or Channel chat",
        ],
        "docs/audits/CHAT_RUNTIME_EVENT_PUMP_AUDIT.md": [
            "Phase 7.7 moves Chat runtime scheduling and event projection out of `ChatUiController`",
            "ChatSendResultEvent",
            "ChatDeliveryEventProjectionAdapter",
            "LegacyKeyVerificationSource",
            "ChatService::processIncoming()",
            "ChatService::flushStore()",
            "IChatUiRefreshSink",
            "ChatPageRuntimeEventPump",
        ],
        "docs/specification/CHAT_RUNTIME_EVENT_PUMP_SPEC.md": [
            "Runtime event projection belongs to the event pump",
            "UI refresh belongs to the controller",
            "IChatUiRefreshSink",
            "ChatPageRuntimeEventPump",
            "ChatPageRuntimeFacade",
            "ChatSendResultEvent",
            "ChatService::processIncoming()",
            "ChatService::flushStore()",
        ],
        "docs/audits/TEAM_RICH_PAYLOAD_PRESENTATION_AUDIT.md": [
            "Team rich payload display is presentation projection",
            "TeamRichPayloadProjector",
            "TeamRichPayloadDisplay",
            "TeamChatPresentationSource",
            "ChatUiController",
            "decodeTeamChatLocation",
            "decodeTeamChatCommand",
            "The chat controller no longer formats or decodes Team location/command display payloads",
        ],
        "docs/audits/TEAM_POSITION_PICKER_RENDERER_AUDIT.md": [
            "Phase 7.9 burns down Team position picker widget lifecycle from `ChatUiController`",
            "renderer / widget lifecycle boundary",
            "TeamPositionPickerRenderer",
            "overlay",
            "panel",
            "description label",
            "focus group",
            "icon event contexts",
            "`ChatUiController` owns only",
            "`TeamPositionPickerRenderer` must not",
            "send Team actions",
        ],
        "docs/specification/TEAM_RICH_PAYLOAD_PRESENTATION_SPEC.md": [
            "Team rich payload display is presentation projection, not controller formatting",
            "TeamRichPayloadKind",
            "TeamCommandDisplayKind",
            "TeamRichPayloadDisplay",
            "TeamRichPayloadProjector",
            "TeamChatLogEntry -> TeamRichPayloadDisplay",
            "TeamChatPresentationSource",
            "TeamMessageRichPayload",
            "has_team_rich_payload",
        ],
        "docs/specification/TEAM_POSITION_PICKER_RENDERER_SPEC.md": [
            "Picker rendering belongs to `TeamPositionPickerRenderer`",
            "Team action sending belongs to `ITeamActionSink`",
            "TeamPositionPickerRenderer::Callbacks",
            "bool open()",
            "void close(bool restore_group)",
            "bool isOpen() const",
            "void updateHint(uint8_t icon_id)",
            "must not interpret the result as a Team send operation",
            "Phase 7.9 does not",
        ],
        "docs/audits/LEGACY_BURNDOWN_REGISTER.md": [
            "Remaining callers",
            "Removal condition",
            "Target phase",
            "Status",
            "LegacyChatDeliveryActionBridge",
            "LegacyKeyVerificationSource",
            "LegacyKeyVerificationActionSink",
            "ChatUiController` key verification modal rendering",
            "ChatUiController` Team payload/action send special case",
            "ChatUiController` delivery mutation",
            "ChatUiController` runtime event pump",
            "ChatUiController` Team rich payload formatting",
            "ChatUiController` Team position picker renderer",
            "Team rich message row structured payload",
            "Map tile path/cache legacy runtime",
            "Map tile visible plan in platform renderer",
            "ESP decoded LVGL tile cache",
        ],
        "docs/audits/CHAT_UI_CONTROLLER_BURNDOWN_AUDIT.md": [
            "Temporary UI Responsibilities",
            "Migrated Runtime Responsibilities",
            "Remaining Legacy Responsibilities",
            "Team location/command payload encoding",
            "Team rich payload display projection",
            "Key verification modal rendering",
            "ChatDeliveryReadModel",
            "ChatDeliveryEventProjector",
            "ChatDeliveryActionService",
            "TeamRichPayloadProjector",
            "TeamPositionPickerRenderer",
        ],
        "docs/audits/PHASE7_6_LEGACY_BURNDOWN_REPORT.md": [
            "Phase 7.6 reduced Chat / Team / key-verification legacy ownership surfaces",
            "Burned Down",
            "Still Contained",
            "Checker Changes",
            "Remaining Work",
        ],
        "docs/audits/PHASE7_7_EVENT_PUMP_BURNDOWN_REPORT.md": [
            "Phase 7.7 moved Chat runtime event projection and ChatService scheduling out of `ChatUiController`",
            "ChatPageRuntimeEventPump",
            "ChatPageRuntimeFacade",
            "ChatService::processIncoming()",
            "ChatService::flushStore()",
            "Burned Down",
            "Still Contained",
        ],
        "docs/audits/PHASE7_8_TEAM_RICH_PAYLOAD_BURNDOWN_REPORT.md": [
            "Phase 7.8 moved Team location/command display decoding out of `ChatUiController`",
            "TeamRichPayloadProjector",
            "TeamChatPresentationSource",
            "format_team_chat_entry",
            "decodeTeamChatLocation",
            "decodeTeamChatCommand",
            "Burned Down",
            "Still Contained",
        ],
        "docs/audits/PHASE7_9_TEAM_POSITION_PICKER_BURNDOWN_REPORT.md": [
            "Phase 7.9 moved Team position picker LVGL widget lifecycle out of `ChatUiController`",
            "TeamPositionPickerRenderer",
            "team_position_picker_overlay_",
            "team_position_picker_panel_",
            "team_position_picker_desc_",
            "team_position_picker_group_",
            "team_position_prev_group_",
            "team_position_icon_ctxs_",
            "TeamPositionIconEventCtx",
            "team_position_icon_event_cb",
            "team_position_cancel_event_cb",
            "Burned Down",
            "Still Contained",
        ],
        "docs/audits/MAP_TILE_SOURCE_CACHE_OWNERSHIP_AUDIT.md": [
            "Phase 7.10 establishes ownership for map tile source, path resolution, and tile availability lookup",
            "MapTileRef",
            "MapTileResolver",
            "FilesystemMapTileSource",
            "IMapTileSource::lookup(...)",
            "MapWorkspaceSnapshot",
            "/maps/base/osm/{z}/{x}/{y}.png",
            "/maps/contour/major-500/{z}/{x}/{y}.png",
            "ESP decoded LVGL tile cache",
            "Linux `MapTileCache` downloader",
        ],
        "docs/specification/MAP_TILE_SOURCE_CACHE_RUNTIME_SPEC.md": [
            "Tile lookup and cache ownership belong to map runtime/source adapters",
            "MapTileRef",
            "MapTileLayer",
            "MapTileFormat",
            "MapTilePayload",
            "IMapTileSource",
            "IMapTileCache",
            "MapTileResolver",
            "FilesystemMapTileSource",
            "Phase 7.10 does not",
        ],
        "docs/audits/PHASE7_10_MAP_TILE_SOURCE_CACHE_BURNDOWN_REPORT.md": [
            "Phase 7.10 moved Trail Mate map tile path mapping and filesystem-backed availability lookup",
            "MapTileResolver",
            "FilesystemMapTileSource",
            "MapTileRef",
            "Burned Down",
            "Still Contained",
            "ESP decoded LVGL tile cache",
            "Linux `platform::linux_runtime::MapTileCache` downloader",
            "uConsole workspace path fields",
        ],
        "docs/audits/MAP_TILE_RENDER_QUEUE_CACHE_AUDIT.md": [
            "Phase 7.11 establishes explicit ownership boundaries for map tile visible render planning and decoded tile cache lifetime",
            "MapTileRenderQueue",
            "MapTileRenderRef",
            "MapTileRenderState",
            "IMapTileDecoderCache",
            "LvglDecodedTileCache",
            "LVGL tile widget records",
            "Platform tile loader cadence",
        ],
        "docs/specification/MAP_TILE_RENDER_QUEUE_CACHE_SPEC.md": [
            "Source owns tile bytes",
            "Decoder cache owns decoded image lifetime",
            "Render queue owns the current visible tile plan",
            "MapTileScreenRect",
            "MapTileRenderState",
            "MapTileRenderRef",
            "MapTileRenderQueue",
            "IMapTileDecoderCache",
            "LvglDecodedTileCache",
            "Phase 7.11 does not",
        ],
        "docs/audits/PHASE7_11_MAP_TILE_RENDER_QUEUE_CACHE_REPORT.md": [
            "Phase 7.11 made the current map tile render plan and ESP decoded tile cache ownership explicit",
            "MapTileRenderQueue",
            "MapTileRenderState",
            "LvglDecodedTileCache",
            "IMapTileDecoderCache",
            "Burned Down",
            "Still Contained",
            "Platform tile loading cadence",
        ],
    }

    failures = 0
    for path, tokens in docs.items():
        if not exists(path):
            continue
        text = read_text(path)
        for token in tokens:
            if token not in text:
                failures += fail(f"{path} missing runtime ownership token: {token}")
    return failures


def check_legacy_burndown_register() -> int:
    failures = 0
    path = "docs/audits/LEGACY_BURNDOWN_REGISTER.md"
    if not exists(path):
        return failures

    text = read_text(path)
    header = (
        "| Legacy surface | New owner | Remaining callers | Removal condition | "
        "Target phase | Status |"
    )
    if header not in text:
        failures += fail("LEGACY_BURNDOWN_REGISTER.md missing required table header")

    required_surfaces = [
        "LegacyChatDeliveryActionBridge",
        "TeamActionRuntimeSink",
        "LegacyKeyVerificationSource",
        "LegacyKeyVerificationActionSink",
        "ChatUiController` key verification modal rendering",
        "ChatUiController` Team payload/action send special case",
        "ChatUiController` delivery mutation",
        "ChatUiController` runtime event pump",
        "ChatUiController` Team rich payload formatting",
        "ChatUiController` Team position picker renderer",
        "Team rich message row structured payload",
        "Map tile path/cache legacy runtime",
        "Map tile visible plan in platform renderer",
        "ESP decoded LVGL tile cache",
        "Map overlay current/team marker projection",
        "Map route/tracker overlay projection",
        "GPS page refresh cadence",
    ]
    for surface in required_surfaces:
        if surface not in text:
            failures += fail(f"LEGACY_BURNDOWN_REGISTER.md missing surface: {surface}")

    for line in text.splitlines():
        if not line.startswith("| `") or line.startswith("| ---"):
            continue
        cells = [cell.strip() for cell in line.strip().strip("|").split("|")]
        if len(cells) < 6:
            failures += fail(f"legacy burn-down row has too few cells: {line}")
            continue
        surface, _owner, callers, removal_condition, target_phase, status = cells[:6]
        if not callers or callers.lower() in {"none", "n/a"}:
            failures += fail(f"{surface} missing remaining caller/none rationale")
        if not removal_condition or removal_condition.lower() in {"none", "n/a"}:
            failures += fail(f"{surface} missing removal condition")
        if not target_phase or target_phase.lower() in {"none", "n/a"}:
            failures += fail(f"{surface} missing target phase")
        if status not in {
            "contained",
            "burned-down",
            "burned-down to deprecated alias",
            "retired from build include surface",
            "remaining legacy",
        }:
            failures += fail(f"{surface} has unsupported burn-down status: {status}")

    return failures


def check_core_delivery_is_runtime_only() -> int:
    forbidden_tokens = [
        "ui_presentation",
        "ui/",
        "lvgl.h",
        "gtk",
        "ChatWorkspaceModel",
        "MessageRow",
        "Renderer",
        "MeshSession",
        "IMeshAdapter",
        "RadioLib",
        "Arduino.h",
        "Preferences",
    ]

    failures = 0
    roots = [
        ROOT / "modules/core_chat/include/chat/delivery",
        ROOT / "modules/core_chat/src/delivery",
    ]
    for path in iter_code_files(*roots):
        text = strip_cpp_comments(path.read_text(encoding="utf-8", errors="ignore"))
        for token in forbidden_tokens:
            if token in text:
                failures += fail(
                    f"{path.relative_to(ROOT)} contains forbidden delivery token {token}"
                )
    return failures


def check_delivery_type_shape() -> int:
    failures = 0

    types = "modules/core_chat/include/chat/delivery/chat_delivery_types.h"
    if exists(types):
        text = read_text(types)
        for token in [
            "enum class DeliveryState",
            "Queued",
            "Sending",
            "Delivered",
            "enum class DeliveryFailureKind",
            "PeerKeyMissing",
            "LocalIdentityMissing",
            "RadioSendFailed",
            "AckTimeout",
            "ChatDeliveryRef",
            "ChatDeliveryRecord",
        ]:
            if token not in text:
                failures += fail(f"chat delivery types missing token: {token}")

    read_model = "modules/core_chat/include/chat/delivery/chat_delivery_read_model.h"
    if exists(read_model):
        text = read_text(read_model)
        for token in ["kMaxRecords", "upsert", "find", "clear", "clearAll"]:
            if token not in text:
                failures += fail(f"ChatDeliveryReadModel missing token: {token}")
        for token in ["std::vector", "sendText", "retry", "cancel"]:
            if token in text:
                failures += fail(f"ChatDeliveryReadModel owns behavior token: {token}")

    projector = (
        "modules/core_chat/include/chat/delivery/chat_delivery_event_projector.h"
    )
    if exists(projector):
        text = read_text(projector)
        for token in [
            "onQueued",
            "onSending",
            "onSent",
            "onDelivered",
            "onFailed",
            "onReceived",
            "SendFailureKind",
        ]:
            if token not in text:
                failures += fail(f"ChatDeliveryEventProjector missing token: {token}")

    event_port = "modules/core_chat/include/chat/delivery/chat_delivery_event_port.h"
    if exists(event_port):
        text = read_text(event_port)
        for token in [
            "ChatDeliveryEvent",
            "IChatDeliveryEventPort",
            "publishDeliveryEvent",
            "ProjectingChatDeliveryEventPort",
        ]:
            if token not in text:
                failures += fail(f"ChatDeliveryEventPort missing token: {token}")
        for token in ["ChatWorkspaceModel", "MessageRow", "lvgl.h", "sendText"]:
            if token in strip_cpp_comments(text):
                failures += fail(f"ChatDeliveryEventPort owns forbidden token: {token}")

    action_types = "modules/core_chat/include/chat/delivery/chat_delivery_action_types.h"
    if exists(action_types):
        text = read_text(action_types)
        for token in [
            "enum class ChatDeliveryActionKind",
            "Retry",
            "CancelPending",
            "ClearFailure",
            "ChatDeliveryActionRequest",
            "ChatDeliveryActionFailure",
            "InvalidRef",
            "Unsupported",
            "NotRetryable",
            "ChatDeliveryActionResult",
        ]:
            if token not in text:
                failures += fail(f"ChatDeliveryActionTypes missing token: {token}")
        for token in ["UiActionResult", "lvgl.h", "ChatWorkspaceModel", "MessageRow"]:
            if token in strip_cpp_comments(text):
                failures += fail(f"ChatDeliveryActionTypes owns forbidden token: {token}")

    action_sink = "modules/core_chat/include/chat/delivery/chat_delivery_action_sink.h"
    if exists(action_sink):
        text = read_text(action_sink)
        for token in ["IChatDeliveryActionSink", "handleDeliveryAction"]:
            if token not in text:
                failures += fail(f"ChatDeliveryActionSink missing token: {token}")

    action_service_header = (
        "modules/core_chat/include/chat/delivery/chat_delivery_action_service.h"
    )
    action_service_source = (
        "modules/core_chat/src/delivery/chat_delivery_action_service.cpp"
    )
    for path in [action_service_header, action_service_source]:
        if exists(path):
            text = strip_cpp_comments(read_text(path))
            for token in ["lvgl.h", "gtk", "ChatWorkspaceModel", "MessageRow", "ui_presentation", "UiActionResult"]:
                if token in text:
                    failures += fail(
                        f"{path} contains forbidden delivery action token: {token}"
                    )
    if exists(action_service_header):
        text = read_text(action_service_header)
        for token in [
            "IRetryChatMessagePort",
            "retryMessage",
            "ChatDeliveryActionService",
            "IChatDeliveryActionSink",
            "ChatDeliveryReadModel& read_model_",
        ]:
            if token not in text:
                failures += fail(f"ChatDeliveryActionService header missing token: {token}")
    if exists(action_service_source):
        text = read_text(action_service_source)
        for token in [
            "ChatDeliveryActionKind::Retry",
            "ChatDeliveryActionKind::CancelPending",
            "ChatDeliveryActionKind::ClearFailure",
            "read_model_.clear",
            "retry_port_->retryMessage",
            "DeliveryState::Queued",
            "DeliveryState::Sending",
            "DeliveryState::Failed",
        ]:
            if token not in text:
                failures += fail(f"ChatDeliveryActionService source missing token: {token}")

    send_result_projection = (
        "modules/core_chat/include/chat/delivery/chat_delivery_send_result_projection.h"
    )
    if exists(send_result_projection):
        text = read_text(send_result_projection)
        for token in [
            "makeChatSendResultDeliveryEvent",
            "makeAckTimeoutDeliveryEvent",
            "ChatDeliveryEvent",
            "SendFailureKind",
        ]:
            if token not in text:
                failures += fail(
                    f"ChatDeliverySendResultProjection missing token: {token}"
                )

    return failures


def check_presentation_source_enrichment() -> int:
    failures = 0
    header = "modules/ui_shared/include/ui/presentation_sources/chat_presentation_source.h"
    source = "modules/ui_shared/src/ui/presentation_sources/chat_presentation_source.cpp"

    if exists(header):
        text = read_text(header)
        for token in [
            "ChatDeliveryReadModel",
            "delivery_read_model = nullptr",
            "delivery_read_model_",
        ]:
            if token not in text:
                failures += fail(f"ChatPresentationSource header missing token: {token}")

    if exists(source):
        text = read_text(source)
        for token in [
            "delivery_read_model_->find",
            "toDeliveryRef(message)",
            "mapDeliveryState",
            "mapDeliveryFailure",
            "row.delivery",
            "row.failure",
        ]:
            if token not in text:
                failures += fail(f"ChatPresentationSource source missing token: {token}")
        for token in [
            "out = ui::chat::ChatWorkspaceSnapshot{}",
            "out = ChatWorkspaceSnapshot{}",
        ]:
            if token in text:
                failures += fail(
                    f"ChatPresentationSource uses stack-heavy snapshot reset: {token}"
                )
        for token in ["onQueued", "onFailed", "ChatDeliveryEventProjector"]:
            if token in strip_cpp_comments(text):
                failures += fail(
                    f"ChatPresentationSource must not project events: {token}"
                )
    return failures


def check_composition_roots_own_delivery() -> int:
    failures = 0
    active_legacy_roots = [
        "legacy/app_implementations/linux_sim/src/linux_sim_composition_root.h",
        "legacy/app_implementations/linux_uconsole/src/uconsole_composition_root.h",
        "legacy/app_implementations/linux_uconsole/src/uconsole_composition_root.cpp",
    ]
    if not any(exists(path) for path in active_legacy_roots):
        return failures

    roots = {
        "legacy/app_implementations/linux_sim/src/linux_sim_composition_root.h": [
            "ChatDeliveryReadModel delivery_read_model_",
            "ChatDeliveryEventProjector delivery_projector_",
            "ProjectingChatDeliveryEventPort delivery_event_port_",
            "ChatDeliveryActionService delivery_action_service_",
            "deliveryReadModel()",
            "deliveryProjector()",
            "deliveryEventPort()",
            "deliveryActionSink()",
        ],
        "legacy/app_implementations/linux_uconsole/src/uconsole_composition_root.h": [
            "ChatDeliveryReadModel delivery_read_model_",
            "ChatDeliveryEventProjector delivery_projector_",
            "ProjectingChatDeliveryEventPort delivery_event_port_",
            "ChatDeliveryActionService delivery_action_service_",
            "deliveryReadModel()",
            "deliveryProjector()",
            "deliveryEventPort()",
            "deliveryActionSink()",
        ],
        "legacy/app_implementations/linux_uconsole/src/uconsole_composition_root.cpp": [
            "&delivery_read_model_",
            "ChatPresentationSource",
            "deliveryEventPort()",
        ],
    }

    for path, tokens in roots.items():
        if not exists(path):
            continue
        text = read_text(path)
        for token in tokens:
            if token not in text:
                failures += fail(f"{path} missing delivery ownership token: {token}")
    return failures


def check_ui_presentation_and_renderers_do_not_own_delivery() -> int:
    failures = 0

    for path in iter_code_files(
        ROOT / "modules/ui_presentation/include",
        ROOT / "modules/ui_presentation/src",
    ):
        text = strip_cpp_comments(path.read_text(encoding="utf-8", errors="ignore"))
        if "chat/delivery" in text or "ChatDelivery" in text:
            failures += fail(
                f"{path.relative_to(ROOT)} imports chat delivery runtime"
            )

    renderer_roots = [
        ROOT / "modules/ui_shared/src/ui/screens",
        ROOT / "modules/ui_shared/src/ui/widgets",
        ROOT / "modules/ui_shared/src/ui/menu",
        ROOT / "legacy/app_implementations/linux_uconsole/src/platform/gtk",
    ]
    for path in iter_code_files(*renderer_roots):
        if path.name == "chat_page_runtime.cpp":
            continue
        text = strip_cpp_comments(path.read_text(encoding="utf-8", errors="ignore"))
        forbidden_delivery_tokens = [
            '#include "chat/delivery',
            "ChatDeliveryReadModel",
            "ChatDeliveryEventProjector",
            "ProjectingChatDeliveryEventPort",
        ]
        for token in forbidden_delivery_tokens:
            if token in text:
                failures += fail(
                    f"{path.relative_to(ROOT)} must not own chat delivery runtime token {token}"
                )
        if (
            "LegacyChatDeliveryEventBridge" in text
            and path.name != "chat_page_runtime_event_pump.cpp"
        ):
            failures += fail(
                f"{path.relative_to(ROOT)} contains delivery event bridge outside approved event pump"
            )
    return failures


def check_delivery_event_bridge_boundary() -> int:
    failures = 0
    bridge_files = [
        "modules/ui_chat_runtime/src/chat_delivery_event_projection_adapter.cpp",
    ]
    for path in bridge_files:
        if not exists(path):
            failures += fail(f"missing delivery event bridge file: {path}")
            continue
        text = strip_cpp_comments(read_text(path))
        for token in [
            "lvgl.h",
            "gtk",
            "ChatWorkspaceModel",
            "MessageRow",
            "sendText(",
            "sendMessage(",
            "retry",
            "Renderer",
        ]:
            if token in text:
                failures += fail(f"{path} contains forbidden delivery bridge token {token}")

    source = "modules/ui_chat_runtime/src/chat_delivery_event_projection_adapter.cpp"
    if exists(source):
        text = read_text(source)
        for token in [
            "onChatSendResult",
            "onAckTimeout",
            "makeChatSendResultDeliveryEvent",
            "publishDeliveryEvent",
            "toDeliveryRef",
            "getMessage",
        ]:
            if token not in text:
                failures += fail(f"ChatDeliveryEventProjectionAdapter missing token: {token}")

    controller = "modules/ui_shared/src/ui/screens/chat/chat_ui_controller.cpp"
    if exists(controller):
        text = strip_cpp_comments(read_text(controller))
        for token in [
            "LegacyChatDeliveryEventBridge",
            "ChatDeliveryReadModel",
            "ChatDeliveryEventProjector",
            "ProjectingChatDeliveryEventPort",
            "delivery_event_bridge",
            "delivery_read_model",
            "delivery_projector",
        ]:
            if token in text:
                failures += fail(f"ChatUiController owns delivery runtime token: {token}")

    event_pump = "modules/ui_shared/src/ui/screens/chat/chat_page_runtime_event_pump.cpp"
    if exists(event_pump):
        text = read_text(event_pump)
        for token in [
            "ChatDeliveryEventProjectionAdapter",
            "delivery_adapter_->onChatSendResult",
        ]:
            if token not in text:
                failures += fail(f"ChatPageRuntimeEventPump missing delivery projection token: {token}")

    runtime = "modules/ui_shared/src/ui/screens/chat/chat_page_runtime.cpp"
    if exists(runtime):
        text = read_text(runtime)
        for token in [
            "ChatDeliveryReadModel",
            "ChatDeliveryEventProjector",
            "ProjectingChatDeliveryEventPort",
            "ChatDeliveryEventProjectionAdapter",
            "s_delivery_event_adapter",
            "s_delivery_read_model",
        ]:
            if token not in text:
                failures += fail(f"chat_page_runtime.cpp missing delivery runtime wiring token: {token}")

    return failures


def check_delivery_action_bridge_boundary() -> int:
    failures = 0
    bridge_files = [
        "modules/ui_chat_runtime/src/chat_delivery_action_port_adapter.cpp",
    ]
    for path in bridge_files:
        if not exists(path):
            failures += fail(f"missing delivery action bridge file: {path}")
            continue
        text = strip_cpp_comments(read_text(path))
        for token in [
            "lvgl.h",
            "gtk",
            "ChatWorkspaceModel",
            "MessageRow",
            "ChatDeliveryReadModel",
            "read_model",
            "sendText(",
            "sendMessage(",
            "Renderer",
        ]:
            if token in text:
                failures += fail(f"{path} contains forbidden delivery action bridge token {token}")

    header = "modules/ui_shared/include/ui/presentation_sources/legacy_chat_delivery_action_bridge.h"
    if exists(header):
        failures += fail(f"LegacyChatDeliveryActionBridge header must stay absent: {header}")

    source = "modules/ui_chat_runtime/src/chat_delivery_action_port_adapter.cpp"
    if exists(source):
        text = read_text(source)
        for token in [
            "toDeliveryRef",
            "MessageRef",
            "ChatDeliveryActionRequest",
            "handleDeliveryAction",
            "ChatDeliveryActionKind::Retry",
            "ChatDeliveryActionKind::CancelPending",
            "ChatDeliveryActionKind::ClearFailure",
        ]:
            if token not in text:
                failures += fail(f"LegacyChatDeliveryActionBridge source missing token: {token}")

    runtime = "modules/ui_shared/src/ui/screens/chat/chat_page_runtime.cpp"
    if exists(runtime):
        text = read_text(runtime)
        for token in [
            "ChatDeliveryActionService",
            "ChatDeliveryActionPortAdapter",
            "s_delivery_action_service",
            "s_delivery_action_adapter",
        ]:
            if token not in text:
                failures += fail(f"chat_page_runtime.cpp missing delivery action wiring token: {token}")

    controller = "modules/ui_shared/src/ui/screens/chat/chat_ui_controller.cpp"
    if exists(controller):
        text = strip_cpp_comments(read_text(controller))
        for token in [
            "ChatDeliveryActionService",
            "ChatDeliveryActionRequest",
            "IChatDeliveryActionSink",
            "LegacyChatDeliveryActionBridge",
            "s_delivery_action",
        ]:
            if token in text:
                failures += fail(f"ChatUiController owns delivery action token: {token}")

    for path in [
        "modules/ui_presentation/include/ui_presentation/chat/chat_workspace_model.h",
        "modules/ui_presentation/src/chat/chat_workspace_model.cpp",
        "modules/ui_presentation/include/ui_presentation/chat/chat_workspace_snapshot.h",
    ]:
        if not exists(path):
            continue
        text = strip_cpp_comments(read_text(path))
        for token in [
            "ChatDeliveryAction",
            "IChatDeliveryActionSink",
            "Retry",
            "CancelPending",
            "ClearFailure",
        ]:
            if token in text:
                failures += fail(f"{path} owns delivery action token: {token}")

    return failures


def check_team_action_type_shape() -> int:
    failures = 0

    types = "modules/ui_shared/include/ui/team_actions/team_action_types.h"
    if exists(types):
        text = read_text(types)
        for token in [
            "enum class TeamActionKind",
            "LocationShare",
            "LocationMarker",
            "Command",
            "enum class TeamCommandKind",
            "TeamLocationShareRequest",
            "TeamLocationMarkerRequest",
            "use_current_location",
            "TeamLocationSnapshot",
            "TeamCommandRequest",
            "TeamActionRequest",
        ]:
            if token not in text:
                failures += fail(f"Team action types missing token: {token}")
        for token in ["lv_obj_t", "lvgl.h", "TeamChatMessage", "DirectPeer"]:
            if token in strip_cpp_comments(text):
                failures += fail(f"TeamActionRequest contains forbidden token: {token}")

    sink = "modules/ui_shared/include/ui/team_actions/team_action_sink.h"
    if exists(sink):
        text = read_text(sink)
        for token in ["ITeamActionSink", "sendTeamAction", "ITeamLocationSource", "currentTeamLocation"]:
            if token not in text:
                failures += fail(f"Team action sink missing token: {token}")
        for token in ["ChatWorkspaceModel", "lvgl.h", "LegacyChatActionSink"]:
            if token in strip_cpp_comments(text):
                failures += fail(f"Team action sink contains forbidden token: {token}")

    return failures


def check_team_action_runtime_sink_boundary() -> int:
    failures = 0
    sink_files = [
        "modules/ui_shared/include/ui/team_actions/team_action_runtime_sink.h",
        "modules/ui_shared/src/ui/team_actions/team_action_runtime_sink.cpp",
    ]

    for path in sink_files:
        if not exists(path):
            failures += fail(f"missing Team action runtime sink file: {path}")
            continue
        text = strip_cpp_comments(read_text(path))
        for token in [
            "lvgl.h",
            "lv_obj_t",
            "LegacyChatActionSink",
            "Legacy" + "TeamActionBridge",
            "DirectPeer",
            "ConversationKind::Direct",
            "ConversationKind::Channel",
            "ChatWorkspaceModel",
            "MapWorkspaceModel",
        ]:
            if token in text:
                failures += fail(f"{path} contains forbidden Team action token: {token}")

    for path in [
        "modules/ui_shared/include/ui/team_actions/" + "legacy_" + "team_action_bridge.h",
        "modules/ui_shared/src/ui/team_actions/" + "legacy_" + "team_action_bridge.cpp",
        "modules/ui_shared/tests/test_" + "legacy_" + "team_action_bridge.cpp",
    ]:
        if exists(path):
            failures += fail(f"removed Team action bridge path still exists: {path}")

    source = "modules/ui_shared/src/ui/team_actions/team_action_runtime_sink.cpp"
    if exists(source):
        text = read_text(source)
        for token in [
            "TeamActionRuntimeSink",
            "ITeamLocationSource",
            "currentTeamLocation",
            "sendLocationShare",
            "encodeTeamChatLocation",
            "encodeTeamChatCommand",
            "ITeamUiSnapshotStore",
            "ITeamUiChatLogStore",
            "appendStructured",
            "sendTeamChat",
        ]:
            if token not in text:
                failures += fail(f"TeamActionRuntimeSink source missing token: {token}")
        for token in ["team_ui_chatlog_append_structured", "team_ui_get_store"]:
            if token in strip_cpp_comments(text):
                failures += fail(
                    f"TeamActionRuntimeSink source still uses legacy Team store token: {token}"
                )

    runtime_adapter = "modules/ui_shared/src/ui/team_actions/team_runtime_adapters.cpp"
    if exists(runtime_adapter):
        text = strip_cpp_comments(read_text(runtime_adapter))
        for token in [
            "TeamControllerChatCommandPort",
            "GpsTeamLocationSource",
            "setKeysFromPsk",
            "sendTeamChat",
            "platform::ui::gps::get_data",
        ]:
            if token not in text:
                failures += fail(f"Team runtime adapter missing token: {token}")

    return failures


def _function_body(text: str, start_marker: str, end_marker: str) -> str:
    start = text.find(start_marker)
    if start < 0:
        return ""
    end = text.find(end_marker, start)
    if end < 0:
        return text[start:]
    return text[start:end]


def check_chat_ui_team_action_migration() -> int:
    failures = 0
    header = "modules/ui_shared/include/ui/screens/chat/chat_ui_controller.h"
    source = "modules/ui_shared/src/ui/screens/chat/chat_ui_controller.cpp"
    workflow_header = "modules/ui_shared/include/ui/screens/chat/chat_team_workflow.h"
    workflow_source = "modules/ui_shared/src/ui/screens/chat/chat_team_workflow.cpp"

    if exists(header):
        text = strip_cpp_comments(read_text(header))
        for token in ["ChatTeamWorkflow", "team_workflow_"]:
            if token not in text:
                failures += fail(f"ChatUiController header missing Team workflow token: {token}")
        for token in [
            "ITeamActionSink",
            "TeamActionRequest",
            "team_action_sink_",
            "sendTeamLocationWithIcon",
        ]:
            if token in text:
                failures += fail(f"ChatUiController header still owns Team action token: {token}")
    else:
        failures += fail("ChatUiController header is missing")

    if exists(source):
        text = strip_cpp_comments(read_text(source))
        for token in [
            "ChatTeamWorkflow",
            "team_workflow_.sendText",
            "team_workflow_.sendCurrentLocationMarker",
            "team_workflow_.buildSelectedSnapshot",
        ]:
            if token not in text:
                failures += fail(f"ChatUiController source missing Team workflow token: {token}")
        for token in [
            "TeamActionRequest",
            "TeamActionKind::LocationMarker",
            "sendTeamAction",
            "ITeamActionSink",
            "team_action_sink_",
            "team_location_marker_icon_name",
            "team_location_marker_icon_is_valid",
            "team_chat_model_.sendMessage",
        ]:
            if token in text:
                failures += fail(f"ChatUiController source still owns Team action token: {token}")
        for token in [
            '#include "platform/ui/gps_runtime.h"',
            '#include "team/usecase/team_controller.h"',
            '#include "ui/team_actions/team_action_sink.h"',
        ]:
            if token in text:
                failures += fail(f"ChatUiController source reintroduced runtime include: {token}")
    else:
        failures += fail("ChatUiController source is missing")

    for path in [workflow_header, workflow_source]:
        if not exists(path):
            failures += fail(f"missing Chat Team workflow file: {path}")
            continue
        text = strip_cpp_comments(read_text(path))
        for token in ["lvgl.h", "lv_obj_t", "ChatService", "TeamController"]:
            if token in text:
                failures += fail(f"{path} contains forbidden Team workflow token: {token}")

    if exists(workflow_source):
        text = strip_cpp_comments(read_text(workflow_source))
        for token in [
            "TeamActionRequest",
            "TeamActionKind::LocationMarker",
            "sendTeamAction",
            "team_location_marker_icon_name",
            "team_location_marker_icon_is_valid",
            "sendCurrentLocationMarker",
            "textSendFailureMessage",
            "locationSendFailureMessage",
        ]:
            if token not in text:
                failures += fail(f"ChatTeamWorkflow source missing token: {token}")

    return failures


def check_team_page_read_model_boundary() -> int:
    failures = 0
    header = "modules/ui_shared/include/ui/screens/team/team_page_read_model.h"
    source = "modules/ui_shared/src/ui/screens/team/team_page_read_model.cpp"
    test = "modules/ui_shared/tests/test_team_page_read_model.cpp"
    controller = "modules/ui_shared/src/ui/screens/team/team_page_components.cpp"

    for path in [header, source, test]:
        if not exists(path):
            failures += fail(f"missing Team Page read model file: {path}")

    for path in [header, source]:
        if not exists(path):
            continue
        text = strip_cpp_comments(read_text(path))
        for token in [
            "lvgl.h",
            "lv_obj_t",
            "TeamController",
            "TeamPairingService",
            "app::teamFacade",
            "team_ui_get_store",
            "ITeamUiSnapshotStore",
            "team_ui_snapshot_store()",
            "gps_runtime",
            "gps_tracker_overlay",
            "sys::Event",
        ]:
            if token in text:
                failures += fail(f"{path} contains forbidden Team Page read model token: {token}")

    if exists(header):
        text = read_text(header)
        for token in [
            "TeamPageReadModel",
            "TeamPageReadModelInput",
            "TeamPageSummaryView",
            "TeamMemberRowView",
            "TeamMemberDetailView",
            "TeamRelativeTimeView",
            "inputFromSnapshot",
            "buildSummary",
            "buildMemberRows",
            "buildSelectedMember",
        ]:
            if token not in text:
                failures += fail(f"Team Page read model header missing token: {token}")

    if exists(source):
        text = read_text(source)
        for token in [
            "isTeamMemberOnline",
            "formatTeamNameFromId",
            "management_actions_enabled",
        ]:
            if token not in text:
                failures += fail(f"Team Page read model source missing token: {token}")

    if exists(controller):
        text = strip_cpp_comments(read_text(controller))
        for token in [
            "team_page_read_model.h",
            "current_read_model_input",
            "buildSelectedMember",
        ]:
            if token not in text:
                failures += fail(f"Team page controller missing read model token: {token}")
        for token in [
            "refreshTeamMemberPresence",
            "isTeamMemberOnline",
            "online_count(",
        ]:
            if token in text:
                failures += fail(f"Team page controller still owns read model token: {token}")

    return failures


def check_team_page_flow_controller_boundary() -> int:
    failures = 0
    types = "modules/ui_shared/include/ui/screens/team/team_page_types.h"
    header = "modules/ui_shared/include/ui/screens/team/team_page_flow_controller.h"
    source = "modules/ui_shared/src/ui/screens/team/team_page_flow_controller.cpp"
    test = "modules/ui_shared/tests/test_team_page_flow_controller.cpp"
    controller = "modules/ui_shared/src/ui/screens/team/team_page_components.cpp"

    for path in [types, header, source, test]:
        if not exists(path):
            failures += fail(f"missing Team Page flow controller file: {path}")

    for path in [types, header, source]:
        if not exists(path):
            continue
        text = strip_cpp_comments(read_text(path))
        for token in [
            "lvgl.h",
            "lv_obj_t",
            "team_state.h",
            "TeamPageState&",
            "TeamPageState*",
            "TeamController",
            "TeamPairingService",
            "app::teamFacade",
            "app::messagingFacade",
            "team_ui_",
            "g_team_state",
            "SystemNotification",
            "sys::Event",
            "EventBus",
            "render_page",
            "ui_request_exit_to_menu",
        ]:
            if token in text:
                failures += fail(f"{path} contains forbidden Team Page flow controller token: {token}")

    if exists(types):
        text = read_text(types)
        for token in [
            "enum class TeamPage",
            "StatusNotInTeam",
            "StatusInTeam",
            "JoinPending",
            "KickedOut",
        ]:
            if token not in text:
                failures += fail(f"Team Page types header missing token: {token}")

    if exists(header):
        text = read_text(header)
        for token in [
            "TeamPageFlowState",
            "TeamPageFlowResult",
            "TeamPageFlowController",
            "navigateTo",
            "navigateBack",
            "resetTo",
            "selectInitialPage",
            "syncRuntime",
            "isPairingActive",
        ]:
            if token not in text:
                failures += fail(f"Team Page flow controller header missing token: {token}")

    if exists(source):
        text = read_text(source)
        for token in [
            "TeamPageFlowController::navigateTo",
            "TeamPageFlowController::navigateBack",
            "TeamPageFlowController::resetTo",
            "TeamPageFlowController::selectInitialPage",
            "TeamPageFlowController::syncRuntime",
            "TeamPageFlowController::isPairingActive",
        ]:
            if token not in text:
                failures += fail(f"Team Page flow controller source missing token: {token}")

    if exists(controller):
        text = strip_cpp_comments(read_text(controller))
        for token in [
            "team_page_flow_controller.h",
            "flow_state_from_page",
            "apply_flow_state_to_page",
            "current_flow_controller",
            "navigateTo",
            "navigateBack",
            "resetTo",
            "selectInitialPage",
            "syncRuntime",
        ]:
            if token not in text:
                failures += fail(f"Team page controller missing flow controller token: {token}")
        for function_name in ["nav_to", "nav_back", "nav_reset", "sync_from_controller", "team_page_create"]:
            match = re.search(
                rf"(?:void|bool)\s+{function_name}\s*\([^)]*\)\s*\{{(?P<body>.*?)\n\}}",
                text,
                re.DOTALL,
            )
            if not match:
                continue
            body = match.group("body")
            for token in [
                "TeamPairingState::Idle &&",
                "TeamPairingState::Completed",
                "TeamPairingState::Failed",
                "g_team_state.nav_stack.push_back",
                "g_team_state.nav_stack.pop_back",
            ]:
                if token in body:
                    failures += fail(f"{function_name} still owns flow controller token: {token}")

    return failures


def check_team_page_lvgl_renderer_boundary() -> int:
    failures = 0
    header = "modules/ui_shared/include/ui/screens/team/team_page_lvgl_renderer.h"
    source = "modules/ui_shared/src/ui/screens/team/team_page_lvgl_renderer.cpp"
    controller = "modules/ui_shared/src/ui/screens/team/team_page_components.cpp"

    for path in [header, source]:
        if not exists(path):
            failures += fail(f"missing Team Page LVGL renderer file: {path}")

    for path in [header, source]:
        if not exists(path):
            continue
        text = strip_cpp_comments(read_text(path))
        for token in [
            "TeamController",
            "TeamPairingService",
            "app::teamFacade",
            "app::messagingFacade",
            "team_ui_",
            "g_team_state",
            "g_gps_state",
            "gps_tracker_load_file",
            "gps_tracker_overlay",
            "SystemNotification",
            "sys::Event",
            "EventBus",
            "TeamPageRuntimePort",
            "TeamPageCommandReducer",
            "TeamPageEventReducer",
        ]:
            if token in text:
                failures += fail(f"{path} contains forbidden Team Page LVGL renderer token: {token}")

    if exists(header):
        text = read_text(header)
        for token in [
            "ITeamPageLvglNameResolver",
            "TeamPageLvglRendererHandlers",
            "TeamPageLvglRendererContext",
            "TeamPageLvglRendererInput",
            "TeamPageLvglRenderer",
            "render",
        ]:
            if token not in text:
                failures += fail(f"Team Page LVGL renderer header missing token: {token}")

    if exists(source):
        text = read_text(source)
        for token in [
            "TeamPageLvglRenderer::render",
            "renderStatusNotInTeam",
            "renderStatusInTeam",
            "renderTeamHome",
            "renderJoinPending",
            "renderMembers",
            "renderMemberDetail",
            "renderKickConfirm",
            "renderKickedOut",
            "createActionButton",
            "createListItem",
            "renderMemberChipRow",
        ]:
            if token not in text:
                failures += fail(f"Team Page LVGL renderer source missing token: {token}")

    if exists(controller):
        text = strip_cpp_comments(read_text(controller))
        for token in [
            "team_page_lvgl_renderer.h",
            "TeamPageLvglRendererContext",
            "TeamPageLvglRendererHandlers",
            "TeamPageLvglRendererInput",
            "TeamPageLvglRenderer(now_secs()).render",
        ]:
            if token not in text:
                failures += fail(f"Team page controller missing LVGL renderer token: {token}")
        for token in [
            "void render_status_not_in_team",
            "void render_status_in_team",
            "void render_team_home",
            "void render_join_pending",
            "void render_members",
            "void render_member_detail",
            "void render_kick_confirm",
            "void render_kicked_out",
            "lv_obj_t* add_label",
            "lv_obj_t* create_action_button",
            "lv_obj_t* create_list_item",
            "render_member_chip_row",
            "render_member_chips_or_empty",
            "format_last_seen",
            "format_last_update",
        ]:
            if token in text:
                failures += fail(f"Team page controller still owns LVGL renderer token: {token}")

    return failures


def check_team_page_create_team_action_boundary() -> int:
    failures = 0
    header = "modules/ui_shared/include/ui/screens/team/team_page_create_team_action.h"
    source = "modules/ui_shared/src/ui/screens/team/team_page_create_team_action.cpp"
    test = "modules/ui_shared/tests/test_team_page_create_team_action.cpp"
    controller = "modules/ui_shared/src/ui/screens/team/team_page_components.cpp"

    for path in [header, source, test]:
        if not exists(path):
            failures += fail(f"missing Team Page create-team action file: {path}")

    for path in [header, source]:
        if not exists(path):
            continue
        text = strip_cpp_comments(read_text(path))
        for token in [
            "lvgl.h",
            "lv_obj_t",
            "team_state.h",
            "TeamPageState&",
            "TeamPageState*",
            "TeamController",
            "TeamPairingService",
            "app::teamFacade",
            "app::messagingFacade",
            "team_ui_",
            "g_gps_state",
            "gps_tracker_load_file",
            "gps_tracker_overlay",
            "SystemNotification",
            "sys::Event",
            "EventBus",
            "sys::millis_now",
            "next_random_byte",
        ]:
            if token in text:
                failures += fail(f"{path} contains forbidden Team Page create-team action token: {token}")

    if exists(header):
        text = read_text(header)
        for token in [
            "ITeamPageCreateTeamRandom",
            "TeamPageCreateTeamFailureAction",
            "TeamPageCreateTeamFailureKind",
            "TeamPageCreateTeamEffects",
            "TeamPageCreateTeamAction",
            "createTeam",
            "TeamPageRuntimePort",
            "TeamPageKeyEventLog",
        ]:
            if token not in text:
                failures += fail(f"Team Page create-team action header missing token: {token}")

    if exists(source):
        text = read_text(source)
        for token in [
            "TeamPageCreateTeamAction::createTeam",
            "generateTeamId",
            "generateTeamPsk",
            "reducer.reduceCreate",
            "key_log.appendTeamCreated",
            "runtime.setKeysFromPsk",
            "runtime.saveKeysNow",
            "runtime.startLeader",
            "reducer.reducePairingStarted",
        ]:
            if token not in text:
                failures += fail(f"Team Page create-team action source missing token: {token}")

    if exists(controller):
        text = strip_cpp_comments(read_text(controller))
        for token in [
            "team_page_create_team_action.h",
            "TeamPageRandomByteAdapter",
            "apply_create_team_failures",
            "TeamPageCreateTeamAction().createTeam",
        ]:
            if token not in text:
                failures += fail(f"Team page controller missing create-team action token: {token}")
        if "handle_create" in text:
            match = re.search(
                r"void\s+handle_create\s*\([^)]*\)\s*\{(?P<body>.*?)\n\}",
                text,
                re.DOTALL,
            )
            if match:
                body = match.group("body")
                for token in [
                    "TeamPageGeneratedTeamSecrets",
                    "generate_team_id",
                    "next_random_byte",
                    "reduceCreate",
                    "appendTeamCreated",
                    "setKeysFromPsk",
                    "saveKeysNow",
                    "start_pairing_leader",
                ]:
                    if token in body:
                        failures += fail(f"handle_create still owns create-team orchestration token: {token}")

    return failures


def check_team_page_pairing_command_action_boundary() -> int:
    failures = 0
    header = "modules/ui_shared/include/ui/screens/team/team_page_pairing_command_action.h"
    source = "modules/ui_shared/src/ui/screens/team/team_page_pairing_command_action.cpp"
    test = "modules/ui_shared/tests/test_team_page_pairing_command_action.cpp"
    controller = "modules/ui_shared/src/ui/screens/team/team_page_components.cpp"

    for path in [header, source, test]:
        if not exists(path):
            failures += fail(f"missing Team Page pairing command action file: {path}")

    for path in [header, source]:
        if not exists(path):
            continue
        text = strip_cpp_comments(read_text(path))
        for token in [
            "lvgl.h",
            "lv_obj_t",
            "team_state.h",
            "TeamPageState&",
            "TeamPageState*",
            "TeamController",
            "TeamPairingService",
            "app::teamFacade",
            "app::messagingFacade",
            "team_ui_",
            "g_gps_state",
            "gps_tracker_load_file",
            "gps_tracker_overlay",
            "SystemNotification",
            "sys::Event",
            "EventBus",
            "sys::millis_now",
            "next_random_byte",
        ]:
            if token in text:
                failures += fail(f"{path} contains forbidden Team Page pairing command action token: {token}")

    if exists(header):
        text = read_text(header)
        for token in [
            "TeamPagePairingCommandRole",
            "TeamPagePairingCommandFailureKind",
            "TeamPagePairingCommandEffects",
            "TeamPagePairingCommandAction",
            "startPairing",
            "TeamPageRuntimePort",
        ]:
            if token not in text:
                failures += fail(f"Team Page pairing command action header missing token: {token}")

    if exists(source):
        text = read_text(source)
        for token in [
            "TeamPagePairingCommandAction::startPairing",
            "runtime.startLeader",
            "runtime.startMember",
            "reducer.reducePairingStarted",
            "TeamPairingRole::Leader",
            "TeamPairingRole::Member",
            "TeamPageCommandReducer::formatTeamNameFromId",
        ]:
            if token not in text:
                failures += fail(f"Team Page pairing command action source missing token: {token}")

    if exists(controller):
        text = strip_cpp_comments(read_text(controller))
        for token in [
            "team_page_pairing_command_action.h",
            "apply_pairing_command_failures",
            "TeamPagePairingCommandAction().startPairing",
            "start_pairing_command",
        ]:
            if token not in text:
                failures += fail(f"Team page controller missing pairing command action token: {token}")
        for token in [
            "start_pairing_leader",
            "start_pairing_member",
            "runtime.startLeader",
            "runtime.startMember",
            "reducePairingStarted",
            "current_team_name",
        ]:
            if token in text:
                failures += fail(f"Team page controller still owns pairing command action token: {token}")

    return failures


def check_team_page_request_keys_action_boundary() -> int:
    failures = 0
    header = "modules/ui_shared/include/ui/screens/team/team_page_request_keys_action.h"
    source = "modules/ui_shared/src/ui/screens/team/team_page_request_keys_action.cpp"
    test = "modules/ui_shared/tests/test_team_page_request_keys_action.cpp"
    controller = "modules/ui_shared/src/ui/screens/team/team_page_components.cpp"

    for path in [header, source, test]:
        if not exists(path):
            failures += fail(f"missing Team Page request-keys action file: {path}")

    for path in [header, source]:
        if not exists(path):
            continue
        text = strip_cpp_comments(read_text(path))
        for token in [
            "lvgl.h",
            "lv_obj_t",
            "team_state.h",
            "TeamPageState&",
            "TeamPageState*",
            "TeamController",
            "TeamPairingService",
            "app::teamFacade",
            "app::messagingFacade",
            "team_ui_",
            "g_team_state",
            "g_gps_state",
            "gps_tracker_load_file",
            "gps_tracker_overlay",
            "SystemNotification",
            "sys::Event",
            "EventBus",
            "sys::millis_now",
        ]:
            if token in text:
                failures += fail(f"{path} contains forbidden Team Page request-keys action token: {token}")

    if exists(header):
        text = read_text(header)
        for token in [
            "TeamPageRequestKeysEffects",
            "TeamPageRequestKeysAction",
            "requestKeys",
            "TeamPageCommandState",
        ]:
            if token not in text:
                failures += fail(f"Team Page request-keys action header missing token: {token}")

    if exists(source):
        text = read_text(source)
        for token in [
            "TeamPageRequestKeysAction::requestKeys",
            "state.in_team",
            "state.has_team_id",
            "state.self_is_leader",
            "runtime.sendKeyRequest",
            "TeamKeyRequest",
        ]:
            if token not in text:
                failures += fail(f"Team Page request-keys action source missing token: {token}")
        if "effects.unsupported" in text:
            failures += fail("Team Page request-keys action still reports unsupported")

    if exists(controller):
        text = strip_cpp_comments(read_text(controller))
        for token in [
            "team_page_request_keys_action.h",
            "TeamPageRequestKeysAction().requestKeys",
        ]:
            if token not in text:
                failures += fail(f"Team page controller missing request-keys action token: {token}")
        match = re.search(
            r"void\s+handle_request_keydist\s*\([^)]*\)\s*\{(?P<body>.*?)\n\}",
            text,
            re.DOTALL,
        )
        if match:
            body = match.group("body")
            for token in [
                "g_team_state.in_team",
                "g_team_state.self_is_leader",
                "g_team_state.has_team_id",
                "return;",
            ]:
                if token in body:
                    failures += fail(f"handle_request_keydist still owns request-keys action token: {token}")

    return failures


def check_team_page_event_reducer_boundary() -> int:
    failures = 0
    header = "modules/ui_shared/include/ui/screens/team/team_page_event_reducer.h"
    source = "modules/ui_shared/src/ui/screens/team/team_page_event_reducer.cpp"
    test = "modules/ui_shared/tests/test_team_page_event_reducer.cpp"
    controller = "modules/ui_shared/src/ui/screens/team/team_page_components.cpp"

    for path in [header, source, test]:
        if not exists(path):
            failures += fail(f"missing Team Page event reducer file: {path}")

    for path in [header, source]:
        if not exists(path):
            continue
        text = strip_cpp_comments(read_text(path))
        for token in [
            "lvgl.h",
            "lv_obj_t",
            "team_state.h",
            "TeamPageState&",
            "TeamPageState*",
            "TeamController",
            "TeamPairingService",
            "app::teamFacade",
            "app::messagingFacade",
            "team_ui_get_store",
            "team_ui_snapshot_store",
            "team_ui_chatlog",
            "team_ui_posring",
            "team_ui_append_key_event",
            "gps_runtime",
            "gps_tracker_overlay",
            "SystemNotification",
            "sys::Event",
            "EventBus",
        ]:
            if token in text:
                failures += fail(f"{path} contains forbidden Team Page event reducer token: {token}")

    if exists(header):
        text = read_text(header)
        for token in [
            "TeamPageEventState",
            "ITeamPageMemberNameResolver",
            "TeamPageEventContext",
            "TeamPageEventEffects",
            "TeamPageEventReducer",
            "findMemberIndex",
            "touchMember",
            "fillStatusMembers",
            "applyStatusRoster",
            "reduceError",
            "reduceStatus",
            "reduceActivity",
            "reduceKick",
            "reduceTransferLeader",
            "reduceKeyDist",
            "TeamPagePairingUpdate",
            "reducePairing",
            "reducePairingStatus",
        ]:
            if token not in text:
                failures += fail(f"Team Page event reducer header missing token: {token}")

    if exists(source):
        text = read_text(source)
        for token in [
            "findTeamMemberIndex",
            "normalizeSeenSeconds",
            "touchTeamMember",
            "formatTeamNameFromId",
            "keydist_confirmed",
            "epoch_rotated",
            "member_kicked_key_event",
            "member_accepted_key_event",
            "show_pairing_success",
        ]:
            if token not in text:
                failures += fail(f"Team Page event reducer source missing token: {token}")

    if exists(controller):
        text = strip_cpp_comments(read_text(controller))
        for token in [
            "team_page_event_reducer.h",
            "TeamPageEventState event_state_from_page",
            "apply_event_state_to_page",
            "current_event_reducer",
            "reduce_event_state",
            "reduceStatus",
            "reduceActivity",
            "reduceError",
            "reduceKick",
            "reduceTransferLeader",
            "reduceKeyDist",
            "pairing_update_from_event",
            "pairing_update_from_status",
            "reducePairing",
            "reducePairingStatus",
        ]:
            if token not in text:
                failures += fail(f"Team page controller missing event reducer token: {token}")
        for token in [
            "ui/team_presence/team_presence_model.h",
            "normalizeSeenSeconds",
            "touchTeamMember",
            "findTeamMemberIndex",
            "apply_member_list_from_status(ev.msg)",
            "update_team_name_from_id",
            "assign_member_color",
            "touch_member(",
            "find_member_index",
            "g_team_state.security_round += 1",
            "g_team_state.team_psk = ev.msg.channel_psk",
            "g_team_state.pairing_role = ev.role",
        ]:
            if token in text:
                failures += fail(f"Team page controller still owns event reducer token: {token}")

    return failures


def check_team_page_event_effect_sink_boundary() -> int:
    failures = 0
    header = "modules/ui_shared/include/ui/screens/team/team_page_event_effect_sink.h"
    source = "modules/ui_shared/src/ui/screens/team/team_page_event_effect_sink.cpp"
    test = "modules/ui_shared/tests/test_team_page_event_effect_sink.cpp"
    controller = "modules/ui_shared/src/ui/screens/team/team_page_components.cpp"

    for path in [header, source, test]:
        if not exists(path):
            failures += fail(f"missing Team Page event effect sink file: {path}")

    for path in [header, source]:
        if not exists(path):
            continue
        text = strip_cpp_comments(read_text(path))
        for token in [
            "lvgl.h",
            "lv_obj_t",
            "team_state.h",
            "TeamPageState&",
            "TeamPageState*",
            "TeamController",
            "TeamPairingService",
            "app::teamFacade",
            "app::messagingFacade",
            "team_ui_",
            "g_team_state",
            "g_gps_state",
            "gps_tracker_load_file",
            "gps_tracker_overlay",
            "SystemNotification",
            "sys::Event",
            "EventBus",
            "sys::millis_now",
        ]:
            if token in text:
                failures += fail(f"{path} contains forbidden Team Page event effect sink token: {token}")

    if exists(header):
        text = read_text(header)
        for token in [
            "ITeamPageEventDeferred",
            "ITeamPageEventNotifier",
            "TeamPageEventEffectResult",
            "TeamPageEventEffectSink",
            "applyEffects",
            "TeamPageRuntimePort",
            "TeamPageKeyEventLog",
        ]:
            if token not in text:
                failures += fail(f"Team Page event effect sink header missing token: {token}")

    if exists(source):
        text = read_text(source)
        for token in [
            "TeamPageEventEffectSink::applyEffects",
            "deferred.confirmKeyDist",
            "key_log.appendEpochRotated",
            "key_log.appendMemberAccepted",
            "runtime.saveKeysNow",
            "runtime.setKeysFromPsk",
            "runtime.sendStatus",
            "runtime.sendStatusPlain",
            "deferred.scheduleStatusBroadcast",
            "notifier.showMessage",
        ]:
            if token not in text:
                failures += fail(f"Team Page event effect sink source missing token: {token}")

    if exists(controller):
        text = strip_cpp_comments(read_text(controller))
        for token in [
            "team_page_event_effect_sink.h",
            "TeamPageEventDeferredAdapter",
            "TeamPageEventNotifierAdapter",
            "apply_event_effects",
            "apply_event_navigation_requests",
            "TeamPageEventEffectSink().applyEffects",
        ]:
            if token not in text:
                failures += fail(f"Team page controller missing event effect sink token: {token}")
        for handler in ["handle_team_status", "handle_team_key_dist", "handle_team_pairing"]:
            match = re.search(
                rf"void\s+{handler}\s*\([^)]*\)\s*\{{(?P<body>.*?)\n\}}",
                text,
                re.DOTALL,
            )
            if not match:
                failures += fail(f"Team page controller missing handler body: {handler}")
                continue
            body = match.group("body")
            for token in [
                "appendEpochRotated",
                "appendMemberAccepted",
                "saveKeysNow",
                "setKeysFromPsk",
                "sendStatus(",
                "sendStatusPlain(",
                "schedule_status_broadcast",
                "SystemNotification::show",
                "g_team_state.page",
                "g_team_state.nav_stack.clear",
            ]:
                if token in body:
                    failures += fail(f"{handler} still owns event effect sink token: {token}")

    return failures


def check_team_page_command_reducer_boundary() -> int:
    failures = 0
    header = "modules/ui_shared/include/ui/screens/team/team_page_command_reducer.h"
    source = "modules/ui_shared/src/ui/screens/team/team_page_command_reducer.cpp"
    test = "modules/ui_shared/tests/test_team_page_command_reducer.cpp"
    controller = "modules/ui_shared/src/ui/screens/team/team_page_components.cpp"

    for path in [header, source, test]:
        if not exists(path):
            failures += fail(f"missing Team Page command reducer file: {path}")

    for path in [header, source]:
        if not exists(path):
            continue
        text = strip_cpp_comments(read_text(path))
        for token in [
            "lvgl.h",
            "lv_obj_t",
            "team_state.h",
            "TeamPageState&",
            "TeamPageState*",
            "TeamController",
            "TeamPairingService",
            "app::teamFacade",
            "app::messagingFacade",
            "team_ui_get_store",
            "team_ui_snapshot_store",
            "team_ui_chatlog",
            "team_ui_posring",
            "team_ui_append_key_event",
            "team_ui_save_keys_now",
            "gps_runtime",
            "gps_tracker_overlay",
            "SystemNotification",
            "sys::Event",
            "EventBus",
            "next_random_byte",
            "sys::millis_now",
        ]:
            if token in text:
                failures += fail(f"{path} contains forbidden Team Page command reducer token: {token}")

    if exists(header):
        text = read_text(header)
        for token in [
            "TeamPageCommandState",
            "TeamPageCommandContext",
            "TeamPageGeneratedTeamSecrets",
            "TeamPageKickRotation",
            "TeamPageCommandEffects",
            "TeamPageCommandReducer",
            "reduceCreate",
            "reduceLeave",
            "reduceKickedOut",
            "reduceClearKickedOut",
            "reducePairingStarted",
            "reduceJoinCanceled",
            "reduceKickConfirmed",
            "reduceTransferLeader",
        ]:
            if token not in text:
                failures += fail(f"Team Page command reducer header missing token: {token}")

    if exists(source):
        text = read_text(source)
        for token in [
            "resetTeamMembership",
            "resetPairing",
            "ensureSelfMember",
            "team_color_index_from_node_id",
            "formatTeamNameFromId",
            "keys_changed",
            "status_should_send",
        ]:
            if token not in text:
                failures += fail(f"Team Page command reducer source missing token: {token}")

    if exists(controller):
        text = strip_cpp_comments(read_text(controller))
        for token in [
            "team_page_command_reducer.h",
            "TeamPageCommandState command_state_from_page",
            "apply_command_state_to_page",
            "current_command_reducer",
            "reduce_command_state",
            "apply_command_runtime_effects",
            "reduceLeave",
            "reduceKickedOut",
            "reduceJoinCanceled",
        ]:
            if token not in text:
                failures += fail(f"Team page controller missing command reducer token: {token}")

    return failures


def check_team_page_key_event_log_boundary() -> int:
    failures = 0
    header = "modules/ui_shared/include/ui/screens/team/team_page_key_event_log.h"
    source = "modules/ui_shared/src/ui/screens/team/team_page_key_event_log.cpp"
    test = "modules/ui_shared/tests/test_team_page_key_event_log.cpp"
    controller = "modules/ui_shared/src/ui/screens/team/team_page_components.cpp"

    for path in [header, source, test]:
        if not exists(path):
            failures += fail(f"missing Team Page key event log file: {path}")

    for path in [header, source]:
        if not exists(path):
            continue
        text = strip_cpp_comments(read_text(path))
        for token in [
            "lvgl.h",
            "lv_obj_t",
            "team_state.h",
            "TeamPageState&",
            "TeamPageState*",
            "TeamController",
            "TeamPairingService",
            "app::teamFacade",
            "app::messagingFacade",
            "team_ui_append_key_event(",
            "team_ui_snapshot_store",
            "team_ui_chatlog",
            "team_ui_posring",
            "gps_runtime",
            "gps_tracker_overlay",
            "SystemNotification",
            "sys::Event",
            "EventBus",
        ]:
            if token in text:
                failures += fail(f"{path} contains forbidden Team Page key event log token: {token}")

    if exists(header):
        text = read_text(header)
        for token in [
            "TeamPageKeyEventState",
            "ITeamPageKeyEventWriter",
            "TeamPageKeyEventLog",
            "appendTeamCreated",
            "appendMemberAccepted",
            "appendMemberKicked",
            "appendLeaderTransferred",
            "appendEpochRotated",
            "teamIdToU64",
        ]:
            if token not in text:
                failures += fail(f"Team Page key event log header missing token: {token}")

    if exists(source):
        text = read_text(source)
        for token in [
            "writeU32Le",
            "writeU64Le",
            "TeamKeyEventType::TeamCreated",
            "TeamKeyEventType::MemberAccepted",
            "TeamKeyEventType::MemberKicked",
            "TeamKeyEventType::LeaderTransferred",
            "TeamKeyEventType::EpochRotated",
            "state.last_event_seq = next_seq",
        ]:
            if token not in text:
                failures += fail(f"Team Page key event log source missing token: {token}")

    if exists(controller):
        text = strip_cpp_comments(read_text(controller))
        for token in [
            "team_page_key_event_log.h",
            "TeamPageRuntimeKeyEventWriter",
            "key_event_state_from_page",
            "apply_key_event_state_to_page",
            "current_key_event_log",
            "append_key_event_to_page",
            "appendMemberKicked",
            "appendEpochRotated",
        ]:
            if token not in text:
                failures += fail(f"Team page controller missing key event log token: {token}")
        for token in [
            "write_u32_le",
            "write_u64_le",
            "team_id_to_u64",
            "bool append_key_event(",
        ]:
            if token in text:
                failures += fail(f"Team page controller still owns key event log token: {token}")

    return failures


def check_team_page_runtime_port_boundary() -> int:
    failures = 0
    header = "modules/ui_shared/include/ui/screens/team/team_page_runtime_port.h"
    source = "modules/ui_shared/src/ui/screens/team/team_page_runtime_port.cpp"
    adapters = "modules/ui_shared/src/ui/screens/team/team_page_runtime_adapters.cpp"
    test = "modules/ui_shared/tests/test_team_page_runtime_port.cpp"
    controller = "modules/ui_shared/src/ui/screens/team/team_page_components.cpp"

    for path in [header, source, adapters, test]:
        if not exists(path):
            failures += fail(f"missing Team Page runtime port file: {path}")

    for path in [header, source, adapters]:
        if not exists(path):
            continue
        text = strip_cpp_comments(read_text(path))
        for token in [
            "lvgl.h",
            "lv_obj_t",
            "team_state.h",
            "TeamPageState&",
            "TeamPageState*",
            "gps_runtime",
            "gps_tracker_overlay",
            "SystemNotification",
            "sys::Event",
            "EventBus",
        ]:
            if token in text:
                failures += fail(f"{path} contains forbidden Team Page runtime port token: {token}")

    if exists(header):
        text = read_text(header)
        for token in [
            "ITeamPageControllerPort",
            "ITeamPagePairingPort",
            "ITeamPageKeyStorePort",
            "TeamPageRuntimePort",
            "TeamPageControllerPortAdapter",
            "TeamPagePairingPortAdapter",
            "TeamPageKeyStorePortAdapter",
            "sendStatus",
            "sendKeyDist",
            "startLeader",
            "saveKeysNow",
        ]:
            if token not in text:
                failures += fail(f"Team Page runtime port header missing token: {token}")

    if exists(source):
        text = read_text(source)
        for token in [
            "TeamPageRuntimePort::TeamPageRuntimePort",
            "TeamPageRuntimePort::clearKeys",
            "TeamPageRuntimePort::sendStatus",
            "TeamPageRuntimePort::startLeader",
            "TeamPageRuntimePort::lastSendError",
        ]:
            if token not in text:
                failures += fail(f"Team Page runtime port source missing token: {token}")
        for token in [
            "team_ui_save_keys_now",
            "controller_->on",
        ]:
            if token in strip_cpp_comments(text):
                failures += fail(f"Team Page runtime port source owns adapter token: {token}")

    if exists(adapters):
        text = read_text(adapters)
        for token in [
            "team_ui_save_keys_now",
            "controller_->onKick",
            "controller_->onTransferLeader",
            "controller_->onKeyDist",
            "controller_->onKeyDistPlain",
            "controller_->onStatus",
            "controller_->onStatusPlain",
            "pairing_->startLeader",
            "pairing_->startMember",
            "pairing_->getStatus",
        ]:
            if token not in text:
                failures += fail(f"Team Page runtime adapter source missing token: {token}")

    if exists(controller):
        text = strip_cpp_comments(read_text(controller))
        for token in [
            "team_page_runtime_port.h",
            "current_runtime_port",
            "TeamPageRuntimePort",
            "TeamPageControllerPortAdapter",
            "TeamPagePairingPortAdapter",
            "TeamPageKeyStorePortAdapter",
        ]:
            if token not in text:
                failures += fail(f"Team page controller missing runtime port token: {token}")
        for token in [
            "TeamController* controller",
            "TeamPairingService* pairing",
            "controller->",
            "pairing->",
            "team_ui_save_keys_now",
            "getLastSendError",
        ]:
            if token in text:
                failures += fail(f"Team page controller still owns runtime port token: {token}")
        if text.count("app::teamFacade()") != 2:
            failures += fail("Team page controller should access app::teamFacade only in current_runtime_port")

    return failures


def check_team_page_activity_sink_boundary() -> int:
    failures = 0
    header = "modules/ui_shared/include/ui/screens/team/team_page_activity_sink.h"
    source = "modules/ui_shared/src/ui/screens/team/team_page_activity_sink.cpp"
    adapters = "modules/ui_shared/src/ui/screens/team/team_page_activity_adapters.cpp"
    test = "modules/ui_shared/tests/test_team_page_activity_sink.cpp"
    controller = "modules/ui_shared/src/ui/screens/team/team_page_components.cpp"

    for path in [header, source, adapters, test]:
        if not exists(path):
            failures += fail(f"missing Team Page activity sink file: {path}")

    for path in [header, source]:
        if not exists(path):
            continue
        text = strip_cpp_comments(read_text(path))
        for token in [
            "lvgl.h",
            "lv_obj_t",
            "team_state.h",
            "TeamPageState&",
            "TeamPageState*",
            "TeamController",
            "TeamPairingService",
            "app::teamFacade",
            "app::messagingFacade",
            "team_ui_",
            "g_gps_state",
            "gps_tracker_load_file",
            "gps_tracker_overlay",
            "SystemNotification",
            "sys::Event",
            "EventBus",
        ]:
            if token in text:
                failures += fail(f"{path} contains forbidden Team Page activity sink token: {token}")

    if exists(header):
        text = read_text(header)
        for token in [
            "ITeamPagePositionLog",
            "ITeamPageChatLog",
            "ITeamPageGpsTrackLoader",
            "ITeamPageUnreadPublisher",
            "TeamPageActivityState",
            "TeamPageActivityContext",
            "TeamPageActivitySink",
            "consumePosition",
            "consumeWaypoint",
            "consumeTrack",
            "consumeChat",
            "TeamPageActivityStoreAdapter",
            "TeamPageGpsTrackLoaderAdapter",
            "TeamPageUnreadPublisherAdapter",
        ]:
            if token not in text:
                failures += fail(f"Team Page activity sink header missing token: {token}")

    if exists(source):
        text = read_text(source)
        for token in [
            "decodeTeamPositionMessage",
            "teamWaypointHasLocation",
            "decodeTeamTrackMessage",
            "appendMemberTrack",
            "memberTrackPath",
            "loadTrackFile",
            "appendStructuredChat",
            "decodeTeamChatLocation",
            "publishTeamUnread",
            "std::numeric_limits<uint32_t>::max()",
        ]:
            if token not in text:
                failures += fail(f"Team Page activity sink source missing token: {token}")

    if exists(adapters):
        text = read_text(adapters)
        for token in [
            "team_ui_posring_append",
            "team_ui_append_member_track",
            "team_ui_get_member_track_path",
            "team_ui_chatlog_append_structured",
            "sys::EventBus::publish",
            "sys::ChatUnreadChangedEvent",
        ]:
            if token not in text:
                failures += fail(f"Team Page activity adapter source missing token: {token}")
        for token in [
            "g_gps_state",
            "gps_tracker_load_file",
        ]:
            if token in text:
                failures += fail(f"Team Page activity adapter still owns GPS map legacy token: {token}")

    if exists(controller):
        text = strip_cpp_comments(read_text(controller))
        for token in [
            "team_page_activity_sink.h",
            "activity_state_from_page",
            "apply_activity_state_to_page",
            "current_activity_sink",
            "consume_activity",
            "sink.consumePosition",
            "sink.consumeWaypoint",
            "sink.consumeTrack",
            "sink.consumeChat",
        ]:
            if token not in text:
                failures += fail(f"Team page controller missing activity sink token: {token}")
        for token in [
            "team_ui_posring_append",
            "team_ui_chatlog_append_structured",
            "team_ui_append_member_track",
            "team_ui_get_member_track_path",
            "gps_tracker_load_file",
            "g_gps_state",
            "ChatUnreadChangedEvent",
            "decodeTeamPositionMessage",
            "decodeTeamTrackMessage",
            "decodeTeamChatLocation",
            "teamWaypointHasLocation",
        ]:
            if token in text:
                failures += fail(f"Team page controller still owns activity sink token: {token}")

    return failures


def check_team_page_deferred_dispatch_boundary() -> int:
    failures = 0
    header = "modules/ui_shared/include/ui/screens/team/team_page_deferred_dispatch.h"
    source = "modules/ui_shared/src/ui/screens/team/team_page_deferred_dispatch.cpp"
    adapters = "modules/ui_shared/src/ui/screens/team/team_page_deferred_dispatch_adapters.cpp"
    test = "modules/ui_shared/tests/test_team_page_deferred_dispatch.cpp"
    controller = "modules/ui_shared/src/ui/screens/team/team_page_components.cpp"

    for path in [header, source, adapters, test]:
        if not exists(path):
            failures += fail(f"missing Team Page deferred dispatch file: {path}")

    for path in [header, source]:
        if not exists(path):
            continue
        text = strip_cpp_comments(read_text(path))
        for token in [
            "lvgl.h",
            "lv_obj_t",
            "team_state.h",
            "TeamPageState&",
            "TeamPageState*",
            "TeamController",
            "TeamPairingService",
            "app::teamFacade",
            "app::messagingFacade",
            "team_ui_",
            "g_gps_state",
            "gps_tracker_load_file",
            "SystemNotification",
            "sys::Event",
            "EventBus",
        ]:
            if token in text:
                failures += fail(f"{path} contains forbidden Team Page deferred dispatch token: {token}")

    if exists(header):
        text = read_text(header)
        for token in [
            "ITeamPageDeferredDispatchPort",
            "TeamPageDeferredDispatchConfig",
            "TeamPageDeferredDispatchState",
            "TeamPageDeferredDispatchEffects",
            "TeamPageDeferredDispatchQueue",
            "enqueueKeyDist",
            "confirmKeyDist",
            "scheduleStatusBroadcast",
            "processKeyDistRetries",
            "processStatusBroadcasts",
            "TeamPageDeferredDispatchRuntimeAdapter",
        ]:
            if token not in text:
                failures += fail(f"Team Page deferred dispatch header missing token: {token}")

    if exists(source):
        text = read_text(source)
        for token in [
            "keydist_pending_",
            "status_pending_",
            "sendKeyDistPlain",
            "sendStatusPlain",
            "TeamPageDeferredDispatchFailureKind::SendFailedDetail",
            "keydist_max_retries",
            "status_rebroadcast_interval_s",
        ]:
            if token not in text:
                failures += fail(f"Team Page deferred dispatch source missing token: {token}")

    if exists(adapters):
        text = read_text(adapters)
        for token in [
            "TeamPageDeferredDispatchRuntimeAdapter",
            "runtime_.sendKeyDistPlain",
            "runtime_.sendStatus",
            "runtime_.sendStatusPlain",
            "runtime_.lastSendError",
        ]:
            if token not in text:
                failures += fail(f"Team Page deferred dispatch adapter source missing token: {token}")

    if exists(controller):
        text = strip_cpp_comments(read_text(controller))
        for token in [
            "team_page_deferred_dispatch.h",
            "TeamPageDeferredDispatchQueue deferred_dispatch",
            "deferred_dispatch_state_from_page",
            "apply_deferred_dispatch_failures",
            "team_page_context().deferred_dispatch.enqueueKeyDist",
            "team_page_context().deferred_dispatch.confirmKeyDist",
            "team_page_context().deferred_dispatch.scheduleStatusBroadcast",
            "team_page_context().deferred_dispatch.processKeyDistRetries",
            "team_page_context().deferred_dispatch.processStatusBroadcasts",
            "TeamPageDeferredDispatchRuntimeAdapter",
        ]:
            if token not in text:
                failures += fail(f"Team page controller missing deferred dispatch token: {token}")
        for token in [
            "KeyDistPending",
            "StatusBroadcastPending",
            "s_keydist_pending",
            "s_status_pending",
            "kKeyDistMaxRetries",
            "kKeyDistRetryIntervalSec",
            "std::remove_if",
            "it->attempts",
            "it->next_retry_s",
            "it->remaining",
        ]:
            if token in text:
                failures += fail(f"Team page controller still owns deferred dispatch token: {token}")

    return failures


def check_team_page_kick_confirm_action_boundary() -> int:
    failures = 0
    header = "modules/ui_shared/include/ui/screens/team/team_page_kick_confirm_action.h"
    source = "modules/ui_shared/src/ui/screens/team/team_page_kick_confirm_action.cpp"
    test = "modules/ui_shared/tests/test_team_page_kick_confirm_action.cpp"
    controller = "modules/ui_shared/src/ui/screens/team/team_page_components.cpp"

    for path in [header, source, test]:
        if not exists(path):
            failures += fail(f"missing Team Page kick-confirm action file: {path}")

    for path in [header, source]:
        if not exists(path):
            continue
        text = strip_cpp_comments(read_text(path))
        for token in [
            "lvgl.h",
            "lv_obj_t",
            "team_state.h",
            "TeamPageState&",
            "TeamPageState*",
            "TeamController",
            "TeamPairingService",
            "app::teamFacade",
            "app::messagingFacade",
            "team_ui_",
            "g_gps_state",
            "gps_tracker_load_file",
            "gps_tracker_overlay",
            "SystemNotification",
            "sys::Event",
            "EventBus",
            "sys::millis_now",
            "next_random_byte",
        ]:
            if token in text:
                failures += fail(f"{path} contains forbidden Team Page kick-confirm action token: {token}")

    if exists(header):
        text = read_text(header)
        for token in [
            "ITeamPageKickConfirmRandom",
            "ITeamPageKickConfirmDeferred",
            "TeamPageKickConfirmFailureAction",
            "TeamPageKickConfirmFailureKind",
            "TeamPageKickConfirmEffects",
            "TeamPageKickConfirmAction",
            "confirmKick",
            "TeamPageRuntimePort",
        ]:
            if token not in text:
                failures += fail(f"Team Page kick-confirm action header missing token: {token}")

    if exists(source):
        text = read_text(source)
        for token in [
            "TeamPageKickConfirmAction::confirmKick",
            "TeamKick kick",
            "TeamKeyDist key_dist",
            "runtime.sendKick",
            "runtime.sendKeyDist",
            "deferred.enqueueKeyDist",
            "reducer.reduceKickConfirmed",
            "runtime.setKeysFromPsk",
            "runtime.sendStatus",
            "runtime.sendStatusPlain",
            "fillStatusMembers",
        ]:
            if token not in text:
                failures += fail(f"Team Page kick-confirm action source missing token: {token}")

    if exists(controller):
        text = strip_cpp_comments(read_text(controller))
        for token in [
            "team_page_kick_confirm_action.h",
            "TeamPageRandomByteAdapter",
            "TeamPageKickConfirmDeferredAdapter",
            "apply_kick_confirm_failures",
            "TeamPageKickConfirmAction().confirmKick",
            "apply_command_state_to_page(state)",
        ]:
            if token not in text:
                failures += fail(f"Team page controller missing kick-confirm action token: {token}")
        for token in [
            "TeamKick kick",
            "TeamKeyDist kd",
            "TeamKeyDist key_dist",
            "sendKeyDist(kd",
            "sendKeyDist(key_dist",
            "new_psk",
            "reducer.reduceKickConfirmed",
            "runtime.sendKick",
        ]:
            if token in text:
                failures += fail(f"Team page controller still owns kick-confirm action token: {token}")
        if "handle_kick_confirm" in text:
            match = re.search(
                r"void\s+handle_kick_confirm\s*\([^)]*\)\s*\{(?P<body>.*?)\n\}",
                text,
                re.DOTALL,
            )
            if match:
                body = match.group("body")
                for token in [
                    "for (const auto&",
                    "sendStatus(",
                    "sendStatusPlain(",
                    "setKeysFromPsk(",
                    "TeamPageKickRotation",
                    "fill_status_members",
                    "add_keydist_pending",
                ]:
                    if token in body:
                        failures += fail(f"handle_kick_confirm still owns kick-confirm orchestration token: {token}")

    return failures


def check_team_page_transfer_leader_action_boundary() -> int:
    failures = 0
    header = "modules/ui_shared/include/ui/screens/team/team_page_transfer_leader_action.h"
    source = "modules/ui_shared/src/ui/screens/team/team_page_transfer_leader_action.cpp"
    test = "modules/ui_shared/tests/test_team_page_transfer_leader_action.cpp"
    controller = "modules/ui_shared/src/ui/screens/team/team_page_components.cpp"

    for path in [header, source, test]:
        if not exists(path):
            failures += fail(f"missing Team Page transfer-leader action file: {path}")

    for path in [header, source]:
        if not exists(path):
            continue
        text = strip_cpp_comments(read_text(path))
        for token in [
            "lvgl.h",
            "lv_obj_t",
            "team_state.h",
            "TeamPageState&",
            "TeamPageState*",
            "TeamController",
            "TeamPairingService",
            "app::teamFacade",
            "app::messagingFacade",
            "team_ui_",
            "g_gps_state",
            "gps_tracker_load_file",
            "gps_tracker_overlay",
            "SystemNotification",
            "sys::Event",
            "EventBus",
        ]:
            if token in text:
                failures += fail(f"{path} contains forbidden Team Page transfer-leader action token: {token}")

    if exists(header):
        text = read_text(header)
        for token in [
            "TeamPageTransferLeaderFailureAction",
            "TeamPageTransferLeaderFailureKind",
            "TeamPageTransferLeaderEffects",
            "TeamPageTransferLeaderAction",
            "transferLeader",
            "TeamPageRuntimePort",
            "TeamPageKeyEventLog",
        ]:
            if token not in text:
                failures += fail(f"Team Page transfer-leader action header missing token: {token}")

    if exists(source):
        text = read_text(source)
        for token in [
            "TeamPageTransferLeaderAction::transferLeader",
            "TeamTransferLeader transfer",
            "runtime.sendTransferLeader",
            "reducer.reduceTransferLeader",
            "key_log.appendLeaderTransferred",
        ]:
            if token not in text:
                failures += fail(f"Team Page transfer-leader action source missing token: {token}")

    if exists(controller):
        text = strip_cpp_comments(read_text(controller))
        for token in [
            "team_page_transfer_leader_action.h",
            "apply_transfer_leader_failures",
            "TeamPageTransferLeaderAction().transferLeader",
            "apply_command_state_to_page(command_state)",
            "apply_key_event_state_to_page(key_state)",
        ]:
            if token not in text:
                failures += fail(f"Team page controller missing transfer-leader action token: {token}")
        if "handle_transfer_leader" in text:
            match = re.search(
                r"void\s+handle_transfer_leader\s*\([^)]*\)\s*\{(?P<body>.*?)\n\}",
                text,
                re.DOTALL,
            )
            if match:
                body = match.group("body")
                for token in [
                    "TeamTransferLeader transfer",
                    "sendTransferLeader",
                    "reduceTransferLeader",
                    "appendLeaderTransferred",
                    "runtime.hasController",
                ]:
                    if token in body:
                        failures += fail(f"handle_transfer_leader still owns transfer-leader orchestration token: {token}")

    return failures


def check_team_page_state_store_boundary() -> int:
    failures = 0
    header = "modules/ui_shared/include/ui/screens/team/team_page_state_store.h"
    source = "modules/ui_shared/src/ui/screens/team/team_page_state_store.cpp"
    test = "modules/ui_shared/tests/test_team_page_state_store.cpp"
    controller = "modules/ui_shared/src/ui/screens/team/team_page_components.cpp"

    for path in [header, source, test]:
        if not exists(path):
            failures += fail(f"missing Team Page state store file: {path}")

    for path in [header, source]:
        if not exists(path):
            continue
        text = strip_cpp_comments(read_text(path))
        for token in [
            "lvgl.h",
            "lv_obj_t",
            "team_state.h",
            "TeamPageState&",
            "TeamPageState*",
            "TeamController",
            "TeamPairingService",
            "app::teamFacade",
            "app::messagingFacade",
            "team_ui_get_store",
            "team_ui_snapshot_store()",
            "gps_runtime",
            "gps_tracker_overlay",
            "sys::Event",
        ]:
            if token in text:
                failures += fail(f"{path} contains forbidden Team Page state store token: {token}")

    if exists(header):
        text = read_text(header)
        for token in [
            "TeamPagePersistentState",
            "TeamPageColorContext",
            "TeamPageStateStore",
            "loadOnce",
            "refresh",
            "resetLoaded",
            "snapshotFromState",
            "applySnapshot",
            "assignTeamPageMemberColors",
            "ITeamUiSnapshotStore",
        ]:
            if token not in text:
                failures += fail(f"Team Page state store header missing token: {token}")

    if exists(source):
        text = read_text(source)
        for token in [
            "team_color_index_from_node_id",
            "loaded_ = true",
            "store.load(snapshot)",
            "store.save(snapshotFromState(state))",
            "assignTeamPageMemberColors",
        ]:
            if token not in text:
                failures += fail(f"Team Page state store source missing token: {token}")

    if exists(controller):
        text = strip_cpp_comments(read_text(controller))
        for token in [
            "team_page_state_store.h",
            "TeamPageStateStore state_store",
            "persistent_state_from_page",
            "apply_persistent_state_to_page",
            "current_color_context",
            "team_page_context().state_store.loadOnce",
            "team_page_context().state_store.refresh",
            "team_page_context().state_store.save",
            "team_page_context().state_store.resetLoaded",
        ]:
            if token not in text:
                failures += fail(f"Team page controller missing state store token: {token}")
        for token in [
            "s_state_loaded",
            "snapshot_from_state",
            "apply_snapshot(",
            "ensure_member_colors",
            "team_ui_snapshot_store().load",
            "team_ui_snapshot_store().save",
        ]:
            if token in text:
                failures += fail(f"Team page controller still owns state store token: {token}")

    return failures


def check_chat_ui_legacy_burndown() -> int:
    failures = 0
    header = "modules/ui_shared/include/ui/screens/chat/chat_ui_controller.h"
    source = "modules/ui_shared/src/ui/screens/chat/chat_ui_controller.cpp"

    controller_forbidden = [
        "encodeTeamChatLocation",
        "encodeTeamChatCommand",
        "TeamChatMessage",
        "setKeysFromPsk",
        "team_ui_chatlog_append_structured",
        "platform::ui::gps::get_data",
        "app::messagingFacade().getMeshAdapter()",
        "submitKeyVerificationNumber",
        "setNodeKeyManuallyVerified",
        "ChatDeliveryReadModel",
        "ChatDeliveryEventProjector",
        "ChatDeliveryActionService",
    ]

    for path in [header, source]:
        if not exists(path):
            continue
        text = strip_cpp_comments(read_text(path))
        for token in controller_forbidden:
            if token in text:
                failures += fail(
                    f"{path} contains forbidden Phase 7.6 legacy ownership token: {token}"
                )

    if exists(source):
        text = strip_cpp_comments(read_text(source))
        required = [
            "KeyVerificationModalCallbacks",
            "key_verify_modal_",
            "submitKeyVerificationInput",
            "key_verification_model_->submitNumber",
            "key_verification_model_->accept",
        ]
        for token in required:
            if token not in text:
                failures += fail(f"ChatUiController missing burn-down token: {token}")

    renderer_header = "modules/ui_shared/include/ui/screens/chat/key_verification_modal_renderer.h"
    renderer_source = "modules/ui_shared/src/ui/screens/chat/key_verification_modal_renderer.cpp"
    for path in [renderer_header, renderer_source]:
        if not exists(path):
            failures += fail(f"missing key verification modal renderer file: {path}")
            continue
        text = strip_cpp_comments(read_text(path))
        for token in [
            "ContactService",
            "IMeshAdapter",
            "LegacyKeyVerificationSession",
            "ChatService",
            "ChatWorkspaceModel",
            "MessageRow",
            "submitKeyVerificationNumber",
            "setNodeKeyManuallyVerified",
            "accept(",
            "reject(",
            "refresh(",
            "submitNumber",
        ]:
            if token in text:
                failures += fail(
                    f"{path} contains forbidden modal renderer ownership token: {token}"
                )
    if exists(renderer_header):
        text = read_text(renderer_header)
        for token in [
            "KeyVerificationModalRefs",
            "KeyVerificationModalCallbacks",
            "renderKeyVerificationModal",
            "destroyKeyVerificationModal",
            "clearKeyVerificationError",
        ]:
            if token not in text:
                failures += fail(f"key verification modal renderer missing token: {token}")

    return failures


def check_team_rich_payload_presentation_boundary() -> int:
    failures = 0
    controller = "modules/ui_shared/src/ui/screens/chat/chat_ui_controller.cpp"
    presentation = "modules/ui_shared/src/ui/presentation_sources/team_chat_presentation_source.cpp"
    message_row_header = "modules/ui_presentation/include/ui_presentation/chat/chat_workspace_snapshot.h"
    chat_renderer = "modules/ui_shared/src/ui/screens/chat/chat_conversation_components.cpp"
    watch_renderer = "modules/ui_shared/src/ui/screens/chat_watch/chat_conversation_components_watch.cpp"
    display_header = "modules/ui_shared/include/ui/team_presentation/team_rich_payload_display.h"
    projector_header = "modules/ui_shared/include/ui/team_presentation/team_rich_payload_projector.h"
    projector_source = "modules/ui_shared/src/ui/team_presentation/team_rich_payload_projector.cpp"

    if exists(message_row_header):
        text = strip_cpp_comments(read_text(message_row_header))
        for token in [
            "enum class TeamMessageRichPayloadKind",
            "TeamMessageLocationPayload",
            "TeamMessageCommandPayload",
            "TeamMessageRichPayload",
            "has_team_rich_payload",
            "team_rich_payload",
            "resetMessageRow",
        ]:
            if token not in text:
                failures += fail(f"MessageRow missing structured Team rich payload token: {token}")
        for token in ["lvgl.h", "TeamRichPayloadDisplay", "TeamChatLogEntry", "team/protocol"]:
            if token in text:
                failures += fail(f"MessageRow owns forbidden Team rich dependency token: {token}")

    if exists(display_header):
        text = read_text(display_header)
        for token in [
            "enum class TeamRichPayloadKind",
            "Text",
            "Location",
            "Command",
            "Unsupported",
            "enum class TeamCommandDisplayKind",
            "MoveTo",
            "RallyPoint",
            "Hold",
            "TeamLocationDisplay",
            "TeamCommandDisplay",
            "TeamRichPayloadDisplay",
            "summary",
            "badge",
        ]:
            if token not in text:
                failures += fail(f"TeamRichPayloadDisplay missing token: {token}")
        for token in [
            "lvgl.h",
            "lv_obj_t",
            "ChatWorkspaceModel",
            "ChatUiController",
            "TeamChatMessage",
            "sendTeamAction",
        ]:
            if token in strip_cpp_comments(text):
                failures += fail(
                    f"TeamRichPayloadDisplay contains forbidden ownership token: {token}"
                )

    if exists(projector_header):
        text = read_text(projector_header)
        for token in ["TeamRichPayloadProjector", "project", "TeamRichPayloadDisplay"]:
            if token not in text:
                failures += fail(f"TeamRichPayloadProjector header missing token: {token}")

    if exists(projector_source):
        text = strip_cpp_comments(read_text(projector_source))
        for token in [
            "TeamRichPayloadProjector::project",
            "decodeTeamChatLocation",
            "decodeTeamChatCommand",
            "TeamRichPayloadKind::Location",
            "TeamRichPayloadKind::Command",
            "TeamRichPayloadKind::Unsupported",
            "TeamCommandDisplayKind::MoveTo",
            "TeamCommandDisplayKind::Hold",
            "team_location_marker_icon_name",
        ]:
            if token not in text:
                failures += fail(f"TeamRichPayloadProjector source missing token: {token}")
        for token in [
            "lvgl.h",
            "lv_obj_t",
            "ChatUiController",
            "ChatWorkspaceModel",
            "TeamActionRuntimeSink",
            "sendTeamAction",
            "encodeTeamChatLocation",
            "encodeTeamChatCommand",
            "team_ui_chatlog_append_structured",
            "buildChatWorkspaceSnapshot",
            "#include <string>",
            "#include <vector>",
            "#include <iostream>",
            "#include <sstream>",
            "#include <format>",
            "std::string",
            "std::vector",
            "std::format",
            "std::stringstream",
            "std::ostringstream",
        ]:
            if token in text:
                failures += fail(
                    f"TeamRichPayloadProjector owns forbidden token: {token}"
                )

    if exists(presentation):
        text = strip_cpp_comments(read_text(presentation))
        for token in [
            "TeamRichPayloadProjector",
            "TeamRichPayloadDisplay",
            "display.summary",
            "copyRichPayload",
            "has_team_rich_payload",
            "team_rich_payload",
            "TeamMessageRichPayloadKind",
        ]:
            if token not in text:
                failures += fail(
                    f"TeamChatPresentationSource missing Team rich projection token: {token}"
                )
        for token in [
            "formatTeamChatEntry",
            "teamCommandName",
            "decodeTeamChatLocation",
            "decodeTeamChatCommand",
            "TeamChatLocation",
            "TeamChatCommand",
        ]:
            if token in text:
                failures += fail(
                    f"TeamChatPresentationSource still owns inline rich payload formatter token: {token}"
                )

    for path in [chat_renderer, watch_renderer]:
        if exists(path):
            text = strip_cpp_comments(read_text(path))
            for token in [
                "has_team_rich_payload",
                "team_rich_payload",
                "format_team_rich_payload_text",
            ]:
                if token not in text:
                    failures += fail(f"{path} missing structured Team rich render token: {token}")

    if exists(controller):
        text = strip_cpp_comments(read_text(controller))
        for token in [
            "format_team_chat_entry",
            "team_command_name",
            "decodeTeamChatLocation",
            "decodeTeamChatCommand",
            "TeamChatLocation",
            "TeamChatCommand",
        ]:
            if token in text:
                failures += fail(
                    f"ChatUiController still owns Team rich payload display token: {token}"
                )

    return failures


def check_team_position_picker_renderer_boundary() -> int:
    failures = 0
    header = "modules/ui_shared/include/ui/screens/chat/chat_ui_controller.h"
    source = "modules/ui_shared/src/ui/screens/chat/chat_ui_controller.cpp"
    renderer_header = "modules/ui_shared/include/ui/screens/chat/team_position_picker_renderer.h"
    renderer_source = "modules/ui_shared/src/ui/screens/chat/team_position_picker_renderer.cpp"

    controller_forbidden = [
        "team_position_picker_overlay_",
        "team_position_picker_panel_",
        "team_position_picker_desc_",
        "team_position_picker_group_",
        "team_position_prev_group_",
        "team_position_icon_ctxs_",
        "TeamPositionIconEventCtx",
        "team_position_icon_event_cb",
        "team_position_cancel_event_cb",
        "lv_obj_create(parent_)",
    ]

    for path in [header, source]:
        if not exists(path):
            continue
        text = strip_cpp_comments(read_text(path))
        for token in controller_forbidden:
            if token in text:
                failures += fail(
                    f"{path} still owns Team position picker widget token: {token}"
                )

    if exists(header):
        text = strip_cpp_comments(read_text(header))
        for token in [
            "TeamPositionPickerRenderer",
            "team_position_picker_",
            "openTeamPositionPicker",
            "closeTeamPositionPicker",
            "updateTeamPositionPickerHint",
            "onTeamPositionIconSelected",
            "onTeamPositionCancel",
            "isTeamPositionPickerOpen",
        ]:
            if token not in text:
                failures += fail(f"ChatUiController missing Team picker workflow token: {token}")

    if exists(source):
        text = strip_cpp_comments(read_text(source))
        for token in [
            "TeamPositionPickerRenderer::Callbacks",
            "team_position_picker_->open()",
            "team_position_picker_->close(restore_group)",
            "team_position_picker_->updateHint(icon_id)",
            "team_position_picker_->isOpen()",
            "team_workflow_.sendCurrentLocationMarker",
        ]:
            if token not in text:
                failures += fail(f"ChatUiController missing Team picker delegation token: {token}")

    for path in [renderer_header, renderer_source]:
        if not exists(path):
            failures += fail(f"missing Team position picker renderer file: {path}")
            continue
        text = strip_cpp_comments(read_text(path))
        for token in [
            "ChatService",
            "TeamUiStore",
            "ITeamActionSink",
            "TeamActionRequest",
            "sendTeamAction",
            "encodeTeamChatLocation",
            "decodeTeamChatLocation",
            "platform::ui::gps::get_data",
            "ChatWorkspaceModel",
            "team_ui_chatlog_append_structured",
        ]:
            if token in text:
                failures += fail(
                    f"{path} contains forbidden Team picker renderer token: {token}"
                )

    if exists(renderer_header):
        text = read_text(renderer_header)
        for token in [
            "TeamPositionPickerRenderer",
            "struct Callbacks",
            "on_icon_selected",
            "on_cancel",
            "bool open()",
            "void close(bool restore_group)",
            "bool isOpen() const",
            "void updateHint(uint8_t icon_id)",
            "IconEventCtx",
            "iconEventCb",
            "cancelEventCb",
        ]:
            if token not in text:
                failures += fail(f"TeamPositionPickerRenderer header missing token: {token}")

    if exists(renderer_source):
        text = strip_cpp_comments(read_text(renderer_source))
        for token in [
            "TeamPositionPickerRenderer::open",
            "TeamPositionPickerRenderer::close",
            "TeamPositionPickerRenderer::updateHint",
            "TeamPositionPickerRenderer::iconEventCb",
            "TeamPositionPickerRenderer::cancelEventCb",
            "lv_obj_create",
            "lv_group_create",
            "set_default_group",
            "callbacks_.on_icon_selected",
            "callbacks_.on_cancel",
        ]:
            if token not in text:
                failures += fail(f"TeamPositionPickerRenderer source missing token: {token}")

    return failures


def check_map_tile_source_cache_boundary() -> int:
    failures = 0

    types_header = "modules/ui_shared/include/ui/map_tiles/map_tile_types.h"
    source_header = "modules/ui_shared/include/ui/map_tiles/map_tile_source.h"
    cache_header = "modules/ui_shared/include/ui/map_tiles/map_tile_cache.h"
    resolver_header = "modules/ui_shared/include/ui/map_tiles/map_tile_resolver.h"
    resolver_source = "modules/ui_shared/src/ui/map_tiles/map_tile_resolver.cpp"
    filesystem_header = "modules/ui_map_runtime/include/ui_map_runtime/map_tiles/filesystem_map_tile_source.h"
    filesystem_source = "modules/ui_map_runtime/src/map_tiles/filesystem_map_tile_source.cpp"
    viewport_source = "modules/ui_shared/src/ui/widgets/map/map_viewport.cpp"
    snapshot_header = "modules/ui_presentation/include/ui_presentation/map/map_workspace_snapshot.h"
    esp_tiles = "platform/esp/arduino_common/src/ui/widgets/map/map_tiles.cpp"
    linux_tiles = "platform/linux/common/src/ui/widgets/map/map_tiles.cpp"
    runtime_register = "docs/audits/PHASE7_RUNTIME_OWNERSHIP_REGISTER.md"
    burndown_register = "docs/audits/LEGACY_BURNDOWN_REGISTER.md"

    if exists(types_header):
        text = read_text(types_header)
        for token in [
            "enum class MapTileLayer",
            "Osm",
            "Terrain",
            "Satellite",
            "ContourMajor500",
            "ContourMajor25",
            "enum class MapTileFormat",
            "enum class MapTileStatus",
            "struct MapTileRef",
            "struct MapTilePayload",
            "struct MapTileLookupResult",
            "mapTileLayerFromBaseSource",
            "mapTileContourLayerForZoom",
            "mapTileFormatForLayer",
            "mapTileLayerIsContour",
        ]:
            if token not in text:
                failures += fail(f"Map tile types missing token: {token}")
        for token in ["lvgl.h", "lv_obj_t", "std::string", "std::vector", "filesystem"]:
            if token in strip_cpp_comments(text):
                failures += fail(f"Map tile types contain forbidden dependency token: {token}")

    if exists(source_header):
        text = read_text(source_header)
        for token in [
            "class IMapTileSource",
            "lookup",
            "read",
            "class IMapTileFileSystem",
            "exists",
            "isDirectory",
            "readFile",
        ]:
            if token not in text:
                failures += fail(f"Map tile source header missing token: {token}")
        for token in ["lvgl.h", "lv_obj_t", "std::filesystem", "FILE*", "SD.open", "LittleFS", "SPIFFS"]:
            if token in strip_cpp_comments(text):
                failures += fail(f"Map tile source header contains forbidden token: {token}")

    if exists(cache_header):
        text = read_text(cache_header)
        for token in ["class IMapTileCache", "clear", "has"]:
            if token not in text:
                failures += fail(f"Map tile cache header missing token: {token}")
        for token in ["lvgl.h", "lv_obj_t", "MapWorkspaceSnapshot", "std::vector", "filesystem"]:
            if token in strip_cpp_comments(text):
                failures += fail(f"Map tile cache header contains forbidden token: {token}")

    if exists(resolver_header):
        text = read_text(resolver_header)
        for token in ["class MapTileResolver", "setRootPrefix", "resolvePath", "resolveDirectory"]:
            if token not in text:
                failures += fail(f"MapTileResolver header missing token: {token}")

    if exists(resolver_source):
        text = strip_cpp_comments(read_text(resolver_source))
        for token in [
            "maps/base",
            "maps/contour",
            "major-500",
            "major-200",
            "major-100",
            "major-50",
            "major-25",
            "MapTileResolver::resolvePath",
            "MapTileResolver::resolveDirectory",
            "std::snprintf",
        ]:
            if token not in text:
                failures += fail(f"MapTileResolver source missing token: {token}")
        for token in ["lvgl.h", "lv_obj_t", "std::filesystem", "FILE*", "fopen", "SD.open", "LittleFS", "SPIFFS"]:
            if token in text:
                failures += fail(f"MapTileResolver owns forbidden storage/render token: {token}")

    if exists(filesystem_header):
        text = read_text(filesystem_header)
        for token in [
            "FilesystemMapTileSource",
            "IMapTileSource",
            "MapTileResolver",
            "lookup",
            "read",
            "resolvePath",
            "resolveDirectory",
            "layerDirectoryAvailable",
            "anyContourDirectoryAvailable",
        ]:
            if token not in text:
                failures += fail(f"FilesystemMapTileSource header missing token: {token}")
        if "LegacyFilesystemMapTileSource" in text:
            failures += fail("FilesystemMapTileSource header still exposes LegacyFilesystemMapTileSource")

    if exists(filesystem_source):
        text = strip_cpp_comments(read_text(filesystem_source))
        for token in [
            "MapTileStatus::Available",
            "MapTileStatus::Missing",
            "file_system_.readFile",
            "file_system_.isDirectory",
            "MapTileLayer::ContourMajor500",
            "MapTileLayer::ContourMajor25",
        ]:
            if token not in text:
                failures += fail(f"FilesystemMapTileSource source missing token: {token}")
        if "FilesystemMapTileSource::lookup" not in text:
            failures += fail("FilesystemMapTileSource source missing lookup implementation token")
        if "FilesystemMapTileSource::read" not in text:
            failures += fail("FilesystemMapTileSource source missing read implementation token")
        if "LegacyFilesystemMapTileSource" in text:
            failures += fail("FilesystemMapTileSource source still references LegacyFilesystemMapTileSource")
        for token in ["lvgl.h", "lv_obj_t", "std::filesystem", "FILE*", "fopen", "SD.open", "LittleFS", "SPIFFS"]:
            if token in text:
                failures += fail(f"FilesystemMapTileSource owns forbidden platform token: {token}")

    if exists(snapshot_header):
        text = strip_cpp_comments(read_text(snapshot_header))
        for token in [
            "MapTilePayload",
            "IMapTileSource",
            "IMapTileCache",
            "lv_image_dsc_t",
            "bitmap",
            "uint8_t*",
            "std::vector<MapTile",
            "filesystem",
            "path",
        ]:
            if token in text:
                failures += fail(f"MapWorkspaceSnapshot owns forbidden tile cache/payload token: {token}")

    if exists(viewport_source):
        text = strip_cpp_comments(read_text(viewport_source))
        for token in [
            "/maps/",
            "maps/base",
            "maps/contour",
            "major-500",
            "major-200",
            "major-100",
            "major-50",
            "major-25",
            "fopen",
            "FILE*",
            "SD.open",
            "LittleFS",
            "SPIFFS",
        ]:
            if token in text:
                failures += fail(f"Map viewport owns forbidden tile path/storage token: {token}")

    for path in [esp_tiles, linux_tiles]:
        if not exists(path):
            failures += fail(f"missing platform map tile runtime: {path}")
            continue
        text = strip_cpp_comments(read_text(path))
        for token in [
            "IMapTileFileSystem",
            "MapTileRef",
            "tile_source().resolvePath",
            "tile_source().lookup",
            "layerDirectoryAvailable",
            "anyContourDirectoryAvailable",
        ]:
            if token not in text:
                failures += fail(f"{path} missing map tile source ownership token: {token}")
        if "LegacyFilesystemMapTileSource" not in text and "FilesystemMapTileSource" not in text:
            failures += fail(f"{path} missing map tile source ownership token: FilesystemMapTileSource")
        for token in [
            "base_source_dir",
            "base_source_ext",
            "major_contour_profile_for_zoom",
            "maps/base/",
            "maps/contour/",
            "/maps/base",
            "/maps/contour",
        ]:
            if token in text:
                failures += fail(f"{path} still owns direct tile path policy token: {token}")

    if exists(runtime_register):
        text = read_text(runtime_register)
        for token in [
            "Map tile/cache ownership",
            "contained",
            "7.10",
            "MapTileResolver",
            "FilesystemMapTileSource",
            "decoded image cache remains contained legacy",
        ]:
            if token not in text:
                failures += fail(f"PHASE7_RUNTIME_OWNERSHIP_REGISTER missing map tile token: {token}")

    if exists(burndown_register):
        text = read_text(burndown_register)
        for token in [
            "Map tile path/cache legacy runtime",
            "MapTileResolver",
            "FilesystemMapTileSource",
            "ESP decoded cache",
            "Linux downloader cache",
            "uConsole path fields",
            "Renderer consumes tile refs/source without direct path/cache ownership",
        ]:
            if token not in text:
                failures += fail(f"LEGACY_BURNDOWN_REGISTER missing map tile token: {token}")

    return failures


def check_map_tile_render_queue_cache_boundary() -> int:
    failures = 0

    queue_header = "modules/ui_shared/include/ui/map_tiles/map_tile_render_queue.h"
    queue_source = "modules/ui_shared/src/ui/map_tiles/map_tile_render_queue.cpp"
    decoder_header = "modules/ui_shared/include/ui/map_tiles/map_tile_decoder_cache.h"
    viewport_source = "modules/ui_shared/src/ui/widgets/map/map_viewport.cpp"
    snapshot_header = "modules/ui_presentation/include/ui_presentation/map/map_workspace_snapshot.h"
    esp_tiles = "platform/esp/arduino_common/src/ui/widgets/map/map_tiles.cpp"
    esp_header = "platform/esp/arduino_common/include/ui/widgets/map/map_tiles.h"
    linux_tiles = "platform/linux/common/src/ui/widgets/map/map_tiles.cpp"
    linux_header = "platform/linux/common/include/ui/widgets/map/map_tiles.h"
    runtime_register = "docs/audits/PHASE7_RUNTIME_OWNERSHIP_REGISTER.md"
    burndown_register = "docs/audits/LEGACY_BURNDOWN_REGISTER.md"

    if exists(queue_header):
        text = strip_cpp_comments(read_text(queue_header))
        for token in [
            "struct MapTileScreenRect",
            "enum class MapTileRenderState",
            "Missing",
            "Loading",
            "Ready",
            "struct MapTileRenderRef",
            "class MapTileRenderQueue",
            "kMaxTiles",
            "void clear()",
            "bool push",
            "std::size_t size() const",
            "const MapTileRenderRef* items() const",
            "MapTileRenderRef items_[kMaxTiles]",
        ]:
            if token not in text:
                failures += fail(f"MapTileRenderQueue header missing token: {token}")
        for token in ["std::vector", "std::string", "lvgl.h", "lv_obj_t", "filesystem", "FILE*"]:
            if token in text:
                failures += fail(f"MapTileRenderQueue owns forbidden dependency token: {token}")

    if exists(queue_source):
        text = strip_cpp_comments(read_text(queue_source))
        for token in [
            "MapTileRenderQueue::clear",
            "MapTileRenderQueue::push",
            "MapTileRenderQueue::size",
            "MapTileRenderQueue::items",
        ]:
            if token not in text:
                failures += fail(f"MapTileRenderQueue source missing token: {token}")
        for token in ["std::vector", "std::string", "lvgl.h", "lv_obj_t", "filesystem", "FILE*"]:
            if token in text:
                failures += fail(f"MapTileRenderQueue source owns forbidden dependency token: {token}")

    if exists(decoder_header):
        text = strip_cpp_comments(read_text(decoder_header))
        for token in ["class IMapTileDecoderCache", "clear", "hasDecoded"]:
            if token not in text:
                failures += fail(f"IMapTileDecoderCache missing token: {token}")
        for token in ["lvgl.h", "lv_obj_t", "lv_image_dsc_t", "filesystem", "FILE*", "MapWorkspaceSnapshot"]:
            if token in text:
                failures += fail(f"IMapTileDecoderCache exposes forbidden token: {token}")

    for path in [esp_header, linux_header]:
        if not exists(path):
            continue
        text = strip_cpp_comments(read_text(path))
        for token in ["map_tile_render_queue.h", "MapTileRenderQueue* render_queue"]:
            if token not in text:
                failures += fail(f"{path} missing render queue context token: {token}")

    for path in [esp_tiles, linux_tiles]:
        if not exists(path):
            continue
        text = strip_cpp_comments(read_text(path))
        for token in [
            "rebuild_render_queue",
            "MapTileRenderRef",
            "MapTileRenderState::Ready",
            "MapTileRenderState::Missing",
            "MapTileRenderState::Loading",
            "ctx.render_queue->clear()",
            "ctx.render_queue->push(item)",
        ]:
            if token not in text:
                failures += fail(f"{path} missing render queue projection token: {token}")

    if exists(esp_tiles):
        text = strip_cpp_comments(read_text(esp_tiles))
        for token in [
            "class LvglDecodedTileCache",
            "IMapTileDecoderCache",
            "hasDecoded",
            "acquireSlot",
            "releaseUsage",
            "decoded_tile_cache()",
            "decoded_tile_cache().clear()",
            "decoded_tile_cache().find",
            "decoded_tile_cache().acquireSlot",
        ]:
            if token not in text:
                failures += fail(f"ESP map tiles missing decoded cache owner token: {token}")
        for token in ["g_tile_decode_cache", "g_tile_cache_initialized", "TILE_DECODE_CACHE_SIZE"]:
            if token in text:
                failures += fail(f"ESP map tiles still owns loose decoded cache global token: {token}")

    if exists(viewport_source):
        text = strip_cpp_comments(read_text(viewport_source))
        for token in ["MapTileRenderQueue render_queue", "&impl->render_queue"]:
            if token not in text:
                failures += fail(f"Map viewport missing render queue runtime token: {token}")
        for token in ["lv_image_dsc_t", "DecodedTileCache", "IMapTileDecoderCache", "LvglDecodedTileCache"]:
            if token in text:
                failures += fail(f"Map viewport owns decoded cache token: {token}")

    if exists(snapshot_header):
        text = strip_cpp_comments(read_text(snapshot_header))
        for token in [
            "MapTileRenderQueue",
            "MapTileRenderRef",
            "IMapTileDecoderCache",
            "LvglDecodedTileCache",
            "DecodedTileCache",
            "lv_image_dsc_t",
        ]:
            if token in text:
                failures += fail(f"MapWorkspaceSnapshot owns render queue/cache token: {token}")

    if exists(runtime_register):
        text = read_text(runtime_register)
        for token in [
            "Map tile render queue / decoded cache ownership",
            "7.11",
            "MapTileRenderQueue",
            "LvglDecodedTileCache",
            "LVGL widget records remain contained legacy",
        ]:
            if token not in text:
                failures += fail(f"PHASE7_RUNTIME_OWNERSHIP_REGISTER missing 7.11 map token: {token}")

    if exists(burndown_register):
        text = read_text(burndown_register)
        for token in [
            "Map tile visible plan in platform renderer",
            "ESP decoded LVGL tile cache",
            "MapTileRenderQueue",
            "LvglDecodedTileCache",
            "IMapTileDecoderCache",
            "Runtime-owned decoder cache is injected",
        ]:
            if token not in text:
                failures += fail(f"LEGACY_BURNDOWN_REGISTER missing 7.11 map token: {token}")

    return failures


def check_map_overlay_route_tracker_boundary() -> int:
    failures = 0

    snapshot_header = "modules/ui_presentation/include/ui_presentation/map/map_overlay_snapshot.h"
    source_header = "modules/ui_presentation/include/ui_presentation/map/map_overlay_source.h"
    projector_header = "modules/ui_shared/include/ui/map_overlay/map_overlay_projector.h"
    projector_source = "modules/ui_shared/src/ui/map_overlay/map_overlay_projector.cpp"
    legacy_header = "modules/ui_shared/include/ui/presentation_sources/legacy_map_overlay_source.h"
    legacy_source = "modules/ui_shared/src/ui/presentation_sources/legacy_map_overlay_source.cpp"
    team_source_header = "modules/ui_shared/include/ui/presentation_sources/team_map_overlay_source.h"
    team_source_source = "modules/ui_shared/src/ui/presentation_sources/team_map_overlay_source.cpp"
    projector_test = "modules/ui_shared/tests/test_map_overlay_projector.cpp"
    legacy_test = "modules/ui_shared/tests/test_legacy_map_overlay_source.cpp"
    team_source_test = "modules/ui_shared/tests/test_team_map_overlay_source.cpp"
    viewport_source = "modules/ui_shared/src/ui/widgets/map/map_viewport.cpp"
    runtime_source = "modules/ui_shared/src/ui/screens/gps/gps_page_runtime.cpp"
    esp_gps_map = "platform/esp/arduino_common/src/ui/screens/gps/gps_page_map.cpp"
    uconsole_dashboard = "platform/linux/uconsole/src/uconsole_dashboard_model.cpp"
    runtime_register = "docs/audits/PHASE7_RUNTIME_OWNERSHIP_REGISTER.md"
    burndown_register = "docs/audits/LEGACY_BURNDOWN_REGISTER.md"

    if exists(snapshot_header):
        text = strip_cpp_comments(read_text(snapshot_header))
        for token in [
            "enum class MapOverlayKind",
            "CurrentPosition",
            "TeamMember",
            "RoutePoint",
            "TrackPoint",
            "MeasurementPoint",
            "SelectedTarget",
            "Warning",
            "enum class MapOverlayStyle",
            "struct MapGeoPoint",
            "struct MapOverlayItem",
            "FixedText<32> label",
            "FixedText<64> detail",
            "uint32_t stable_id",
            "struct MapOverlaySnapshot",
            "kMaxItems",
            "MapOverlayItem items[kMaxItems]",
            "bool truncated",
        ]:
            if token not in text:
                failures += fail(f"MapOverlaySnapshot missing token: {token}")
        for token in [
            "lvgl.h",
            "lv_obj_t",
            "MapTilePayload",
            "IMapTileSource",
            "IMapTileCache",
            "bitmap",
            "filesystem",
            "path",
            "GpsStatusSource",
            "ITeamUiStore",
            "TeamUiStore",
            "route store",
        ]:
            if token in text:
                failures += fail(f"MapOverlaySnapshot owns forbidden runtime/render token: {token}")

    if exists(source_header):
        text = strip_cpp_comments(read_text(source_header))
        for token in ["IMapOverlayPresentationSource", "buildMapOverlaySnapshot"]:
            if token not in text:
                failures += fail(f"Map overlay source header missing token: {token}")
        for token in ["lvgl.h", "platform::ui::gps", "team_ui_chatlog_load_recent", "ITeamUiStore", "filesystem"]:
            if token in text:
                failures += fail(f"Map overlay source port exposes forbidden token: {token}")

    for path in [projector_header, projector_source]:
        if exists(path):
            text = strip_cpp_comments(read_text(path))
            for token in [
                "platform::ui::gps::get_data",
                "team_ui_chatlog_load_recent",
                "team_ui_posring_load_latest",
                "decodeTeamChatLocation",
                "lvgl.h",
                "lv_obj_t",
                "IMapTileSource",
                "filesystem",
                "fopen",
            ]:
                if token in text:
                    failures += fail(f"{path} contains forbidden overlay projector token: {token}")

    if exists(projector_header):
        text = read_text(projector_header)
        for token in ["MapOverlayProjector", "projectCurrentPosition", "projectTeamMember"]:
            if token not in text:
                failures += fail(f"MapOverlayProjector header missing token: {token}")

    if exists(projector_source):
        text = read_text(projector_source)
        for token in [
            "MapOverlayProjector::projectCurrentPosition",
            "MapOverlayKind::CurrentPosition",
            "MapOverlayStyle::OwnPosition",
            "MapOverlayProjector::projectTeamMember",
            "MapOverlayKind::TeamMember",
            "MapOverlayStyle::Team",
        ]:
            if token not in text:
                failures += fail(f"MapOverlayProjector source missing token: {token}")

    if exists(legacy_header):
        failures += fail(f"LegacyMapOverlaySource header must stay absent: {legacy_header}")

    if exists(legacy_source):
        text = strip_cpp_comments(read_text(legacy_source))
        for token in [
            "MapOverlaySnapshotSource::buildMapOverlaySnapshot",
            "adapter_.project",
        ]:
            if token not in text:
                failures += fail(f"MapOverlaySnapshotSource source missing token: {token}")
        for token in ["lvgl.h", "lv_obj_t", "platform::ui::gps::get_data", "team_ui_chatlog_load_recent", "decodeTeamChatLocation", "IMapTileSource"]:
            if token in text:
                failures += fail(f"MapOverlaySnapshotSource source owns forbidden render/source token: {token}")

    if exists(team_source_header):
        text = strip_cpp_comments(read_text(team_source_header))
        for token in [
            "TeamMapOverlaySource",
            "IMapOverlayTeamSource",
            "latestTeamPoints",
            "latestTeamLocations",
            "loadMemberLocation",
            "ITeamUiSnapshotStore",
        ]:
            if token not in text:
                failures += fail(f"TeamMapOverlaySource header missing token: {token}")
        for token in ["lvgl.h", "lv_obj_t", "gps_runtime", "ChatWorkspaceModel"]:
            if token in text:
                failures += fail(f"TeamMapOverlaySource header exposes forbidden token: {token}")

    if exists(team_source_source):
        text = strip_cpp_comments(read_text(team_source_source))
        for token in [
            "TeamMapOverlaySource::latestTeamPoints",
            "TeamMapOverlaySource::latestTeamLocations",
            "TeamMapOverlaySource::loadMemberLocation",
            "team_ui_posring_load_latest",
            "locationFromSample",
            "team_color_from_index",
        ]:
            if token not in text:
                failures += fail(f"TeamMapOverlaySource source missing token: {token}")
        for token in ["lvgl.h", "lv_obj_t", "team_state.h", "g_team_state", "gps_runtime"]:
            if token in text:
                failures += fail(f"TeamMapOverlaySource source owns forbidden token: {token}")

    for path in [projector_test, legacy_test, team_source_test]:
        if exists(path):
            text = read_text(path)
            if "MapOverlay" not in text:
                failures += fail(f"{path} does not exercise map overlay boundary")

    if exists(viewport_source):
        text = strip_cpp_comments(read_text(viewport_source))
        for token in [
            "platform::ui::gps::get_data",
            "team_ui_chatlog_load_recent",
            "team_ui_posring_load_latest",
            "decodeTeamChatLocation",
            "ITeamUiStore",
            "TeamUiStore",
        ]:
            if token in text:
                failures += fail(f"Map viewport owns forbidden overlay source token: {token}")

    if exists(runtime_source):
        text = read_text(runtime_source)
        for token in [
            "MapOverlaySnapshotSource",
            "IMapOverlayGpsSource",
            "TeamMapOverlaySource",
            "buildMapOverlaySnapshot",
        ]:
            if token not in text:
                failures += fail(f"gps_page_runtime.cpp missing overlay runtime wiring token: {token}")
        for token in ["team_ui_posring_load_latest", "TeamPosSample"]:
            if token in strip_cpp_comments(text):
                failures += fail(f"gps_page_runtime.cpp still reads Team posring directly: {token}")

    for path in [esp_gps_map, uconsole_dashboard]:
        if exists(path):
            text = strip_cpp_comments(read_text(path))
            for token in ["TeamMapOverlaySource"]:
                if token not in text:
                    failures += fail(f"{path} missing TeamMapOverlaySource consumer token")
            for token in ["team_ui_posring_load_latest", "TeamPosSample"]:
                if token in text:
                    failures += fail(f"{path} still reads Team posring directly: {token}")

    if exists(runtime_register):
        text = read_text(runtime_register)
        for token in [
            "Map overlay/route/tracker ownership",
            "7.12",
            "MapOverlaySnapshot",
            "LegacyMapOverlaySource",
            "TeamMapOverlaySource",
            "route/tracker overlay sources have exit conditions",
        ]:
            if token not in text:
                failures += fail(f"PHASE7_RUNTIME_OWNERSHIP_REGISTER missing map overlay token: {token}")

    if exists(burndown_register):
        text = read_text(burndown_register)
        for token in [
            "Map overlay current/team marker projection",
            "TeamMapOverlaySource",
            "Map route/tracker overlay projection",
            "MapOverlayProjector",
            "keep runtime map overlay headers as the only build-visible API",
        ]:
            if token not in text:
                failures += fail(f"LEGACY_BURNDOWN_REGISTER missing map overlay token: {token}")

    return failures


def check_gps_runtime_scheduling_boundary() -> int:
    failures = 0

    sink_header = "modules/ui_shared/include/ui/screens/gps/gps_ui_refresh_sink.h"
    pump_header = "modules/ui_shared/include/ui/screens/gps/gps_page_runtime_pump.h"
    pump_source = "modules/ui_shared/src/ui/screens/gps/gps_page_runtime_pump.cpp"
    pump_test = "modules/ui_shared/tests/test_gps_page_runtime_pump.cpp"
    runtime_source = "modules/ui_shared/src/ui/screens/gps/gps_page_runtime.cpp"
    runtime_register = "docs/audits/PHASE7_RUNTIME_OWNERSHIP_REGISTER.md"
    burndown_register = "docs/audits/LEGACY_BURNDOWN_REGISTER.md"

    if exists(sink_header):
        text = strip_cpp_comments(read_text(sink_header))
        for token in ["class IGpsUiRefreshSink", "onGpsRuntimeUpdated"]:
            if token not in text:
                failures += fail(f"IGpsUiRefreshSink missing token: {token}")
        for token in ["platform::ui::gps", "GpsState", "GpsStatusSnapshot", "lvgl.h", "lv_obj_t"]:
            if token in text:
                failures += fail(f"IGpsUiRefreshSink exposes forbidden runtime/render token: {token}")

    if exists(pump_header):
        text = strip_cpp_comments(read_text(pump_header))
        for token in [
            "class IGpsStatusRefreshModel",
            "virtual void refresh()",
            "class GpsPageRuntimePump",
            "setActive",
            "active() const",
            "update(uint32_t now_ms)",
            "interval_ms_",
            "last_update_ms_",
            "has_last_update_",
        ]:
            if token not in text:
                failures += fail(f"GpsPageRuntimePump header missing token: {token}")
        for token in ["platform::ui::gps", "lvgl.h", "lv_obj_t", "GpsStatusSnapshot", "MapWorkspaceModel"]:
            if token in text:
                failures += fail(f"GpsPageRuntimePump header owns forbidden token: {token}")

    if exists(pump_source):
        text = strip_cpp_comments(read_text(pump_source))
        for token in [
            "GpsPageRuntimePump::setActive",
            "GpsPageRuntimePump::active",
            "GpsPageRuntimePump::update",
            "model_.refresh()",
            "ui_->onGpsRuntimeUpdated()",
            "has_last_update_",
        ]:
            if token not in text:
                failures += fail(f"GpsPageRuntimePump source missing token: {token}")
        for token in ["platform::ui::gps", "lvgl.h", "lv_obj_t", "millis()", "MapWorkspaceModel"]:
            if token in text:
                failures += fail(f"GpsPageRuntimePump source owns forbidden token: {token}")

    if exists(pump_test):
        text = read_text(pump_test)
        for token in ["GpsPageRuntimePump", "setActive(true)", "setActive(false)", "update"]:
            if token not in text:
                failures += fail(f"GPS pump test missing token: {token}")

    if exists(runtime_source):
        text = read_text(runtime_source)
        for token in [
            "GpsPageRuntimePump",
            "IGpsStatusRefreshModel",
            "IGpsUiRefreshSink",
            "gps_runtime_pump().update(sys::millis_now())",
            "gps_runtime_pump().setActive(true)",
            "gps_runtime_pump().setActive(false)",
        ]:
            if token not in text:
                failures += fail(f"gps_page_runtime.cpp missing GPS pump wiring token: {token}")

    if exists(runtime_register):
        text = read_text(runtime_register)
        for token in [
            "GPS page timers/tasks",
            "contained",
            "7.13",
            "GpsPageRuntimePump",
            "LVGL timers are tick hooks only",
        ]:
            if token not in text:
                failures += fail(f"PHASE7_RUNTIME_OWNERSHIP_REGISTER missing GPS scheduling token: {token}")

    if exists(burndown_register):
        text = read_text(burndown_register)
        for token in [
            "GPS page refresh cadence",
            "GpsPageRuntimePump",
            "Page-local adapters are replaced by GPS presentation refresh models",
        ]:
            if token not in text:
                failures += fail(f"LEGACY_BURNDOWN_REGISTER missing GPS scheduling token: {token}")

    return failures


def check_team_consumer_shadow_state_closeout() -> int:
    failures = 0

    hostlink_header = "platform/esp/arduino_common/include/platform/esp/arduino_common/hostlink/hostlink_bridge_radio.h"
    hostlink_source = "platform/esp/arduino_common/src/hostlink/hostlink_bridge_radio.cpp"
    esp_gps_map = "platform/esp/arduino_common/src/ui/screens/gps/gps_page_map.cpp"
    map_source = "modules/ui_shared/src/ui/presentation_sources/runtime_map_workspace_source.cpp"
    app_events = "platform/esp/arduino_common/src/app_event_runtime_support.cpp"
    contacts_header = "modules/ui_shared/include/ui/screens/contacts/contacts_team_snapshot_source.h"
    contacts_source = "modules/ui_shared/src/ui/screens/contacts/contacts_team_snapshot_source.cpp"
    contacts_page = "modules/ui_shared/src/ui/screens/contacts/contacts_page_components.cpp"

    if exists(hostlink_header):
        text = strip_cpp_comments(read_text(hostlink_header))
        for token in ["void on_event", "void on_team_state_changed", "void on_link_ready"]:
            if token not in text:
                failures += fail(f"Hostlink bridge header missing Team state closeout token: {token}")

    if exists(hostlink_source):
        text = strip_cpp_comments(read_text(hostlink_source))
        for token in [
            "team_ui_snapshot_store().load",
            "make_team_state_snapshot(const team::ui::TeamUiSnapshot&",
            "team_presence::isTeamMemberOnline",
            "on_team_state_changed",
            "maybe_send_team_state(false)",
            "maybe_send_team_state(true)",
        ]:
            if token not in text:
                failures += fail(f"Hostlink bridge missing Team shadow-state closeout token: {token}")
        for token in [
            "s_runtime_team_state",
            "touch_runtime_member",
            "find_runtime_member_index",
            "update_runtime_team_context",
            "ensure_runtime_team_state_loaded",
            "member.online",
        ]:
            if token in text:
                failures += fail(f"Hostlink bridge still owns Team shadow-state token: {token}")

    if exists(app_events):
        text = strip_cpp_comments(read_text(app_events))
        for token in [
            "isTeamRuntimeEvent",
            "team::ui::shell::handle_event(nullptr, event)",
            "hostlink::bridge::on_team_state_changed()",
        ]:
            if token not in text:
                failures += fail(f"ESP app event runtime missing Team state sync token: {token}")

    if exists(esp_gps_map):
        text = strip_cpp_comments(read_text(esp_gps_map))
        for token in [
            "team_ui_snapshot_store().load",
            "load_team_data",
            "ensure_member_colors",
        ]:
            if token not in text:
                failures += fail(f"ESP GPS map missing Team snapshot overlay token: {token}")
        for token in ["team_state.h", "g_team_state"]:
            if token in text:
                failures += fail(f"ESP GPS map still reads Team Page shadow state token: {token}")

    if exists(map_source):
        text = strip_cpp_comments(read_text(map_source))
        for token in [
            "team_presence::isTeamMemberOnline",
            "presenceNowForMember",
            "ITeamUiSnapshotStore",
        ]:
            if token not in text:
                failures += fail(f"Runtime map workspace source missing Team presence token: {token}")
        if "member.online" in text:
            failures += fail("Runtime map workspace source still infers liveness from member.online")

    for path in [contacts_header, contacts_source]:
        if not exists(path):
            failures += fail(f"missing Contacts Team snapshot source file: {path}")
            continue
        text = strip_cpp_comments(read_text(path))
        for token in [
            "ContactsTeamSnapshot",
            "ContactsTeamSnapshotSource",
            "ITeamUiSnapshotStore",
            "contactsTeamDisplayName",
        ]:
            if token not in text:
                failures += fail(f"{path} missing Contacts Team snapshot token: {token}")
        for token in ["team_state.h", "g_team_state", "lvgl.h", "lv_obj_t"]:
            if token in text:
                failures += fail(f"{path} contains forbidden Contacts Team snapshot token: {token}")

    if exists(contacts_page):
        text = strip_cpp_comments(read_text(contacts_page))
        for token in [
            "contacts_team_snapshot_source.h",
            "contacts_team_snapshot_source",
            "load_contacts_team_snapshot",
            "contacts_team_title",
        ]:
            if token not in text:
                failures += fail(f"Contacts page missing Team snapshot closeout token: {token}")
        for token in ["team_state.h", "g_team_state"]:
            if token in text:
                failures += fail(f"Contacts page still reads Team Page state token: {token}")

    return failures


def check_phase7_final_readiness() -> int:
    failures = 0

    runtime_register = "docs/audits/PHASE7_RUNTIME_OWNERSHIP_REGISTER.md"
    burndown_register = "docs/audits/LEGACY_BURNDOWN_REGISTER.md"
    final_report = "docs/audits/PHASE7_FINAL_RUNTIME_OWNERSHIP_READINESS_REPORT.md"
    final_matrix = "docs/audits/PHASE7_FINAL_LEGACY_SURFACE_MATRIX.md"
    map_snapshot = "modules/ui_presentation/include/ui_presentation/map/map_workspace_snapshot.h"
    chat_model_header = "modules/ui_presentation/include/ui_presentation/chat/chat_workspace_model.h"
    chat_model_source = "modules/ui_presentation/src/chat/chat_workspace_model.cpp"

    if exists(runtime_register):
        text = read_text(runtime_register)
        for token in ["future", "later", "TBD"]:
            if re.search(rf"\b{re.escape(token)}\b", text, flags=re.IGNORECASE):
                failures += fail(f"PHASE7_RUNTIME_OWNERSHIP_REGISTER contains naked deferred token: {token}")
        in_runtime_items = False
        for line in text.splitlines():
            if line.strip() == "## Runtime Ownership Items":
                in_runtime_items = True
                continue
            if in_runtime_items and line.startswith("## "):
                break
            if not in_runtime_items or not line.startswith("| ") or line.startswith("| ---") or line.startswith("| Runtime item"):
                continue
            cells = [cell.strip() for cell in line.strip().strip("|").split("|")]
            if len(cells) < 4:
                failures += fail(f"runtime ownership row has too few cells: {line}")
                continue
            status = cells[1].lower()
            for token in ["in progress", "future", "later", "tbd"]:
                if token in status:
                    failures += fail(
                        f"Runtime Ownership Items row has non-final status '{cells[1]}': {cells[0]}"
                    )

    if exists(burndown_register):
        text = read_text(burndown_register)
        for token in ["future", "later", "TBD"]:
            if re.search(rf"\b{re.escape(token)}\b", text, flags=re.IGNORECASE):
                failures += fail(f"LEGACY_BURNDOWN_REGISTER contains naked deferred token: {token}")

    if exists(final_report):
        text = read_text(final_report)
        for token in [
            "Chat delivery",
            "Chat event pump",
            "Team action sends",
            "Team rich payload display",
            "Team position picker",
            "Key verification",
            "Map tile source/cache",
            "Map render queue/cache",
            "Map overlay/route/tracker",
            "GPS runtime scheduling",
            "PHASE7_FINAL_LEGACY_SURFACE_MATRIX.md",
        ]:
            if token not in text:
                failures += fail(f"final readiness report missing token: {token}")

    if exists(final_matrix):
        text = read_text(final_matrix)
        header = "| Surface | Current owner | New owner | Status | Removal condition | Blocking reason | Future phase |"
        if header not in text:
            failures += fail("final legacy surface matrix missing required header")
        for token in [
            "Chat delivery send-result bridge",
            "Map overlay current position",
            "Map route/tracker overlay",
            "GPS page refresh cadence",
            "ACK timeout projection",
        ]:
            if token not in text:
                failures += fail(f"final legacy surface matrix missing token: {token}")
        for line in text.splitlines():
            if not line.startswith("| ") or line.startswith("| ---") or line.startswith("| Surface"):
                continue
            cells = [cell.strip() for cell in line.strip().strip("|").split("|")]
            if len(cells) < 7:
                failures += fail(f"final legacy matrix row has too few cells: {line}")
                continue
            if not cells[4]:
                failures += fail(f"final legacy matrix row missing removal condition: {line}")
            if not cells[5]:
                failures += fail(f"final legacy matrix row missing blocking reason: {line}")

    if exists(map_snapshot):
        text = strip_cpp_comments(read_text(map_snapshot))
        for token in [
            "MapTilePayload",
            "IMapTileSource",
            "IMapTileCache",
            "IMapTileDecoderCache",
            "lv_image_dsc_t",
            "bitmap",
            "cache object",
            "filesystem path",
        ]:
            if token in text:
                failures += fail(f"MapWorkspaceSnapshot owns forbidden final map token: {token}")

    for path in [chat_model_header, chat_model_source]:
        if exists(path):
            text = strip_cpp_comments(read_text(path))
            for token in [
                "ChatDeliveryReadModel",
                "KeyVerificationModel",
                "ChatDeliveryActionService",
            ]:
                if token in text:
                    failures += fail(f"{path} owns forbidden final chat runtime token: {token}")

    return failures


def check_chat_runtime_event_pump_boundary() -> int:
    failures = 0
    header = "modules/ui_shared/include/ui/screens/chat/chat_ui_controller.h"
    source = "modules/ui_shared/src/ui/screens/chat/chat_ui_controller.cpp"
    runtime = "modules/ui_shared/src/ui/screens/chat/chat_page_runtime.cpp"
    pump_header = "modules/ui_shared/include/ui/screens/chat/chat_page_runtime_event_pump.h"
    pump_source = "modules/ui_shared/src/ui/screens/chat/chat_page_runtime_event_pump.cpp"
    refresh_sink = "modules/ui_shared/include/ui/screens/chat/chat_ui_refresh_sink.h"

    for path in [header, source]:
        if not exists(path):
            continue
        text = strip_cpp_comments(read_text(path))
        for token in [
            "public IChatUiRuntime",
            "onChatEvent",
            "processIncoming(",
            "flushStore(",
            "delivery_event_bridge_",
            "LegacyChatDeliveryEventBridge",
            "key_verification_source_",
            "LegacyKeyVerificationSource",
            "onNumberRequest",
            "onNumberInform",
            "onFinal",
            "KeyVerificationNumberRequestEvent",
            "KeyVerificationNumberInformEvent",
            "KeyVerificationFinalEvent",
        ]:
            if token in text:
                failures += fail(
                    f"{path} still owns runtime event pump token: {token}"
                )

    if exists(header):
        text = read_text(header)
        for token in [
            "public IChatUiRefreshSink",
            "onRuntimeMessageArrived",
            "onRuntimeSendResult",
            "onRuntimeUnreadChanged",
            "showKeyVerification",
        ]:
            if token not in text:
                failures += fail(f"ChatUiController header missing refresh sink token: {token}")

    if exists(refresh_sink):
        text = strip_cpp_comments(read_text(refresh_sink))
        for token in [
            "IChatUiRefreshSink",
            "onRuntimeMessageArrived",
            "onRuntimeSendResult",
            "onRuntimeUnreadChanged",
            "showKeyVerification",
        ]:
            if token not in text:
                failures += fail(f"IChatUiRefreshSink missing token: {token}")
        for token in [
            "ChatService",
            "ChatDeliveryReadModel",
            "ChatDeliveryEventProjector",
            "LegacyKeyVerificationSource",
            "sys::Event",
        ]:
            if token in text:
                failures += fail(f"IChatUiRefreshSink exposes runtime token: {token}")

    for path in [pump_header, pump_source]:
        if not exists(path):
            failures += fail(f"missing ChatPageRuntimeEventPump file: {path}")
            continue
        text = strip_cpp_comments(read_text(path))
        for token in ["lvgl.h", "lv_obj_t", "ChatWorkspaceModel", "MessageRow", "encodeTeamChat"]:
            if token in text:
                failures += fail(f"{path} contains forbidden event pump UI token: {token}")

    if exists(pump_source):
        text = read_text(pump_source)
        for token in [
            "service_.processIncoming()",
            "service_.flushStore()",
            "delivery_adapter_->onChatSendResult",
            "key_verification_source_->onNumberRequest",
            "key_verification_source_->onNumberInform",
            "key_verification_source_->onFinal",
            "ui_->onRuntimeMessageArrived",
            "ui_->onRuntimeSendResult",
            "ui_->onRuntimeUnreadChanged",
            "ui_->showKeyVerification",
            "delete event",
        ]:
            if token not in text:
                failures += fail(f"ChatPageRuntimeEventPump source missing token: {token}")

    if exists(runtime):
        text = read_text(runtime)
        for token in [
            "ChatPageRuntimeFacade",
            "s_event_pump",
            "s_runtime_facade",
            "ChatPageRuntimeEventPump",
            "event_pump_.update();",
            "controller_.update();",
            "event_pump_.handleEvent(event);",
            "setChatUiRuntime(s_runtime_facade.get())",
        ]:
            if token not in text:
                failures += fail(f"chat_page_runtime.cpp missing event pump wiring token: {token}")
        if "setChatUiRuntime(s_ui_controller.get())" in text:
            failures += fail("chat_page_runtime.cpp still registers controller directly")

    return failures


def check_chat_runtime_wires_team_action_sink() -> int:
    failures = 0
    path = "modules/ui_shared/src/ui/screens/chat/chat_page_runtime.cpp"
    if not exists(path):
        failures += fail("chat_page_runtime.cpp is missing")
        return failures

    text = read_text(path)
    for token in [
        "TeamActionRuntimeSink",
        "GpsTeamLocationSource",
        "TeamControllerChatCommandPort",
        "s_team_action_sink",
        "s_team_location_source",
        "s_team_action_sink.get()",
        "team_runtime_adapters.h",
    ]:
        if token not in text:
            failures += fail(f"chat_page_runtime.cpp missing Team action wiring token: {token}")
    if "platform/ui/gps_runtime.h" in text:
        failures += fail("chat_page_runtime.cpp reintroduced direct GPS runtime include")
    return failures


def check_ui_presentation_does_not_own_team_actions() -> int:
    failures = 0
    for path in iter_code_files(
        ROOT / "modules/ui_presentation/include",
        ROOT / "modules/ui_presentation/src",
    ):
        text = strip_cpp_comments(path.read_text(encoding="utf-8", errors="ignore"))
        if "team_actions" in text or "TeamActionRequest" in text:
            failures += fail(
                f"{path.relative_to(ROOT)} imports Team action runtime"
            )
    return failures


def check_key_verification_type_shape() -> int:
    failures = 0

    snapshot = "modules/ui_presentation/include/ui_presentation/key_verification/key_verification_snapshot.h"
    if exists(snapshot):
        text = read_text(snapshot)
        for token in [
            "enum class VerificationProtocol",
            "Meshtastic",
            "MeshCore",
            "enum class VerificationState",
            "Pending",
            "Verified",
            "enum class VerificationFailureKind",
            "MissingPeerKey",
            "MissingLocalIdentity",
            "enum class VerificationPromptKind",
            "EnterNumber",
            "ShowNumber",
            "CompareCode",
            "KeyVerificationSnapshot",
            "can_accept",
            "can_reject",
            "can_refresh",
            "can_copy_code",
            "requires_number",
        ]:
            if token not in text:
                failures += fail(f"KeyVerificationSnapshot missing token: {token}")
        for token in ["private", "public_key", "PSK", "packet", "ChatWorkspaceModel", "MessageRow"]:
            if token in strip_cpp_comments(text):
                failures += fail(f"KeyVerificationSnapshot owns forbidden token: {token}")

    model_header = "modules/ui_presentation/include/ui_presentation/key_verification/key_verification_model.h"
    model_source = "modules/ui_presentation/src/key_verification/key_verification_model.cpp"
    for path in [model_header, model_source]:
        if exists(path):
            text = strip_cpp_comments(read_text(path))
            for token in [
                "ChatService",
                "IMeshAdapter",
                "ContactService",
                "lvgl.h",
                "gtk",
                "sys/event_bus",
                "ChatWorkspaceModel",
                "MessageRow",
                "platform/",
                "setNodeKeyManuallyVerified",
                "submitKeyVerificationNumber",
            ]:
                if token in text:
                    failures += fail(f"{path} contains forbidden key verification model token: {token}")

    if exists(model_header):
        text = read_text(model_header)
        for token in [
            "KeyVerificationModel",
            "selectPeer",
            "clearSelection",
            "snapshot",
            "accept",
            "reject",
            "refresh",
            "copyCode",
            "submitNumber",
            "IKeyVerificationPresentationSource& source_",
            "IKeyVerificationActionSink& sink_",
        ]:
            if token not in text:
                failures += fail(f"KeyVerificationModel missing token: {token}")

    source = "modules/ui_presentation/include/ui_presentation/key_verification/key_verification_source.h"
    if exists(source):
        text = read_text(source)
        for token in ["KeyVerificationRequest", "IKeyVerificationPresentationSource", "buildKeyVerificationSnapshot"]:
            if token not in text:
                failures += fail(f"KeyVerificationSource missing token: {token}")

    sink = "modules/ui_presentation/include/ui_presentation/key_verification/key_verification_action_sink.h"
    if exists(sink):
        text = read_text(sink)
        for token in ["IKeyVerificationActionSink", "accept", "reject", "refresh", "copyCode", "submitNumber"]:
            if token not in text:
                failures += fail(f"KeyVerificationActionSink missing token: {token}")

    return failures


def check_key_verification_bridge_boundary() -> int:
    failures = 0
    bridge_files = [
        "modules/ui_shared/src/ui/presentation_sources/legacy_key_verification_source.cpp",
        "modules/ui_shared/src/ui/presentation_sources/legacy_key_verification_action_sink.cpp",
    ]
    for path in bridge_files:
        if not exists(path):
            failures += fail(f"missing key verification bridge file: {path}")
            continue
        text = strip_cpp_comments(read_text(path))
        for token in [
            "lvgl.h",
            "gtk",
            "ChatWorkspaceModel",
            "MessageRow",
            "ChatDeliveryReadModel",
            "LegacyChatActionSink",
            "sendMessage(",
            "sendText(",
            "Renderer",
        ]:
            if token in text:
                failures += fail(f"{path} contains forbidden key verification bridge token {token}")

    source_header = "modules/ui_shared/include/ui/presentation_sources/legacy_key_verification_source.h"
    source_impl = "modules/ui_shared/src/ui/presentation_sources/legacy_key_verification_source.cpp"
    if exists(source_header):
        failures += fail(f"LegacyKeyVerificationSource header must stay absent: {source_header}")
    if exists(source_impl):
        text = read_text(source_impl)
        for token in [
            "buildKeyVerificationSnapshot",
            "VerificationPromptKind::EnterNumber",
            "VerificationPromptKind::ShowNumber",
            "VerificationPromptKind::CompareCode",
            "getContactName",
            "backendProtocol",
        ]:
            if token not in text:
                failures += fail(f"LegacyKeyVerificationSource source missing token: {token}")
        for token in ["submitKeyVerificationNumber", "setNodeKeyManuallyVerified"]:
            if token in strip_cpp_comments(text):
                failures += fail(f"LegacyKeyVerificationSource mutates key verification runtime: {token}")

    sink_header = "modules/ui_shared/include/ui/presentation_sources/legacy_key_verification_action_sink.h"
    sink_impl = "modules/ui_shared/src/ui/presentation_sources/legacy_key_verification_action_sink.cpp"
    if exists(sink_header):
        failures += fail(f"LegacyKeyVerificationActionSink header must stay absent: {sink_header}")
    if exists(sink_impl):
        text = read_text(sink_impl)
        for token in [
            "startKeyVerification",
            "submitKeyVerificationNumber",
            "setNodeKeyManuallyVerified",
            "VerificationState::Verified",
            "VerificationState::Rejected",
        ]:
            if token not in text:
                failures += fail(f"LegacyKeyVerificationActionSink source missing token: {token}")

    return failures


def check_chat_ui_key_verification_migration() -> int:
    failures = 0

    header = "modules/ui_shared/include/ui/screens/chat/chat_ui_controller.h"
    source = "modules/ui_shared/src/ui/screens/chat/chat_ui_controller.cpp"

    if exists(header):
        text = strip_cpp_comments(read_text(header))
        for token in ["KeyVerificationModel", "key_verification_model_"]:
            if token not in text:
                failures += fail(f"ChatUiController header missing key verification token: {token}")
        for token in ["LegacyKeyVerificationSource", "key_verification_source_"]:
            if token in text:
                failures += fail(f"ChatUiController header still owns key verification source token: {token}")
        for token in [
            "key_verify_node_id_",
            "key_verify_nonce_",
            "key_verify_expects_number_",
            "key_verify_can_trust_",
            "openKeyVerificationNumberModal",
            "openKeyVerificationInfoModal",
            "openKeyVerificationFinalModal",
        ]:
            if token in text:
                failures += fail(f"ChatUiController header still owns key verification token: {token}")
    else:
        failures += fail("ChatUiController header is missing")

    if exists(source):
        text = strip_cpp_comments(read_text(source))
        for token in [
            "renderKeyVerificationModal",
            "KeyVerificationModalCallbacks",
            "key_verification_model_->submitNumber",
            "key_verification_model_->accept",
            "submitKeyVerificationInput",
            "showKeyVerification",
        ]:
            if token not in text:
                failures += fail(f"ChatUiController source missing key verification token: {token}")
        for token in [
            "key_verify_node_id_",
            "key_verify_nonce_",
            "key_verify_expects_number_",
            "key_verify_can_trust_",
            "submitKeyVerificationNumber",
            "setNodeKeyManuallyVerified",
            "KeyVerificationNumberRequestEvent",
            "KeyVerificationNumberInformEvent",
            "KeyVerificationFinalEvent",
            "key_verification_source_->onNumberRequest",
            "key_verification_source_->onNumberInform",
            "key_verification_source_->onFinal",
            "key_verification_model_->selectPeer",
        ]:
            if token in text:
                failures += fail(f"ChatUiController source still owns key verification token: {token}")
        if "getMeshAdapter()" in text:
            failures += fail("ChatUiController key verification path still reads mesh adapter directly")
    else:
        failures += fail("ChatUiController source is missing")

    runtime = "modules/ui_shared/src/ui/screens/chat/chat_page_runtime.cpp"
    if exists(runtime):
        text = read_text(runtime)
        for token in [
            "KeyVerificationSessionAdapter",
            "KeyVerificationPresentationSource",
            "KeyVerificationActionSink",
            "KeyVerificationModel",
            "s_key_verification_session",
            "s_key_verification_source",
            "s_key_verification_sink",
            "s_key_verification_model",
        ]:
            if token not in text:
                failures += fail(f"chat_page_runtime.cpp missing key verification wiring token: {token}")
    else:
        failures += fail("chat_page_runtime.cpp is missing")

    for path in [
        "modules/ui_presentation/include/ui_presentation/chat/chat_workspace_model.h",
        "modules/ui_presentation/src/chat/chat_workspace_model.cpp",
        "modules/ui_presentation/include/ui_presentation/chat/chat_workspace_snapshot.h",
    ]:
        if not exists(path):
            continue
        text = strip_cpp_comments(read_text(path))
        for token in [
            "KeyVerification",
            "VerificationPrompt",
            "verification_code",
            "can_accept_key",
            "MessageKeyVerification",
        ]:
            if token in text:
                failures += fail(f"{path} owns key verification token: {token}")

    return failures


def main() -> int:
    failures = 0
    failures += check_required_files()
    failures += check_removed_chat_read_projection_not_reintroduced()
    failures += check_docs()
    failures += check_legacy_burndown_register()
    failures += check_core_delivery_is_runtime_only()
    failures += check_delivery_type_shape()
    failures += check_presentation_source_enrichment()
    failures += check_composition_roots_own_delivery()
    failures += check_ui_presentation_and_renderers_do_not_own_delivery()
    failures += check_delivery_event_bridge_boundary()
    failures += check_delivery_action_bridge_boundary()
    failures += check_team_action_type_shape()
    failures += check_team_action_runtime_sink_boundary()
    failures += check_chat_ui_team_action_migration()
    failures += check_team_page_read_model_boundary()
    failures += check_team_page_flow_controller_boundary()
    failures += check_team_page_lvgl_renderer_boundary()
    failures += check_team_page_command_reducer_boundary()
    failures += check_team_page_create_team_action_boundary()
    failures += check_team_page_pairing_command_action_boundary()
    failures += check_team_page_request_keys_action_boundary()
    failures += check_team_page_event_reducer_boundary()
    failures += check_team_page_event_effect_sink_boundary()
    failures += check_team_page_key_event_log_boundary()
    failures += check_team_page_runtime_port_boundary()
    failures += check_team_page_activity_sink_boundary()
    failures += check_team_page_deferred_dispatch_boundary()
    failures += check_team_page_kick_confirm_action_boundary()
    failures += check_team_page_transfer_leader_action_boundary()
    failures += check_team_page_state_store_boundary()
    failures += check_chat_ui_legacy_burndown()
    failures += check_team_rich_payload_presentation_boundary()
    failures += check_team_position_picker_renderer_boundary()
    failures += check_map_tile_source_cache_boundary()
    failures += check_map_tile_render_queue_cache_boundary()
    failures += check_map_overlay_route_tracker_boundary()
    failures += check_gps_runtime_scheduling_boundary()
    failures += check_team_consumer_shadow_state_closeout()
    failures += check_phase7_final_readiness()
    failures += check_chat_runtime_event_pump_boundary()
    failures += check_chat_runtime_wires_team_action_sink()
    failures += check_ui_presentation_does_not_own_team_actions()
    failures += check_key_verification_type_shape()
    failures += check_key_verification_bridge_boundary()
    failures += check_chat_ui_key_verification_migration()

    if failures == 0:
        print("[phase7-runtime-ready] OK")
        return 0

    print(f"[phase7-runtime-ready] {failures} failure(s)")
    return 1


if __name__ == "__main__":
    sys.exit(main())
