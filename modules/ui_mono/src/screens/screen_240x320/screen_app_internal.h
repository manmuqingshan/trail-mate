#pragma once

#include "ui/mono/screens/screen_240x320/screen_app.h"

#include "app/app_facade_access.h"
#include "chat/infra/mesh_protocol_utils.h"
#include "chat_presentation_adapters/chat_conversation_mapper.h"
#include "platform/ui/team_ui_chat_log_store.h"
#include "platform/ui/team_ui_snapshot_store.h"
#include "sys/clock.h"
#include "ui/app_runtime.h"
#include "ui/localization.h"
#include "ui/menu/menu_layout.h"
#include "ui/mono/screens/screen_240x320/protocol_probe_port.h"
#include "ui/presentation_sources/chat_presentation_source.h"
#include "ui/presentation_sources/runtime_chat_action_sink.h"
#include "ui/presentation_sources/runtime_device_status_source.h"
#include "ui/presentation_sources/runtime_gps_status_source.h"
#include "ui/presentation_sources/runtime_map_workspace_source.h"
#include "ui/presentation_sources/runtime_mesh_status_source.h"
#include "ui/presentation_sources/runtime_settings_source.h"
#include "ui/presentation_sources/team_chat_action_sink.h"
#include "ui/presentation_sources/team_chat_presentation_source.h"
#include "ui/screens/team/team_page_command_reducer.h"
#include "ui/screens/team/team_page_create_team_action.h"
#include "ui/screens/team/team_page_key_event_log.h"
#include "ui/screens/team/team_page_pairing_command_action.h"
#include "ui/screens/team/team_page_runtime_port.h"
#include "ui/team_actions/team_runtime_adapters.h"
#include "ui_presentation/chat/chat_workspace_model.h"

#include <cstddef>
#include <cstring>
#include <memory>

