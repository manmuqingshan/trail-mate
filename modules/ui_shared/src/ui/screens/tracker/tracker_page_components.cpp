#include "ui/screens/tracker/tracker_page_components.h"
#include "app/app_config.h"
#include "app/app_facade_access.h"
#include "platform/ui/device_runtime.h"
#include "platform/ui/route_storage.h"
#include "platform/ui/tracker_runtime.h"
#include "platform/ui/wifi_runtime.h"
#include "ui/app_runtime.h"
#include "ui/assets/fonts/font_utils.h"
#include "ui/localization.h"
#include "ui/page/page_profile.h"
#include "ui/screens/tracker/tracker_page_input.h"
#include "ui/screens/tracker/tracker_page_layout.h"
#include "ui/screens/tracker/tracker_state.h"
#include "ui/support/lvgl_fs_utils.h"
#include "ui/ui_common.h"
#include "ui/widgets/map/map_viewport.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace tracker
{
namespace ui
{
namespace components
{

namespace
{
constexpr int kBottomBarButtonWidth = 70;
constexpr uintptr_t kListUserDataOffset = 1;
static constexpr intptr_t kBackListItemUserData = -2;
std::vector<std::string> s_route_names;
std::vector<std::string> s_record_names;
std::string s_record_empty_text = "No tracks yet";
std::string s_route_empty_text = "No KML routes";
lv_timer_t* s_record_list_refresh_timer = nullptr;

constexpr std::size_t kPreviewMaxDrawPoints = 96;
constexpr std::size_t kPreviewMaxImages = 64;
constexpr lv_coord_t kPreviewElevationHeight = 82;
constexpr lv_coord_t kPreviewPlotPad = 4;
constexpr lv_coord_t kPreviewAxisLeft = 42;
constexpr lv_coord_t kPreviewAxisRight = 6;
constexpr lv_coord_t kPreviewAxisTop = 8;
constexpr lv_coord_t kPreviewAxisBottom = 18;
constexpr lv_coord_t kPreviewButtonGap = 3;
constexpr lv_coord_t kPreviewButtonWidth = 78;
constexpr lv_coord_t kPreviewButtonWidthDense = 66;

struct RoutePreviewPoint
{
    double lat = 0.0;
    double lon = 0.0;
    double altitude_m = 0.0;
    bool has_altitude = false;
};

struct RoutePreviewImage
{
    std::string url{};
    std::string local_path{};
    double lat = 0.0;
    double lon = 0.0;
    bool has_position = false;
    bool downloaded = false;
};

struct RoutePreviewMetrics
{
    double min_altitude_m = 0.0;
    double max_altitude_m = 0.0;
    double ascent_m = 0.0;
    double descent_m = 0.0;
    double distance_m = 0.0;
    std::size_t altitude_count = 0;
};

enum class RoutePreviewDownloadState : uint8_t
{
    Idle = 0,
    Downloading,
    Done,
    Failed,
};

std::vector<RoutePreviewPoint> s_preview_route_points;
std::vector<RoutePreviewImage> s_preview_images;
std::vector<lv_point_precise_t> s_preview_map_line_points;
std::vector<lv_point_precise_t> s_preview_elevation_line_points;
RoutePreviewMetrics s_preview_metrics;
std::string s_preview_route_name;
std::string s_preview_asset_id;
std::string s_preview_status_text;
RoutePreviewDownloadState s_preview_download_state = RoutePreviewDownloadState::Idle;
int s_preview_selected_image = 0;
bool s_preview_elevation_visible = true;
::ui::widgets::map::Runtime s_preview_map_runtime;
lv_obj_t* s_preview_download_modal = nullptr;
lv_obj_t* s_preview_download_title = nullptr;
lv_obj_t* s_preview_download_detail = nullptr;
lv_obj_t* s_preview_download_footer = nullptr;
lv_obj_t* s_preview_download_bar = nullptr;
bool s_preview_download_busy = false;

constexpr uint32_t kPanelBtnBg = 0xFAF0D8;
constexpr uint32_t kPanelBtnBorder = 0xE7C98F;
constexpr uint32_t kPanelBtnFocused = 0xEBA341;
constexpr uint32_t kPanelBtnText = 0x6B4A1E;
constexpr uint32_t kPanelTextMuted = 0x8A6A3A;

bool s_btn_styles_inited = false;
lv_style_t s_btn_main;
lv_style_t s_btn_focused;
lv_style_t s_btn_disabled;
lv_style_t s_btn_label;

enum class ActionMenuCommand : uintptr_t
{
    Load = 1,
    Unload,
    Preview,
    Delete,
    Cancel,
};

lv_group_t* tracker_group();
const ::ui::page_profile::PageLayoutProfile& page_profile();
lv_coord_t filter_panel_width();
lv_coord_t filter_button_height();
lv_coord_t list_item_height();
lv_coord_t bottom_bar_button_width();
lv_coord_t modal_button_width();
lv_coord_t action_menu_button_height();
lv_coord_t action_menu_row_gap();
void update_record_status();
void update_start_stop_button();
void update_record_page();
void update_route_status();
void update_route_page();
void cancel_deferred_record_list_refresh();
void schedule_deferred_record_list_refresh(uint32_t delay_ms);
bool can_delete_selected_item();
bool can_load_selected_route();
bool can_unload_active_route();
bool can_preview_selected_route();
bool is_any_modal_open();
void on_route_load_clicked(lv_event_t* e);
void on_route_unload_clicked(lv_event_t* e);
void open_route_preview_page();
void close_route_preview_page();
void render_route_preview_page();
void refresh_route_preview_map();
void update_route_preview_status();
void update_route_preview_load_button();
void toggle_route_preview_elevation();
void cycle_route_preview_map_layer();
void open_route_preview_help_modal();
void close_route_preview_help_modal();
void close_route_preview_download_modal();
void on_route_preview_key(lv_event_t* e);
void on_route_preview_download_clicked(lv_event_t* e);
void on_route_preview_load_clicked(lv_event_t* e);
void open_delete_confirm_modal();
void on_list_item_clicked(lv_event_t* e);
void on_list_item_focused(lv_event_t* e);
void on_list_item_defocused(lv_event_t* e);
void on_backspace_key(lv_event_t* e);
lv_obj_t* create_action_menu_button(lv_obj_t* parent, const char* text);
void on_action_menu_key(lv_event_t* e);
void on_action_menu_item_clicked(lv_event_t* e);
void open_action_menu_modal();
void focus_main_panel();
std::string format_list_name(const std::string& name);
void clear_list_items();
lv_obj_t* create_list_item_button(const std::string& text, intptr_t user_data, bool checked, bool disabled);
void append_back_list_item();

void on_back(void*)
{
    if (g_tracker_state.route_preview_help_modal)
    {
        close_route_preview_help_modal();
        return;
    }
    if (g_tracker_state.route_preview_page)
    {
        close_route_preview_page();
        return;
    }
    ui_request_exit_to_menu();
}

const ::ui::page_profile::PageLayoutProfile& page_profile()
{
    return ::ui::page_profile::current();
}

lv_coord_t filter_panel_width()
{
    return page_profile().filter_panel_width;
}

lv_coord_t filter_button_height()
{
    return page_profile().filter_button_height;
}

lv_coord_t list_item_height()
{
    return page_profile().list_item_height;
}

lv_coord_t bottom_bar_button_width()
{
    if (page_profile().dense)
    {
        return ::ui::page_profile::resolve_control_button_min_width();
    }
    return page_profile().large_touch_hitbox ? 96 : kBottomBarButtonWidth;
}

lv_coord_t modal_button_width()
{
    return ::ui::page_profile::resolve_control_button_min_width();
}

lv_coord_t action_menu_button_height()
{
    return page_profile().filter_button_height;
}

lv_coord_t action_menu_row_gap()
{
    if (page_profile().dense)
    {
        return 2;
    }
    return page_profile().large_touch_hitbox ? 6 : 4;
}

void on_backspace_key(lv_event_t* e)
{
    if (!e)
    {
        return;
    }
    uint32_t key = lv_event_get_key(e);
    if (key != LV_KEY_BACKSPACE && key != LV_KEY_ESC)
    {
        return;
    }
    if (g_tracker_state.top_bar.back_btn)
    {
        lv_obj_send_event(g_tracker_state.top_bar.back_btn, LV_EVENT_CLICKED, nullptr);
        return;
    }
    on_back(nullptr);
}

void modal_prepare_group()
{
    auto& state = g_tracker_state;
    if (!state.modal_group)
    {
        state.modal_group = lv_group_create();
    }
    lv_group_remove_all_objs(state.modal_group);
    state.prev_group = lv_group_get_default();
    lv_group_t* group = tracker_group();
    if (group && state.prev_group != group)
    {
        state.prev_group = group;
    }
    set_default_group(state.modal_group);
}

void modal_restore_group()
{
    auto& state = g_tracker_state;
    lv_group_t* restore = state.prev_group ? state.prev_group : tracker_group();
    if (restore)
    {
        set_default_group(restore);
    }
    state.prev_group = nullptr;
}

lv_obj_t* create_modal_root(int width, int height)
{
    lv_obj_t* screen = lv_screen_active();
    lv_coord_t screen_w = lv_obj_get_width(screen);
    lv_coord_t screen_h = lv_obj_get_height(screen);

    lv_obj_t* bg = lv_obj_create(screen);
    lv_obj_set_size(bg, screen_w, screen_h);
    lv_obj_set_pos(bg, 0, 0);
    lv_obj_set_style_bg_color(bg, lv_color_hex(0x3A2A1A), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bg, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_border_width(bg, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(bg, 0, LV_PART_MAIN);
    lv_obj_clear_flag(bg, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(bg, LV_OBJ_FLAG_CLICKABLE);

    const auto modal_size = ::ui::page_profile::resolve_modal_size(width, height, screen);
    lv_obj_t* win = lv_obj_create(bg);
    lv_obj_set_size(win, modal_size.width, modal_size.height);
    lv_obj_center(win);
    lv_obj_set_style_bg_color(win, lv_color_hex(0xFFF7E9), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(win, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(win, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(win, lv_color_hex(0xD9B06A), LV_PART_MAIN);
    lv_obj_set_style_radius(win, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_all(win, ::ui::page_profile::resolve_modal_pad(), LV_PART_MAIN);
    lv_obj_clear_flag(win, LV_OBJ_FLAG_SCROLLABLE);

    return bg;
}

void modal_close(lv_obj_t*& modal_obj)
{
    if (modal_obj)
    {
        lv_obj_del(modal_obj);
        modal_obj = nullptr;
    }
    modal_restore_group();
}

bool is_any_modal_open()
{
    return g_tracker_state.del_confirm_modal != nullptr ||
           g_tracker_state.action_menu_modal != nullptr ||
           g_tracker_state.route_preview_help_modal != nullptr ||
           s_preview_download_modal != nullptr;
}

void init_button_styles()
{
    if (s_btn_styles_inited)
    {
        return;
    }
    lv_style_init(&s_btn_main);
    lv_style_set_bg_color(&s_btn_main, lv_color_hex(kPanelBtnBg));
    lv_style_set_bg_opa(&s_btn_main, LV_OPA_COVER);
    lv_style_set_border_width(&s_btn_main, 1);
    lv_style_set_border_color(&s_btn_main, lv_color_hex(kPanelBtnBorder));
    lv_style_set_radius(&s_btn_main, 6);
    lv_style_set_text_color(&s_btn_main, lv_color_hex(kPanelBtnText));

    lv_style_init(&s_btn_focused);
    lv_style_set_bg_color(&s_btn_focused, lv_color_hex(kPanelBtnFocused));
    lv_style_set_bg_opa(&s_btn_focused, LV_OPA_COVER);
    lv_style_set_outline_width(&s_btn_focused, 0);

    lv_style_init(&s_btn_disabled);
    lv_style_set_bg_opa(&s_btn_disabled, LV_OPA_50);

    lv_style_init(&s_btn_label);
    lv_style_set_text_color(&s_btn_label, lv_color_hex(kPanelBtnText));
    lv_style_set_text_font(&s_btn_label, ::ui::fonts::localized_font(::ui::fonts::ui_chrome_font()));

    s_btn_styles_inited = true;
}

void apply_action_button(lv_obj_t* btn, lv_obj_t* label)
{
    if (!btn)
    {
        return;
    }
    init_button_styles();
    lv_obj_add_style(btn, &s_btn_main, LV_PART_MAIN);
    lv_obj_add_style(btn, &s_btn_focused, LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_add_style(btn, &s_btn_disabled, LV_PART_MAIN | LV_STATE_DISABLED);
    if (label)
    {
        lv_obj_add_style(label, &s_btn_label, LV_PART_MAIN);
    }
}

void apply_list_button(lv_obj_t* btn)
{
    if (!btn)
    {
        return;
    }
    init_button_styles();
    lv_obj_add_style(btn, &s_btn_main, LV_PART_MAIN);
    lv_obj_add_style(btn, &s_btn_focused, LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_add_style(btn, &s_btn_focused, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_add_style(btn, &s_btn_disabled, LV_PART_MAIN | LV_STATE_DISABLED);
    if (lv_obj_t* label = lv_obj_get_child(btn, -1))
    {
        lv_obj_add_style(label, &s_btn_label, LV_PART_MAIN);
    }
}

void style_mode_button(lv_obj_t* btn, lv_obj_t* label, bool active)
{
    if (!btn)
    {
        return;
    }
    lv_color_t bg = active ? lv_color_hex(0xEBA341) : lv_color_hex(0xFAF0D8);
    lv_color_t fg = lv_color_hex(0x6B4A1E);
    lv_obj_set_style_bg_color(btn, bg, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn, lv_color_hex(0xE7C98F), LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 12, LV_PART_MAIN);
    if (label)
    {
        lv_obj_set_style_text_color(label, fg, LV_PART_MAIN);
    }
}

void update_mode_buttons()
{
    auto& state = g_tracker_state;
    bool record_active = (state.mode == TrackerPageState::Mode::Record);
    style_mode_button(state.mode_record_btn, state.mode_record_label, record_active);
    style_mode_button(state.mode_route_btn, state.mode_route_label, !record_active);
}

std::string path_basename(const std::string& path)
{
    const char* base = strrchr(path.c_str(), '/');
    if (base && base[1] != '\0')
    {
        return std::string(base + 1);
    }
    return path;
}

bool route_path_looks_degraded(const std::string& path)
{
    return path.find('?') != std::string::npos;
}

bool can_delete_selected_item()
{
    auto& state = g_tracker_state;
    bool can_delete = false;
    if (state.mode == TrackerPageState::Mode::Record)
    {
        if (state.selected_record_idx >= 0 &&
            state.selected_record_idx < static_cast<int>(s_record_names.size()))
        {
            can_delete = true;
            if (platform::ui::tracker::is_recording())
            {
                std::string current;
                platform::ui::tracker::current_path(current);
                std::string base = path_basename(current);
                if (base == s_record_names[state.selected_record_idx])
                {
                    can_delete = false;
                }
            }
        }
    }
    else
    {
        if (state.selected_route_idx >= 0 &&
            state.selected_route_idx < static_cast<int>(s_route_names.size()))
        {
            can_delete = true;
        }
    }
    return can_delete;
}

bool can_load_selected_route()
{
    auto& state = g_tracker_state;
    return state.mode == TrackerPageState::Mode::Route &&
           state.active_route.empty() &&
           state.selected_route_idx >= 0 &&
           !state.selected_route.empty();
}

bool can_unload_active_route()
{
    auto& state = g_tracker_state;
    return state.mode == TrackerPageState::Mode::Route &&
           !state.active_route.empty();
}

bool can_preview_selected_route()
{
    auto& state = g_tracker_state;
    return state.mode == TrackerPageState::Mode::Route &&
           state.selected_route_idx >= 0 &&
           state.selected_route_idx < static_cast<int>(s_route_names.size()) &&
           !state.selected_route.empty();
}

std::string route_path_for_name(const std::string& route_name)
{
    return std::string(platform::ui::route_storage::route_dir()) + "/" + route_name;
}

bool repair_degraded_active_route_from_list()
{
    auto& state = g_tracker_state;
    app::IAppFacade& app_ctx = app::appFacade();
    auto& cfg = app_ctx.getConfig();
    if (!cfg.route_enabled || cfg.route_path[0] == '\0')
    {
        return false;
    }

    const std::string saved_path = cfg.route_path;
    if (!route_path_looks_degraded(saved_path))
    {
        return false;
    }
    if (s_route_names.size() != 1)
    {
        return false;
    }

    const std::string repaired_name = s_route_names.front();
    const std::string repaired_path = route_path_for_name(repaired_name);
    std::strncpy(cfg.route_path, repaired_path.c_str(), sizeof(cfg.route_path) - 1);
    cfg.route_path[sizeof(cfg.route_path) - 1] = '\0';
    cfg.route_enabled = true;
    app_ctx.saveConfig();
    state.active_route = repaired_name;
    return true;
}

std::string route_asset_id_for_name(const std::string& route_name)
{
    std::uint32_t hash = 2166136261U;
    for (unsigned char ch : route_name)
    {
        hash ^= static_cast<std::uint32_t>(ch);
        hash *= 16777619U;
    }
    char text[16];
    std::snprintf(text, sizeof(text), "kml-%08lx", static_cast<unsigned long>(hash));
    return std::string(text);
}

std::string route_asset_root_for_id(const std::string& asset_id)
{
    return std::string(platform::ui::route_storage::route_dir()) + "/.trailmate/" + asset_id;
}

std::size_t find_case_insensitive(const std::string& text,
                                  const char* needle,
                                  std::size_t start = 0)
{
    if (!needle || needle[0] == '\0')
    {
        return std::string::npos;
    }
    const std::size_t needle_len = std::strlen(needle);
    if (needle_len > text.size())
    {
        return std::string::npos;
    }
    for (std::size_t pos = start; pos + needle_len <= text.size(); ++pos)
    {
        bool match = true;
        for (std::size_t index = 0; index < needle_len; ++index)
        {
            const unsigned char lhs = static_cast<unsigned char>(text[pos + index]);
            const unsigned char rhs = static_cast<unsigned char>(needle[index]);
            if (std::tolower(lhs) != std::tolower(rhs))
            {
                match = false;
                break;
            }
        }
        if (match)
        {
            return pos;
        }
    }
    return std::string::npos;
}

std::string html_decode_attr(std::string value)
{
    auto replace_all = [&value](const char* from, const char* to)
    {
        std::size_t pos = 0;
        const std::size_t from_len = std::strlen(from);
        while ((pos = value.find(from, pos)) != std::string::npos)
        {
            value.replace(pos, from_len, to);
            pos += std::strlen(to);
        }
    };
    replace_all("&amp;", "&");
    replace_all("&quot;", "\"");
    replace_all("&#34;", "\"");
    replace_all("&#38;", "&");
    return value;
}

void extract_img_srcs(const std::string& text, std::vector<std::string>& out_urls)
{
    std::size_t pos = 0;
    while (out_urls.size() < kPreviewMaxImages)
    {
        const std::size_t img_pos = find_case_insensitive(text, "<img", pos);
        if (img_pos == std::string::npos)
        {
            break;
        }
        const std::size_t tag_end = text.find('>', img_pos);
        const std::size_t tag_len =
            (tag_end == std::string::npos) ? (text.size() - img_pos) : (tag_end - img_pos + 1);
        const std::string tag = text.substr(img_pos, tag_len);
        const std::size_t src_pos = find_case_insensitive(tag, "src");
        if (src_pos != std::string::npos)
        {
            std::size_t eq_pos = tag.find('=', src_pos + 3);
            if (eq_pos != std::string::npos)
            {
                ++eq_pos;
                while (eq_pos < tag.size() &&
                       std::isspace(static_cast<unsigned char>(tag[eq_pos])))
                {
                    ++eq_pos;
                }
                if (eq_pos < tag.size())
                {
                    const char quote = (tag[eq_pos] == '\'' || tag[eq_pos] == '"') ? tag[eq_pos] : '\0';
                    const std::size_t value_start = quote ? eq_pos + 1 : eq_pos;
                    std::size_t value_end = value_start;
                    while (value_end < tag.size())
                    {
                        const char ch = tag[value_end];
                        if ((quote && ch == quote) ||
                            (!quote && (std::isspace(static_cast<unsigned char>(ch)) || ch == '>')))
                        {
                            break;
                        }
                        ++value_end;
                    }
                    if (value_end > value_start)
                    {
                        out_urls.push_back(html_decode_attr(tag.substr(value_start, value_end - value_start)));
                    }
                }
            }
        }
        if (tag_end == std::string::npos)
        {
            break;
        }
        pos = tag_end + 1;
    }
}

bool extract_tag_text(const std::string& line, const char* tag, std::string& out_text)
{
    out_text.clear();
    std::string open = "<";
    open += tag;
    const std::size_t open_pos = find_case_insensitive(line, open.c_str());
    if (open_pos == std::string::npos)
    {
        return false;
    }
    const std::size_t body_start = line.find('>', open_pos);
    if (body_start == std::string::npos)
    {
        return false;
    }
    std::string close = "</";
    close += tag;
    close += ">";
    const std::size_t close_pos = find_case_insensitive(line, close.c_str(), body_start + 1);
    if (close_pos == std::string::npos || close_pos <= body_start)
    {
        return false;
    }
    out_text = line.substr(body_start + 1, close_pos - body_start - 1);
    return true;
}

bool parse_comma_coordinate_token(const std::string& token,
                                  double& lat,
                                  double& lon,
                                  double& altitude_m,
                                  bool& has_altitude)
{
    const char* ptr = token.c_str();
    char* end = nullptr;
    const double parsed_lon = std::strtod(ptr, &end);
    if (end == ptr || *end != ',')
    {
        return false;
    }
    ptr = end + 1;
    const double parsed_lat = std::strtod(ptr, &end);
    if (end == ptr)
    {
        return false;
    }
    double parsed_alt = 0.0;
    bool parsed_has_alt = false;
    if (*end == ',')
    {
        ptr = end + 1;
        char* alt_end = nullptr;
        parsed_alt = std::strtod(ptr, &alt_end);
        parsed_has_alt = alt_end != ptr;
    }
    if (!std::isfinite(parsed_lat) || !std::isfinite(parsed_lon))
    {
        return false;
    }
    lat = parsed_lat;
    lon = parsed_lon;
    altitude_m = parsed_alt;
    has_altitude = parsed_has_alt && std::isfinite(parsed_alt);
    return true;
}

bool parse_space_coordinate_text(const std::string& text,
                                 double& lat,
                                 double& lon,
                                 double& altitude_m,
                                 bool& has_altitude)
{
    const char* ptr = text.c_str();
    char* end = nullptr;
    const double parsed_lon = std::strtod(ptr, &end);
    if (end == ptr)
    {
        return false;
    }
    ptr = end;
    const double parsed_lat = std::strtod(ptr, &end);
    if (end == ptr)
    {
        return false;
    }
    ptr = end;
    const double parsed_alt = std::strtod(ptr, &end);
    const bool parsed_has_alt = end != ptr;
    if (!std::isfinite(parsed_lat) || !std::isfinite(parsed_lon))
    {
        return false;
    }
    lat = parsed_lat;
    lon = parsed_lon;
    altitude_m = parsed_alt;
    has_altitude = parsed_has_alt && std::isfinite(parsed_alt);
    return true;
}

void append_preview_route_point(double lat, double lon, double altitude_m, bool has_altitude)
{
    if (!std::isfinite(lat) || !std::isfinite(lon))
    {
        return;
    }
    RoutePreviewPoint point{};
    point.lat = lat;
    point.lon = lon;
    point.altitude_m = altitude_m;
    point.has_altitude = has_altitude && std::isfinite(altitude_m);
    s_preview_route_points.push_back(point);
}

void append_pending_images(const std::vector<std::string>& urls,
                           double lat,
                           double lon,
                           bool has_position)
{
    for (const auto& url : urls)
    {
        if (s_preview_images.size() >= kPreviewMaxImages)
        {
            return;
        }
        if (url.empty())
        {
            continue;
        }
        RoutePreviewImage image{};
        image.url = url;
        image.lat = lat;
        image.lon = lon;
        image.has_position = has_position && std::isfinite(lat) && std::isfinite(lon);
        s_preview_images.push_back(image);
    }
}

double preview_degrees_to_radians(double degrees)
{
    return degrees * 0.017453292519943295;
}

double preview_distance_m(const RoutePreviewPoint& a, const RoutePreviewPoint& b)
{
    constexpr double kEarthRadiusM = 6371000.0;
    const double lat1 = preview_degrees_to_radians(a.lat);
    const double lat2 = preview_degrees_to_radians(b.lat);
    const double dlat = preview_degrees_to_radians(b.lat - a.lat);
    const double dlon = preview_degrees_to_radians(b.lon - a.lon);
    const double sin_lat = std::sin(dlat / 2.0);
    const double sin_lon = std::sin(dlon / 2.0);
    const double h = (sin_lat * sin_lat) +
                     std::cos(lat1) * std::cos(lat2) * (sin_lon * sin_lon);
    return 2.0 * kEarthRadiusM * std::atan2(std::sqrt(h), std::sqrt(std::max(0.0, 1.0 - h)));
}

void refresh_preview_metrics()
{
    s_preview_metrics = RoutePreviewMetrics{};
    bool have_altitude = false;
    double previous_altitude = 0.0;
    bool have_previous = false;
    const RoutePreviewPoint* previous_point = nullptr;
    for (const auto& point : s_preview_route_points)
    {
        if (previous_point)
        {
            const double distance = preview_distance_m(*previous_point, point);
            if (std::isfinite(distance))
            {
                s_preview_metrics.distance_m += distance;
            }
        }
        previous_point = &point;

        if (!point.has_altitude || !std::isfinite(point.altitude_m))
        {
            continue;
        }
        if (!have_altitude)
        {
            s_preview_metrics.min_altitude_m = point.altitude_m;
            s_preview_metrics.max_altitude_m = point.altitude_m;
            have_altitude = true;
        }
        else
        {
            s_preview_metrics.min_altitude_m =
                std::min(s_preview_metrics.min_altitude_m, point.altitude_m);
            s_preview_metrics.max_altitude_m =
                std::max(s_preview_metrics.max_altitude_m, point.altitude_m);
        }
        if (have_previous)
        {
            const double delta = point.altitude_m - previous_altitude;
            if (delta > 0.0)
            {
                s_preview_metrics.ascent_m += delta;
            }
            else
            {
                s_preview_metrics.descent_m += -delta;
            }
        }
        previous_altitude = point.altitude_m;
        have_previous = true;
        ++s_preview_metrics.altitude_count;
    }
}

void assign_preview_image_paths()
{
    const std::string asset_root = route_asset_root_for_id(s_preview_asset_id);
    for (std::size_t index = 0; index < s_preview_images.size(); ++index)
    {
        char name[32];
        std::snprintf(name, sizeof(name), "/images/img-%04u.jpg", static_cast<unsigned>(index + 1));
        s_preview_images[index].local_path = asset_root + name;
        s_preview_images[index].downloaded =
            platform::ui::route_storage::route_asset_file_exists(s_preview_images[index].local_path);
    }
}

void clear_preview_model()
{
    s_preview_route_points.clear();
    s_preview_images.clear();
    s_preview_map_line_points.clear();
    s_preview_elevation_line_points.clear();
    s_preview_metrics = RoutePreviewMetrics{};
    s_preview_route_name.clear();
    s_preview_asset_id.clear();
    s_preview_status_text.clear();
    s_preview_download_state = RoutePreviewDownloadState::Idle;
    s_preview_selected_image = 0;
    s_preview_elevation_visible = true;
}

bool load_route_preview_model(const std::string& route_name)
{
    clear_preview_model();
    if (route_name.empty())
    {
        return false;
    }

    s_preview_route_name = route_name;
    s_preview_asset_id = route_asset_id_for_name(route_name);
    std::vector<RoutePreviewPoint> fallback_line_points;
    std::vector<std::string> pending_image_urls;
    bool in_line_string = false;

    const std::string path = route_path_for_name(route_name);
    const bool read_ok = ::ui::fs::read_text_file_lines(
        path.c_str(),
        [&](std::string& line)
        {
            if (find_case_insensitive(line, "<Placemark") != std::string::npos)
            {
                pending_image_urls.clear();
            }
            if (find_case_insensitive(line, "<LineString") != std::string::npos)
            {
                in_line_string = true;
            }

            extract_img_srcs(line, pending_image_urls);

            std::string text;
            if (extract_tag_text(line, "gx:coord", text))
            {
                double lat = 0.0;
                double lon = 0.0;
                double altitude_m = 0.0;
                bool has_altitude = false;
                if (parse_space_coordinate_text(text, lat, lon, altitude_m, has_altitude))
                {
                    append_preview_route_point(lat, lon, altitude_m, has_altitude);
                }
            }

            if (extract_tag_text(line, "coordinates", text))
            {
                std::size_t token_start = 0;
                bool first_token = true;
                while (token_start < text.size())
                {
                    while (token_start < text.size() &&
                           std::isspace(static_cast<unsigned char>(text[token_start])))
                    {
                        ++token_start;
                    }
                    if (token_start >= text.size())
                    {
                        break;
                    }
                    std::size_t token_end = token_start;
                    while (token_end < text.size() &&
                           !std::isspace(static_cast<unsigned char>(text[token_end])))
                    {
                        ++token_end;
                    }
                    const std::string token = text.substr(token_start, token_end - token_start);
                    double lat = 0.0;
                    double lon = 0.0;
                    double altitude_m = 0.0;
                    bool has_altitude = false;
                    if (parse_comma_coordinate_token(token, lat, lon, altitude_m, has_altitude))
                    {
                        if (first_token && !pending_image_urls.empty())
                        {
                            append_pending_images(pending_image_urls, lat, lon, true);
                            pending_image_urls.clear();
                        }
                        if (in_line_string)
                        {
                            RoutePreviewPoint point{};
                            point.lat = lat;
                            point.lon = lon;
                            point.altitude_m = altitude_m;
                            point.has_altitude = has_altitude;
                            fallback_line_points.push_back(point);
                        }
                    }
                    first_token = false;
                    token_start = token_end;
                }
            }

            if (find_case_insensitive(line, "</LineString") != std::string::npos)
            {
                in_line_string = false;
            }
            if (find_case_insensitive(line, "</Placemark") != std::string::npos)
            {
                if (!pending_image_urls.empty())
                {
                    append_pending_images(pending_image_urls, 0.0, 0.0, false);
                    pending_image_urls.clear();
                }
            }
            return true;
        },
        8192);

    if (!read_ok)
    {
        clear_preview_model();
        return false;
    }
    if (s_preview_route_points.empty())
    {
        s_preview_route_points = fallback_line_points;
    }
    refresh_preview_metrics();
    assign_preview_image_paths();
    if (!s_preview_images.empty())
    {
        s_preview_selected_image = 0;
    }
    return !s_preview_route_points.empty() || !s_preview_images.empty();
}

struct PreviewBounds
{
    double min_lat = 0.0;
    double max_lat = 0.0;
    double min_lon = 0.0;
    double max_lon = 0.0;
    bool valid = false;
};

void include_preview_bounds(PreviewBounds& bounds, double lat, double lon)
{
    if (!std::isfinite(lat) || !std::isfinite(lon))
    {
        return;
    }
    if (!bounds.valid)
    {
        bounds.min_lat = lat;
        bounds.max_lat = lat;
        bounds.min_lon = lon;
        bounds.max_lon = lon;
        bounds.valid = true;
        return;
    }
    bounds.min_lat = std::min(bounds.min_lat, lat);
    bounds.max_lat = std::max(bounds.max_lat, lat);
    bounds.min_lon = std::min(bounds.min_lon, lon);
    bounds.max_lon = std::max(bounds.max_lon, lon);
}

PreviewBounds compute_preview_bounds()
{
    PreviewBounds bounds{};
    for (const auto& point : s_preview_route_points)
    {
        include_preview_bounds(bounds, point.lat, point.lon);
    }
    for (const auto& image : s_preview_images)
    {
        if (image.has_position)
        {
            include_preview_bounds(bounds, image.lat, image.lon);
        }
    }
    return bounds;
}

bool preview_model_fits_bounds(lv_obj_t* viewport_root,
                               const ::ui::widgets::map::Model& model,
                               const PreviewBounds& bounds)
{
    if (!viewport_root || !lv_obj_is_valid(viewport_root) || !bounds.valid)
    {
        return false;
    }

    lv_obj_update_layout(viewport_root);
    const lv_coord_t width = lv_obj_get_content_width(viewport_root);
    const lv_coord_t height = lv_obj_get_content_height(viewport_root);
    const lv_coord_t margin = std::max<lv_coord_t>(10, std::min(width, height) / 10);

    const ::ui::widgets::map::GeoPoint corners[] = {
        {true, bounds.min_lat, bounds.min_lon},
        {true, bounds.min_lat, bounds.max_lon},
        {true, bounds.max_lat, bounds.min_lon},
        {true, bounds.max_lat, bounds.max_lon},
    };
    for (const auto& geo : corners)
    {
        lv_point_t point{};
        if (!::ui::widgets::map::preview_project_point(viewport_root, model, geo, point))
        {
            return false;
        }
        if (point.x < margin ||
            point.y < margin ||
            point.x > width - margin ||
            point.y > height - margin)
        {
            return false;
        }
    }
    return true;
}

::ui::widgets::map::Model build_preview_map_model()
{
    ::ui::widgets::map::Model model{};
    const PreviewBounds bounds = compute_preview_bounds();
    const auto layers = ::ui::widgets::map::current_layer_state();
    model.map_source = layers.map_source;
    model.contour_enabled = layers.contour_enabled;
    model.coord_system = app::configFacade().getConfig().map_coord_system;
    model.zoom = ::ui::widgets::map::kDefaultZoom;
    model.focus_point.valid = bounds.valid;
    model.focus_point.lat = bounds.valid ? (bounds.min_lat + bounds.max_lat) / 2.0 : 0.0;
    model.focus_point.lon = bounds.valid ? (bounds.min_lon + bounds.max_lon) / 2.0 : 0.0;
    if (!bounds.valid || !g_tracker_state.route_preview_map_host)
    {
        return model;
    }

    model.zoom = ::ui::widgets::map::kMinZoom;
    for (int zoom = ::ui::widgets::map::kMaxZoom;
         zoom >= ::ui::widgets::map::kMinZoom;
         --zoom)
    {
        model.zoom = zoom;
        if (preview_model_fits_bounds(g_tracker_state.route_preview_map_host, model, bounds))
        {
            break;
        }
    }
    return model;
}

void draw_preview_map_overlay()
{
    const auto& widgets = ::ui::widgets::map::widgets(s_preview_map_runtime);
    if (!widgets.overlay_layer || !lv_obj_is_valid(widgets.overlay_layer))
    {
        return;
    }
    lv_obj_clean(widgets.overlay_layer);

    if (!s_preview_route_points.empty())
    {
        const std::size_t draw_count =
            std::min<std::size_t>(kPreviewMaxDrawPoints, s_preview_route_points.size());
        s_preview_map_line_points.clear();
        s_preview_map_line_points.reserve(draw_count);
        for (std::size_t index = 0; index < draw_count; ++index)
        {
            const std::size_t src_index = draw_count <= 1
                                              ? 0
                                              : (index * (s_preview_route_points.size() - 1)) / (draw_count - 1);
            const auto& point = s_preview_route_points[src_index];
            lv_point_t projected{};
            if (::ui::widgets::map::project_point(
                    s_preview_map_runtime,
                    {true, point.lat, point.lon},
                    projected))
            {
                lv_point_precise_t precise{};
                precise.x = static_cast<float>(projected.x);
                precise.y = static_cast<float>(projected.y);
                s_preview_map_line_points.push_back(precise);
            }
        }
        if (s_preview_map_line_points.size() >= 2)
        {
            lv_obj_t* line = lv_line_create(widgets.overlay_layer);
            lv_line_set_points(line,
                               s_preview_map_line_points.data(),
                               static_cast<std::uint16_t>(s_preview_map_line_points.size()));
            lv_obj_set_style_line_width(line, 3, LV_PART_MAIN);
            lv_obj_set_style_line_color(line, lv_color_hex(0xE94F2E), LV_PART_MAIN);
            lv_obj_set_style_line_rounded(line, true, LV_PART_MAIN);
        }
    }

    for (std::size_t index = 0; index < s_preview_images.size(); ++index)
    {
        const auto& image = s_preview_images[index];
        if (!image.has_position)
        {
            continue;
        }
        lv_point_t point{};
        if (!::ui::widgets::map::project_point(
                s_preview_map_runtime,
                {true, image.lat, image.lon},
                point))
        {
            continue;
        }
        const bool selected = static_cast<int>(index) == s_preview_selected_image;
        lv_obj_t* pin = lv_btn_create(widgets.overlay_layer);
        lv_obj_set_size(pin, selected ? 14 : 11, selected ? 14 : 11);
        lv_obj_set_pos(pin,
                       point.x - (selected ? 7 : 5),
                       point.y - (selected ? 13 : 10));
        lv_obj_set_style_radius(pin, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_border_width(pin, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(pin, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
        lv_obj_set_style_bg_color(
            pin,
            selected ? lv_color_hex(0xF0442E) : lv_color_hex(0xF4A11E),
            LV_PART_MAIN);
        lv_obj_set_style_bg_opa(pin, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_clear_flag(pin, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(pin, LV_OBJ_FLAG_CLICK_FOCUSABLE);
        lv_group_remove_obj(pin);
        lv_obj_set_user_data(pin, reinterpret_cast<void*>(index + 1));
        lv_obj_add_event_cb(
            pin,
            [](lv_event_t* e)
            {
                lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
                const std::size_t raw = static_cast<std::size_t>(
                    reinterpret_cast<uintptr_t>(lv_obj_get_user_data(target)));
                if (raw > 0 && raw <= s_preview_images.size())
                {
                    s_preview_selected_image = static_cast<int>(raw - 1);
                    s_preview_status_text.clear();
                    s_preview_download_state = RoutePreviewDownloadState::Idle;
                    refresh_route_preview_map();
                    update_route_preview_status();
                }
            },
            LV_EVENT_CLICKED,
            nullptr);
    }
}

void refresh_route_preview_map()
{
    auto& state = g_tracker_state;
    if (!state.route_preview_map_host ||
        !lv_obj_is_valid(state.route_preview_map_host))
    {
        return;
    }
    lv_obj_update_layout(state.route_preview_map_host);
    const lv_coord_t width = lv_obj_get_content_width(state.route_preview_map_host);
    const lv_coord_t height = lv_obj_get_content_height(state.route_preview_map_host);
    if (width <= 0 || height <= 0)
    {
        return;
    }

    if (!::ui::widgets::map::widgets(s_preview_map_runtime).root)
    {
        (void)::ui::widgets::map::create(s_preview_map_runtime, state.route_preview_map_host, 180);
        ::ui::widgets::map::set_gesture_enabled(s_preview_map_runtime, false);
    }
    ::ui::widgets::map::set_size(s_preview_map_runtime, width, height);
    ::ui::widgets::map::apply_model(s_preview_map_runtime, build_preview_map_model());
    draw_preview_map_overlay();
}

void set_plain_panel(lv_obj_t* obj, uint32_t bg)
{
    lv_obj_set_style_bg_color(obj, lv_color_hex(bg), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

void create_axis_rect(lv_obj_t* parent,
                      lv_coord_t x,
                      lv_coord_t y,
                      lv_coord_t w,
                      lv_coord_t h,
                      uint32_t color)
{
    lv_obj_t* rect = lv_obj_create(parent);
    lv_obj_set_pos(rect, x, y);
    lv_obj_set_size(rect, w, h);
    lv_obj_set_style_bg_color(rect, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(rect, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(rect, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(rect, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(rect, 0, LV_PART_MAIN);
    lv_obj_clear_flag(rect, LV_OBJ_FLAG_SCROLLABLE);
}

void add_axis_label(lv_obj_t* parent,
                    const char* text,
                    lv_coord_t x,
                    lv_coord_t y,
                    lv_coord_t w,
                    lv_text_align_t align)
{
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, text ? text : "");
    lv_obj_set_pos(label, x, y);
    lv_obj_set_width(label, w);
    lv_obj_set_height(label, 13);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(label, align, LV_PART_MAIN);
    lv_obj_set_style_text_line_space(label, 0, LV_PART_MAIN);
    lv_obj_set_style_text_font(label,
                               ::ui::fonts::localized_font(::ui::fonts::ui_chrome_font()),
                               LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(0x4F3A20), LV_PART_MAIN);
}

void draw_preview_elevation_chart()
{
    lv_obj_t* elevation_box = g_tracker_state.route_preview_elevation_panel;
    if (!elevation_box)
    {
        return;
    }
    lv_obj_clean(elevation_box);
    lv_obj_update_layout(elevation_box);
    const lv_coord_t width = lv_obj_get_content_width(elevation_box);
    const lv_coord_t height = lv_obj_get_content_height(elevation_box);
    const lv_coord_t plot_w =
        std::max<lv_coord_t>(1, width - kPreviewAxisLeft - kPreviewAxisRight);
    const lv_coord_t plot_h =
        std::max<lv_coord_t>(1, height - kPreviewAxisTop - kPreviewAxisBottom);
    const lv_coord_t x0 = kPreviewAxisLeft;
    const lv_coord_t y0 = kPreviewAxisTop + plot_h;

    create_axis_rect(elevation_box, x0, kPreviewAxisTop, 1, plot_h, 0x715A36);
    create_axis_rect(elevation_box, x0, y0, plot_w, 1, 0x715A36);
    for (int tick = 0; tick <= 2; ++tick)
    {
        const lv_coord_t y = kPreviewAxisTop + static_cast<lv_coord_t>((plot_h * tick) / 2);
        create_axis_rect(elevation_box, x0 - 3, y, 4, 1, 0x715A36);
    }
    for (int tick = 0; tick <= 2; ++tick)
    {
        const lv_coord_t x = x0 + static_cast<lv_coord_t>((plot_w * tick) / 2);
        create_axis_rect(elevation_box, x, y0, 1, 4, 0x715A36);
    }

    if (s_preview_metrics.altitude_count < 2)
    {
        add_axis_label(elevation_box, "m", 1, 1, 28, LV_TEXT_ALIGN_LEFT);
        add_axis_label(elevation_box, "No altitude", x0 + 4, kPreviewAxisTop + 16, plot_w - 8, LV_TEXT_ALIGN_CENTER);
        return;
    }

    char max_label[16];
    char min_label[16];
    char total_label[24];
    std::snprintf(max_label, sizeof(max_label), "%.0fm", s_preview_metrics.max_altitude_m);
    std::snprintf(min_label, sizeof(min_label), "%.0fm", s_preview_metrics.min_altitude_m);
    std::snprintf(total_label, sizeof(total_label), "%.1fkm", s_preview_metrics.distance_m / 1000.0);
    add_axis_label(elevation_box, max_label, 1, kPreviewAxisTop - 3, kPreviewAxisLeft - 4, LV_TEXT_ALIGN_RIGHT);
    add_axis_label(elevation_box, min_label, 1, y0 - 9, kPreviewAxisLeft - 4, LV_TEXT_ALIGN_RIGHT);
    add_axis_label(elevation_box, "0", x0 - 2, y0 + 4, 24, LV_TEXT_ALIGN_LEFT);
    add_axis_label(elevation_box, total_label, x0 + plot_w - 54, y0 + 4, 58, LV_TEXT_ALIGN_RIGHT);

    struct ElevationPoint
    {
        const RoutePreviewPoint* point = nullptr;
        double distance_m = 0.0;
    };
    std::vector<ElevationPoint> altitude_points;
    altitude_points.reserve(s_preview_metrics.altitude_count);
    double distance_m = 0.0;
    const RoutePreviewPoint* previous = nullptr;
    for (const auto& point : s_preview_route_points)
    {
        if (previous)
        {
            const double step = preview_distance_m(*previous, point);
            if (std::isfinite(step))
            {
                distance_m += step;
            }
        }
        previous = &point;
        if (point.has_altitude && std::isfinite(point.altitude_m))
        {
            altitude_points.push_back({&point, distance_m});
        }
    }
    const std::size_t draw_count =
        std::min<std::size_t>(kPreviewMaxDrawPoints, altitude_points.size());
    s_preview_elevation_line_points.assign(draw_count, lv_point_precise_t{});

    const double range = std::max(
        1.0,
        s_preview_metrics.max_altitude_m - s_preview_metrics.min_altitude_m);
    const double total_distance = std::max(1.0, s_preview_metrics.distance_m);
    for (std::size_t index = 0; index < draw_count; ++index)
    {
        const std::size_t src_index = draw_count <= 1
                                          ? 0
                                          : (index * (altitude_points.size() - 1)) / (draw_count - 1);
        const auto& sample = altitude_points[src_index];
        const double x_ratio = sample.distance_m / total_distance;
        const double y_ratio =
            (s_preview_metrics.max_altitude_m - sample.point->altitude_m) / range;
        s_preview_elevation_line_points[index].x =
            static_cast<float>(x0 + std::lround(x_ratio * plot_w));
        s_preview_elevation_line_points[index].y =
            static_cast<float>(kPreviewAxisTop + std::lround(y_ratio * plot_h));
    }

    lv_obj_t* line = lv_line_create(elevation_box);
    lv_line_set_points(line,
                       s_preview_elevation_line_points.data(),
                       static_cast<std::uint16_t>(s_preview_elevation_line_points.size()));
    lv_obj_set_style_line_width(line, 2, LV_PART_MAIN);
    lv_obj_set_style_line_color(line, lv_color_hex(0x2D7F6F), LV_PART_MAIN);
    lv_obj_set_style_line_rounded(line, true, LV_PART_MAIN);
}

std::size_t preview_saved_image_count()
{
    return static_cast<std::size_t>(
        std::count_if(s_preview_images.begin(),
                      s_preview_images.end(),
                      [](const RoutePreviewImage& image)
                      {
                          return image.downloaded;
                      }));
}

void update_route_preview_status()
{
    auto& state = g_tracker_state;
    const std::size_t saved_count = preview_saved_image_count();
    char status[160];
    if (s_preview_images.empty())
    {
        std::snprintf(status,
                      sizeof(status),
                      "%u pts  %.1fkm  %.0f-%.0fm",
                      static_cast<unsigned>(s_preview_route_points.size()),
                      s_preview_metrics.distance_m / 1000.0,
                      s_preview_metrics.min_altitude_m,
                      s_preview_metrics.max_altitude_m);
    }
    else
    {
        const bool selected_ok =
            s_preview_selected_image >= 0 &&
            s_preview_selected_image < static_cast<int>(s_preview_images.size());
        const char* local_state = selected_ok &&
                                          s_preview_images[static_cast<std::size_t>(s_preview_selected_image)].downloaded
                                      ? "local"
                                      : "remote";
        std::snprintf(status,
                      sizeof(status),
                      "Img %u/%u %s  saved %u/%u  %.1fkm",
                      static_cast<unsigned>(s_preview_selected_image + 1),
                      static_cast<unsigned>(s_preview_images.size()),
                      local_state,
                      static_cast<unsigned>(saved_count),
                      static_cast<unsigned>(s_preview_images.size()),
                      s_preview_metrics.distance_m / 1000.0);
    }
    std::string status_text = status;
    if (!s_preview_status_text.empty())
    {
        status_text += " | ";
        status_text += s_preview_status_text;
    }
    if (state.route_preview_status_label)
    {
        ::ui::i18n::set_label_text_raw(state.route_preview_status_label, status_text.c_str());
    }
    if (state.route_preview_progress)
    {
        int value = 0;
        if (!s_preview_images.empty())
        {
            value = static_cast<int>((saved_count * 100U) / s_preview_images.size());
        }
        lv_bar_set_value(state.route_preview_progress, value, LV_ANIM_ON);
    }
}

lv_obj_t* create_preview_page_button(lv_obj_t* parent,
                                     const char* text,
                                     lv_event_cb_t cb,
                                     lv_coord_t width)
{
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_size(btn, width, action_menu_button_height());
    lv_obj_t* label = lv_label_create(btn);
    ::ui::i18n::set_label_text(label, text);
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_center(label);
    apply_action_button(btn, label);
    if (cb)
    {
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
    }
    lv_obj_add_event_cb(btn, on_route_preview_key, LV_EVENT_KEY, nullptr);
    if (lv_group_t* group = tracker_group())
    {
        lv_group_add_obj(group, btn);
    }
    return btn;
}

bool preview_selected_route_is_active()
{
    const auto& state = g_tracker_state;
    return !state.active_route.empty() &&
           !state.selected_route.empty() &&
           state.active_route == state.selected_route;
}

void update_route_preview_load_button()
{
    auto& state = g_tracker_state;
    if (!state.route_preview_load_label)
    {
        return;
    }
    ::ui::i18n::set_label_text(state.route_preview_load_label,
                               preview_selected_route_is_active() ? "Off" : "Load");
    lv_obj_center(state.route_preview_load_label);
}

void sync_route_preview_elevation_visibility()
{
    auto& state = g_tracker_state;
    if (!state.route_preview_page ||
        !state.route_preview_elevation_panel ||
        !lv_obj_is_valid(state.route_preview_elevation_panel))
    {
        return;
    }

    if (s_preview_elevation_visible)
    {
        lv_obj_clear_flag(state.route_preview_elevation_panel, LV_OBJ_FLAG_HIDDEN);
        draw_preview_elevation_chart();
    }
    else
    {
        lv_obj_add_flag(state.route_preview_elevation_panel, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_update_layout(state.route_preview_page);
    refresh_route_preview_map();
}

void toggle_route_preview_elevation()
{
    s_preview_elevation_visible = !s_preview_elevation_visible;
    s_preview_status_text = s_preview_elevation_visible ? "Profile shown" : "Profile hidden";
    sync_route_preview_elevation_visibility();
    update_route_preview_status();
}

void cycle_route_preview_map_layer()
{
    const auto layer_state = ::ui::widgets::map::current_layer_state();
    const uint8_t next_source = static_cast<uint8_t>((layer_state.map_source + 1U) % 3U);
    ::ui::widgets::map::LayerNotice notice{};
    (void)::ui::widgets::map::set_layer_map_source(next_source, &notice);
    if (notice.has_message)
    {
        s_preview_status_text = notice.message;
    }
    else
    {
        char text[40]{};
        std::snprintf(text,
                      sizeof(text),
                      "Base %s",
                      ::ui::i18n::tr(::ui::widgets::map::layer_map_source_label_key(next_source)));
        s_preview_status_text = text;
    }
    refresh_route_preview_map();
    update_route_preview_status();
}

void consume_route_preview_key_event(lv_event_t* e)
{
    if (!e)
    {
        return;
    }
    lv_event_stop_bubbling(e);
    lv_event_stop_processing(e);
}

void close_route_preview_help_modal()
{
    modal_close(g_tracker_state.route_preview_help_modal);
}

void close_route_preview_download_modal()
{
    s_preview_download_busy = false;
    modal_close(s_preview_download_modal);
    s_preview_download_title = nullptr;
    s_preview_download_detail = nullptr;
    s_preview_download_footer = nullptr;
    s_preview_download_bar = nullptr;
}

void on_route_preview_download_modal_key(lv_event_t* e)
{
    if (!e)
    {
        return;
    }
    const uint32_t key = lv_event_get_key(e);
    if (s_preview_download_busy)
    {
        consume_route_preview_key_event(e);
        return;
    }
    if (key == LV_KEY_ESC || key == LV_KEY_BACKSPACE || key == LV_KEY_ENTER)
    {
        consume_route_preview_key_event(e);
        close_route_preview_download_modal();
    }
}

void update_route_preview_download_modal(const char* title,
                                         const char* detail,
                                         std::size_t processed,
                                         std::size_t total)
{
    if (!s_preview_download_modal || !lv_obj_is_valid(s_preview_download_modal))
    {
        return;
    }
    if (s_preview_download_title)
    {
        ::ui::i18n::set_label_text_raw(s_preview_download_title, title ? title : "");
    }
    if (s_preview_download_detail)
    {
        ::ui::i18n::set_label_text_raw(s_preview_download_detail, detail ? detail : "");
    }
    if (s_preview_download_footer)
    {
        ::ui::i18n::set_label_text_raw(
            s_preview_download_footer,
            s_preview_download_busy ? "Please wait" : "Back / Enter to close");
    }
    if (s_preview_download_bar)
    {
        const int value = total > 0 ? static_cast<int>((processed * 100U) / total) : 0;
        lv_bar_set_value(s_preview_download_bar, value, LV_ANIM_ON);
    }
    lv_timer_handler();
}

bool open_route_preview_download_modal()
{
    if (s_preview_download_modal && lv_obj_is_valid(s_preview_download_modal))
    {
        return true;
    }

    modal_prepare_group();
    s_preview_download_modal = create_modal_root(292, 128);
    lv_obj_set_style_bg_color(s_preview_download_modal, lv_color_hex(0x1C1812), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_preview_download_modal, LV_OPA_70, LV_PART_MAIN);
    lv_obj_t* win = lv_obj_get_child(s_preview_download_modal, 0);
    if (!win)
    {
        close_route_preview_download_modal();
        return false;
    }

    lv_obj_set_flex_flow(win, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(win, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_bg_color(win, lv_color_hex(0xFFF3DF), LV_PART_MAIN);
    lv_obj_set_style_border_width(win, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(win, lv_color_hex(0x8A6E43), LV_PART_MAIN);
    lv_obj_set_style_radius(win, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_left(win, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_right(win, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_top(win, 7, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(win, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_row(win, 5, LV_PART_MAIN);
    lv_obj_clear_flag(win, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(win, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(win, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_add_event_cb(win, on_route_preview_download_modal_key, LV_EVENT_KEY, nullptr);

    s_preview_download_title = lv_label_create(win);
    lv_obj_set_width(s_preview_download_title, LV_PCT(100));
    lv_obj_set_style_text_font(s_preview_download_title, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_preview_download_title, lv_color_hex(0x25170D), LV_PART_MAIN);
    lv_label_set_long_mode(s_preview_download_title, LV_LABEL_LONG_DOT);
    ::ui::i18n::set_label_text_raw(s_preview_download_title, "Downloading Images");

    s_preview_download_detail = lv_label_create(win);
    lv_obj_set_width(s_preview_download_detail, LV_PCT(100));
    lv_obj_set_style_text_font(s_preview_download_detail,
                               ::ui::fonts::localized_font(::ui::fonts::ui_chrome_font()),
                               LV_PART_MAIN);
    lv_obj_set_style_text_color(s_preview_download_detail, lv_color_hex(0x3E2B18), LV_PART_MAIN);
    lv_label_set_long_mode(s_preview_download_detail, LV_LABEL_LONG_DOT);
    ::ui::i18n::set_label_text_raw(s_preview_download_detail, "Preparing");

    s_preview_download_bar = lv_bar_create(win);
    lv_obj_set_size(s_preview_download_bar, LV_PCT(100), 10);
    lv_bar_set_range(s_preview_download_bar, 0, 100);
    lv_bar_set_value(s_preview_download_bar, 0, LV_ANIM_OFF);

    s_preview_download_footer = lv_label_create(win);
    lv_obj_set_width(s_preview_download_footer, LV_PCT(100));
    lv_obj_set_style_text_font(s_preview_download_footer, &lv_font_montserrat_10, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_preview_download_footer, lv_color_hex(kPanelTextMuted), LV_PART_MAIN);
    lv_label_set_long_mode(s_preview_download_footer, LV_LABEL_LONG_DOT);
    ::ui::i18n::set_label_text_raw(s_preview_download_footer, "Please wait");

    lv_group_add_obj(g_tracker_state.modal_group, win);
    lv_group_focus_obj(win);
    lv_obj_move_foreground(s_preview_download_modal);
    lv_timer_handler();
    return true;
}

void on_route_preview_help_key(lv_event_t* e)
{
    if (!e)
    {
        return;
    }
    const uint32_t key = lv_event_get_key(e);
    if (key == LV_KEY_ESC || key == LV_KEY_BACKSPACE ||
        key == LV_KEY_ENTER || key == 'h' || key == 'H')
    {
        consume_route_preview_key_event(e);
        close_route_preview_help_modal();
        return;
    }
    if (key == LV_KEY_UP || key == 'w' || key == 'W')
    {
        lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
        if (target && lv_obj_is_valid(target))
        {
            lv_obj_scroll_by(target, 0, 18, LV_ANIM_OFF);
        }
        consume_route_preview_key_event(e);
        return;
    }
    if (key == LV_KEY_DOWN || key == 's' || key == 'S')
    {
        lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
        if (target && lv_obj_is_valid(target))
        {
            lv_obj_scroll_by(target, 0, -18, LV_ANIM_OFF);
        }
        consume_route_preview_key_event(e);
    }
}

void open_route_preview_help_modal()
{
    auto& state = g_tracker_state;
    if (state.route_preview_help_modal)
    {
        return;
    }

    modal_prepare_group();
    state.route_preview_help_modal = create_modal_root(304, 176);
    lv_obj_set_style_bg_color(state.route_preview_help_modal, lv_color_hex(0x1C1812), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(state.route_preview_help_modal, LV_OPA_70, LV_PART_MAIN);
    lv_obj_t* win = lv_obj_get_child(state.route_preview_help_modal, 0);
    if (!win)
    {
        modal_close(state.route_preview_help_modal);
        return;
    }

    lv_obj_set_flex_flow(win, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(win, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_bg_color(win, lv_color_hex(0xFFF3DF), LV_PART_MAIN);
    lv_obj_set_style_border_width(win, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(win, lv_color_hex(0x8A6E43), LV_PART_MAIN);
    lv_obj_set_style_radius(win, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_left(win, 7, LV_PART_MAIN);
    lv_obj_set_style_pad_right(win, 7, LV_PART_MAIN);
    lv_obj_set_style_pad_top(win, 5, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(win, 5, LV_PART_MAIN);
    lv_obj_set_style_pad_row(win, 2, LV_PART_MAIN);
    lv_obj_add_flag(win, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(win, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(win, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_flag(win, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(win, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_add_event_cb(win, on_route_preview_help_key, LV_EVENT_KEY, nullptr);

    lv_obj_t* title = lv_label_create(win);
    ::ui::i18n::set_label_text(title, "Preview Help");
    lv_obj_set_width(title, LV_PCT(100));
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_hex(0x25170D), LV_PART_MAIN);

    auto add_keycap = [](lv_obj_t* parent, const char* text, lv_coord_t width)
    {
        lv_obj_t* keycap = lv_label_create(parent);
        lv_obj_set_size(keycap, width, 14);
        lv_obj_set_style_bg_color(keycap, lv_color_hex(0xF8E6C3), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(keycap, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(keycap, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(keycap, lv_color_hex(0x8A6E43), LV_PART_MAIN);
        lv_obj_set_style_radius(keycap, 3, LV_PART_MAIN);
        lv_obj_set_style_text_font(keycap, &lv_font_montserrat_10, LV_PART_MAIN);
        lv_obj_set_style_text_color(keycap, lv_color_hex(0x25170D), LV_PART_MAIN);
        lv_obj_set_style_text_align(keycap, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_label_set_long_mode(keycap, LV_LABEL_LONG_CLIP);
        lv_label_set_text(keycap, text ? text : "");
        return keycap;
    };

    auto add_help_row = [&](const char* primary,
                            const char* secondary,
                            const char* description)
    {
        lv_obj_t* row = lv_obj_create(win);
        lv_obj_set_size(row, LV_PCT(100), 15);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row,
                              LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_column(row, 3, LV_PART_MAIN);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* keys = lv_obj_create(row);
        lv_obj_set_size(keys, 76, 15);
        lv_obj_set_flex_flow(keys, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(keys,
                              LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_bg_opa(keys, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(keys, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(keys, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_column(keys, 2, LV_PART_MAIN);
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
        lv_obj_set_style_text_font(text, &lv_font_montserrat_10, LV_PART_MAIN);
        lv_obj_set_style_text_color(text, lv_color_hex(0x3E2B18), LV_PART_MAIN);
        lv_label_set_long_mode(text, LV_LABEL_LONG_DOT);
        lv_label_set_text(text, description ? description : "");
    };

    add_help_row("D", nullptr, "Download images");
    add_help_row("R", nullptr, "Load/off route");
    add_help_row("L", nullptr, "Change base layer");
    add_help_row("E", nullptr, "Show/hide profile");
    add_help_row("Pin", nullptr, "Select image point");
    add_help_row("H", "Back", "Close help");

    lv_obj_move_foreground(state.route_preview_help_modal);
    lv_group_add_obj(state.modal_group, win);
    lv_group_focus_obj(win);
}

void on_route_preview_load_clicked(lv_event_t*)
{
    auto& state = g_tracker_state;
    if (preview_selected_route_is_active())
    {
        on_route_unload_clicked(nullptr);
        s_preview_status_text = "Route off";
    }
    else if (state.active_route.empty())
    {
        on_route_load_clicked(nullptr);
        s_preview_status_text = "Route loaded";
    }
    else
    {
        s_preview_status_text = "Another route active";
    }
    update_route_preview_load_button();
    update_route_preview_status();
}

void close_route_preview_page()
{
    auto& state = g_tracker_state;
    if (state.route_preview_help_modal)
    {
        modal_close(state.route_preview_help_modal);
    }
    if (s_preview_download_modal)
    {
        close_route_preview_download_modal();
    }
    ::ui::widgets::map::destroy(s_preview_map_runtime);
    if (state.route_preview_page)
    {
        lv_obj_del(state.route_preview_page);
    }
    state.route_preview_page = nullptr;
    state.route_preview_map_host = nullptr;
    state.route_preview_elevation_panel = nullptr;
    state.route_preview_status_label = nullptr;
    state.route_preview_progress = nullptr;
    state.route_preview_load_label = nullptr;
    clear_preview_model();
    if (state.filter_panel)
    {
        lv_obj_clear_flag(state.filter_panel, LV_OBJ_FLAG_HIDDEN);
    }
    if (state.list_panel)
    {
        lv_obj_clear_flag(state.list_panel, LV_OBJ_FLAG_HIDDEN);
    }
    ::ui::widgets::top_bar_set_title(state.top_bar, ::ui::i18n::tr("Tracker"));
    update_route_status();
    tracker::ui::input::tracker_input_on_ui_refreshed();
    focus_main_panel();
}

void on_route_preview_key(lv_event_t* e)
{
    if (!e)
    {
        return;
    }
    const uint32_t key = lv_event_get_key(e);
    if (key == LV_KEY_ESC || key == LV_KEY_BACKSPACE)
    {
        close_route_preview_page();
        return;
    }
    if (key == 'd' || key == 'D')
    {
        on_route_preview_download_clicked(nullptr);
        return;
    }
    if (key == 'r' || key == 'R')
    {
        on_route_preview_load_clicked(nullptr);
        return;
    }
    if (key == 'l' || key == 'L')
    {
        cycle_route_preview_map_layer();
        return;
    }
    if (key == 'e' || key == 'E')
    {
        toggle_route_preview_elevation();
        return;
    }
    if (key == 'h' || key == 'H' || key == '?')
    {
        open_route_preview_help_modal();
    }
}

void on_route_preview_download_clicked(lv_event_t*)
{
    assign_preview_image_paths();
    if (s_preview_images.empty())
    {
        s_preview_status_text = "No route images";
        s_preview_download_state = RoutePreviewDownloadState::Failed;
        update_route_preview_status();
        return;
    }

    const std::size_t total = s_preview_images.size();
    std::size_t saved_count = preview_saved_image_count();
    std::size_t failed_count = 0;
    std::uint32_t saved_bytes = 0;
    char detail[96]{};

    const auto wifi = platform::ui::wifi::status();
    if (!wifi.supported || !wifi.connected)
    {
        s_preview_status_text = "Wi-Fi required";
        s_preview_download_state = RoutePreviewDownloadState::Failed;
        if (open_route_preview_download_modal())
        {
            s_preview_download_busy = false;
            std::snprintf(detail,
                          sizeof(detail),
                          "Wi-Fi required  saved %u/%u",
                          static_cast<unsigned>(saved_count),
                          static_cast<unsigned>(total));
            update_route_preview_download_modal("Download Failed", detail, saved_count, total);
        }
        update_route_preview_status();
        return;
    }

    std::string asset_dir;
    if (!platform::ui::route_storage::ensure_route_asset_dir(s_preview_asset_id, asset_dir))
    {
        s_preview_status_text = "Create image dir failed";
        s_preview_download_state = RoutePreviewDownloadState::Failed;
        if (open_route_preview_download_modal())
        {
            s_preview_download_busy = false;
            update_route_preview_download_modal("Download Failed", "Create image dir failed", saved_count, total);
        }
        update_route_preview_status();
        return;
    }

    assign_preview_image_paths();

    if (!open_route_preview_download_modal())
    {
        s_preview_status_text = "Open progress failed";
        s_preview_download_state = RoutePreviewDownloadState::Failed;
        update_route_preview_status();
        return;
    }

    saved_count = preview_saved_image_count();
    s_preview_download_state = RoutePreviewDownloadState::Downloading;
    s_preview_download_busy = true;
    std::snprintf(detail,
                  sizeof(detail),
                  "Starting  saved %u/%u",
                  static_cast<unsigned>(saved_count),
                  static_cast<unsigned>(total));
    s_preview_status_text = detail;
    update_route_preview_download_modal("Downloading Images", detail, 0, total);
    update_route_preview_status();

    if (::ui::widgets::map::widgets(s_preview_map_runtime).root)
    {
        ::ui::widgets::map::destroy(s_preview_map_runtime);
        if (g_tracker_state.route_preview_map_host &&
            lv_obj_is_valid(g_tracker_state.route_preview_map_host))
        {
            lv_obj_clean(g_tracker_state.route_preview_map_host);
        }
        update_route_preview_download_modal(
            "Downloading Images", "Released map cache for HTTPS", 0, total);
    }

    std::size_t consecutive_connection_failures = 0;
    bool stopped_for_connection_failure = false;
    for (std::size_t index = 0; index < total; ++index)
    {
        RoutePreviewImage& image = s_preview_images[index];
        s_preview_selected_image = static_cast<int>(index);
        if (image.downloaded && platform::ui::route_storage::route_asset_file_exists(image.local_path))
        {
            saved_count = preview_saved_image_count();
            std::snprintf(detail,
                          sizeof(detail),
                          "Image %u/%u already saved",
                          static_cast<unsigned>(index + 1),
                          static_cast<unsigned>(total));
            s_preview_status_text = detail;
            update_route_preview_status();
            update_route_preview_download_modal("Downloading Images", detail, index + 1, total);
            continue;
        }

        if (image.url.empty())
        {
            ++failed_count;
            std::snprintf(detail,
                          sizeof(detail),
                          "Image %u/%u has no URL",
                          static_cast<unsigned>(index + 1),
                          static_cast<unsigned>(total));
            s_preview_status_text = detail;
            update_route_preview_status();
            update_route_preview_download_modal("Downloading Images", detail, index + 1, total);
            continue;
        }

        std::snprintf(detail,
                      sizeof(detail),
                      "Image %u/%u  saved %u/%u",
                      static_cast<unsigned>(index + 1),
                      static_cast<unsigned>(total),
                      static_cast<unsigned>(saved_count),
                      static_cast<unsigned>(total));
        s_preview_status_text = detail;
        update_route_preview_status();
        update_route_preview_download_modal("Downloading Images", detail, index, total);

        const auto result =
            platform::ui::route_storage::download_route_image(image.url, image.local_path);
        if (result.ok)
        {
            image.downloaded = true;
            saved_bytes += result.bytes;
            consecutive_connection_failures = 0;
        }
        else
        {
            ++failed_count;
            if (result.error == "Open HTTP request failed")
            {
                ++consecutive_connection_failures;
            }
            else
            {
                consecutive_connection_failures = 0;
            }
        }
        assign_preview_image_paths();
        saved_count = preview_saved_image_count();
        if (result.ok)
        {
            std::snprintf(detail,
                          sizeof(detail),
                          "Saved %u/%u  %u KB",
                          static_cast<unsigned>(saved_count),
                          static_cast<unsigned>(total),
                          static_cast<unsigned>((saved_bytes + 1023U) / 1024U));
        }
        else
        {
            std::snprintf(detail,
                          sizeof(detail),
                          "Image %u failed: %.28s",
                          static_cast<unsigned>(index + 1),
                          result.error.empty() ? "Download failed" : result.error.c_str());
        }
        s_preview_status_text = detail;
        update_route_preview_status();
        update_route_preview_download_modal("Downloading Images", detail, index + 1, total);
        if (consecutive_connection_failures >= 3)
        {
            stopped_for_connection_failure = true;
            failed_count += total - index - 1;
            std::snprintf(detail,
                          sizeof(detail),
                          "Connection failed; stopped at %u/%u",
                          static_cast<unsigned>(index + 1),
                          static_cast<unsigned>(total));
            s_preview_status_text = detail;
            update_route_preview_status();
            update_route_preview_download_modal("Download Stopped", detail, index + 1, total);
            break;
        }
    }

    s_preview_download_busy = false;
    assign_preview_image_paths();
    saved_count = preview_saved_image_count();
    if (failed_count == 0 && saved_count >= total)
    {
        s_preview_download_state = RoutePreviewDownloadState::Done;
        std::snprintf(detail,
                      sizeof(detail),
                      "Saved all %u images  %u KB",
                      static_cast<unsigned>(total),
                      static_cast<unsigned>((saved_bytes + 1023U) / 1024U));
        s_preview_status_text = detail;
        update_route_preview_download_modal("Download Complete", detail, total, total);
    }
    else
    {
        s_preview_download_state = failed_count == 0 ? RoutePreviewDownloadState::Done
                                                     : RoutePreviewDownloadState::Failed;
        std::snprintf(detail,
                      sizeof(detail),
                      "Saved %u/%u  failed %u",
                      static_cast<unsigned>(saved_count),
                      static_cast<unsigned>(total),
                      static_cast<unsigned>(failed_count));
        s_preview_status_text = detail;
        update_route_preview_download_modal(
            stopped_for_connection_failure ? "Download Stopped"
                                           : (failed_count == 0 ? "Download Complete" : "Download Finished"),
            detail,
            total,
            total);
    }

    refresh_route_preview_map();
    update_route_preview_status();
}

void render_route_preview_page()
{
    auto& state = g_tracker_state;
    if (!state.route_preview_page)
    {
        return;
    }
    ::ui::widgets::map::destroy(s_preview_map_runtime);
    lv_obj_clean(state.route_preview_page);
    state.route_preview_load_label = nullptr;
    lv_group_t* group = tracker_group();
    if (group)
    {
        lv_group_remove_all_objs(group);
        if (state.top_bar.back_btn)
        {
            lv_group_add_obj(group, state.top_bar.back_btn);
        }
    }

    state.route_preview_map_host = lv_obj_create(state.route_preview_page);
    set_plain_panel(state.route_preview_map_host, 0xD8C090);
    lv_obj_set_width(state.route_preview_map_host, LV_PCT(100));
    lv_obj_set_height(state.route_preview_map_host, 0);
    lv_obj_set_flex_grow(state.route_preview_map_host, 1);
    lv_obj_add_flag(state.route_preview_map_host, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(state.route_preview_map_host, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_set_style_border_width(state.route_preview_map_host, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(state.route_preview_map_host, lv_color_hex(0xD2A868), LV_PART_MAIN);
    lv_obj_set_style_border_color(state.route_preview_map_host, lv_color_hex(kPanelBtnFocused), LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_add_event_cb(state.route_preview_map_host, on_route_preview_key, LV_EVENT_KEY, nullptr);
    if (group)
    {
        lv_group_add_obj(group, state.route_preview_map_host);
    }

    state.route_preview_elevation_panel = lv_obj_create(state.route_preview_page);
    set_plain_panel(state.route_preview_elevation_panel, 0xFFF6E3);
    lv_obj_set_size(state.route_preview_elevation_panel, LV_PCT(100), kPreviewElevationHeight);
    lv_obj_set_style_border_width(state.route_preview_elevation_panel, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(state.route_preview_elevation_panel, lv_color_hex(0xD2A868), LV_PART_MAIN);

    state.route_preview_status_label = lv_label_create(state.route_preview_page);
    lv_obj_set_width(state.route_preview_status_label, LV_PCT(100));
    lv_label_set_long_mode(state.route_preview_status_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(
        state.route_preview_status_label, ::ui::fonts::localized_font(::ui::fonts::ui_chrome_font()), 0);
    lv_obj_set_style_text_color(state.route_preview_status_label, lv_color_hex(kPanelTextMuted), 0);

    state.route_preview_progress = lv_bar_create(state.route_preview_page);
    lv_obj_set_size(state.route_preview_progress, LV_PCT(100), 7);
    lv_bar_set_range(state.route_preview_progress, 0, 100);

    lv_obj_t* row = lv_obj_create(state.route_preview_page);
    lv_obj_set_size(row, LV_PCT(100), action_menu_button_height());
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(row, kPreviewButtonGap, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_update_layout(state.route_preview_page);
    const lv_coord_t page_w =
        std::max<lv_coord_t>(120, lv_obj_get_content_width(state.route_preview_page));
    const lv_coord_t base_button_w =
        page_profile().dense ? kPreviewButtonWidthDense : kPreviewButtonWidth;
    const lv_coord_t max_button_w =
        std::max<lv_coord_t>(46, (page_w - (kPreviewButtonGap * 3)) / 2);
    const lv_coord_t button_w = std::min<lv_coord_t>(base_button_w, max_button_w);
    lv_obj_t* download_btn = create_preview_page_button(row, "Down", on_route_preview_download_clicked, button_w);
    lv_obj_t* load_btn = create_preview_page_button(row, "Load", on_route_preview_load_clicked, button_w);
    state.route_preview_load_label = lv_obj_get_child(load_btn, 0);
    update_route_preview_load_button();

    lv_obj_t* help_hint = lv_label_create(state.route_preview_page);
    lv_obj_set_size(help_hint, 20, 16);
    lv_obj_add_flag(help_hint, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_add_flag(help_hint, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(help_hint, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_set_style_bg_color(help_hint, lv_color_hex(0xF8E6C3), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(help_hint, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(help_hint, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(help_hint, lv_color_hex(0x8A6E43), LV_PART_MAIN);
    lv_obj_set_style_radius(help_hint, 3, LV_PART_MAIN);
    lv_obj_set_style_text_font(help_hint, &lv_font_montserrat_10, LV_PART_MAIN);
    lv_obj_set_style_text_color(help_hint, lv_color_hex(0x25170D), LV_PART_MAIN);
    lv_obj_set_style_text_align(help_hint, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_long_mode(help_hint, LV_LABEL_LONG_CLIP);
    lv_label_set_text(help_hint, "H");
    lv_obj_align(help_hint, LV_ALIGN_BOTTOM_LEFT, 2, -1);
    lv_obj_add_event_cb(
        help_hint,
        [](lv_event_t* e)
        {
            if (lv_event_get_code(e) == LV_EVENT_CLICKED)
            {
                open_route_preview_help_modal();
            }
        },
        LV_EVENT_CLICKED,
        nullptr);
    lv_group_remove_obj(help_hint);

    if (s_preview_images.empty())
    {
        lv_obj_add_state(download_btn, LV_STATE_DISABLED);
    }
    lv_obj_t* focus_obj = s_preview_images.empty() ? load_btn : download_btn;
    if (!state.active_route.empty() && !preview_selected_route_is_active())
    {
        lv_obj_add_state(load_btn, LV_STATE_DISABLED);
        if (focus_obj == load_btn)
        {
            focus_obj = state.route_preview_map_host;
        }
    }
    if (group)
    {
        lv_group_focus_obj(focus_obj);
    }

    lv_obj_update_layout(state.route_preview_page);
    sync_route_preview_elevation_visibility();
    update_route_preview_status();
}

void open_route_preview_page()
{
    auto& state = g_tracker_state;
    if (is_any_modal_open() || state.route_preview_page)
    {
        return;
    }
    if (!can_preview_selected_route())
    {
        if (state.status_label)
        {
            ::ui::i18n::set_label_text(state.status_label, "Select a route");
        }
        return;
    }
    if (!load_route_preview_model(state.selected_route))
    {
        if (state.status_label)
        {
            ::ui::i18n::set_label_text(state.status_label, "Preview failed");
        }
        return;
    }

    if (state.filter_panel)
    {
        lv_obj_add_flag(state.filter_panel, LV_OBJ_FLAG_HIDDEN);
    }
    if (state.list_panel)
    {
        lv_obj_add_flag(state.list_panel, LV_OBJ_FLAG_HIDDEN);
    }
    ::ui::widgets::top_bar_set_title(state.top_bar, ::ui::i18n::tr("Route Preview"));

    state.route_preview_page = lv_obj_create(state.content);
    if (!state.route_preview_page)
    {
        clear_preview_model();
        return;
    }
    lv_obj_set_width(state.route_preview_page, LV_PCT(100));
    lv_obj_set_height(state.route_preview_page, LV_PCT(100));
    lv_obj_set_flex_grow(state.route_preview_page, 1);
    lv_obj_set_flex_flow(state.route_preview_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(state.route_preview_page, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(state.route_preview_page, page_profile().dense ? 2 : 3, LV_PART_MAIN);
    lv_obj_set_style_pad_row(state.route_preview_page, page_profile().dense ? 2 : 3, LV_PART_MAIN);
    lv_obj_set_style_bg_color(state.route_preview_page, lv_color_hex(0xFAF0D8), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(state.route_preview_page, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(state.route_preview_page, 0, LV_PART_MAIN);
    lv_obj_clear_flag(state.route_preview_page, LV_OBJ_FLAG_SCROLLABLE);
    render_route_preview_page();
}

lv_group_t* tracker_group()
{
    if (lv_group_t* group = tracker::ui::input::tracker_input_get_group())
    {
        return group;
    }
    return ::app_g;
}

void clear_list_items()
{
    auto& state = g_tracker_state;
    if (!state.list_container)
    {
        return;
    }
    while (lv_obj_get_child_count(state.list_container) > 0)
    {
        lv_obj_del(lv_obj_get_child(state.list_container, 0));
    }
}

lv_obj_t* create_list_item_button(const std::string& text, intptr_t user_data, bool checked, bool disabled)
{
    auto& state = g_tracker_state;
    if (!state.list_container)
    {
        return nullptr;
    }

    lv_obj_t* btn = lv_btn_create(state.list_container);
    lv_obj_set_size(btn, LV_PCT(100), list_item_height());
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    apply_list_button(btn);

    lv_obj_t* label = lv_label_create(btn);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, page_profile().dense ? 6 : 10, 0);
    lv_label_set_text(label, text.c_str());
    ::ui::i18n::log_direct_text_route("tracker_list_item", label, text.c_str());
    lv_obj_add_style(label, &s_btn_label, LV_PART_MAIN);
    lv_obj_set_style_text_font(label, ::ui::fonts::localized_font(::ui::fonts::ui_chrome_font()), 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(label, LV_PCT(100));

    lv_obj_set_user_data(btn, reinterpret_cast<void*>(user_data));
    if (checked)
    {
        lv_obj_add_state(btn, LV_STATE_CHECKED);
    }
    else
    {
        lv_obj_clear_state(btn, LV_STATE_CHECKED);
    }
    if (disabled)
    {
        lv_obj_add_state(btn, LV_STATE_DISABLED);
    }
    else
    {
        lv_obj_clear_state(btn, LV_STATE_DISABLED);
    }

    lv_obj_add_event_cb(btn, on_list_item_clicked, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(btn, on_list_item_focused, LV_EVENT_FOCUSED, nullptr);
    lv_obj_add_event_cb(btn, on_list_item_defocused, LV_EVENT_DEFOCUSED, nullptr);
    return btn;
}

void append_back_list_item()
{
    create_list_item_button(::ui::i18n::tr("Back"), kBackListItemUserData, false, false);
}

void sync_list_item_checked_states()
{
    auto& state = g_tracker_state;
    if (!state.list_container)
    {
        return;
    }

    const int selected_idx =
        (state.mode == TrackerPageState::Mode::Record) ? state.selected_record_idx : state.selected_route_idx;
    const uint32_t child_count = lv_obj_get_child_count(state.list_container);
    for (uint32_t index = 0; index < child_count; ++index)
    {
        lv_obj_t* btn = lv_obj_get_child(state.list_container, index);
        if (!btn)
        {
            continue;
        }
        const intptr_t raw = reinterpret_cast<intptr_t>(lv_obj_get_user_data(btn));
        const bool checked = raw >= static_cast<intptr_t>(kListUserDataOffset) &&
                             static_cast<int>(raw - static_cast<intptr_t>(kListUserDataOffset)) == selected_idx;
        if (checked)
        {
            lv_obj_add_state(btn, LV_STATE_CHECKED);
        }
        else
        {
            lv_obj_clear_state(btn, LV_STATE_CHECKED);
        }
    }
}

lv_obj_t* first_visible_list_item()
{
    auto& state = g_tracker_state;
    if (!state.list_container)
    {
        return state.start_stop_btn;
    }

    const uint32_t child_count = lv_obj_get_child_count(state.list_container);
    for (uint32_t index = 0; index < child_count; ++index)
    {
        lv_obj_t* btn = lv_obj_get_child(state.list_container, index);
        if (btn && lv_obj_has_state(btn, LV_STATE_CHECKED) &&
            !lv_obj_has_flag(btn, LV_OBJ_FLAG_HIDDEN) &&
            !lv_obj_has_state(btn, LV_STATE_DISABLED))
        {
            return btn;
        }
    }
    for (uint32_t index = 0; index < child_count; ++index)
    {
        lv_obj_t* btn = lv_obj_get_child(state.list_container, index);
        if (btn && !lv_obj_has_flag(btn, LV_OBJ_FLAG_HIDDEN) &&
            !lv_obj_has_state(btn, LV_STATE_DISABLED))
        {
            return btn;
        }
    }
    return state.start_stop_btn;
}

void focus_mode_panel()
{
    tracker::ui::input::tracker_focus_to_filter();
}

void focus_main_panel()
{
    tracker::ui::input::tracker_focus_to_list();
}

void set_mode(TrackerPageState::Mode mode)
{
    auto& state = g_tracker_state;
    state.mode = mode;
    update_mode_buttons();
    if (state.bottom_bar)
    {
        if (mode == TrackerPageState::Mode::Record)
        {
            lv_obj_clear_flag(state.bottom_bar, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(state.bottom_bar, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (state.start_stop_btn)
    {
        if (mode == TrackerPageState::Mode::Record)
        {
            lv_obj_clear_flag(state.start_stop_btn, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(state.start_stop_btn, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (mode == TrackerPageState::Mode::Record)
    {
        update_record_status();
        update_start_stop_button();
        update_record_page();
    }
    else
    {
        update_route_status();
        update_route_page();
    }
    if (state.focus_col == TrackerPageState::FocusColumn::Main)
    {
        tracker::ui::input::tracker_input_on_ui_refreshed();
    }
}

void update_record_status()
{
    auto& state = g_tracker_state;
    if (!state.status_label)
    {
        return;
    }
    const bool recording = platform::ui::tracker::is_recording();
    if (state.mode == TrackerPageState::Mode::Record)
    {
        ::ui::i18n::set_label_text(state.status_label, recording ? "Recording" : "Stopped");
    }
}

void update_start_stop_button()
{
    auto& state = g_tracker_state;
    const bool recording = platform::ui::tracker::is_recording();
    if (state.start_stop_label)
    {
        ::ui::i18n::set_label_text(state.start_stop_label, recording ? "Stop" : "New");
    }
}

size_t utf8_count_chars(const std::string& text)
{
    size_t count = 0;
    const size_t len = text.length();
    size_t i = 0;
    while (i < len)
    {
        uint8_t c = static_cast<uint8_t>(text[i]);
        size_t advance = 1;
        if ((c & 0x80) == 0x00)
        {
            advance = 1;
        }
        else if ((c & 0xE0) == 0xC0)
        {
            advance = 2;
        }
        else if ((c & 0xF0) == 0xE0)
        {
            advance = 3;
        }
        else if ((c & 0xF8) == 0xF0)
        {
            advance = 4;
        }
        i += advance;
        ++count;
    }
    return count;
}

std::string utf8_truncate(const std::string& text, size_t max_chars)
{
    if (max_chars == 0)
    {
        return std::string("...");
    }
    size_t count = 0;
    const size_t len = text.length();
    size_t i = 0;
    while (i < len)
    {
        uint8_t c = static_cast<uint8_t>(text[i]);
        size_t advance = 1;
        if ((c & 0x80) == 0x00)
        {
            advance = 1;
        }
        else if ((c & 0xE0) == 0xC0)
        {
            advance = 2;
        }
        else if ((c & 0xF0) == 0xE0)
        {
            advance = 3;
        }
        else if ((c & 0xF8) == 0xF0)
        {
            advance = 4;
        }
        if (count + 1 > max_chars)
        {
            break;
        }
        i += advance;
        ++count;
    }
    if (i >= len)
    {
        return text;
    }
    std::string out = text.substr(0, i);
    out += "...";
    return out;
}

std::string format_list_name(const std::string& name)
{
    const size_t max_chars = page_profile().dense ? 18U : 20U;
    if (utf8_count_chars(name) <= max_chars)
    {
        return name;
    }
    return utf8_truncate(name, max_chars);
}

void update_record_page()
{
    auto& state = g_tracker_state;
    if (!state.list_container)
    {
        return;
    }

    const lv_coord_t scroll_y = lv_obj_get_scroll_y(state.list_container);
    clear_list_items();

    if (s_record_names.empty())
    {
        create_list_item_button(s_record_empty_text.c_str(), 0, false, true);
    }
    else
    {
        for (size_t index = 0; index < s_record_names.size(); ++index)
        {
            create_list_item_button(format_list_name(s_record_names[index]),
                                    static_cast<intptr_t>(index) + kListUserDataOffset,
                                    static_cast<int>(index) == state.selected_record_idx,
                                    false);
        }
    }
    append_back_list_item();

    lv_obj_add_flag(state.list_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(state.list_container, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(state.list_container, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_scroll_to_y(state.list_container, scroll_y, LV_ANIM_OFF);
    if (state.focus_col == TrackerPageState::FocusColumn::Main && !is_any_modal_open())
    {
        tracker::ui::input::tracker_input_on_ui_refreshed();
    }
}

void refresh_record_list()
{
    auto& state = g_tracker_state;
    if (!state.list_container)
    {
        return;
    }
    s_record_names.clear();
    s_record_empty_text = ::ui::i18n::tr("No tracks yet");

    if (!platform::ui::device::sd_ready())
    {
        s_record_empty_text = ::ui::i18n::tr("No SD Card");
        if (state.mode == TrackerPageState::Mode::Record)
        {
            update_record_page();
        }
        return;
    }

    constexpr size_t kMaxTracks = 32;
    platform::ui::tracker::list_tracks(s_record_names, kMaxTracks);
    if (state.selected_record_idx >= static_cast<int>(s_record_names.size()))
    {
        state.selected_record_idx = -1;
        state.selected_record.clear();
    }
    if (state.mode == TrackerPageState::Mode::Record)
    {
        update_record_page();
    }
}

void deferred_record_list_refresh_cb(lv_timer_t* timer)
{
    if (timer)
    {
        lv_timer_del(timer);
    }
    if (timer == s_record_list_refresh_timer)
    {
        s_record_list_refresh_timer = nullptr;
    }

    auto& state = g_tracker_state;
    if (!state.root || !lv_obj_is_valid(state.root))
    {
        return;
    }
    refresh_record_list();
}

void cancel_deferred_record_list_refresh()
{
    if (s_record_list_refresh_timer)
    {
        lv_timer_del(s_record_list_refresh_timer);
        s_record_list_refresh_timer = nullptr;
    }
}

void schedule_deferred_record_list_refresh(uint32_t delay_ms)
{
    cancel_deferred_record_list_refresh();
    s_record_list_refresh_timer = lv_timer_create(deferred_record_list_refresh_cb, delay_ms, nullptr);
    if (s_record_list_refresh_timer)
    {
        lv_timer_set_repeat_count(s_record_list_refresh_timer, 1);
    }
}

void update_route_status()
{
    auto& state = g_tracker_state;
    if (!state.status_label)
    {
        return;
    }
    lv_obj_set_style_text_font(
        state.status_label, ::ui::fonts::localized_font(::ui::fonts::ui_chrome_font()), 0);
    if (state.mode == TrackerPageState::Mode::Route)
    {
        if (!state.active_route.empty())
        {
            const std::string text = ::ui::i18n::format("Active: %s", state.active_route.c_str());
            ::ui::i18n::set_label_text_raw(state.status_label, text.c_str());
        }
        else if (!state.selected_route.empty())
        {
            const std::string text = ::ui::i18n::format("Selected: %s", state.selected_route.c_str());
            ::ui::i18n::set_label_text_raw(state.status_label, text.c_str());
        }
        else
        {
            ::ui::i18n::set_label_text(state.status_label, "No route selected");
        }
    }
}

void update_route_page()
{
    auto& state = g_tracker_state;
    if (!state.list_container)
    {
        return;
    }

    const lv_coord_t scroll_y = lv_obj_get_scroll_y(state.list_container);
    clear_list_items();

    if (s_route_names.empty())
    {
        create_list_item_button(s_route_empty_text.c_str(), 0, false, true);
    }
    else
    {
        for (size_t index = 0; index < s_route_names.size(); ++index)
        {
            create_list_item_button(format_list_name(s_route_names[index]),
                                    static_cast<intptr_t>(index) + kListUserDataOffset,
                                    static_cast<int>(index) == state.selected_route_idx,
                                    false);
        }
    }
    append_back_list_item();

    lv_obj_add_flag(state.list_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(state.list_container, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(state.list_container, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_scroll_to_y(state.list_container, scroll_y, LV_ANIM_OFF);
    if (state.focus_col == TrackerPageState::FocusColumn::Main && !is_any_modal_open())
    {
        tracker::ui::input::tracker_input_on_ui_refreshed();
    }
}

void refresh_route_list()
{
    auto& state = g_tracker_state;
    if (!state.list_container)
    {
        return;
    }
    s_route_names.clear();
    state.selected_route_idx = -1;
    state.selected_route.clear();
    s_route_empty_text = ::ui::i18n::tr("No KML routes");

    if (!platform::ui::device::sd_ready())
    {
        s_route_empty_text = ::ui::i18n::tr("No SD Card");
        if (state.mode == TrackerPageState::Mode::Route)
        {
            update_route_page();
        }
        return;
    }

    const bool route_dir_ready = platform::ui::route_storage::list_routes(s_route_names, 64);
    if (!route_dir_ready)
    {
        s_route_empty_text = ::ui::i18n::tr("No routes folder");
        if (state.mode == TrackerPageState::Mode::Route)
        {
            update_route_page();
        }
        return;
    }

    if (s_route_names.empty())
    {
        if (state.mode == TrackerPageState::Mode::Route)
        {
            update_route_page();
        }
        return;
    }

    std::sort(s_route_names.begin(), s_route_names.end());
    (void)repair_degraded_active_route_from_list();
    if (state.mode == TrackerPageState::Mode::Route)
    {
        update_route_page();
    }
}

void sync_active_route_from_config()
{
    auto& state = g_tracker_state;
    app::IAppFacade& app_ctx = app::appFacade();
    const auto& cfg = app_ctx.getConfig();
    if (cfg.route_enabled && cfg.route_path[0] != '\0')
    {
        const char* base = strrchr(cfg.route_path, '/');
        if (base && base[1] != '\0')
        {
            state.active_route = base + 1;
        }
        else
        {
            state.active_route = cfg.route_path;
        }
    }
    else
    {
        state.active_route.clear();
    }
}

void on_start_stop_clicked(lv_event_t*)
{
    const bool was_recording = platform::ui::tracker::is_recording();
    if (was_recording)
    {
        platform::ui::tracker::stop_recording();
    }
    else
    {
        if (!platform::ui::tracker::start_recording())
        {
            update_record_status();
            update_start_stop_button();
            if (g_tracker_state.status_label)
            {
                ::ui::i18n::set_label_text(g_tracker_state.status_label, "Start failed");
            }
            return;
        }
    }
    update_record_status();
    update_start_stop_button();
    if (was_recording)
    {
        schedule_deferred_record_list_refresh(120);
    }
}

void on_mode_record_clicked(lv_event_t*)
{
    set_mode(TrackerPageState::Mode::Record);
    focus_main_panel();
}

void on_mode_route_clicked(lv_event_t*)
{
    set_mode(TrackerPageState::Mode::Route);
    focus_main_panel();
}

void on_list_item_clicked(lv_event_t* e)
{
    if (!e)
    {
        return;
    }
    auto& state = g_tracker_state;
    lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
    void* ud = lv_obj_get_user_data(target);
    if (!ud)
    {
        return;
    }
    const intptr_t raw = reinterpret_cast<intptr_t>(ud);
    if (raw == kBackListItemUserData)
    {
        if (state.mode == TrackerPageState::Mode::Record)
        {
            state.selected_record_idx = -1;
            state.selected_record.clear();
            update_record_status();
        }
        else
        {
            state.selected_route_idx = -1;
            state.selected_route.clear();
            update_route_status();
        }
        sync_list_item_checked_states();
        focus_mode_panel();
        return;
    }
    if (raw < static_cast<intptr_t>(kListUserDataOffset))
    {
        return;
    }
    const size_t idx = static_cast<size_t>(raw - static_cast<intptr_t>(kListUserDataOffset));

    if (state.mode == TrackerPageState::Mode::Record)
    {
        if (idx >= s_record_names.size())
        {
            return;
        }
        state.selected_record_idx = static_cast<int>(idx);
        state.selected_record = s_record_names[idx].c_str();
        update_record_status();
    }
    else
    {
        if (idx >= s_route_names.size())
        {
            return;
        }
        state.selected_route_idx = static_cast<int>(idx);
        state.selected_route = s_route_names[idx].c_str();
        update_route_status();
    }
    sync_list_item_checked_states();
    open_action_menu_modal();
}

lv_obj_t* create_action_menu_button(lv_obj_t* parent, const char* text)
{
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_size(btn, LV_PCT(100), action_menu_button_height());

    lv_obj_t* label = lv_label_create(btn);
    ::ui::i18n::set_label_text(label, text);
    lv_obj_center(label);
    apply_action_button(btn, label);
    return btn;
}

void on_action_menu_key(lv_event_t* e)
{
    if (!e)
    {
        return;
    }
    const uint32_t key = lv_event_get_key(e);
    if (key == LV_KEY_ESC || key == LV_KEY_BACKSPACE)
    {
        modal_close(g_tracker_state.action_menu_modal);
        focus_main_panel();
    }
}

void on_action_menu_item_clicked(lv_event_t* e)
{
    if (!e)
    {
        return;
    }
    const auto cmd = static_cast<ActionMenuCommand>(
        reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));

    modal_close(g_tracker_state.action_menu_modal);

    switch (cmd)
    {
    case ActionMenuCommand::Load:
        on_route_load_clicked(nullptr);
        focus_main_panel();
        break;
    case ActionMenuCommand::Unload:
        on_route_unload_clicked(nullptr);
        focus_main_panel();
        break;
    case ActionMenuCommand::Preview:
        open_route_preview_page();
        break;
    case ActionMenuCommand::Delete:
        open_delete_confirm_modal();
        break;
    case ActionMenuCommand::Cancel:
    default:
        focus_main_panel();
        break;
    }
}

void open_action_menu_modal()
{
    auto& state = g_tracker_state;
    if (is_any_modal_open())
    {
        return;
    }

    bool has_item = false;
    int action_count = 1; // Cancel
    if (state.mode == TrackerPageState::Mode::Record)
    {
        has_item = state.selected_record_idx >= 0 &&
                   state.selected_record_idx < static_cast<int>(s_record_names.size());
        if (can_delete_selected_item())
        {
            ++action_count;
        }
    }
    else
    {
        has_item = state.selected_route_idx >= 0 &&
                   state.selected_route_idx < static_cast<int>(s_route_names.size());
        if (can_load_selected_route())
        {
            ++action_count;
        }
        if (can_unload_active_route())
        {
            ++action_count;
        }
        if (can_preview_selected_route())
        {
            ++action_count;
        }
        if (can_delete_selected_item())
        {
            ++action_count;
        }
    }

    if (!has_item)
    {
        return;
    }

    modal_prepare_group();
    const int max_modal_h = page_profile().large_touch_hitbox ? 320 : 220;
    const int modal_content_h = 56 + static_cast<int>(action_count) *
                                         static_cast<int>(action_menu_button_height() + action_menu_row_gap());
    const int modal_h = std::min<int>(max_modal_h, modal_content_h);
    state.action_menu_modal = create_modal_root(190, modal_h);
    lv_obj_t* win = lv_obj_get_child(state.action_menu_modal, 0);
    if (!win)
    {
        modal_close(state.action_menu_modal);
        return;
    }

    lv_obj_set_flex_flow(win, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(win, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(win, 4, LV_PART_MAIN);

    lv_obj_t* title_label = lv_label_create(win);
    const char* title = (state.mode == TrackerPageState::Mode::Record) ? "Track Actions" : "Route Actions";
    ::ui::i18n::set_label_text(title_label, title);
    lv_obj_set_width(title_label, LV_PCT(100));
    lv_label_set_long_mode(title_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(title_label, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t* list = lv_obj_create(win);
    lv_obj_set_width(list, LV_PCT(100));
    lv_obj_set_height(list, 0);
    lv_obj_set_flex_grow(list, 1);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(list, action_menu_row_gap(), LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);

    lv_obj_t* first_focus = nullptr;
    auto add_action = [&](ActionMenuCommand cmd, const char* text)
    {
        lv_obj_t* btn = create_action_menu_button(list, text);
        lv_obj_add_event_cb(btn,
                            on_action_menu_item_clicked,
                            LV_EVENT_CLICKED,
                            reinterpret_cast<void*>(static_cast<uintptr_t>(cmd)));
        lv_obj_add_event_cb(btn, on_action_menu_key, LV_EVENT_KEY, nullptr);
        lv_group_add_obj(state.modal_group, btn);
        if (!first_focus)
        {
            first_focus = btn;
        }
    };

    if (state.mode == TrackerPageState::Mode::Route)
    {
        if (can_load_selected_route())
        {
            add_action(ActionMenuCommand::Load, "Load");
        }
        if (can_unload_active_route())
        {
            add_action(ActionMenuCommand::Unload, "Off");
        }
        if (can_preview_selected_route())
        {
            add_action(ActionMenuCommand::Preview, "Preview");
        }
    }
    if (can_delete_selected_item())
    {
        add_action(ActionMenuCommand::Delete, "Delete");
    }
    add_action(ActionMenuCommand::Cancel, "Cancel");

    if (first_focus)
    {
        lv_group_focus_obj(first_focus);
    }
}

void on_list_item_focused(lv_event_t* e)
{
    if (!e)
    {
        return;
    }
    lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
    void* ud = lv_obj_get_user_data(target);
    if (!ud)
    {
        return;
    }
    lv_obj_scroll_to_view(target, LV_ANIM_OFF);
    const intptr_t raw = reinterpret_cast<intptr_t>(ud);
    if (raw == kBackListItemUserData)
    {
        if (lv_obj_t* label = lv_obj_get_child(target, -1))
        {
            ::ui::i18n::set_label_text(label, "Back");
            lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        }
        return;
    }
    if (raw < static_cast<intptr_t>(kListUserDataOffset))
    {
        return;
    }
    const size_t idx = static_cast<size_t>(raw - static_cast<intptr_t>(kListUserDataOffset));
    const auto& names =
        (g_tracker_state.mode == TrackerPageState::Mode::Record) ? s_record_names : s_route_names;
    if (idx >= names.size())
    {
        return;
    }
    lv_obj_t* label = lv_obj_get_child(target, -1);
    if (!label)
    {
        return;
    }
    lv_label_set_text(label, names[idx].c_str());
    lv_obj_set_style_text_font(label, ::ui::fonts::localized_font(::ui::fonts::ui_chrome_font()), 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
}

void on_list_item_defocused(lv_event_t* e)
{
    if (!e)
    {
        return;
    }
    lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
    void* ud = lv_obj_get_user_data(target);
    if (!ud)
    {
        return;
    }
    const intptr_t raw = reinterpret_cast<intptr_t>(ud);
    if (raw == kBackListItemUserData)
    {
        if (lv_obj_t* label = lv_obj_get_child(target, -1))
        {
            lv_label_set_text(label, ::ui::i18n::tr("Back"));
            lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        }
        return;
    }
    if (raw < static_cast<intptr_t>(kListUserDataOffset))
    {
        return;
    }
    const size_t idx = static_cast<size_t>(raw - static_cast<intptr_t>(kListUserDataOffset));
    const auto& names =
        (g_tracker_state.mode == TrackerPageState::Mode::Record) ? s_record_names : s_route_names;
    if (idx >= names.size())
    {
        return;
    }
    lv_obj_t* label = lv_obj_get_child(target, -1);
    if (!label)
    {
        return;
    }
    std::string display_name = format_list_name(names[idx]);
    lv_label_set_text(label, display_name.c_str());
    lv_obj_add_style(label, &s_btn_label, LV_PART_MAIN);
    lv_obj_set_style_text_font(label, ::ui::fonts::localized_font(::ui::fonts::ui_chrome_font()), 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
}

void on_mode_record_focused(lv_event_t*)
{
    set_mode(TrackerPageState::Mode::Record);
}

void on_mode_route_focused(lv_event_t*)
{
    set_mode(TrackerPageState::Mode::Route);
}

void on_mode_record_key(lv_event_t* e)
{
    if (!e)
    {
        return;
    }
    uint32_t key = lv_event_get_key(e);
    if (key != LV_KEY_ENTER)
    {
        return;
    }
    on_mode_record_clicked(e);
}

void on_mode_route_key(lv_event_t* e)
{
    if (!e)
    {
        return;
    }
    uint32_t key = lv_event_get_key(e);
    if (key != LV_KEY_ENTER)
    {
        return;
    }
    on_mode_route_clicked(e);
}

void on_route_load_clicked(lv_event_t*)
{
    auto& state = g_tracker_state;
    if (state.selected_route_idx < 0 || state.selected_route.empty())
    {
        if (state.status_label)
        {
            ::ui::i18n::set_label_text(state.status_label, "Select a route");
        }
        return;
    }
    state.active_route = state.selected_route;
    {
        app::IAppFacade& app_ctx = app::appFacade();
        auto& cfg = app_ctx.getConfig();
        cfg.route_enabled = true;
        char path[96];
        snprintf(path, sizeof(path), "%s/%s", platform::ui::route_storage::route_dir(), state.active_route.c_str());
        strncpy(cfg.route_path, path, sizeof(cfg.route_path) - 1);
        cfg.route_path[sizeof(cfg.route_path) - 1] = '\0';
        app_ctx.saveConfig();
    }
    update_route_status();
}

void on_route_unload_clicked(lv_event_t*)
{
    auto& state = g_tracker_state;
    if (state.active_route.empty())
    {
        update_route_status();
        return;
    }
    state.active_route.clear();
    {
        app::IAppFacade& app_ctx = app::appFacade();
        auto& cfg = app_ctx.getConfig();
        cfg.route_enabled = false;
        cfg.route_path[0] = '\0';
        app_ctx.saveConfig();
    }
    update_route_status();
}

void on_del_confirm_clicked(lv_event_t*)
{
    auto& state = g_tracker_state;
    bool ok = false;
    if (state.pending_delete_path.empty())
    {
        modal_close(state.del_confirm_modal);
        return;
    }
    std::string path = state.pending_delete_path;

    if (state.pending_delete_mode == TrackerPageState::Mode::Record)
    {
        if (platform::ui::tracker::is_recording())
        {
            std::string current;
            platform::ui::tracker::current_path(current);
            if (!current.empty() && path_basename(current) == state.pending_delete_name)
            {
                if (state.status_label)
                {
                    ::ui::i18n::set_label_text(state.status_label, "Stop recording first");
                }
                modal_close(state.del_confirm_modal);
                return;
            }
        }
        ok = platform::ui::tracker::remove_track(path);
        if (ok)
        {
            state.selected_record_idx = -1;
            state.selected_record.clear();
            refresh_record_list();
            if (state.mode == TrackerPageState::Mode::Record)
            {
                update_record_page();
            }
            update_record_status();
        }
    }
    else
    {
        if (!state.active_route.empty() && state.active_route == state.pending_delete_name)
        {
            app::IAppFacade& app_ctx = app::appFacade();
            auto& cfg = app_ctx.getConfig();
            cfg.route_enabled = false;
            cfg.route_path[0] = '\0';
            app_ctx.saveConfig();
            state.active_route.clear();
        }
        ok = platform::ui::route_storage::remove_route(path);
        if (ok)
        {
            state.selected_route_idx = -1;
            state.selected_route.clear();
            refresh_route_list();
            if (state.mode == TrackerPageState::Mode::Route)
            {
                update_route_page();
            }
            update_route_status();
        }
    }

    if (!ok && state.status_label)
    {
        ::ui::i18n::set_label_text(state.status_label, "Delete failed");
    }
    modal_close(state.del_confirm_modal);
}

void on_del_cancel_clicked(lv_event_t*)
{
    modal_close(g_tracker_state.del_confirm_modal);
}

void open_delete_confirm_modal()
{
    auto& state = g_tracker_state;
    if (state.del_confirm_modal)
    {
        return;
    }

    if (state.mode == TrackerPageState::Mode::Record)
    {
        if (state.selected_record_idx < 0 ||
            state.selected_record_idx >= static_cast<int>(s_record_names.size()))
        {
            if (state.status_label)
            {
                ::ui::i18n::set_label_text(state.status_label, "Select a track");
            }
            return;
        }
        state.pending_delete_mode = TrackerPageState::Mode::Record;
        state.pending_delete_name = s_record_names[state.selected_record_idx].c_str();
        std::string path =
            std::string(platform::ui::tracker::track_dir()) + "/" + s_record_names[state.selected_record_idx];
        state.pending_delete_path = path;
    }
    else
    {
        if (state.selected_route_idx < 0 ||
            state.selected_route_idx >= static_cast<int>(s_route_names.size()))
        {
            if (state.status_label)
            {
                ::ui::i18n::set_label_text(state.status_label, "Select a route");
            }
            return;
        }
        state.pending_delete_mode = TrackerPageState::Mode::Route;
        state.pending_delete_name = s_route_names[state.selected_route_idx].c_str();
        std::string path = std::string(platform::ui::route_storage::route_dir()) + "/" + s_route_names[state.selected_route_idx];
        state.pending_delete_path = path;
    }

    modal_prepare_group();
    state.del_confirm_modal = create_modal_root(280, 140);
    lv_obj_t* win = lv_obj_get_child(state.del_confirm_modal, 0);

    const std::string msg = ::ui::i18n::format("Delete %s?", state.pending_delete_name.c_str());
    lv_obj_t* label = lv_label_create(win);
    ::ui::i18n::set_label_text_raw(label, msg.c_str());
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 10);

    lv_obj_t* btn_row = lv_obj_create(win);
    lv_obj_set_size(btn_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_align(btn_row, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(btn_row, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_row, 0, LV_PART_MAIN);
    lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* confirm_btn = lv_btn_create(btn_row);
    lv_obj_set_size(confirm_btn, modal_button_width(), action_menu_button_height());
    lv_obj_t* confirm_label = lv_label_create(confirm_btn);
    ::ui::i18n::set_label_text(confirm_label, "Confirm");
    lv_obj_center(confirm_label);
    apply_action_button(confirm_btn, confirm_label);
    lv_obj_add_event_cb(confirm_btn, on_del_confirm_clicked, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* cancel_btn = lv_btn_create(btn_row);
    lv_obj_set_size(cancel_btn, modal_button_width(), action_menu_button_height());
    lv_obj_t* cancel_label = lv_label_create(cancel_btn);
    ::ui::i18n::set_label_text(cancel_label, "Cancel");
    lv_obj_center(cancel_label);
    apply_action_button(cancel_btn, cancel_label);
    lv_obj_add_event_cb(cancel_btn, on_del_cancel_clicked, LV_EVENT_CLICKED, nullptr);

    lv_group_add_obj(state.modal_group, confirm_btn);
    lv_group_add_obj(state.modal_group, cancel_btn);
    lv_group_focus_obj(cancel_btn);
}

} // namespace

void refresh_page()
{
    sync_active_route_from_config();
    update_mode_buttons();
    update_record_status();
    update_start_stop_button();
    refresh_record_list();
    refresh_route_list();
    update_route_status();
    tracker::ui::input::tracker_input_on_ui_refreshed();
    ui_update_top_bar_battery(g_tracker_state.top_bar);
}

void init_page(lv_obj_t* parent)
{
    auto& state = g_tracker_state;
    if (!parent)
    {
        return;
    }

    if (state.root)
    {
        cleanup_page();
    }

    state.root = layout::create_root(parent);
    state.header = layout::create_header(state.root);
    state.content = layout::create_content(state.root);
    state.filter_panel = layout::create_filter_panel(state.content, filter_panel_width());
    state.list_panel = layout::create_list_panel(state.content);

    state.mode_record_btn = lv_btn_create(state.filter_panel);
    lv_obj_set_width(state.mode_record_btn, LV_PCT(100));
    lv_obj_set_height(state.mode_record_btn, filter_button_height());
    state.mode_record_label = lv_label_create(state.mode_record_btn);
    lv_obj_add_style(state.mode_record_label, &s_btn_label, LV_PART_MAIN);
    ::ui::i18n::set_label_text(state.mode_record_label, "Record");
    lv_obj_center(state.mode_record_label);

    state.mode_route_btn = lv_btn_create(state.filter_panel);
    lv_obj_set_width(state.mode_route_btn, LV_PCT(100));
    lv_obj_set_height(state.mode_route_btn, filter_button_height());
    state.mode_route_label = lv_label_create(state.mode_route_btn);
    lv_obj_add_style(state.mode_route_label, &s_btn_label, LV_PART_MAIN);
    ::ui::i18n::set_label_text(state.mode_route_label, "Route");
    lv_obj_center(state.mode_route_label);

    state.status_label = lv_label_create(state.list_panel);
    ::ui::i18n::set_label_text(state.status_label, "Stopped");
    lv_obj_set_width(state.status_label, LV_PCT(100));
    lv_obj_set_style_text_font(
        state.status_label, ::ui::fonts::localized_font(::ui::fonts::ui_chrome_font()), 0);
    lv_obj_set_style_text_color(state.status_label, lv_color_hex(kPanelTextMuted), 0);

    state.list_container = layout::create_list_container(state.list_panel);
    lv_obj_add_flag(state.list_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(state.list_container, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(state.list_container, LV_SCROLLBAR_MODE_AUTO);

    state.bottom_bar = layout::create_bottom_bar(state.list_panel);
    state.start_stop_btn = lv_btn_create(state.bottom_bar);
    lv_obj_set_size(state.start_stop_btn, bottom_bar_button_width(), filter_button_height());
    state.start_stop_label = lv_label_create(state.start_stop_btn);
    lv_obj_set_width(state.start_stop_label, LV_PCT(100));
    lv_label_set_long_mode(state.start_stop_label, LV_LABEL_LONG_DOT);
    lv_obj_center(state.start_stop_label);
    apply_action_button(state.start_stop_btn, state.start_stop_label);

    ::ui::widgets::TopBarConfig cfg;
    cfg.height = page_profile().top_bar_height;
    ::ui::widgets::top_bar_init(state.top_bar, state.header, cfg);
    ::ui::widgets::top_bar_set_title(state.top_bar, ::ui::i18n::tr("Tracker"));
    ::ui::widgets::top_bar_set_back_callback(state.top_bar, on_back, nullptr);

    lv_obj_add_event_cb(state.mode_record_btn, on_mode_record_clicked, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(state.mode_route_btn, on_mode_route_clicked, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(state.mode_record_btn, on_mode_record_focused, LV_EVENT_FOCUSED, nullptr);
    lv_obj_add_event_cb(state.mode_route_btn, on_mode_route_focused, LV_EVENT_FOCUSED, nullptr);
    lv_obj_add_event_cb(state.mode_record_btn, on_mode_record_key, LV_EVENT_KEY, nullptr);
    lv_obj_add_event_cb(state.mode_route_btn, on_mode_route_key, LV_EVENT_KEY, nullptr);
    lv_obj_add_event_cb(state.start_stop_btn, on_start_stop_clicked, LV_EVENT_CLICKED, nullptr);

    set_mode(TrackerPageState::Mode::Record);
    refresh_page();
}

void cleanup_page()
{
    auto& state = g_tracker_state;
    cancel_deferred_record_list_refresh();
    if (state.action_menu_modal)
    {
        lv_obj_del(state.action_menu_modal);
        state.action_menu_modal = nullptr;
    }
    if (state.del_confirm_modal)
    {
        lv_obj_del(state.del_confirm_modal);
        state.del_confirm_modal = nullptr;
    }
    if (state.route_preview_help_modal)
    {
        lv_obj_del(state.route_preview_help_modal);
        state.route_preview_help_modal = nullptr;
    }
    if (s_preview_download_modal)
    {
        close_route_preview_download_modal();
    }
    ::ui::widgets::map::destroy(s_preview_map_runtime);
    if (state.route_preview_page)
    {
        lv_obj_del(state.route_preview_page);
        state.route_preview_page = nullptr;
    }
    state.route_preview_map_host = nullptr;
    state.route_preview_elevation_panel = nullptr;
    state.route_preview_status_label = nullptr;
    state.route_preview_progress = nullptr;
    state.route_preview_load_label = nullptr;
    if (state.modal_group)
    {
        lv_group_del(state.modal_group);
        state.modal_group = nullptr;
    }
    clear_preview_model();
    state.prev_group = nullptr;
    if (state.root)
    {
        lv_obj_del(state.root);
    }
    state = TrackerPageState{};
}

} // namespace components
} // namespace ui
} // namespace tracker
