#include "ui/screens/gps/gps_page_runtime.h"

using Host = gps::ui::shell::Host;
using Projection = gps::ui::shell::Projection;

#include "app/app_config.h"
#include "app/app_facade_access.h"
#include "gps/domain/gps_diagnostics.h"
#include "platform/ui/device_runtime.h"
#include "platform/ui/gps_runtime.h"
#include "platform/ui/team_ui_store_runtime.h"
#include "platform/ui/tracker_runtime.h"
#include "sys/clock.h"
#include "ui/app_runtime.h"
#include "ui/formatters.h"
#include "ui/localization.h"
#include "ui/page/page_profile.h"
#include "ui/presentation_sources/runtime_gps_status_source.h"
#include "ui/presentation_sources/runtime_map_workspace_source.h"
#include "ui/presentation_sources/team_map_overlay_source.h"
#include "ui/runtime/ui_feedback.h"
#include "ui/screens/gps/gps_constants.h"
#include "ui/support/lvgl_fs_utils.h"
#include "ui/team_presentation/team_member_label.h"
#include "ui/ui_common.h"
#include "ui/widgets/map/map_viewport.h"
#include "ui/widgets/top_bar.h"
#include "ui_gps_runtime/gps_page_runtime_pump.h"
#include "ui_map_runtime/map_overlay_snapshot_source.h"
#include "ui_presentation/gps/gps_status_model.h"
#include "ui_presentation/map/map_overlay_snapshot.h"
#include "ui_presentation/map/map_workspace_model.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#if !defined(LV_FONT_MONTSERRAT_10) || !LV_FONT_MONTSERRAT_10
#define lv_font_montserrat_10 lv_font_montserrat_12
#endif

#if !defined(LV_FONT_MONTSERRAT_12) || !LV_FONT_MONTSERRAT_12
#define lv_font_montserrat_12 lv_font_montserrat_14
#endif

#if defined(ESP_PLATFORM)
#include "esp_attr.h"
#define UI_GPS_PAGE_STATE_RAM_ATTR EXT_RAM_ATTR
#else
#define UI_GPS_PAGE_STATE_RAM_ATTR
#endif

bool isGPSLoadingTiles()
{
    return false;
}

void show_toast(const char* message, uint32_t duration_ms)
{
    ::ui::feedback::show_notice(message ? message : "", duration_ms);
}

void hide_toast()
{
    ::ui::feedback::hide_notice();
}