namespace ui::mono::screens::screen_240x320::detail
{

constexpr lv_coord_t kScreenWidth = 240;
constexpr lv_coord_t kScreenHeight = 320;
constexpr lv_coord_t kMargin = 8;
constexpr lv_coord_t kContentWidth = kScreenWidth - (kMargin * 2);
constexpr lv_coord_t kHeaderRuleY = 32;
constexpr lv_coord_t kBodyTop = 42;
constexpr lv_coord_t kLineHeight = 17;
constexpr lv_coord_t kActionTop = 232;
constexpr lv_coord_t kFooterRuleY = 278;
constexpr lv_coord_t kFooterTop = 280;
constexpr lv_coord_t kButtonTop = 298;
constexpr size_t kMaxLines = 10;
constexpr size_t kMaxActions = 6;

enum class Action : unsigned char
{
    Back,
    Refresh,
    CenterOnSelf,
    ZoomIn,
    ZoomOut,
    ToggleTerrain,
    ToggleGps,
    OpenCellularSettings,
    SettingsPrevious,
    SettingsNext,
    SettingsOpen,
    ToggleTracker,
    ToggleWalkie,
    ToggleWalkieMonitor,
    ToggleSstv,
    ToggleUsb,
    TeamCreate,
    TeamJoin,
    TeamLeave,
    TeamChat,
    ContactsPrevious,
    ContactsNext,
    ContactsOpenChat,
    ContactsToggleView,
    ExtensionsPrevious,
    ExtensionsNext,
    ExtensionsRefresh,
    ExtensionsApply,
    ExtensionsRemove,
    ProbeStartStop,
    ProbePrevious,
    ProbeNext,
    ProbeApply,
    ProbeSync,
    ChatPrevious,
    ChatNext,
    ChatOpen,
    ChatList,
    ChatType,
    ChatSend,
    ChatDiscard,
    TeamMembers,
    TeamMemberPrevious,
    TeamMemberNext,
    TeamMemberOpen,
    TeamLeaveConfirm,
    TeamLeaveCancel,
    ContactsDetails,
    ContactsBackToList,
    ExtensionsDetails,
    ExtensionsConfirm,
    ExtensionsCancel,
    ProbeDetails,
    ProbeConfirm,
    ProbeCancel,
};

// A route is a navigable surface: it has its own title, focus entry point and
// Back target.  It deliberately replaces the former `*_open` / editing
// booleans, which made a page interior look like a page transition.
enum class ChatRoute : unsigned char
{
    ConversationList,
    Conversation,
    Compose,
};

struct ChatFlowState
{
    ::ui::chat::ChatWorkspaceSnapshot snapshot{};
    ChatRoute route = ChatRoute::ConversationList;
    size_t selected_index = 0;
    char draft[121]{};
};

struct ActionButton
{
    lv_obj_t* button = nullptr;
    lv_obj_t* label = nullptr;
    Action action = Action::Refresh;
};

struct ScreenState
{
    ScreenApp* adapter = nullptr;
    lv_obj_t* root = nullptr;
    lv_obj_t* title = nullptr;
    lv_obj_t* subtitle = nullptr;
    lv_obj_t* lines[kMaxLines]{};
    lv_obj_t* footer = nullptr;
    ActionButton actions[kMaxActions]{};
    size_t action_count = 0;
    char scratch[96]{};
    char notice[64]{};
};

extern ScreenState s_state;

bool valid(const lv_obj_t* object);
void style_paper(lv_obj_t* object);
lv_obj_t* create_text(lv_obj_t* parent,
                      lv_coord_t width,
                      lv_text_align_t align = LV_TEXT_ALIGN_LEFT);
void set_text(lv_obj_t* label, const char* text);
void set_line(size_t index, const char* text);
void set_linef(size_t index, const char* format, ...);
void clear_lines_from(size_t index);
void set_notice(const char* text);
void set_action(size_t index, const char* label, Action action);
void set_action_visible(size_t index, bool visible);
void focus_action(size_t index);
void add_action(const char* label,
                Action action,
                lv_coord_t x,
                lv_coord_t y,
                lv_coord_t width);

void render_map();
void add_map_actions();
bool handle_map_action(Action action);
void reset_map_page_state();
void destroy_map_page();
void render_sky_plot();
void add_sky_plot_actions();
bool handle_sky_plot_action(Action action);
void reset_sky_plot_page_state();
void destroy_sky_plot_page();
void render_network();
void add_network_actions();
void render_settings();
void add_settings_actions();
bool handle_settings_action(Action action);
void reset_settings_page_state();
void configure_settings_actions();
void render_tracker();
void add_tracker_actions();
bool handle_tracker_action(Action action);
void render_walkie();
void add_walkie_actions();
bool handle_walkie_action(Action action);
void render_sstv();
void add_sstv_actions();
bool handle_sstv_action(Action action);
void render_usb_storage();
void add_usb_storage_actions();
bool handle_usb_storage_action(Action action);
void render_chat();
void configure_chat_actions();
void add_chat_actions();
bool handle_chat_action(Action action);
bool queue_chat_conversation(const ::ui::chat::ConversationId& conversation);
void cancel_queued_chat_conversation();
ChatFlowState& direct_chat_flow();
ChatFlowState& team_chat_flow();
bool ensure_team_chat_flow();
void reset_direct_chat_flow();
void reset_team_chat_flow();
void reset_chat_conversation_page();
void render_chat_conversation_page(ChatFlowState& flow, const char* title_prefix);
void render_chat_compose_page(ChatFlowState& flow, const char* title_prefix);
void enter_chat_compose_page(ChatFlowState& flow);
void leave_chat_compose_page(ChatFlowState& flow, bool preserve_draft);
bool chat_compose_page_active();
const char* chat_compose_page_text();
void clear_chat_compose_page_text();
void configure_chat_compose_actions(ChatFlowState& flow, const char* back_label);
bool handle_chat_compose_action(Action action,
                                ChatFlowState& flow,
                                ::ui::chat::ChatWorkspaceModel* model,
                                const char* return_notice);
void render_team();
void reset_team_page_state();
void configure_team_actions();
void add_team_actions();
bool handle_team_action(Action action);
::ui::chat::ChatWorkspaceModel* ensure_team_chat_model();
void render_contacts();
void reset_contacts_page_state();
void configure_contacts_actions();
void add_contacts_actions();
bool handle_contacts_action(Action action);
void render_extensions();
void reset_extensions_page_state();
void configure_extensions_actions();
void add_extensions_actions();
bool handle_extensions_action(Action action);
void render_protocol_probe();
void reset_protocol_probe_page_state();
void configure_protocol_probe_actions();
void add_protocol_probe_actions();
bool handle_protocol_probe_action(Action action);
void create_page_actions(PageKind page_kind);
void reset_page_state(PageKind page_kind);
void refresh_page();
bool run_page_action(Action action);

} // namespace ui::mono::screens::screen_240x320::detail