namespace
{

constexpr int kCardputerZeroMapDefaultZoom = gps_ui::kDefaultZoom;
constexpr lv_coord_t kMapControlBarHeight = 24;
constexpr lv_coord_t kMapControlButtonHeight = 20;
constexpr lv_coord_t kMapControlButtonSmallWidth = 26;
constexpr lv_coord_t kMapControlButtonMediumWidth = 36;
constexpr lv_coord_t kMapControlButtonWideWidth = 44;
constexpr lv_coord_t kMapControlButtonContourWidth = 56;
constexpr lv_coord_t kMapControlButtonTrackerWidth = 42;
constexpr lv_coord_t kMapSideRailWidth = 72;
constexpr std::uint32_t kLvglFunctionKeyF1 = 0x110001U;
constexpr std::uint32_t kInvalidMemberId = 0xFFFFFFFFU;
constexpr std::size_t kMaxTrackOverlayPoints = 48;
constexpr std::size_t kTrackFileReadBufferBytes = 256;
constexpr std::size_t kMaxTrackFileLineBytes = 2048;
constexpr std::size_t kMaxKmlTagBytes = 80;
constexpr std::size_t kMaxKmlCoordinateTokenBytes = 96;
constexpr int kDefaultTrackerZoom = 16;

struct TrackOverlayPoint
{
    double lat = 0.0;
    double lon = 0.0;
};

enum class TrackOverlayFileKind : uint8_t
{
    Track,
    Route,
};

enum class MapControlAction : uint8_t
{
    ZoomOut = 0,
    ZoomIn,
    Center,
    Layer,
    Contour,
    Tracker,
    Help,
    Route,
    TeamMember,
};

const Host* s_host = nullptr;
lv_obj_t* s_root = nullptr;
lv_timer_t* s_timer = nullptr;
::ui::widgets::TopBar s_top_bar;
::ui::widgets::map::Runtime s_map_runtime;
int s_map_zoom = kCardputerZeroMapDefaultZoom;
int s_map_pan_x = 0;
int s_map_pan_y = 0;
bool s_map_view_initialized = false;
UI_GPS_PAGE_STATE_RAM_ATTR ::ui::map::MapOverlaySnapshot s_overlay_snapshot;
Projection s_projection = Projection::Map;
bool s_gps_power_lease_active = false;
lv_obj_t* s_gps_status_label = nullptr;
lv_obj_t* s_gps_coord_label = nullptr;
lv_obj_t* s_gps_sat_label = nullptr;
lv_obj_t* s_gps_alt_label = nullptr;
lv_obj_t* s_gps_motion_label = nullptr;
lv_obj_t* s_gps_time_label = nullptr;
lv_obj_t* s_gps_diag_label = nullptr;
lv_obj_t* s_map_control_bar = nullptr;
lv_obj_t* s_map_zoom_label = nullptr;
lv_obj_t* s_map_zoom_out_btn = nullptr;
lv_obj_t* s_map_zoom_in_btn = nullptr;
lv_obj_t* s_map_center_btn = nullptr;
lv_obj_t* s_map_layer_btn = nullptr;
lv_obj_t* s_map_contour_btn = nullptr;
lv_obj_t* s_map_help_btn = nullptr;
lv_obj_t* s_map_tracker_btn = nullptr;
lv_obj_t* s_map_notice_panel = nullptr;
lv_obj_t* s_map_notice_label = nullptr;
lv_obj_t* s_map_context_rail = nullptr;
lv_obj_t* s_map_route_btn = nullptr;
lv_obj_t* s_map_help_modal = nullptr;
lv_obj_t* s_tracker_modal = nullptr;
bool s_map_help_open_pending = false;
bool s_map_refresh_pending = false;
bool s_map_drag_active = false;
uint8_t s_map_context_mask = 0xFF;
char s_map_notice_text[64]{};
uint32_t s_map_notice_until_ms = 0;
int s_map_drag_start_pan_x = 0;
int s_map_drag_start_pan_y = 0;
std::vector<TrackOverlayPoint> s_track_points;
std::vector<std::string> s_track_modal_names;
std::string s_track_file;
bool s_track_overlay_active = false;
TrackOverlayFileKind s_track_overlay_kind = TrackOverlayFileKind::Track;
std::vector<lv_obj_t*> s_member_buttons;
std::vector<uint32_t> s_member_button_ids;
uint32_t s_member_list_hash = 0;
uint32_t s_selected_member_id = kInvalidMemberId;
uint32_t s_member_panel_last_ms = 0;

void refresh_view();
void root_key_event_cb(lv_event_t* e);
void open_map_help_modal();
void open_tracker_modal();
void add_map_controls_to_group(lv_group_t* group);
void request_refresh_view();
void consume_key_event(lv_event_t* e);
bool load_map_track_file_impl(const char* path, bool show_fail_toast);
void append_track_point(std::vector<TrackOverlayPoint>& out,
                        double lat,
                        double lon);
::ui::presentation_sources::TeamMapOverlaySource& team_map_overlay_source();
void apply_map_drag_preview();
lv_obj_t* create_map_control_button(lv_obj_t* parent,
                                    lv_coord_t width,
                                    const char* text,
                                    MapControlAction action);

void request_exit()
{
    if (s_host)
    {
        ::ui::page::request_exit(s_host);
        return;
    }
    ui_request_exit_to_menu();
}

::ui::map::MapWorkspaceModel& map_workspace_model()
{
    static ::ui::presentation_sources::RuntimeMapWorkspaceSource source(
        ::ui::presentation_sources::runtime_gps_status_source(),
        ::ui::presentation_sources::runtime_map_workspace_state(),
        &::team::ui::team_ui_snapshot_store());
    static ::ui::presentation_sources::RuntimeMapActionSink sink(
        ::ui::presentation_sources::runtime_gps_status_source(),
        ::ui::presentation_sources::runtime_map_workspace_state());
    static ::ui::map::MapWorkspaceModel model(source, sink);
    return model;
}

class RuntimeMapOverlayGpsSource final : public ::ui::map_overlay::IMapOverlayGpsSource
{
  public:
    bool currentFix(double& lat, double& lon, bool& valid) const override
    {
        ::ui::gps::GpsStatusSnapshot gps;
        if (!::ui::presentation_sources::runtime_gps_status_source().buildGpsStatusSnapshot(gps))
        {
            return false;
        }

        lat = gps.latitude;
        lon = gps.longitude;
        valid = gps.header.valid && gps.fix_valid;
        return true;
    }
};

::ui::map::IMapOverlayPresentationSource& map_overlay_source()
{
    static RuntimeMapOverlayGpsSource gps;
    static ::ui::map_overlay::MapOverlaySnapshotSource source(&gps, &team_map_overlay_source());
    return source;
}

::ui::presentation_sources::TeamMapOverlaySource& team_map_overlay_source()
{
    static ::ui::presentation_sources::TeamMapOverlaySource team(
        ::team::ui::team_ui_snapshot_store());
    return team;
}

uint8_t current_map_zoom()
{
    return static_cast<uint8_t>(
        std::max(::ui::widgets::map::kMinZoom,
                 std::min(s_map_zoom, ::ui::widgets::map::kMaxZoom)));
}

bool has_valid_viewport_center(const ::ui::map::MapViewport& viewport)
{
    return std::isfinite(viewport.center_lat) &&
           std::isfinite(viewport.center_lon) &&
           (viewport.center_lat != 0.0 || viewport.center_lon != 0.0);
}

void sync_workspace_layers_from_renderer()
{
    auto& state = ::ui::presentation_sources::runtime_map_workspace_state();
    const auto layers = ::ui::widgets::map::current_layer_state();
    state.layers.osm = layers.map_source == 0;
    state.layers.terrain = layers.map_source == 1;
    state.layers.satellite = layers.map_source == 2;
    state.layers.contour = layers.contour_enabled;
}

void sync_workspace_viewport_from_renderer()
{
    auto& model = map_workspace_model();
    auto viewport = model.viewport();
    viewport.zoom = current_map_zoom();
    if (has_valid_viewport_center(viewport))
    {
        (void)model.setViewport(viewport);
    }
    (void)model.setActiveTool(::ui::map::MapToolKind::Pan);
}

bool sync_workspace_center_from_screen()
{
    if (app::configFacade().getConfig().map_coord_system != 0)
    {
        return false;
    }

    ::ui::widgets::map::GeoPoint center{};
    if (!::ui::widgets::map::screen_center(s_map_runtime, center) || !center.valid)
    {
        return false;
    }

    auto& model = map_workspace_model();
    auto viewport = model.viewport();
    viewport.center_lat = center.lat;
    viewport.center_lon = center.lon;
    viewport.zoom = current_map_zoom();
    (void)model.setViewport(viewport);
    return true;
}

bool commit_pending_map_pan_from_screen()
{
    if (s_map_pan_x == 0 && s_map_pan_y == 0)
    {
        return false;
    }

    if (!sync_workspace_center_from_screen())
    {
        return false;
    }

    s_map_pan_x = 0;
    s_map_pan_y = 0;
    sync_workspace_viewport_from_renderer();
    return true;
}

::ui::widgets::map::Model build_map_model(
    const ::ui::map::MapWorkspaceSnapshot& snapshot)
{
    const auto& config = app::configFacade().getConfig();

    ::ui::widgets::map::Model model{};
    const bool has_viewport_center = has_valid_viewport_center(snapshot.viewport);
    model.focus_point.valid = true;
    model.focus_point.lat = has_viewport_center
                                ? snapshot.viewport.center_lat
                                : (snapshot.self.valid ? snapshot.self.lat
                                                       : gps_ui::kDefaultLat);
    model.focus_point.lon = has_viewport_center
                                ? snapshot.viewport.center_lon
                                : (snapshot.self.valid ? snapshot.self.lon
                                                       : gps_ui::kDefaultLng);
    model.zoom = snapshot.viewport.zoom == 0 ? s_map_zoom : snapshot.viewport.zoom;
    model.pan_x = s_map_pan_x;
    model.pan_y = s_map_pan_y;
    model.map_source = config.map_source;
    model.contour_enabled = snapshot.layers.contour;
    model.coord_system = config.map_coord_system;
    return model;
}

const char* diagnostic_label()
{
    const auto diagnostics = ::platform::ui::gps::diagnostics();
    return ::gps::gpsDiagnosticCodeName(diagnostics.code);
}

::ui::gps::GpsStatusModel& gps_status_model()
{
    static ::ui::gps::GpsStatusModel model(
        ::ui::presentation_sources::runtime_gps_status_source());
    return model;
}

void set_compact_label(lv_obj_t* label, const char* text)
{
    if (!label || !lv_obj_is_valid(label))
    {
        return;
    }
    lv_label_set_text(label, text ? text : "");
}

bool format_current_gps_map_title(char* out, size_t out_len)
{
    if (!out || out_len == 0)
    {
        return false;
    }
    out[0] = '\0';

    const auto gps = ::platform::ui::gps::get_data();
    if (!gps.valid || !std::isfinite(gps.lat) || !std::isfinite(gps.lng))
    {
        return false;
    }

    char coord_buf[64]{};
    ui_format_coords(gps.lat,
                     gps.lng,
                     app::configFacade().getConfig().gps_coord_format,
                     coord_buf,
                     sizeof(coord_buf));
    if (coord_buf[0] == '\0')
    {
        return false;
    }

    std::snprintf(out, out_len, "%s - %.48s", ::ui::i18n::tr("Map"), coord_buf);
    return true;
}

void update_map_top_bar_title()
{
    if (s_projection != Projection::Map)
    {
        ::ui::widgets::top_bar_set_title(s_top_bar, ::ui::i18n::tr("GPS"));
        return;
    }

    char title[64]{};
    if (format_current_gps_map_title(title, sizeof(title)))
    {
        ::ui::widgets::top_bar_set_title(s_top_bar, title);
        return;
    }

    ::ui::widgets::top_bar_set_title(s_top_bar, ::ui::i18n::tr("Map"));
}

void set_map_notice(const char* text, uint32_t duration_ms)
{
    s_map_notice_text[0] = '\0';
    s_map_notice_until_ms = 0;
    if (!text || text[0] == '\0')
    {
        return;
    }

    std::snprintf(s_map_notice_text, sizeof(s_map_notice_text), "%s", text);
    s_map_notice_until_ms = sys::millis_now() + duration_ms;
}

const char* compact_map_source_label(uint8_t map_source)
{
    switch (map_source)
    {
    case 1:
        return "Ter";
    case 2:
        return "Sat";
    case 0:
    default:
        return "OSM";
    }
}

void set_button_label(lv_obj_t* btn, const char* text)
{
    if (!btn || !lv_obj_is_valid(btn))
    {
        return;
    }
    lv_obj_t* label = lv_obj_get_child(btn, 0);
    set_compact_label(label, text);
}

void clear_map_controls()
{
    s_map_control_bar = nullptr;
    s_map_zoom_label = nullptr;
    s_map_zoom_out_btn = nullptr;
    s_map_zoom_in_btn = nullptr;
    s_map_center_btn = nullptr;
    s_map_layer_btn = nullptr;
    s_map_contour_btn = nullptr;
    s_map_help_btn = nullptr;
    s_map_tracker_btn = nullptr;
    s_map_notice_panel = nullptr;
    s_map_notice_label = nullptr;
    s_map_context_rail = nullptr;
    s_map_route_btn = nullptr;
    s_map_help_modal = nullptr;
    s_tracker_modal = nullptr;
    s_map_help_open_pending = false;
    s_map_refresh_pending = false;
    s_map_drag_active = false;
    s_map_context_mask = 0xFF;
    s_map_notice_text[0] = '\0';
    s_map_notice_until_ms = 0;
    s_member_buttons.clear();
    s_member_button_ids.clear();
    s_member_list_hash = 0;
    s_member_panel_last_ms = 0;
}

void set_hidden(lv_obj_t* obj, bool hidden)
{
    if (!obj || !lv_obj_is_valid(obj))
    {
        return;
    }

    if (hidden)
    {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

bool map_control_visible(lv_obj_t* obj)
{
    return obj && lv_obj_is_valid(obj) && !lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

bool help_uses_f1()
{
#if defined(TRAIL_MATE_CARDPUTER_ZERO_LINUX) || defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    return true;
#else
    return false;
#endif
}

const char* help_key_label()
{
    return help_uses_f1() ? "F1" : "H";
}

const char* tracker_button_label()
{
    return "Track";
}

lv_coord_t tracker_button_width()
{
    return kMapControlButtonTrackerWidth;
}

bool is_help_key(uint32_t key)
{
    if (help_uses_f1())
    {
        return key == kLvglFunctionKeyF1;
    }
    return key == 'h' || key == 'H';
}

void rebuild_map_control_group()
{
    if (!app_g || (s_map_help_modal && lv_obj_is_valid(s_map_help_modal)) ||
        (s_tracker_modal && lv_obj_is_valid(s_tracker_modal)))
    {
        return;
    }

    lv_group_remove_all_objs(app_g);
    if (s_top_bar.back_btn)
    {
        lv_group_add_obj(app_g, s_top_bar.back_btn);
    }
    add_map_controls_to_group(app_g);
}

bool route_context_available()
{
    const auto& config = app::configFacade().getConfig();
    return config.route_enabled && config.route_path[0] != '\0';
}

bool load_configured_route_overlay(bool show_fail_toast)
{
    const auto& config = app::configFacade().getConfig();
    if (!config.route_enabled || config.route_path[0] == '\0')
    {
        return false;
    }
    if (s_track_overlay_active &&
        s_track_overlay_kind == TrackOverlayFileKind::Route &&
        s_track_file == config.route_path)
    {
        return true;
    }
    return load_map_track_file_impl(config.route_path, show_fail_toast);
}

bool load_team_snapshot(::team::ui::TeamUiSnapshot& out)
{
    return ::team::ui::team_ui_snapshot_store().load(out) &&
           out.in_team &&
           out.has_team_id;
}

uint32_t member_id_for_button(const ::team::ui::TeamMemberUi& member)
{
    if (member.node_id != 0)
    {
        return member.node_id;
    }
    return app::messagingFacade().getSelfNodeId();
}

uint32_t member_color(const ::team::ui::TeamMemberUi& member)
{
    uint8_t color_index = member.color_index;
    if (color_index >= ::team::ui::kTeamMaxMembers)
    {
        color_index = ::team::ui::team_color_index_from_node_id(member_id_for_button(member));
    }
    return ::team::ui::team_color_from_index(color_index);
}

std::string member_label(const ::team::ui::TeamMemberUi& member)
{
    if (member.node_id == 0)
    {
        return member.name.empty() ? std::string("You") : member.name;
    }
    return ::ui::team_presentation::shortTeamMemberLabel(
        member_id_for_button(member));
}

uint32_t hash_member_list(const ::team::ui::TeamUiSnapshot& snapshot)
{
    uint32_t hash = 2166136261U;
    auto mix = [&](uint32_t value)
    {
        hash ^= value;
        hash *= 16777619U;
    };

    mix(static_cast<uint32_t>(snapshot.members.size()));
    for (const auto& member : snapshot.members)
    {
        const uint32_t member_id = member_id_for_button(member);
        mix(member_id);
        mix(member.color_index);
        for (char ch : member.name)
        {
            mix(static_cast<uint8_t>(ch));
        }
    }
    return hash;
}

bool member_exists(const ::team::ui::TeamUiSnapshot& snapshot, uint32_t member_id)
{
    for (const auto& member : snapshot.members)
    {
        if (member_id_for_button(member) == member_id)
        {
            return true;
        }
    }
    return false;
}

void style_member_button_selected(lv_obj_t* btn, bool selected)
{
    if (!btn || !lv_obj_is_valid(btn))
    {
        return;
    }
    lv_obj_set_style_outline_width(btn, selected ? 2 : 0, LV_PART_MAIN);
    lv_obj_set_style_outline_color(btn, lv_color_hex(0x111827), LV_PART_MAIN);
    lv_obj_set_style_outline_pad(btn, 0, LV_PART_MAIN);
}

void update_member_button_states()
{
    for (std::size_t index = 0; index < s_member_buttons.size(); ++index)
    {
        const bool selected = index < s_member_button_ids.size() &&
                              s_member_button_ids[index] == s_selected_member_id;
        style_member_button_selected(s_member_buttons[index], selected);
    }
}

void clear_member_buttons()
{
    for (lv_obj_t* btn : s_member_buttons)
    {
        if (btn && lv_obj_is_valid(btn))
        {
            lv_obj_del(btn);
        }
    }
    s_member_buttons.clear();
    s_member_button_ids.clear();
}

void select_member(uint32_t member_id)
{
    if (member_id == 0 || member_id == kInvalidMemberId)
    {
        return;
    }

    s_selected_member_id = member_id;
    update_member_button_states();

    ::team::ui::TeamUiSnapshot snapshot;
    if (load_team_snapshot(snapshot))
    {
        std::string track_path;
        if (::team::ui::team_ui_get_member_track_path(snapshot.team_id, member_id, track_path))
        {
            (void)load_map_track_file_impl(track_path.c_str(), true);
        }
    }

    char text[40]{};
    std::snprintf(text,
                  sizeof(text),
                  "Member %04lX",
                  static_cast<unsigned long>(member_id & 0xFFFFU));
    set_map_notice(text, 1200);
    request_refresh_view();
}

void member_button_event_cb(lv_event_t* e)
{
    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_KEY)
    {
        const uint32_t key = lv_event_get_key(e);
        if (key != LV_KEY_ENTER)
        {
            return;
        }
        consume_key_event(e);
    }
    else if (code != LV_EVENT_CLICKED)
    {
        return;
    }

    const uint32_t member_id = static_cast<uint32_t>(
        reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
    select_member(member_id);
}

lv_obj_t* create_member_button(const ::team::ui::TeamMemberUi& member)
{
    if (!s_map_context_rail || !lv_obj_is_valid(s_map_context_rail))
    {
        return nullptr;
    }

    const uint32_t member_id = member_id_for_button(member);
    lv_obj_t* btn = create_map_control_button(s_map_context_rail,
                                              kMapSideRailWidth - 10,
                                              "",
                                              MapControlAction::TeamMember);
    lv_obj_set_height(btn, kMapControlButtonHeight);
    lv_obj_set_style_pad_left(btn, 3, LV_PART_MAIN);
    lv_obj_set_style_pad_right(btn, 3, LV_PART_MAIN);
    lv_obj_add_event_cb(btn,
                        member_button_event_cb,
                        LV_EVENT_CLICKED,
                        reinterpret_cast<void*>(static_cast<uintptr_t>(member_id)));
    lv_obj_add_event_cb(btn,
                        member_button_event_cb,
                        LV_EVENT_KEY,
                        reinterpret_cast<void*>(static_cast<uintptr_t>(member_id)));

    lv_obj_t* dot = lv_obj_create(btn);
    lv_obj_set_size(dot, 7, 7);
    lv_obj_align(dot, LV_ALIGN_LEFT_MID, 2, 0);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(dot, lv_color_hex(member_color(member)), LV_PART_MAIN);
    lv_obj_set_style_border_width(dot, 0, LV_PART_MAIN);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* label = lv_obj_get_child(btn, 0);
    const std::string text = member_label(member);
    if (label && lv_obj_is_valid(label))
    {
        lv_label_set_text(label, text.c_str());
        lv_obj_set_width(label, LV_PCT(100));
        lv_obj_set_style_pad_left(label, 12, LV_PART_MAIN);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    }

    return btn;
}

void sync_map_notice_overlay()
{
    if (!s_map_notice_panel || !lv_obj_is_valid(s_map_notice_panel) ||
        !s_map_notice_label || !lv_obj_is_valid(s_map_notice_label))
    {
        return;
    }

    const uint32_t now = sys::millis_now();
    if (s_map_notice_text[0] != '\0' && now < s_map_notice_until_ms)
    {
        set_compact_label(s_map_notice_label, s_map_notice_text);
        lv_obj_clear_flag(s_map_notice_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_map_notice_panel);
        return;
    }

    s_map_notice_text[0] = '\0';
    s_map_notice_until_ms = 0;
    lv_obj_add_flag(s_map_notice_panel, LV_OBJ_FLAG_HIDDEN);
}

void sync_map_context_buttons(const ::ui::map::MapWorkspaceSnapshot& snapshot)
{
    const bool show_route = route_context_available();
    ::team::ui::TeamUiSnapshot team_snapshot;
    const bool has_team_members = load_team_snapshot(team_snapshot) && !team_snapshot.members.empty();
    const uint8_t next_mask = static_cast<uint8_t>((show_route ? 0x01 : 0x00) |
                                                   (has_team_members ? 0x02 : 0x00));
    const uint32_t now_ms = sys::millis_now();
    bool group_dirty = next_mask != s_map_context_mask;

    set_hidden(s_map_route_btn, !show_route);
    set_hidden(s_map_context_rail, next_mask == 0);

    if (has_team_members)
    {
        const uint32_t hash = hash_member_list(team_snapshot);
        const bool refresh_due = (now_ms - s_member_panel_last_ms) >= 2000U;
        if (hash != s_member_list_hash ||
            team_snapshot.members.size() != s_member_buttons.size() ||
            refresh_due)
        {
            s_member_panel_last_ms = now_ms;
            if (hash != s_member_list_hash ||
                team_snapshot.members.size() != s_member_buttons.size())
            {
                clear_member_buttons();
                s_member_buttons.reserve(team_snapshot.members.size());
                s_member_button_ids.reserve(team_snapshot.members.size());
                for (const auto& member : team_snapshot.members)
                {
                    const uint32_t member_id = member_id_for_button(member);
                    lv_obj_t* btn = create_member_button(member);
                    if (!btn)
                    {
                        continue;
                    }
                    s_member_buttons.push_back(btn);
                    s_member_button_ids.push_back(member_id);
                }
                s_member_list_hash = hash;
                group_dirty = true;
            }
        }

        if (!member_exists(team_snapshot, s_selected_member_id))
        {
            s_selected_member_id = kInvalidMemberId;
        }
        update_member_button_states();
    }
    else if (!s_member_buttons.empty() || s_member_list_hash != 0)
    {
        clear_member_buttons();
        s_member_list_hash = 0;
        s_selected_member_id = kInvalidMemberId;
        group_dirty = true;
    }

    if (group_dirty)
    {
        s_map_context_mask = next_mask;
        rebuild_map_control_group();
    }
    (void)snapshot;
}

void sync_map_control_labels(const ::ui::map::MapWorkspaceSnapshot& snapshot)
{
    if (!s_map_control_bar || !lv_obj_is_valid(s_map_control_bar))
    {
        return;
    }

    const auto layers = ::ui::widgets::map::current_layer_state();
    set_button_label(s_map_layer_btn, compact_map_source_label(layers.map_source));
    set_button_label(s_map_contour_btn, layers.contour_enabled ? "Contour*" : "Contour");
    sync_map_context_buttons(snapshot);

    char zoom_buf[8]{};
    std::snprintf(zoom_buf, sizeof(zoom_buf), "Z%d", static_cast<int>(current_map_zoom()));
    set_compact_label(s_map_zoom_label, zoom_buf);

    uint8_t missing_source = 0;
    if (::ui::widgets::map::take_missing_tile_notice(s_map_runtime, &missing_source))
    {
        char notice[48]{};
        std::snprintf(notice,
                      sizeof(notice),
                      "%s tile loading",
                      compact_map_source_label(missing_source));
        set_map_notice(notice, 1400);
    }

    sync_map_notice_overlay();
}

lv_obj_t* create_status_row(lv_obj_t* parent, const char* title, lv_obj_t** out_value)
{
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 18);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row,
                          LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, 6, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* key = lv_label_create(row);
    lv_label_set_text(key, title);
    lv_obj_set_width(key, 76);
    lv_obj_set_style_text_font(key, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(key, lv_color_hex(0x6D5B43), 0);
    lv_label_set_long_mode(key, LV_LABEL_LONG_DOT);

    lv_obj_t* value = lv_label_create(row);
    lv_label_set_text(value, "--");
    lv_obj_set_width(value, 0);
    lv_obj_set_flex_grow(value, 1);
    lv_obj_set_style_text_font(value, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(value, lv_color_hex(0x1D160F), 0);
    lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(value, LV_LABEL_LONG_DOT);

    if (out_value)
    {
        *out_value = value;
    }
    return row;
}

void refresh_gps_status_view()
{
    if (!s_root || s_projection != Projection::GpsStatus)
    {
        return;
    }

    ui_update_top_bar_battery(s_top_bar);

    const auto snapshot = gps_status_model().snapshot();
    if (!snapshot.header.valid)
    {
        set_compact_label(s_gps_status_label, "Unavailable");
        set_compact_label(s_gps_coord_label, "--");
        set_compact_label(s_gps_sat_label, "--");
        set_compact_label(s_gps_alt_label, "--");
        set_compact_label(s_gps_motion_label, "--");
        set_compact_label(s_gps_time_label, "--");
        set_compact_label(s_gps_diag_label, "No status source");
        return;
    }

    set_compact_label(s_gps_status_label, snapshot.fix_label.c_str());
    set_compact_label(s_gps_coord_label, snapshot.coordinate_label.c_str());
    set_compact_label(s_gps_sat_label, snapshot.satellite_label.c_str());

    char line[48]{};
    if (snapshot.fix_valid && std::isfinite(snapshot.altitude_m))
    {
        std::snprintf(line, sizeof(line), "%.0f m", static_cast<double>(snapshot.altitude_m));
    }
    else
    {
        std::snprintf(line, sizeof(line), "--");
    }
    set_compact_label(s_gps_alt_label, line);

    if (snapshot.fix_valid)
    {
        std::snprintf(line,
                      sizeof(line),
                      "%.1f m/s %.0f deg",
                      static_cast<double>(snapshot.speed_mps),
                      static_cast<double>(snapshot.course_deg));
    }
    else
    {
        std::snprintf(line, sizeof(line), "--");
    }
    set_compact_label(s_gps_motion_label, line);
    set_compact_label(s_gps_time_label, snapshot.time_label.c_str());
    set_compact_label(s_gps_diag_label, diagnostic_label());
}

std::string trim_copy(std::string value)
{
    const auto is_space = [](unsigned char ch)
    {
        return std::isspace(ch) != 0;
    };

    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [&](char ch)
                                            { return !is_space(static_cast<unsigned char>(ch)); }));
    value.erase(std::find_if(value.rbegin(), value.rend(), [&](char ch)
                             { return !is_space(static_cast<unsigned char>(ch)); })
                    .base(),
                value.end());
    return value;
}

bool ends_with_ignore_case(const std::string& value, const char* suffix)
{
    if (!suffix)
    {
        return false;
    }
    const std::size_t suffix_len = std::strlen(suffix);
    if (value.size() < suffix_len)
    {
        return false;
    }

    const std::size_t start = value.size() - suffix_len;
    for (std::size_t index = 0; index < suffix_len; ++index)
    {
        const unsigned char lhs = static_cast<unsigned char>(value[start + index]);
        const unsigned char rhs = static_cast<unsigned char>(suffix[index]);
        if (std::tolower(lhs) != std::tolower(rhs))
        {
            return false;
        }
    }
    return true;
}

bool parse_double_token(const std::string& token, double& out)
{
    char* end = nullptr;
    out = std::strtod(token.c_str(), &end);
    return end != token.c_str();
}

bool parse_attr_double(const std::string& line, const char* key, double& out)
{
    const std::string token = std::string(key) + "=\"";
    const std::size_t start = line.find(token);
    if (start == std::string::npos)
    {
        return false;
    }
    const std::size_t value_start = start + token.size();
    const std::size_t value_end = line.find('"', value_start);
    if (value_end == std::string::npos || value_end <= value_start)
    {
        return false;
    }
    return parse_double_token(line.substr(value_start, value_end - value_start), out);
}

bool ascii_equal_ignore_case(const std::string& value, const char* expected)
{
    if (!expected)
    {
        return false;
    }
    const std::size_t expected_len = std::strlen(expected);
    if (value.size() != expected_len)
    {
        return false;
    }
    for (std::size_t index = 0; index < expected_len; ++index)
    {
        const unsigned char lhs = static_cast<unsigned char>(value[index]);
        const unsigned char rhs = static_cast<unsigned char>(expected[index]);
        if (std::tolower(lhs) != std::tolower(rhs))
        {
            return false;
        }
    }
    return true;
}

bool kml_tag_name_matches(const std::string& tag, const char* expected, bool& closing)
{
    closing = false;
    std::size_t index = 0;
    while (index < tag.size() &&
           std::isspace(static_cast<unsigned char>(tag[index])))
    {
        ++index;
    }
    if (index < tag.size() && tag[index] == '/')
    {
        closing = true;
        ++index;
    }
    while (index < tag.size() &&
           std::isspace(static_cast<unsigned char>(tag[index])))
    {
        ++index;
    }

    const std::size_t name_start = index;
    while (index < tag.size())
    {
        const char ch = tag[index];
        if (std::isspace(static_cast<unsigned char>(ch)) || ch == '/' || ch == '>')
        {
            break;
        }
        ++index;
    }
    if (index <= name_start)
    {
        return false;
    }

    std::string name = tag.substr(name_start, index - name_start);
    const std::size_t colon = name.find(':');
    if (colon != std::string::npos && colon + 1 < name.size())
    {
        name.erase(0, colon + 1);
    }
    return ascii_equal_ignore_case(name, expected);
}

bool parse_kml_coordinate_token(const std::string& token, double& lat, double& lon)
{
    const std::size_t comma1 = token.find(',');
    if (comma1 == std::string::npos || comma1 == 0)
    {
        return false;
    }

    const std::size_t comma2 = token.find(',', comma1 + 1);
    const std::string lon_token = token.substr(0, comma1);
    const std::string lat_token = comma2 == std::string::npos
                                      ? token.substr(comma1 + 1)
                                      : token.substr(comma1 + 1, comma2 - comma1 - 1);
    if (lat_token.empty())
    {
        return false;
    }
    return parse_double_token(lon_token, lon) &&
           parse_double_token(lat_token, lat);
}

template <typename ChunkHandler>
bool read_track_file_chunks(const char* path, ChunkHandler on_chunk)
{
    const std::string normalized = ::ui::fs::normalize_path(path);
    if (normalized.empty())
    {
        return false;
    }

    lv_fs_file_t file;
    if (lv_fs_open(&file, normalized.c_str(), LV_FS_MODE_RD) != LV_FS_RES_OK)
    {
        return false;
    }

    char buffer[kTrackFileReadBufferBytes];
    bool ok = true;
    while (true)
    {
        uint32_t bytes_read = 0;
        if (lv_fs_read(&file, buffer, sizeof(buffer), &bytes_read) != LV_FS_RES_OK)
        {
            ok = false;
            break;
        }
        if (bytes_read == 0)
        {
            break;
        }

        on_chunk(buffer, bytes_read);
    }

    lv_fs_close(&file);
    return ok;
}

template <typename LineHandler>
bool read_track_file_lines(const char* path, LineHandler on_line)
{
    std::string line;
    bool discard_line = false;
    const bool ok = read_track_file_chunks(path, [&](const char* buffer, uint32_t bytes_read)
                                           {
        for (uint32_t index = 0; index < bytes_read; ++index)
        {
            const char ch = buffer[index];
            if (ch == '\n')
            {
                if (!discard_line)
                {
                    if (!line.empty() && line.back() == '\r')
                    {
                        line.pop_back();
                    }
                    on_line(line);
                }
                line.clear();
                discard_line = false;
                continue;
            }

            if (discard_line)
            {
                continue;
            }
            if (line.size() >= kMaxTrackFileLineBytes)
            {
                line.clear();
                discard_line = true;
                continue;
            }
            line.push_back(ch);
        } });

    if (ok && !discard_line && !line.empty())
    {
        if (line.back() == '\r')
        {
            line.pop_back();
        }
        on_line(line);
    }
    return ok;
}

struct KmlCoordinateStreamParser
{
    std::vector<TrackOverlayPoint>& points;
    bool in_tag = false;
    bool tag_truncated = false;
    bool in_coordinates = false;
    bool discard_token = false;
    std::string tag;
    std::string token;

    explicit KmlCoordinateStreamParser(std::vector<TrackOverlayPoint>& out)
        : points(out)
    {
        tag.reserve(kMaxKmlTagBytes);
        token.reserve(kMaxKmlCoordinateTokenBytes);
    }

    void flush_token()
    {
        if (discard_token)
        {
            token.clear();
            discard_token = false;
            return;
        }
        if (token.empty())
        {
            return;
        }

        double lat = 0.0;
        double lon = 0.0;
        if (parse_kml_coordinate_token(token, lat, lon))
        {
            append_track_point(points, lat, lon);
        }
        token.clear();
    }

    void handle_tag()
    {
        if (tag_truncated)
        {
            tag.clear();
            tag_truncated = false;
            return;
        }

        bool closing = false;
        if (kml_tag_name_matches(tag, "coordinates", closing))
        {
            if (closing)
            {
                flush_token();
                in_coordinates = false;
            }
            else
            {
                in_coordinates = true;
                token.clear();
                discard_token = false;
            }
        }
        tag.clear();
    }

    void consume_char(char ch)
    {
        if (in_tag)
        {
            if (ch == '>')
            {
                in_tag = false;
                handle_tag();
                return;
            }
            if (tag.size() < kMaxKmlTagBytes)
            {
                tag.push_back(ch);
            }
            else
            {
                tag_truncated = true;
            }
            return;
        }

        if (ch == '<')
        {
            if (in_coordinates)
            {
                flush_token();
            }
            in_tag = true;
            tag.clear();
            tag_truncated = false;
            return;
        }

        if (!in_coordinates)
        {
            return;
        }
        if (std::isspace(static_cast<unsigned char>(ch)))
        {
            flush_token();
            return;
        }
        if (discard_token)
        {
            return;
        }
        if (token.size() >= kMaxKmlCoordinateTokenBytes)
        {
            token.clear();
            discard_token = true;
            return;
        }
        token.push_back(ch);
    }

    void consume(const char* data, uint32_t size)
    {
        if (!data)
        {
            return;
        }
        for (uint32_t index = 0; index < size; ++index)
        {
            consume_char(data[index]);
        }
    }

    void finish()
    {
        if (in_coordinates)
        {
            flush_token();
        }
    }
};

void downsample_track_points(std::vector<TrackOverlayPoint>& points)
{
    if (points.size() <= kMaxTrackOverlayPoints)
    {
        return;
    }

    const std::size_t total = points.size();
    for (std::size_t index = 0; index < kMaxTrackOverlayPoints; ++index)
    {
        const std::size_t src = (index * (total - 1)) / (kMaxTrackOverlayPoints - 1);
        points[index] = points[src];
    }
    points.resize(kMaxTrackOverlayPoints);
}

void append_track_point(std::vector<TrackOverlayPoint>& out,
                        double lat,
                        double lon)
{
    TrackOverlayPoint point{};
    point.lat = lat;
    point.lon = lon;
    out.push_back(point);
    downsample_track_points(out);
}

bool load_gpx_track_points(const char* path, std::vector<TrackOverlayPoint>& out)
{
    out.clear();
    if (!platform::ui::device::sd_ready())
    {
        return false;
    }

    out.reserve(kMaxTrackOverlayPoints);
    const bool read_ok = read_track_file_lines(path, [&out](const std::string& line)
                                               {
        if (line.find("<trkpt") != std::string::npos)
        {
            double lat = 0.0;
            double lon = 0.0;
            if (parse_attr_double(line, "lat", lat) && parse_attr_double(line, "lon", lon))
            {
                append_track_point(out, lat, lon);
            }
        } });
    return read_ok && !out.empty();
}

bool load_csv_track_points(const char* path, std::vector<TrackOverlayPoint>& out)
{
    out.clear();
    if (!platform::ui::device::sd_ready())
    {
        return false;
    }

    out.reserve(kMaxTrackOverlayPoints);
    const bool read_ok = read_track_file_lines(path, [&out](std::string line)
                                               {
        line = trim_copy(std::move(line));
        if (!line.empty())
        {
            const char first = line.front();
            if ((first >= '0' && first <= '9') || first == '-')
            {
                const std::size_t comma1 = line.find(',');
                if (comma1 != std::string::npos && comma1 > 0)
                {
                    const std::size_t comma2 = line.find(',', comma1 + 1);
                    const std::string lat_str = line.substr(0, comma1);
                    const std::string lon_str = comma2 != std::string::npos
                                                    ? line.substr(comma1 + 1, comma2 - comma1 - 1)
                                                    : line.substr(comma1 + 1);

                    double lat = 0.0;
                    double lon = 0.0;
                    if (parse_double_token(lat_str, lat) && parse_double_token(lon_str, lon))
                    {
                        append_track_point(out, lat, lon);
                    }
                }
            }
        } });
    return read_ok && !out.empty();
}

bool load_kml_track_points(const char* path, std::vector<TrackOverlayPoint>& out)
{
    out.clear();
    if (!platform::ui::device::sd_ready())
    {
        return false;
    }

    out.reserve(kMaxTrackOverlayPoints);
    KmlCoordinateStreamParser parser(out);
    const bool read_ok = read_track_file_chunks(path, [&](const char* data, uint32_t size)
                                                { parser.consume(data, size); });
    parser.finish();
    return read_ok && !out.empty();
}

void append_track_overlay(::ui::map::MapOverlaySnapshot& snapshot)
{
    if (!s_track_overlay_active || s_track_points.empty())
    {
        return;
    }

    const std::size_t available =
        snapshot.item_count < ::ui::map::MapOverlaySnapshot::kMaxItems
            ? ::ui::map::MapOverlaySnapshot::kMaxItems - snapshot.item_count
            : 0;
    if (available == 0)
    {
        snapshot.truncated = true;
        return;
    }

    const std::size_t total = s_track_points.size();
    const std::size_t count = std::min<std::size_t>(available, total);
    for (std::size_t index = 0; index < count; ++index)
    {
        const std::size_t src = count == total
                                    ? index
                                    : (index * (total - 1)) / (count - 1 == 0 ? 1 : count - 1);
        const auto& point = s_track_points[src];
        auto& item = snapshot.items[snapshot.item_count++];
        const bool is_route = s_track_overlay_kind == TrackOverlayFileKind::Route;
        item.kind = is_route ? ::ui::map::MapOverlayKind::RoutePoint
                             : ::ui::map::MapOverlayKind::TrackPoint;
        item.style = is_route ? ::ui::map::MapOverlayStyle::Route
                              : ::ui::map::MapOverlayStyle::Track;
        item.point.valid = true;
        item.point.lat = point.lat;
        item.point.lon = point.lon;
        item.stable_id = static_cast<uint32_t>((is_route ? 0x52540000U : 0x54520000U) + index);
        item.visible = true;
    }

    if (count < total)
    {
        snapshot.truncated = true;
    }
}

bool load_map_track_file_impl(const char* path, bool show_fail_toast)
{
    if (!path || path[0] == '\0')
    {
        return false;
    }

    std::vector<TrackOverlayPoint> points;
    const std::string normalized = ::ui::fs::normalize_path(path);
    TrackOverlayFileKind file_kind = TrackOverlayFileKind::Track;
    bool loaded = false;
    if (ends_with_ignore_case(normalized, ".csv"))
    {
        loaded = load_csv_track_points(path, points);
    }
    else if (ends_with_ignore_case(normalized, ".kml"))
    {
        loaded = load_kml_track_points(path, points);
        file_kind = TrackOverlayFileKind::Route;
    }
    else
    {
        loaded = load_gpx_track_points(path, points);
    }
    if (!loaded)
    {
        if (show_fail_toast)
        {
            set_map_notice("No track yet", 1500);
            request_refresh_view();
        }
        s_track_overlay_active = false;
        s_track_points.clear();
        s_track_file.clear();
        s_track_overlay_kind = TrackOverlayFileKind::Track;
        return false;
    }

    s_track_file = path;
    s_track_points = std::move(points);
    s_track_overlay_active = true;
    s_track_overlay_kind = file_kind;
    if (!s_track_points.empty())
    {
        const auto& last = s_track_points.back();
        auto& model = map_workspace_model();
        auto viewport = model.viewport();
        viewport.center_lat = last.lat;
        viewport.center_lon = last.lon;
        viewport.zoom = kDefaultTrackerZoom;
        (void)model.setViewport(viewport);
        s_map_zoom = kDefaultTrackerZoom;
        s_map_pan_x = 0;
        s_map_pan_y = 0;
    }

    set_map_notice(file_kind == TrackOverlayFileKind::Route ? "Route loaded" : "Track loaded", 1200);
    request_refresh_view();
    return true;
}

void map_gesture_callback(const ::ui::widgets::map::GestureEvent& event, void*)
{
    switch (event.phase)
    {
    case ::ui::widgets::map::GesturePhase::Pressed:
        s_map_drag_start_pan_x = s_map_pan_x;
        s_map_drag_start_pan_y = s_map_pan_y;
        s_map_drag_active = false;
        break;
    case ::ui::widgets::map::GesturePhase::DragBegin:
        s_map_drag_active = true;
        apply_map_drag_preview();
        break;
    case ::ui::widgets::map::GesturePhase::DragUpdate:
        s_map_drag_active = true;
        s_map_pan_x = s_map_drag_start_pan_x + event.total_dx;
        s_map_pan_y = s_map_drag_start_pan_y + event.total_dy;
        apply_map_drag_preview();
        break;
    case ::ui::widgets::map::GesturePhase::DragEnd:
    case ::ui::widgets::map::GesturePhase::Cancel:
        if (s_map_drag_active)
        {
            (void)sync_workspace_center_from_screen();
            s_map_pan_x = 0;
            s_map_pan_y = 0;
            sync_workspace_viewport_from_renderer();
            request_refresh_view();
        }
        s_map_drag_active = false;
        break;
    }
}

void apply_map_drag_preview()
{
    if (!s_root)
    {
        return;
    }

    const auto snapshot = map_workspace_model().snapshot();
    if (!snapshot.header.valid)
    {
        return;
    }

    ::ui::widgets::map::apply_model_lightweight(
        s_map_runtime,
        build_map_model(snapshot));
}

void refresh_view()
{
    if (!s_root)
    {
        return;
    }

    ui_update_top_bar_battery(s_top_bar);
    update_map_top_bar_title();

    sync_workspace_layers_from_renderer();
    auto snapshot = map_workspace_model().snapshot();
    (void)map_overlay_source().buildMapOverlaySnapshot(s_overlay_snapshot);
    append_track_overlay(s_overlay_snapshot);

    if (snapshot.header.valid)
    {
        if (s_map_drag_active)
        {
            ::ui::widgets::map::apply_model_lightweight(
                s_map_runtime,
                build_map_model(snapshot));
        }
        else
        {
            ::ui::widgets::map::apply_model(s_map_runtime, build_map_model(snapshot));
        }
        if (!s_map_drag_active && commit_pending_map_pan_from_screen())
        {
            snapshot = map_workspace_model().snapshot();
            ::ui::widgets::map::apply_model(s_map_runtime, build_map_model(snapshot));
        }
        if (!s_map_drag_active)
        {
            ::ui::widgets::map::apply_overlay(s_map_runtime, s_overlay_snapshot);
        }
    }
    else
    {
        ::ui::widgets::map::clear(s_map_runtime);
    }
    sync_map_control_labels(snapshot);
}

void refresh_view_async(void*)
{
    s_map_refresh_pending = false;
    refresh_view();
}

void request_refresh_view()
{
    if (s_map_refresh_pending)
    {
        return;
    }

    s_map_refresh_pending = true;
    lv_async_call(refresh_view_async, nullptr);
}

class SharedGpsRuntimeRefreshModel final : public ::ui::screens::gps::IGpsStatusRefreshModel
{
  public:
    void refresh() override {}
};

class SharedGpsUiRefreshSink final : public ::ui::screens::gps::IGpsUiRefreshSink
{
  public:
    void onGpsRuntimeUpdated() override
    {
        if (s_projection == Projection::GpsStatus)
        {
            refresh_gps_status_view();
            return;
        }
        if (s_map_drag_active)
        {
            return;
        }
        refresh_view();
    }
};

SharedGpsRuntimeRefreshModel& gps_runtime_refresh_model()
{
    static SharedGpsRuntimeRefreshModel model;
    return model;
}

SharedGpsUiRefreshSink& gps_runtime_refresh_sink()
{
    static SharedGpsUiRefreshSink sink;
    return sink;
}

::ui::screens::gps::GpsPageRuntimePump& gps_runtime_pump()
{
    static ::ui::screens::gps::GpsPageRuntimePump pump(
        gps_runtime_refresh_model(),
        &gps_runtime_refresh_sink(),
        750);
    return pump;
}

void refresh_timer_cb(lv_timer_t* timer)
{
    (void)timer;
    gps_runtime_pump().update(sys::millis_now());
}

void consume_key_event(lv_event_t* e)
{
    if (!e)
    {
        return;
    }

    lv_event_stop_bubbling(e);
    lv_event_stop_processing(e);
}

void open_map_help_modal_async(void*)
{
    s_map_help_open_pending = false;
    open_map_help_modal();
}

void request_open_map_help_modal()
{
    if (s_map_help_open_pending)
    {
        return;
    }

    s_map_help_open_pending = true;
    lv_async_call(open_map_help_modal_async, nullptr);
}

lv_obj_t* create_map_control_button(lv_obj_t* parent,
                                    lv_coord_t width,
                                    const char* text,
                                    MapControlAction action);
void add_map_controls_to_group(lv_group_t* group);

void adjust_map_zoom(int delta)
{
    const int next_zoom = std::max(::ui::widgets::map::kMinZoom,
                                   std::min(s_map_zoom + delta,
                                            ::ui::widgets::map::kMaxZoom));
    if (next_zoom == s_map_zoom)
    {
        return;
    }

    (void)sync_workspace_center_from_screen();
    s_map_pan_x = 0;
    s_map_pan_y = 0;
    s_map_zoom = next_zoom;
    sync_workspace_viewport_from_renderer();
    request_refresh_view();
}

void center_map_on_self()
{
    const auto result = map_workspace_model().centerOnSelf();
    const auto snapshot = map_workspace_model().snapshot();
    const auto& config = app::configFacade().getConfig();
    if (result.ok)
    {
        s_map_pan_x = 0;
        s_map_pan_y = 0;
        set_map_notice("Centered", 900);
        std::printf("[MAP][POS] center_on_self ok self_valid=%d self_lat=%.6f self_lon=%.6f viewport_lat=%.6f viewport_lon=%.6f zoom=%u map_coord=%u\n",
                    snapshot.self.valid ? 1 : 0,
                    snapshot.self.lat,
                    snapshot.self.lon,
                    snapshot.viewport.center_lat,
                    snapshot.viewport.center_lon,
                    static_cast<unsigned>(snapshot.viewport.zoom),
                    static_cast<unsigned>(config.map_coord_system));
    }
    else
    {
        set_map_notice("No GPS fix", 1200);
        std::printf("[MAP][POS] center_on_self failed self_valid=%d self_lat=%.6f self_lon=%.6f status=%s map_coord=%u\n",
                    snapshot.self.valid ? 1 : 0,
                    snapshot.self.lat,
                    snapshot.self.lon,
                    snapshot.status_line.c_str(),
                    static_cast<unsigned>(config.map_coord_system));
    }
    sync_workspace_viewport_from_renderer();
    request_refresh_view();
}

void cycle_map_layer()
{
    const auto layer_state = ::ui::widgets::map::current_layer_state();
    const uint8_t next_source = static_cast<uint8_t>((layer_state.map_source + 1U) % 3U);
    ::ui::widgets::map::LayerNotice notice{};
    (void)::ui::widgets::map::set_layer_map_source(next_source, &notice);
    if (notice.has_message)
    {
        set_map_notice(notice.message, notice.duration_ms > 0 ? notice.duration_ms : 1400);
    }
    else
    {
        char text[40]{};
        std::snprintf(text,
                      sizeof(text),
                      "Base %s",
                      compact_map_source_label(next_source));
        set_map_notice(text, 900);
    }
    sync_workspace_layers_from_renderer();
    request_refresh_view();
}

void toggle_map_contour()
{
    ::ui::widgets::map::LayerNotice notice{};
    (void)::ui::widgets::map::toggle_layer_contour(&notice);
    const auto layer_state = ::ui::widgets::map::current_layer_state();
    if (notice.has_message)
    {
        set_map_notice(notice.message, notice.duration_ms > 0 ? notice.duration_ms : 1400);
    }
    else
    {
        set_map_notice(layer_state.contour_enabled ? "Contour on" : "Contour off", 900);
    }
    sync_workspace_layers_from_renderer();
    request_refresh_view();
}

void close_map_help_modal()
{
    if (!s_map_help_modal || !lv_obj_is_valid(s_map_help_modal))
    {
        s_map_help_modal = nullptr;
        return;
    }

    lv_obj_del(s_map_help_modal);
    s_map_help_modal = nullptr;
    if (app_g)
    {
        lv_group_remove_all_objs(app_g);
        if (s_top_bar.back_btn)
        {
            lv_group_add_obj(app_g, s_top_bar.back_btn);
        }
        add_map_controls_to_group(app_g);
        if (s_map_help_btn)
        {
            lv_group_focus_obj(s_map_help_btn);
        }
    }
}

void on_map_help_modal_key(lv_event_t* e)
{
    const uint32_t key = lv_event_get_key(e);
    if (key == LV_KEY_BACKSPACE || key == LV_KEY_ESC || key == LV_KEY_ENTER ||
        is_help_key(key))
    {
        consume_key_event(e);
        close_map_help_modal();
        return;
    }

    consume_key_event(e);
}

void open_map_help_modal()
{
    if (s_map_help_modal && lv_obj_is_valid(s_map_help_modal))
    {
        close_map_help_modal();
        return;
    }
    if (!s_root || !lv_obj_is_valid(s_root))
    {
        return;
    }

    s_map_help_modal = lv_obj_create(s_root);
    lv_obj_set_size(s_map_help_modal, LV_PCT(100), LV_PCT(100));
    lv_obj_align(s_map_help_modal, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_add_flag(s_map_help_modal, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_style_bg_color(s_map_help_modal, lv_color_hex(0x1C1812), 0);
    lv_obj_set_style_bg_opa(s_map_help_modal, LV_OPA_70, 0);
    lv_obj_set_style_border_width(s_map_help_modal, 0, 0);
    lv_obj_set_style_pad_all(s_map_help_modal, 4, 0);
    lv_obj_clear_flag(s_map_help_modal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_map_help_modal, on_map_help_modal_key, LV_EVENT_KEY, nullptr);

    lv_obj_t* panel = lv_obj_create(s_map_help_modal);
    lv_obj_set_size(panel, 304, 176);
    lv_obj_center(panel);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0xFFF3DF), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(0x8A6E43), 0);
    lv_obj_set_style_radius(panel, 4, 0);
    lv_obj_set_style_pad_left(panel, 7, 0);
    lv_obj_set_style_pad_right(panel, 7, 0);
    lv_obj_set_style_pad_top(panel, 5, 0);
    lv_obj_set_style_pad_bottom(panel, 5, 0);
    lv_obj_set_style_pad_row(panel, 2, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(panel, on_map_help_modal_key, LV_EVENT_KEY, nullptr);

    lv_obj_t* title = lv_label_create(panel);
    lv_label_set_text(title, "Map Help");
    lv_obj_set_width(title, LV_PCT(100));
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x25170D), 0);

    auto add_keycap = [](lv_obj_t* parent, const char* text, lv_coord_t width)
    {
        lv_obj_t* keycap = lv_label_create(parent);
        lv_obj_set_size(keycap, width, 14);
        lv_obj_set_style_bg_color(keycap, lv_color_hex(0xF8E6C3), 0);
        lv_obj_set_style_bg_opa(keycap, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(keycap, 1, 0);
        lv_obj_set_style_border_color(keycap, lv_color_hex(0x8A6E43), 0);
        lv_obj_set_style_radius(keycap, 3, 0);
        lv_obj_set_style_text_font(keycap, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(keycap, lv_color_hex(0x25170D), 0);
        lv_obj_set_style_text_align(keycap, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(keycap, LV_LABEL_LONG_CLIP);
        lv_label_set_text(keycap, text ? text : "");
        return keycap;
    };

    auto add_help_row = [&](const char* primary,
                            const char* secondary,
                            const char* description)
    {
        lv_obj_t* row = lv_obj_create(panel);
        lv_obj_set_size(row, LV_PCT(100), 15);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row,
                              LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_style_pad_column(row, 3, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* keys = lv_obj_create(row);
        lv_obj_set_size(keys, 76, 15);
        lv_obj_set_flex_flow(keys, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(keys,
                              LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_bg_opa(keys, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(keys, 0, 0);
        lv_obj_set_style_pad_all(keys, 0, 0);
        lv_obj_set_style_pad_column(keys, 2, 0);
        lv_obj_clear_flag(keys, LV_OBJ_FLAG_SCROLLABLE);

        if (secondary && secondary[0] != '\0')
        {
            const lv_coord_t secondary_width =
                std::strlen(secondary) > 4 ? 48 : (std::strlen(secondary) > 2 ? 34 : 22);
            add_keycap(keys, primary, std::strlen(primary) > 2 ? 34 : 22);
            add_keycap(keys, secondary, secondary_width);
        }
        else
        {
            add_keycap(keys, primary, 72);
        }

        lv_obj_t* text = lv_label_create(row);
        lv_obj_set_width(text, 0);
        lv_obj_set_flex_grow(text, 1);
        lv_obj_set_style_text_font(text, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(text, lv_color_hex(0x3E2B18), 0);
        lv_label_set_long_mode(text, LV_LABEL_LONG_DOT);
        lv_label_set_text(text, description ? description : "");
    };

    add_help_row("WASD", nullptr, "Move map");
    add_help_row("Q", "E", "Zoom map");
    add_help_row("P", "Pos", "Center current position");
    add_help_row("L", nullptr, "Change base layer");
    add_help_row("O", "Contour", "Toggle contour overlay");
    add_help_row("T", "Track", "Select track file");
    add_help_row("Route", nullptr, "Shown when route active");
    add_help_row("Members", nullptr, "Shown when team active");
    add_help_row(help_key_label(), "Back", "Close help");

    lv_obj_move_foreground(s_map_help_modal);
    if (app_g)
    {
        lv_group_remove_all_objs(app_g);
        lv_group_add_obj(app_g, panel);
        lv_group_focus_obj(panel);
    }
}

void close_tracker_modal()
{
    if (!s_tracker_modal || !lv_obj_is_valid(s_tracker_modal))
    {
        s_tracker_modal = nullptr;
        return;
    }

    lv_obj_del(s_tracker_modal);
    s_tracker_modal = nullptr;
    rebuild_map_control_group();
    if (app_g && s_map_tracker_btn && lv_obj_is_valid(s_map_tracker_btn))
    {
        lv_group_focus_obj(s_map_tracker_btn);
    }
}

void tracker_modal_bg_event_cb(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }
    if (lv_event_get_target_obj(e) == s_tracker_modal)
    {
        close_tracker_modal();
    }
}

void tracker_modal_close_event_cb(lv_event_t* e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED)
    {
        close_tracker_modal();
    }
}

void tracker_modal_key_event_cb(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_KEY)
    {
        return;
    }

    const uint32_t key = lv_event_get_key(e);
    if (key == LV_KEY_BACKSPACE || key == LV_KEY_ESC)
    {
        consume_key_event(e);
        close_tracker_modal();
    }
}

void tracker_track_button_event_cb(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    const std::uintptr_t index = reinterpret_cast<std::uintptr_t>(lv_event_get_user_data(e));
    if (index >= s_track_modal_names.size())
    {
        return;
    }

    const std::string path =
        std::string(::platform::ui::tracker::track_dir()) + "/" + s_track_modal_names[index];
    (void)load_map_track_file_impl(path.c_str(), true);
    close_tracker_modal();
}

lv_obj_t* create_tracker_modal_button(lv_obj_t* parent,
                                      const char* text,
                                      lv_event_cb_t cb,
                                      void* user_data)
{
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_width(btn, LV_PCT(100));
    lv_obj_set_height(btn, ::ui::page_profile::current().large_touch_hitbox ? 52 : 24);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0xF8E6C3), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x8A6E43), LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_left(btn, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_right(btn, 8, LV_PART_MAIN);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);
    lv_obj_add_event_cb(btn, tracker_modal_key_event_cb, LV_EVENT_KEY, nullptr);

    lv_obj_t* label = lv_label_create(btn);
    lv_label_set_text(label, text ? text : "");
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(0x25170D), LV_PART_MAIN);
    lv_obj_center(label);
    return btn;
}

void open_tracker_modal()
{
    if (s_tracker_modal && lv_obj_is_valid(s_tracker_modal))
    {
        close_tracker_modal();
        return;
    }
    if (!s_root || !lv_obj_is_valid(s_root))
    {
        return;
    }
    if (!platform::ui::device::sd_ready())
    {
        set_map_notice("No SD Card", 1200);
        request_refresh_view();
        return;
    }

    s_tracker_modal = lv_obj_create(s_root);
    lv_obj_set_size(s_tracker_modal, LV_PCT(100), LV_PCT(100));
    lv_obj_align(s_tracker_modal, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_add_flag(s_tracker_modal, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_add_flag(s_tracker_modal, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_tracker_modal, lv_color_hex(0x1C1812), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_tracker_modal, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_tracker_modal, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_tracker_modal, 4, LV_PART_MAIN);
    lv_obj_clear_flag(s_tracker_modal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_tracker_modal, tracker_modal_bg_event_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(s_tracker_modal, tracker_modal_key_event_cb, LV_EVENT_KEY, nullptr);

    const bool touch_layout = ::ui::page_profile::current().large_touch_hitbox;
    const auto size = ::ui::page_profile::resolve_modal_size(touch_layout ? 520 : 300,
                                                             touch_layout ? 520 : 178);
    lv_obj_t* panel = lv_obj_create(s_tracker_modal);
    lv_obj_set_size(panel, size.width, size.height);
    lv_obj_center(panel);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0xFFF3DF), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(panel, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(panel, lv_color_hex(0x8A6E43), LV_PART_MAIN);
    lv_obj_set_style_radius(panel, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_all(panel, touch_layout ? 12 : 6, LV_PART_MAIN);
    lv_obj_set_style_pad_row(panel, touch_layout ? 8 : 3, LV_PART_MAIN);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(panel, tracker_modal_key_event_cb, LV_EVENT_KEY, nullptr);

    lv_obj_t* title = lv_label_create(panel);
    lv_label_set_text(title, "Select Track");
    lv_obj_set_width(title, LV_PCT(100));
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_hex(0x25170D), LV_PART_MAIN);

    lv_obj_t* list = lv_obj_create(panel);
    lv_obj_set_width(list, LV_PCT(100));
    lv_obj_set_height(list, 0);
    lv_obj_set_flex_grow(list, 1);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(list, touch_layout ? 6 : 2, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);

    s_track_modal_names.clear();
    ::platform::ui::tracker::list_tracks(s_track_modal_names, 64);
    std::sort(s_track_modal_names.begin(), s_track_modal_names.end());

    lv_obj_t* first_focus = nullptr;
    std::vector<lv_obj_t*> focusables;
    if (s_track_modal_names.empty())
    {
        lv_obj_t* empty = lv_label_create(list);
        lv_label_set_text(empty, "No track files");
        lv_obj_set_width(empty, LV_PCT(100));
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_style_text_font(empty, &lv_font_montserrat_12, LV_PART_MAIN);
        lv_obj_set_style_text_color(empty, lv_color_hex(0x6D5B43), LV_PART_MAIN);
    }
    else
    {
        for (std::size_t index = 0; index < s_track_modal_names.size(); ++index)
        {
            lv_obj_t* btn = create_tracker_modal_button(
                list,
                s_track_modal_names[index].c_str(),
                tracker_track_button_event_cb,
                reinterpret_cast<void*>(index));
            if (!first_focus)
            {
                first_focus = btn;
            }
            focusables.push_back(btn);
        }
    }

    lv_obj_t* close_btn =
        create_tracker_modal_button(panel, "Close", tracker_modal_close_event_cb, nullptr);
    if (!first_focus)
    {
        first_focus = close_btn;
    }
    focusables.push_back(close_btn);

    lv_obj_move_foreground(s_tracker_modal);
    if (app_g)
    {
        lv_group_remove_all_objs(app_g);
        for (lv_obj_t* obj : focusables)
        {
            if (obj)
            {
                lv_group_add_obj(app_g, obj);
            }
        }
        if (first_focus)
        {
            lv_group_focus_obj(first_focus);
        }
    }
}

void show_route_context_notice()
{
    if (!route_context_available())
    {
        set_map_notice("No route", 1200);
        request_refresh_view();
        return;
    }

    if (!load_configured_route_overlay(true))
    {
        set_map_notice("Route load failed", 1500);
        request_refresh_view();
        return;
    }
    set_map_notice("Route active", 1200);
    request_refresh_view();
}

void show_team_overlay_notice()
{
    const auto snapshot = map_workspace_model().snapshot();
    if (!snapshot.team.available)
    {
        set_map_notice("No team", 1200);
        request_refresh_view();
        return;
    }

    char message[48]{};
    std::snprintf(message,
                  sizeof(message),
                  "Team %u/%u",
                  static_cast<unsigned>(snapshot.team.visible_members),
                  static_cast<unsigned>(snapshot.team.stale_members));
    set_map_notice(message, 1400);
    request_refresh_view();
}

void on_map_control_clicked(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    const auto action = static_cast<MapControlAction>(
        reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
    switch (action)
    {
    case MapControlAction::ZoomOut:
        adjust_map_zoom(-1);
        break;
    case MapControlAction::ZoomIn:
        adjust_map_zoom(1);
        break;
    case MapControlAction::Center:
        center_map_on_self();
        break;
    case MapControlAction::Layer:
        cycle_map_layer();
        break;
    case MapControlAction::Contour:
        toggle_map_contour();
        break;
    case MapControlAction::Tracker:
        open_tracker_modal();
        break;
    case MapControlAction::Help:
        request_open_map_help_modal();
        break;
    case MapControlAction::Route:
        show_route_context_notice();
        break;
    case MapControlAction::TeamMember:
        break;
    }
}

bool handle_map_key(uint32_t key, lv_event_t* e)
{
    switch (key)
    {
    case kLvglFunctionKeyF1:
        if (!is_help_key(key))
        {
            return false;
        }
        consume_key_event(e);
        request_open_map_help_modal();
        return true;
    case 'a':
    case 'A':
    case LV_KEY_LEFT:
        s_map_pan_x += gps_ui::kMapPanStep;
        break;
    case 'd':
    case 'D':
    case LV_KEY_RIGHT:
        s_map_pan_x -= gps_ui::kMapPanStep;
        break;
    case 'w':
    case 'W':
    case LV_KEY_UP:
        s_map_pan_y += gps_ui::kMapPanStep;
        break;
    case 's':
    case 'S':
    case LV_KEY_DOWN:
        s_map_pan_y -= gps_ui::kMapPanStep;
        break;
    case 'q':
    case 'Q':
        adjust_map_zoom(-1);
        consume_key_event(e);
        return true;
    case 'e':
    case 'E':
        adjust_map_zoom(1);
        consume_key_event(e);
        return true;
    case 'c':
    case 'C':
    case 'p':
    case 'P':
        center_map_on_self();
        consume_key_event(e);
        return true;
    case 'l':
    case 'L':
        cycle_map_layer();
        consume_key_event(e);
        return true;
    case 'o':
    case 'O':
        toggle_map_contour();
        consume_key_event(e);
        return true;
    case 't':
    case 'T':
        open_tracker_modal();
        consume_key_event(e);
        return true;
    case 'h':
    case 'H':
        if (!is_help_key(key))
        {
            return false;
        }
        request_open_map_help_modal();
        consume_key_event(e);
        return true;
    case 'r':
    case 'R':
        show_route_context_notice();
        consume_key_event(e);
        return true;
    default:
        return false;
    }

    sync_workspace_viewport_from_renderer();
    request_refresh_view();
    consume_key_event(e);
    return true;
}

lv_obj_t* create_map_control_button(lv_obj_t* parent,
                                    lv_coord_t width,
                                    const char* text,
                                    MapControlAction action)
{
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_size(btn, width, kMapControlButtonHeight);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0xF8E6C3), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x8A6E43), 0);
    lv_obj_set_style_radius(btn, 4, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_set_style_text_color(btn, lv_color_hex(0x25170D), 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn,
                        on_map_control_clicked,
                        LV_EVENT_CLICKED,
                        reinterpret_cast<void*>(static_cast<uintptr_t>(action)));
    lv_obj_add_event_cb(btn, root_key_event_cb, LV_EVENT_KEY, nullptr);

    lv_obj_t* label = lv_label_create(btn);
    lv_label_set_text(label, text ? text : "");
    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_obj_center(label);
    return btn;
}

void add_map_controls_to_group(lv_group_t* group)
{
    if (!group)
    {
        return;
    }
    if (s_map_zoom_out_btn) lv_group_add_obj(group, s_map_zoom_out_btn);
    if (s_map_zoom_in_btn) lv_group_add_obj(group, s_map_zoom_in_btn);
    if (s_map_center_btn) lv_group_add_obj(group, s_map_center_btn);
    if (s_map_layer_btn) lv_group_add_obj(group, s_map_layer_btn);
    if (s_map_contour_btn) lv_group_add_obj(group, s_map_contour_btn);
    if (s_map_tracker_btn) lv_group_add_obj(group, s_map_tracker_btn);
    if (s_map_help_btn) lv_group_add_obj(group, s_map_help_btn);
    if (map_control_visible(s_map_route_btn)) lv_group_add_obj(group, s_map_route_btn);
    for (lv_obj_t* member_btn : s_member_buttons)
    {
        if (map_control_visible(member_btn))
        {
            lv_group_add_obj(group, member_btn);
        }
    }
}

void create_map_control_bar(lv_obj_t* viewport)
{
    s_map_control_bar = lv_obj_create(viewport);
    lv_obj_set_size(s_map_control_bar, LV_PCT(100), kMapControlBarHeight);
    lv_obj_align(s_map_control_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_flex_flow(s_map_control_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_map_control_bar,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(s_map_control_bar, lv_color_hex(0xFFF3DF), 0);
    lv_obj_set_style_bg_opa(s_map_control_bar, LV_OPA_90, 0);
    lv_obj_set_style_border_width(s_map_control_bar, 1, 0);
    lv_obj_set_style_border_color(s_map_control_bar, lv_color_hex(0xB3915D), 0);
    lv_obj_set_style_radius(s_map_control_bar, 0, 0);
    lv_obj_set_style_pad_left(s_map_control_bar, 3, 0);
    lv_obj_set_style_pad_right(s_map_control_bar, 3, 0);
    lv_obj_set_style_pad_top(s_map_control_bar, 2, 0);
    lv_obj_set_style_pad_bottom(s_map_control_bar, 2, 0);
    lv_obj_set_style_pad_column(s_map_control_bar, 3, 0);
    lv_obj_clear_flag(s_map_control_bar, LV_OBJ_FLAG_SCROLLABLE);

    s_map_zoom_out_btn = create_map_control_button(
        s_map_control_bar,
        kMapControlButtonSmallWidth,
        "-",
        MapControlAction::ZoomOut);
    s_map_zoom_label = lv_label_create(s_map_control_bar);
    lv_label_set_text(s_map_zoom_label, "Z7");
    lv_obj_set_width(s_map_zoom_label, 24);
    lv_obj_set_style_text_font(s_map_zoom_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_map_zoom_label, lv_color_hex(0x3E2B18), 0);
    lv_obj_set_style_text_align(s_map_zoom_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_map_zoom_label, LV_LABEL_LONG_CLIP);

    s_map_zoom_in_btn = create_map_control_button(
        s_map_control_bar,
        kMapControlButtonSmallWidth,
        "+",
        MapControlAction::ZoomIn);
    s_map_center_btn = create_map_control_button(
        s_map_control_bar,
        kMapControlButtonMediumWidth,
        "Pos",
        MapControlAction::Center);
    s_map_layer_btn = create_map_control_button(
        s_map_control_bar,
        kMapControlButtonMediumWidth,
        "OSM",
        MapControlAction::Layer);
    s_map_contour_btn = create_map_control_button(
        s_map_control_bar,
        kMapControlButtonContourWidth,
        "Contour",
        MapControlAction::Contour);
    s_map_tracker_btn = create_map_control_button(
        s_map_control_bar,
        tracker_button_width(),
        tracker_button_label(),
        MapControlAction::Tracker);
    s_map_help_btn = create_map_control_button(
        s_map_control_bar,
        kMapControlButtonSmallWidth,
        help_key_label(),
        MapControlAction::Help);
    lv_obj_move_foreground(s_map_control_bar);
}

void create_map_notice_overlay(lv_obj_t* viewport)
{
    s_map_notice_panel = lv_obj_create(viewport);
    lv_obj_set_size(s_map_notice_panel, LV_SIZE_CONTENT, 18);
    lv_obj_align(s_map_notice_panel, LV_ALIGN_TOP_LEFT, 4, 4);
    lv_obj_add_flag(s_map_notice_panel, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_add_flag(s_map_notice_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(s_map_notice_panel, lv_color_hex(0x25170D), 0);
    lv_obj_set_style_bg_opa(s_map_notice_panel, LV_OPA_70, 0);
    lv_obj_set_style_border_width(s_map_notice_panel, 0, 0);
    lv_obj_set_style_radius(s_map_notice_panel, 4, 0);
    lv_obj_set_style_pad_left(s_map_notice_panel, 6, 0);
    lv_obj_set_style_pad_right(s_map_notice_panel, 6, 0);
    lv_obj_set_style_pad_top(s_map_notice_panel, 2, 0);
    lv_obj_set_style_pad_bottom(s_map_notice_panel, 2, 0);
    lv_obj_clear_flag(s_map_notice_panel, LV_OBJ_FLAG_SCROLLABLE);

    s_map_notice_label = lv_label_create(s_map_notice_panel);
    lv_label_set_text(s_map_notice_label, "");
    lv_obj_set_width(s_map_notice_label, 154);
    lv_obj_set_style_text_font(s_map_notice_label, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(s_map_notice_label, lv_color_hex(0xFFF3DF), 0);
    lv_label_set_long_mode(s_map_notice_label, LV_LABEL_LONG_DOT);
    lv_obj_center(s_map_notice_label);
}

void create_map_context_rail(lv_obj_t* viewport)
{
    s_map_context_rail = lv_obj_create(viewport);
    lv_obj_set_size(s_map_context_rail, kMapSideRailWidth + 4, LV_SIZE_CONTENT);
    lv_obj_align(s_map_context_rail, LV_ALIGN_TOP_RIGHT, -3, 4);
    lv_obj_set_flex_flow(s_map_context_rail, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_map_context_rail,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(s_map_context_rail, lv_color_hex(0xFFF3DF), 0);
    lv_obj_set_style_bg_opa(s_map_context_rail, LV_OPA_70, 0);
    lv_obj_set_style_border_width(s_map_context_rail, 1, 0);
    lv_obj_set_style_border_color(s_map_context_rail, lv_color_hex(0xB3915D), 0);
    lv_obj_set_style_radius(s_map_context_rail, 4, 0);
    lv_obj_set_style_pad_all(s_map_context_rail, 3, 0);
    lv_obj_set_style_pad_row(s_map_context_rail, 3, 0);
    lv_obj_clear_flag(s_map_context_rail, LV_OBJ_FLAG_SCROLLABLE);

    s_map_route_btn = create_map_control_button(
        s_map_context_rail,
        kMapControlButtonWideWidth,
        "Route",
        MapControlAction::Route);
    set_hidden(s_map_route_btn, true);
    set_hidden(s_map_context_rail, true);
    lv_obj_move_foreground(s_map_context_rail);
}

void on_back(void*)
{
    request_exit();
}

void root_key_event_cb(lv_event_t* e)
{
    const uint32_t key = lv_event_get_key(e);
    if (s_projection == Projection::Map && handle_map_key(key, e))
    {
        return;
    }
    if (key == LV_KEY_BACKSPACE || key == LV_KEY_ESC)
    {
        consume_key_event(e);
        request_exit();
    }
}

void clear_gps_status_labels()
{
    s_gps_status_label = nullptr;
    s_gps_coord_label = nullptr;
    s_gps_sat_label = nullptr;
    s_gps_alt_label = nullptr;
    s_gps_motion_label = nullptr;
    s_gps_time_label = nullptr;
    s_gps_diag_label = nullptr;
}

void create_gps_status_content(lv_obj_t* content)
{
    lv_obj_t* panel = lv_obj_create(content);
    lv_obj_set_size(panel, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_opa(panel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_pad_left(panel, 10, 0);
    lv_obj_set_style_pad_right(panel, 10, 0);
    lv_obj_set_style_pad_top(panel, 5, 0);
    lv_obj_set_style_pad_bottom(panel, 4, 0);
    lv_obj_set_style_pad_row(panel, 2, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    create_status_row(panel, "Fix", &s_gps_status_label);
    create_status_row(panel, "Coord", &s_gps_coord_label);
    create_status_row(panel, "Sat", &s_gps_sat_label);
    create_status_row(panel, "Alt", &s_gps_alt_label);
    create_status_row(panel, "Motion", &s_gps_motion_label);
    create_status_row(panel, "Time", &s_gps_time_label);
    create_status_row(panel, "Diag", &s_gps_diag_label);

    refresh_gps_status_view();
}

void create_map_content(lv_obj_t* content)
{
    lv_obj_t* viewport = lv_obj_create(content);
    lv_obj_set_size(viewport, LV_PCT(100), 0);
    lv_obj_set_flex_grow(viewport, 1);
    lv_obj_set_style_bg_color(viewport, lv_color_hex(0xEAD9B2), 0);
    lv_obj_set_style_bg_opa(viewport, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(viewport, 0, 0);
    lv_obj_set_style_radius(viewport, 0, 0);
    lv_obj_set_style_pad_all(viewport, 0, 0);
    lv_obj_clear_flag(viewport, LV_OBJ_FLAG_SCROLLABLE);

    const auto map_widgets = ::ui::widgets::map::create(s_map_runtime, viewport, 180);
    ::ui::widgets::map::set_gesture_callback(s_map_runtime, map_gesture_callback, nullptr);
    ::ui::widgets::map::set_gesture_enabled(s_map_runtime, true);
    lv_obj_update_layout(content);
    lv_obj_update_layout(viewport);
    ::ui::widgets::map::set_size(s_map_runtime,
                                 lv_obj_get_content_width(viewport),
                                 lv_obj_get_content_height(viewport));
    if (map_widgets.root)
    {
        lv_obj_align(map_widgets.root, LV_ALIGN_CENTER, 0, 0);
    }
    create_map_control_bar(viewport);
    create_map_notice_overlay(viewport);
    create_map_context_rail(viewport);

    if (route_context_available())
    {
        (void)load_configured_route_overlay(false);
    }
    refresh_view();
}

} // namespace

namespace gps::ui::runtime
{

bool is_available()
{
    return platform::ui::device::gps_supported();
}

void remember_gps_view_state()
{
}

bool restore_gps_view_state()
{
    return s_map_view_initialized;
}

uint32_t selected_map_member_id()
{
    return s_selected_member_id;
}

bool load_map_track_file(const char* path, bool show_fail_toast)
{
    return load_map_track_file_impl(path, show_fail_toast);
}

void enter(const shell::Host* host, lv_obj_t* parent, shell::Projection projection)
{
    if (!s_gps_power_lease_active)
    {
        platform::ui::gps::acquire_power_lease(projection == Projection::GpsStatus ? "lvgl-gps" : "lvgl-map");
        s_gps_power_lease_active = true;
    }
    s_host = host;
    s_projection = projection;
    clear_gps_status_labels();
    clear_map_controls();
    if (s_projection == Projection::Map && !s_map_view_initialized)
    {
        s_map_zoom = kCardputerZeroMapDefaultZoom;
        s_map_pan_x = 0;
        s_map_pan_y = 0;
        s_map_view_initialized = true;
    }
    if (s_projection == Projection::Map)
    {
        sync_workspace_viewport_from_renderer();
    }

    lv_group_t* prev_group = lv_group_get_default();
    set_default_group(nullptr);

    s_root = lv_obj_create(parent);
    lv_obj_set_size(s_root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(s_root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_color(s_root, lv_color_hex(0xFFF3DF), 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_root, 0, 0);
    lv_obj_set_style_pad_all(s_root, 0, 0);
    lv_obj_set_style_pad_row(s_root, 0, 0);
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_root, root_key_event_cb, LV_EVENT_KEY, nullptr);

    ::ui::widgets::TopBarConfig top_bar_config{};
    top_bar_config.height = ::ui::page_profile::current().top_bar_height;
    ::ui::widgets::top_bar_init(s_top_bar, s_root, top_bar_config);
    ::ui::widgets::top_bar_set_title(
        s_top_bar,
        ::ui::i18n::tr(s_projection == Projection::GpsStatus ? "GPS" : "Map"));
    ::ui::widgets::top_bar_set_back_callback(s_top_bar, on_back, nullptr);
    if (s_top_bar.back_btn)
    {
        lv_obj_add_event_cb(s_top_bar.back_btn, root_key_event_cb, LV_EVENT_KEY, nullptr);
    }
    ui_update_top_bar_battery(s_top_bar);

    if (app_g && s_top_bar.back_btn)
    {
        lv_group_remove_all_objs(app_g);
        lv_group_add_obj(app_g, s_top_bar.back_btn);
        lv_group_focus_obj(s_top_bar.back_btn);
        set_default_group(app_g);
        lv_group_set_editing(app_g, false);
    }
    else
    {
        set_default_group(prev_group);
    }

    lv_obj_t* content = lv_obj_create(s_root);
    lv_obj_set_size(content, LV_PCT(100), 0);
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_set_style_pad_row(content, 0, 0);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    if (s_projection == Projection::GpsStatus)
    {
        create_gps_status_content(content);
    }
    else
    {
        create_map_content(content);
        if (app_g)
        {
            add_map_controls_to_group(app_g);
        }
    }
    gps_runtime_pump().setActive(true);
    if (!s_timer)
    {
        s_timer = lv_timer_create(refresh_timer_cb, 750, nullptr);
    }
}

void exit(lv_obj_t* parent)
{
    (void)parent;

    if (s_timer)
    {
        lv_timer_del(s_timer);
        s_timer = nullptr;
    }
    gps_runtime_pump().setActive(false);
    if (s_projection == Projection::Map)
    {
        ::ui::widgets::map::destroy(s_map_runtime);
    }
    clear_gps_status_labels();
    clear_map_controls();
    if (s_root)
    {
        lv_obj_del(s_root);
        s_root = nullptr;
    }
    const bool was_gps_status = s_projection == Projection::GpsStatus;
    s_host = nullptr;
    s_projection = Projection::Map;
    if (s_gps_power_lease_active)
    {
        platform::ui::gps::release_power_lease(was_gps_status ? "lvgl-gps" : "lvgl-map");
        s_gps_power_lease_active = false;
    }
}

} // namespace gps::ui::runtime
