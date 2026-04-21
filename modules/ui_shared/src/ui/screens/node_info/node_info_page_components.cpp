/**
 * @file node_info_page_components.cpp
 * @brief Node info page UI components
 */

#include "ui/screens/node_info/node_info_page_components.h"

#include "app/app_config.h"
#include "app/app_facade_access.h"
#include "chat/infra/meshtastic/mt_region.h"
#include "chat/usecase/contact_service.h"
#include "platform/ui/gps_runtime.h"
#include "sys/clock.h"
#include "ui/assets/fonts/font_utils.h"
#include "ui/localization.h"
#include "ui/menu/dashboard/dashboard_style.h"
#include "ui/page/page_profile.h"
#include "ui/screens/node_info/node_info_page_layout.h"
#include "ui/ui_common.h"
#include "ui/widgets/top_bar.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace node_info
{
namespace ui
{

namespace
{

namespace dashboard = ::ui::menu::dashboard;

NodeInfoWidgets s_widgets;
::ui::widgets::TopBar s_top_bar;

struct GeoPoint
{
    bool valid = false;
    double lat = 0.0;
    double lon = 0.0;
};

struct NodeInfoRuntimeState
{
    bool has_node = false;
    bool map_ready = false;
    chat::contacts::NodeInfo node{};
    GeoPoint self_point{};
    int zoom = 12;
    char tile_paths[kNodeInfoTileCount][96]{};
    lv_point_precise_t link_points[2]{};
};

NodeInfoRuntimeState s_state;

struct ViewMetrics
{
    lv_coord_t width = 0;
    lv_coord_t height = 0;
    lv_coord_t pad = 12;
    lv_coord_t right_col_w = 120;
    lv_coord_t right_x = 0;
    lv_coord_t left_w = 0;
    lv_coord_t focus_x = 0;
    lv_coord_t focus_y = 0;
    lv_coord_t info_top = 0;
    lv_coord_t info_line_h = 16;
    lv_coord_t info_gap = 5;
    lv_coord_t zoom_size = 30;
    lv_coord_t zoom_gap = 8;
    bool compact = true;
};

constexpr int kTileSize = 256;
constexpr int kDefaultZoom = 12;
constexpr int kMinZoom = 2;
constexpr int kMaxZoom = 16;

static const lv_color_t kColorBackdrop = lv_color_hex(0x0D1520);
static const lv_color_t kColorBackdropAlt = lv_color_hex(0x121F2A);
static const lv_color_t kColorTopBar = lv_color_hex(0x081018);
static const lv_color_t kColorTopText = lv_color_hex(0xF4E6C7);
static const lv_color_t kColorId = lv_color_hex(0xFFB44E);
static const lv_color_t kColorLon = lv_color_hex(0x5ED8FF);
static const lv_color_t kColorLat = lv_color_hex(0x96EA69);
static const lv_color_t kColorDistance = lv_color_hex(0xFFD166);
static const lv_color_t kColorNodeMarker = lv_color_hex(0xFF8C42);
static const lv_color_t kColorSelfMarker = lv_color_hex(0x4ED9FF);
static const lv_color_t kColorLink = lv_color_hex(0x8BE2C8);
static const lv_color_t kColorMuted = lv_color_hex(0xB5C7D3);
static const lv_color_t kColorButtonBg = lv_color_hex(0x182739);
static const lv_color_t kColorButtonDisabled = lv_color_hex(0x243545);

static const lv_color_t kInfoLineColors[kNodeInfoInfoLineCount] = {
    lv_color_hex(0xFF9D6C),
    lv_color_hex(0x69E5DB),
    lv_color_hex(0xB1F06D),
    lv_color_hex(0xFFD26A),
    lv_color_hex(0xB99CFF),
    lv_color_hex(0xFF9CCA),
    lv_color_hex(0x83CAFF),
    lv_color_hex(0xF4F28C),
};

const lv_font_t* font_montserrat_12_safe()
{
#if defined(LV_FONT_MONTSERRAT_12) && LV_FONT_MONTSERRAT_12
    return &lv_font_montserrat_12;
#else
    return LV_FONT_DEFAULT;
#endif
}

const lv_font_t* font_montserrat_14_safe()
{
#if defined(LV_FONT_MONTSERRAT_14) && LV_FONT_MONTSERRAT_14
    return &lv_font_montserrat_14;
#else
    return font_montserrat_12_safe();
#endif
}

const lv_font_t* font_montserrat_16_safe()
{
#if defined(LV_FONT_MONTSERRAT_16) && LV_FONT_MONTSERRAT_16
    return &lv_font_montserrat_16;
#else
    return font_montserrat_14_safe();
#endif
}

const lv_font_t* font_montserrat_18_safe()
{
#if defined(LV_FONT_MONTSERRAT_18) && LV_FONT_MONTSERRAT_18
    return &lv_font_montserrat_18;
#else
    return font_montserrat_16_safe();
#endif
}

const lv_font_t* font_montserrat_22_safe()
{
#if defined(LV_FONT_MONTSERRAT_22) && LV_FONT_MONTSERRAT_22
    return &lv_font_montserrat_22;
#else
    return font_montserrat_18_safe();
#endif
}

lv_coord_t clamp_coord(lv_coord_t value, lv_coord_t min_value, lv_coord_t max_value)
{
    if (max_value < min_value)
    {
        max_value = min_value;
    }
    if (value < min_value)
    {
        return min_value;
    }
    if (value > max_value)
    {
        return max_value;
    }
    return value;
}

double clamp_double(double value, double min_value, double max_value)
{
    if (max_value < min_value)
    {
        max_value = min_value;
    }
    if (value < min_value)
    {
        return min_value;
    }
    if (value > max_value)
    {
        return max_value;
    }
    return value;
}

void make_plain(lv_obj_t* obj)
{
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
}

void set_label_text(lv_obj_t* label, const char* text)
{
    if (!label || !text)
    {
        return;
    }
    lv_label_set_text(label, text);
    ::ui::fonts::apply_localized_font(label, text, lv_obj_get_style_text_font(label, LV_PART_MAIN));
}

lv_obj_t* create_label(lv_obj_t* parent,
                       const char* text,
                       const lv_font_t* font,
                       lv_color_t color)
{
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, 0);
    ::ui::fonts::apply_localized_font(label, text, font);
    return label;
}

void apply_top_bar_style()
{
    if (!s_top_bar.container)
    {
        return;
    }

    lv_obj_set_style_bg_color(s_top_bar.container, kColorTopBar, 0);
    lv_obj_set_style_bg_opa(s_top_bar.container, 220, 0);
    lv_obj_set_style_border_width(s_top_bar.container, 1, 0);
    lv_obj_set_style_border_side(s_top_bar.container, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(s_top_bar.container, lv_color_hex(0x294057), 0);

    if (s_widgets.title_label)
    {
        lv_obj_set_style_text_color(s_widgets.title_label, kColorTopText, 0);
    }
    if (s_widgets.battery_label)
    {
        lv_obj_set_style_text_color(s_widgets.battery_label, kColorTopText, 0);
    }
    if (s_widgets.back_label)
    {
        lv_obj_set_style_text_color(s_widgets.back_label, kColorTopText, 0);
    }
}

void apply_zoom_button_style(lv_obj_t* button, lv_obj_t* label)
{
    if (!button)
    {
        return;
    }

    lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(button, kColorButtonBg, 0);
    lv_obj_set_style_bg_opa(button, 190, 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, lv_color_hex(0x44627C), 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_bg_color(button, kColorButtonDisabled, LV_STATE_DISABLED);
    lv_obj_set_style_bg_opa(button, 120, LV_STATE_DISABLED);
    lv_obj_set_style_border_color(button, lv_color_hex(0x354B5E), LV_STATE_DISABLED);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(button, LV_SCROLLBAR_MODE_OFF);

    if (label)
    {
        lv_obj_set_style_text_font(label, font_montserrat_22_safe(), 0);
        lv_obj_set_style_text_color(label, kColorTopText, 0);
        ::ui::fonts::apply_localized_font(label, lv_label_get_text(label), font_montserrat_22_safe());
    }
}

void apply_marker_style(lv_obj_t* obj, lv_coord_t size, lv_color_t color, bool filled)
{
    if (!obj)
    {
        return;
    }

    lv_obj_set_size(obj, size, size);
    lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(obj, filled ? 0 : 2, 0);
    lv_obj_set_style_border_color(obj, color, 0);
    lv_obj_set_style_bg_color(obj, color, 0);
    lv_obj_set_style_bg_opa(obj, filled ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
}

void apply_scrim_style(lv_obj_t* obj, lv_color_t color, lv_opa_t opa)
{
    if (!obj)
    {
        return;
    }

    lv_obj_set_style_bg_color(obj, color, 0);
    lv_obj_set_style_bg_opa(obj, opa, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    make_plain(obj);
}

ViewMetrics view_metrics()
{
    ViewMetrics metrics{};
    const auto& profile = ::ui::page_profile::current();

    metrics.width = s_widgets.content ? lv_obj_get_width(s_widgets.content) : 0;
    metrics.height = s_widgets.content ? lv_obj_get_height(s_widgets.content) : 0;
    if (metrics.width <= 0 && s_widgets.root)
    {
        metrics.width = lv_obj_get_width(s_widgets.root);
    }
    if (metrics.height <= 0 && s_widgets.root)
    {
        metrics.height = lv_obj_get_height(s_widgets.root) - profile.top_bar_height;
    }

    metrics.pad = profile.large_touch_hitbox ? 18 : 12;
    metrics.compact = metrics.width < 420;
    metrics.right_col_w = metrics.compact ? 124 : 176;
    if (metrics.width > 0)
    {
        const lv_coord_t max_right = metrics.width - (metrics.pad * 2) - 96;
        if (max_right < metrics.right_col_w)
        {
            metrics.right_col_w = max_right;
        }
    }
    if (metrics.right_col_w < 108)
    {
        metrics.right_col_w = 108;
    }
    metrics.right_x = metrics.width - metrics.pad - metrics.right_col_w;
    metrics.left_w = metrics.right_x - metrics.pad;
    if (metrics.left_w < 96)
    {
        metrics.left_w = 96;
    }

    const lv_coord_t focus_min = metrics.pad + 42;
    const lv_coord_t focus_max = metrics.right_x - metrics.pad - 28;
    metrics.focus_x = clamp_coord(metrics.width / 3, focus_min, focus_max);
    metrics.focus_y = metrics.height / 2;

    metrics.info_top = metrics.pad + 10;
    metrics.info_line_h = metrics.compact ? 14 : 16;
    metrics.info_gap = metrics.compact ? 5 : 6;
    metrics.zoom_size = metrics.compact ? 30 : 36;
    metrics.zoom_gap = metrics.compact ? 8 : 10;
    return metrics;
}

uint8_t sanitize_map_source(uint8_t map_source)
{
    return map_source <= 2 ? map_source : 0;
}

bool build_base_tile_path(int zoom, int x, int y, uint8_t map_source, char* out_path, size_t out_size)
{
    if (!out_path || out_size == 0)
    {
        return false;
    }

    const char* source_dir = "osm";
    const char* ext = "png";
    switch (sanitize_map_source(map_source))
    {
    case 1:
        source_dir = "terrain";
        break;
    case 2:
        source_dir = "satellite";
        ext = "jpg";
        break;
    default:
        break;
    }

    std::snprintf(out_path, out_size, "A:/maps/base/%s/%d/%d/%d.%s", source_dir, zoom, x, y, ext);
    out_path[out_size - 1] = '\0';
    return true;
}

bool tile_exists(const char* path)
{
    if (!path || path[0] == '\0')
    {
        return false;
    }

    lv_fs_file_t file;
    const lv_fs_res_t res = lv_fs_open(&file, path, LV_FS_MODE_RD);
    if (res != LV_FS_RES_OK)
    {
        return false;
    }

    lv_fs_close(&file);
    return true;
}

bool latlng_to_world_pixels(double lat, double lon, int zoom, double& out_x, double& out_y)
{
    const double kMaxLat = 85.05112878;
    lat = clamp_double(lat, -kMaxLat, kMaxLat);
    while (lon < -180.0)
    {
        lon += 360.0;
    }
    while (lon >= 180.0)
    {
        lon -= 360.0;
    }

    const double n = static_cast<double>(1 << zoom);
    const double lat_rad = lat * M_PI / 180.0;
    const double tile_x = (lon + 180.0) / 360.0 * n;
    const double tile_y =
        (1.0 - std::log(std::tan(lat_rad) + 1.0 / std::cos(lat_rad)) / M_PI) / 2.0 * n;

    out_x = tile_x * static_cast<double>(kTileSize);
    out_y = tile_y * static_cast<double>(kTileSize);
    return true;
}

int normalize_tile_x(int x, int zoom)
{
    const int tile_count = 1 << zoom;
    if (tile_count <= 0)
    {
        return 0;
    }

    int value = x % tile_count;
    if (value < 0)
    {
        value += tile_count;
    }
    return value;
}

GeoPoint node_point_from_info(const chat::contacts::NodeInfo& node)
{
    GeoPoint point{};
    if (!node.position.valid)
    {
        return point;
    }

    point.valid = true;
    point.lat = static_cast<double>(node.position.latitude_i) * 1e-7;
    point.lon = static_cast<double>(node.position.longitude_i) * 1e-7;
    return point;
}

GeoPoint resolve_self_position()
{
    GeoPoint point{};

    if (app::hasAppFacade())
    {
        const chat::NodeId self_node_id = app::messagingFacade().getSelfNodeId();
        if (self_node_id != 0)
        {
            const auto* self_info = app::messagingFacade().getContactService().getNodeInfo(self_node_id);
            if (self_info && self_info->position.valid)
            {
                point.valid = true;
                point.lat = static_cast<double>(self_info->position.latitude_i) * 1e-7;
                point.lon = static_cast<double>(self_info->position.longitude_i) * 1e-7;
                return point;
            }
        }
    }

    const auto gps = platform::ui::gps::get_data();
    if (gps.valid)
    {
        point.valid = true;
        point.lat = gps.lat;
        point.lon = gps.lng;
    }
    return point;
}

bool center_tile_exists(const GeoPoint& node_point, int zoom)
{
    if (!node_point.valid)
    {
        return false;
    }

    double world_x = 0.0;
    double world_y = 0.0;
    latlng_to_world_pixels(node_point.lat, node_point.lon, zoom, world_x, world_y);
    const int tile_x = normalize_tile_x(static_cast<int>(std::floor(world_x / kTileSize)), zoom);
    const int tile_y = static_cast<int>(std::floor(world_y / kTileSize));
    const int max_tile = (1 << zoom) - 1;
    if (tile_y < 0 || tile_y > max_tile)
    {
        return false;
    }

    char path[96];
    if (!build_base_tile_path(
            zoom, tile_x, tile_y, sanitize_map_source(app::configFacade().getConfig().map_source), path, sizeof(path)))
    {
        return false;
    }
    return tile_exists(path);
}

int best_available_zoom(const GeoPoint& node_point)
{
    if (!node_point.valid)
    {
        return kDefaultZoom;
    }

    if (center_tile_exists(node_point, kDefaultZoom))
    {
        return kDefaultZoom;
    }

    for (int delta = 1; delta <= (kMaxZoom - kMinZoom); ++delta)
    {
        const int higher = kDefaultZoom + delta;
        if (higher <= kMaxZoom && center_tile_exists(node_point, higher))
        {
            return higher;
        }
        const int lower = kDefaultZoom - delta;
        if (lower >= kMinZoom && center_tile_exists(node_point, lower))
        {
            return lower;
        }
    }

    return kDefaultZoom;
}

int compute_initial_zoom(const GeoPoint& node_point, const GeoPoint& self_point, const ViewMetrics& metrics)
{
    if (!node_point.valid)
    {
        return kDefaultZoom;
    }
    if (!self_point.valid)
    {
        return best_available_zoom(node_point);
    }

    const double min_x = static_cast<double>(metrics.pad + 8);
    const double max_x = static_cast<double>(metrics.right_x - 14);
    const double min_y = static_cast<double>(metrics.pad + 6);
    const double max_y = static_cast<double>(metrics.height - metrics.pad - 6);

    for (int zoom = kMaxZoom; zoom >= kMinZoom; --zoom)
    {
        if (!center_tile_exists(node_point, zoom))
        {
            continue;
        }

        double node_x = 0.0;
        double node_y = 0.0;
        double self_x = 0.0;
        double self_y = 0.0;
        latlng_to_world_pixels(node_point.lat, node_point.lon, zoom, node_x, node_y);
        latlng_to_world_pixels(self_point.lat, self_point.lon, zoom, self_x, self_y);

        const double draw_x = static_cast<double>(metrics.focus_x) + (self_x - node_x);
        const double draw_y = static_cast<double>(metrics.focus_y) + (self_y - node_y);
        if (draw_x >= min_x && draw_x <= max_x && draw_y >= min_y && draw_y <= max_y)
        {
            return zoom;
        }
    }

    return best_available_zoom(node_point);
}

int select_zoom_after_delta(const GeoPoint& node_point, int current_zoom, int delta)
{
    if (!node_point.valid)
    {
        return current_zoom;
    }

    const int start = clamp_coord(current_zoom + delta, kMinZoom, kMaxZoom);
    if (center_tile_exists(node_point, start))
    {
        return start;
    }

    if (delta > 0)
    {
        for (int zoom = start + 1; zoom <= kMaxZoom; ++zoom)
        {
            if (center_tile_exists(node_point, zoom))
            {
                return zoom;
            }
        }
    }
    else if (delta < 0)
    {
        for (int zoom = start - 1; zoom >= kMinZoom; --zoom)
        {
            if (center_tile_exists(node_point, zoom))
            {
                return zoom;
            }
        }
    }

    return current_zoom;
}

void set_hidden(lv_obj_t* obj, bool hidden)
{
    if (!obj)
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

void format_node_id(uint32_t node_id, char* out, size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }

    if (node_id <= 0xFFFFFFUL)
    {
        std::snprintf(out, out_len, "ID !%06lX", static_cast<unsigned long>(node_id));
    }
    else
    {
        std::snprintf(out, out_len, "ID !%08lX", static_cast<unsigned long>(node_id));
    }
}

void format_age_short(uint32_t ts, char* out, size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }

    if (ts == 0)
    {
        std::snprintf(out, out_len, "Seen --");
        return;
    }

    const uint32_t now = sys::epoch_seconds_now();
    if (now <= ts)
    {
        std::snprintf(out, out_len, "Seen 0s");
        return;
    }

    const uint32_t age = now - ts;
    if (age < 60)
    {
        std::snprintf(out, out_len, "Seen %us", static_cast<unsigned>(age));
        return;
    }
    if (age < 3600)
    {
        std::snprintf(out, out_len, "Seen %um", static_cast<unsigned>(age / 60));
        return;
    }
    if (age < 86400)
    {
        std::snprintf(out, out_len, "Seen %uh", static_cast<unsigned>(age / 3600));
        return;
    }
    std::snprintf(out, out_len, "Seen %ud", static_cast<unsigned>(age / 86400));
}

double compute_accuracy_m(const chat::contacts::NodePosition& pos)
{
    if (pos.gps_accuracy_mm == 0)
    {
        return -1.0;
    }
    double acc = static_cast<double>(pos.gps_accuracy_mm) / 1000.0;
    const uint32_t dop = pos.pdop ? pos.pdop : (pos.hdop ? pos.hdop : pos.vdop);
    if (dop > 0)
    {
        acc *= static_cast<double>(dop) / 100.0;
    }
    return acc;
}

bool get_meshtastic_radio_params(double& out_freq_mhz, unsigned& out_sf, double& out_bw_khz)
{
    const auto& cfg = app::configFacade().getConfig();
    auto region_code =
        static_cast<meshtastic_Config_LoRaConfig_RegionCode>(cfg.meshtastic_config.region);
    if (region_code == meshtastic_Config_LoRaConfig_RegionCode_UNSET)
    {
        region_code = meshtastic_Config_LoRaConfig_RegionCode_CN;
    }

    const chat::meshtastic::RegionInfo* region = chat::meshtastic::findRegion(region_code);
    if (!region)
    {
        return false;
    }

    double bw_khz = 250.0;
    unsigned sf = 11;
    const auto preset = static_cast<meshtastic_Config_LoRaConfig_ModemPreset>(
        cfg.meshtastic_config.modem_preset);

    switch (preset)
    {
    case meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO:
        bw_khz = region->wide_lora ? 1625.0 : 500.0;
        sf = 7;
        break;
    case meshtastic_Config_LoRaConfig_ModemPreset_SHORT_FAST:
        bw_khz = region->wide_lora ? 812.5 : 250.0;
        sf = 7;
        break;
    case meshtastic_Config_LoRaConfig_ModemPreset_SHORT_SLOW:
        bw_khz = region->wide_lora ? 812.5 : 250.0;
        sf = 8;
        break;
    case meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST:
        bw_khz = region->wide_lora ? 812.5 : 250.0;
        sf = 9;
        break;
    case meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_SLOW:
        bw_khz = region->wide_lora ? 812.5 : 250.0;
        sf = 10;
        break;
    case meshtastic_Config_LoRaConfig_ModemPreset_LONG_TURBO:
        bw_khz = region->wide_lora ? 1625.0 : 500.0;
        sf = 11;
        break;
    case meshtastic_Config_LoRaConfig_ModemPreset_LONG_MODERATE:
        bw_khz = region->wide_lora ? 406.25 : 125.0;
        sf = 11;
        break;
    case meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW:
        bw_khz = region->wide_lora ? 406.25 : 125.0;
        sf = 12;
        break;
    case meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST:
    default:
        bw_khz = region->wide_lora ? 812.5 : 250.0;
        sf = 11;
        break;
    }

    const char* channel_name = chat::meshtastic::presetDisplayName(preset);
    double freq_mhz = chat::meshtastic::computeFrequencyMhz(
        region, static_cast<float>(bw_khz), channel_name);
    if (freq_mhz <= 0.0)
    {
        freq_mhz = region->freq_start_mhz + (bw_khz / 2000.0);
    }

    out_freq_mhz = freq_mhz;
    out_sf = sf;
    out_bw_khz = bw_khz;
    return true;
}

const char* protocol_name(chat::contacts::NodeProtocolType protocol)
{
    switch (protocol)
    {
    case chat::contacts::NodeProtocolType::Meshtastic:
        return "Meshtastic";
    case chat::contacts::NodeProtocolType::MeshCore:
        return "MeshCore";
    case chat::contacts::NodeProtocolType::RNode:
        return "RNode";
    case chat::contacts::NodeProtocolType::LXMF:
        return "LXMF";
    default:
        return "Unknown";
    }
}

const char* protocol_short_name(chat::contacts::NodeProtocolType protocol)
{
    switch (protocol)
    {
    case chat::contacts::NodeProtocolType::Meshtastic:
        return "MT";
    case chat::contacts::NodeProtocolType::MeshCore:
        return "MC";
    case chat::contacts::NodeProtocolType::RNode:
        return "RN";
    case chat::contacts::NodeProtocolType::LXMF:
        return "LX";
    default:
        return "--";
    }
}

std::string preferred_node_title(const chat::contacts::NodeInfo& node)
{
    if (!node.display_name.empty())
    {
        return node.display_name;
    }
    if (node.short_name[0] != '\0')
    {
        return node.short_name;
    }
    if (node.long_name[0] != '\0')
    {
        return node.long_name;
    }
    return ::ui::i18n::tr("NODE INFO");
}

void set_top_bar_title(const chat::contacts::NodeInfo& node)
{
    if (!s_widgets.title_label)
    {
        return;
    }

    const std::string title = preferred_node_title(node);
    ::ui::i18n::set_content_label_text_raw(s_widgets.title_label, title.c_str());
    lv_obj_set_style_text_color(s_widgets.title_label, kColorTopText, 0);
}

void format_coord_label(const char* prefix, double value, char* out, size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }
    std::snprintf(out, out_len, "%s %.5f", prefix, value);
}

bool append_info_line(std::size_t& count, const char* text)
{
    if (!text || text[0] == '\0' || count >= kNodeInfoInfoLineCount)
    {
        return false;
    }

    set_label_text(s_widgets.info_labels[count], text);
    set_hidden(s_widgets.info_labels[count], false);
    ++count;
    return true;
}

void hide_unused_info_lines(std::size_t visible_count)
{
    for (std::size_t index = visible_count; index < kNodeInfoInfoLineCount; ++index)
    {
        set_hidden(s_widgets.info_labels[index], true);
    }
}

void build_protocol_line(const chat::contacts::NodeInfo& node, bool compact, char* out, size_t out_len)
{
    const char* medium = node.via_mqtt ? "MQTT" : "LoRa";
    if (compact)
    {
        std::snprintf(out, out_len, "%s / %s", protocol_short_name(node.protocol), medium);
        return;
    }
    std::snprintf(out, out_len, "Protocol  %s / %s", protocol_name(node.protocol), medium);
}

void build_signal_line(const chat::contacts::NodeInfo& node, bool compact, char* out, size_t out_len)
{
    char rssi[24];
    char snr[24];

    if (std::isnan(node.rssi))
    {
        std::snprintf(rssi, sizeof(rssi), "RSSI --");
    }
    else
    {
        std::snprintf(rssi, sizeof(rssi), "RSSI %.0f", node.rssi);
    }

    if (std::isnan(node.snr))
    {
        std::snprintf(snr, sizeof(snr), "SNR --");
    }
    else
    {
        std::snprintf(snr, sizeof(snr), "SNR %.1f", node.snr);
    }

    if (compact)
    {
        std::snprintf(out, out_len, "%s / %s", rssi, snr);
    }
    else
    {
        std::snprintf(out, out_len, "Signal  %s / %s", rssi + 5, snr + 4);
    }
}

bool build_radio_line(const chat::contacts::NodeInfo& node, bool compact, char* out, size_t out_len)
{
    if (node.protocol == chat::contacts::NodeProtocolType::Meshtastic)
    {
        double freq_mhz = 0.0;
        double bw_khz = 0.0;
        unsigned sf = 0;
        if (!get_meshtastic_radio_params(freq_mhz, sf, bw_khz))
        {
            return false;
        }

        if (compact)
        {
            std::snprintf(out, out_len, "%.3f / SF%u / %.0fk", freq_mhz, sf, bw_khz);
        }
        else
        {
            std::snprintf(out, out_len, "Radio  %.3f MHz / SF%u / %.0fk", freq_mhz, sf, bw_khz);
        }
        return true;
    }

    if (node.channel != 0xFF)
    {
        if (compact)
        {
            std::snprintf(out, out_len, "CH %u", static_cast<unsigned>(node.channel));
        }
        else
        {
            std::snprintf(out, out_len, "Channel  %u", static_cast<unsigned>(node.channel));
        }
        return true;
    }

    return false;
}

bool build_route_line(const chat::contacts::NodeInfo& node, bool compact, char* out, size_t out_len)
{
    if (node.hops_away == 0xFF && node.next_hop == 0 && !node.via_mqtt)
    {
        return false;
    }

    if (node.via_mqtt && node.hops_away == 0xFF && node.next_hop == 0)
    {
        if (compact)
        {
            std::snprintf(out, out_len, "MQTT path");
        }
        else
        {
            std::snprintf(out, out_len, "Route  MQTT path");
        }
        return true;
    }

    if (compact)
    {
        if (node.via_mqtt && node.hops_away != 0xFF)
        {
            std::snprintf(out,
                          out_len,
                          "MQTT / %u hops",
                          static_cast<unsigned>(node.hops_away));
            return true;
        }
        if (node.hops_away != 0xFF && node.next_hop != 0)
        {
            std::snprintf(out,
                          out_len,
                          "%u hops / NH %02X",
                          static_cast<unsigned>(node.hops_away),
                          static_cast<unsigned>(node.next_hop));
            return true;
        }
        if (node.hops_away != 0xFF)
        {
            std::snprintf(out, out_len, "%u hops", static_cast<unsigned>(node.hops_away));
            return true;
        }
        if (node.via_mqtt)
        {
            std::snprintf(out, out_len, "MQTT / NH %02X", static_cast<unsigned>(node.next_hop));
            return true;
        }
        std::snprintf(out, out_len, "NH %02X", static_cast<unsigned>(node.next_hop));
        return true;
    }

    if (node.via_mqtt && node.hops_away != 0xFF)
    {
        std::snprintf(out,
                      out_len,
                      "Route  MQTT / %u hops",
                      static_cast<unsigned>(node.hops_away));
        return true;
    }
    if (node.hops_away != 0xFF && node.next_hop != 0)
    {
        std::snprintf(out,
                      out_len,
                      "Route  %u hops / NH %02X",
                      static_cast<unsigned>(node.hops_away),
                      static_cast<unsigned>(node.next_hop));
        return true;
    }
    if (node.hops_away != 0xFF)
    {
        std::snprintf(out, out_len, "Route  %u hops", static_cast<unsigned>(node.hops_away));
        return true;
    }

    if (node.via_mqtt)
    {
        std::snprintf(out, out_len, "Route  MQTT / NH %02X", static_cast<unsigned>(node.next_hop));
        return true;
    }

    std::snprintf(out, out_len, "Route  NH %02X", static_cast<unsigned>(node.next_hop));
    return true;
}

void build_status_line(const chat::contacts::NodeInfo& node, bool compact, char* out, size_t out_len)
{
    const char* state = node.is_ignored ? "Ignored" : "Active";
    const char* pki = node.key_manually_verified ? "Verified"
                                                 : (node.has_public_key ? "Known" : "");

    if (compact)
    {
        if (pki[0] != '\0')
        {
            std::snprintf(out, out_len, "%s / %s", state, pki);
        }
        else
        {
            std::snprintf(out, out_len, "%s", state);
        }
        return;
    }

    if (pki[0] != '\0')
    {
        std::snprintf(out, out_len, "State  %s / %s", state, pki);
    }
    else
    {
        std::snprintf(out, out_len, "State  %s", state);
    }
}

bool build_range_line(const chat::contacts::NodeInfo& node,
                      const GeoPoint& self_point,
                      bool compact,
                      char* out,
                      size_t out_len)
{
    const GeoPoint node_point = node_point_from_info(node);
    if (!node_point.valid || !self_point.valid)
    {
        return false;
    }

    const double meters = dashboard::haversine_m(
        node_point.lat, node_point.lon, self_point.lat, self_point.lon);
    char distance_buf[24];
    dashboard::format_distance(meters, distance_buf, sizeof(distance_buf));
    const float bearing = dashboard::bearing_between(
        self_point.lat, self_point.lon, node_point.lat, node_point.lon);

    if (compact)
    {
        std::snprintf(out, out_len, "%s / %s", distance_buf, dashboard::compass_rose(bearing));
    }
    else
    {
        std::snprintf(out, out_len, "Range  %s / %s", distance_buf, dashboard::compass_rose(bearing));
    }
    return true;
}

bool build_altitude_line(const chat::contacts::NodeInfo& node, bool compact, char* out, size_t out_len)
{
    if (!node.position.valid)
    {
        return false;
    }

    const double accuracy_m = compute_accuracy_m(node.position);
    if (!node.position.has_altitude && accuracy_m < 0.0)
    {
        return false;
    }

    if (node.position.has_altitude && accuracy_m >= 0.0)
    {
        if (compact)
        {
            std::snprintf(out,
                          out_len,
                          "Alt %ldm / %.0fm",
                          static_cast<long>(node.position.altitude),
                          accuracy_m);
        }
        else
        {
            std::snprintf(out,
                          out_len,
                          "Altitude  %ld m / +/- %.0f m",
                          static_cast<long>(node.position.altitude),
                          accuracy_m);
        }
        return true;
    }

    if (node.position.has_altitude)
    {
        if (compact)
        {
            std::snprintf(out, out_len, "Alt %ldm", static_cast<long>(node.position.altitude));
        }
        else
        {
            std::snprintf(out, out_len, "Altitude  %ld m", static_cast<long>(node.position.altitude));
        }
        return true;
    }

    if (compact)
    {
        std::snprintf(out, out_len, "Acc %.0fm", accuracy_m);
    }
    else
    {
        std::snprintf(out, out_len, "Accuracy  +/- %.0f m", accuracy_m);
    }
    return true;
}

void update_overlay_text()
{
    if (!s_state.has_node)
    {
        return;
    }

    const auto& node = s_state.node;
    const GeoPoint node_point = node_point_from_info(node);
    const ViewMetrics metrics = view_metrics();

    char id_buf[24];
    format_node_id(node.node_id, id_buf, sizeof(id_buf));
    set_label_text(s_widgets.id_label, id_buf);

    char coord_buf[40];
    if (node_point.valid)
    {
        format_coord_label("LON", node_point.lon, coord_buf, sizeof(coord_buf));
        set_label_text(s_widgets.lon_label, coord_buf);
        format_coord_label("LAT", node_point.lat, coord_buf, sizeof(coord_buf));
        set_label_text(s_widgets.lat_label, coord_buf);
    }
    else
    {
        set_label_text(s_widgets.lon_label, "LON --");
        set_label_text(s_widgets.lat_label, "LAT --");
    }

    set_top_bar_title(node);

    std::size_t line_count = 0;
    char line[128];

    build_protocol_line(node, metrics.compact, line, sizeof(line));
    append_info_line(line_count, line);

    build_signal_line(node, metrics.compact, line, sizeof(line));
    append_info_line(line_count, line);

    if (build_radio_line(node, metrics.compact, line, sizeof(line)))
    {
        append_info_line(line_count, line);
    }

    if (build_route_line(node, metrics.compact, line, sizeof(line)))
    {
        append_info_line(line_count, line);
    }

    build_status_line(node, metrics.compact, line, sizeof(line));
    append_info_line(line_count, line);

    if (build_range_line(node, s_state.self_point, metrics.compact, line, sizeof(line)))
    {
        append_info_line(line_count, line);
    }

    if (build_altitude_line(node, metrics.compact, line, sizeof(line)))
    {
        append_info_line(line_count, line);
    }

    format_age_short(node.last_seen, line, sizeof(line));
    append_info_line(line_count, line);

    hide_unused_info_lines(line_count);
}

void position_overlay_widgets()
{
    const ViewMetrics metrics = view_metrics();
    if (!s_widgets.map_stage)
    {
        return;
    }

    lv_obj_set_size(s_widgets.map_stage, metrics.width, metrics.height);
    if (s_widgets.tile_layer)
    {
        lv_obj_set_size(s_widgets.tile_layer, metrics.width, metrics.height);
    }

    const lv_coord_t left_scrim_w = clamp_coord(metrics.left_w + metrics.pad, 120, metrics.width);
    lv_obj_set_pos(s_widgets.left_scrim, 0, 0);
    lv_obj_set_size(s_widgets.left_scrim, left_scrim_w, metrics.height);

    const lv_coord_t right_scrim_x = clamp_coord(metrics.right_x - 18, 0, metrics.width);
    lv_obj_set_pos(s_widgets.right_scrim, right_scrim_x, 0);
    lv_obj_set_size(s_widgets.right_scrim, metrics.width - right_scrim_x, metrics.height);

    lv_obj_set_pos(s_widgets.id_label, metrics.pad, metrics.pad);
    lv_obj_set_size(s_widgets.id_label, metrics.left_w, LV_SIZE_CONTENT);

    lv_obj_set_pos(s_widgets.lon_label, metrics.pad, metrics.height - metrics.pad - 34);
    lv_obj_set_size(s_widgets.lon_label, metrics.left_w, LV_SIZE_CONTENT);

    lv_obj_set_pos(s_widgets.lat_label, metrics.pad, metrics.height - metrics.pad - 18);
    lv_obj_set_size(s_widgets.lat_label, metrics.left_w, LV_SIZE_CONTENT);

    for (std::size_t index = 0; index < kNodeInfoInfoLineCount; ++index)
    {
        lv_obj_set_pos(s_widgets.info_labels[index],
                       metrics.right_x,
                       metrics.info_top + static_cast<lv_coord_t>(index) *
                                              (metrics.info_line_h + metrics.info_gap));
        lv_obj_set_size(s_widgets.info_labels[index], metrics.right_col_w, metrics.info_line_h);
    }

    lv_obj_update_layout(s_widgets.no_position_label);
    const lv_coord_t no_pos_w = lv_obj_get_width(s_widgets.no_position_label);
    const lv_coord_t no_pos_h = lv_obj_get_height(s_widgets.no_position_label);
    lv_obj_set_pos(s_widgets.no_position_label,
                   (metrics.width - no_pos_w) / 2,
                   (metrics.height - no_pos_h) / 2);

    const lv_coord_t zoom_x = metrics.width - metrics.pad - metrics.zoom_size;
    const lv_coord_t zoom_out_y = metrics.height - metrics.pad - metrics.zoom_size;
    const lv_coord_t zoom_in_y = zoom_out_y - metrics.zoom_size - metrics.zoom_gap;
    lv_obj_set_size(s_widgets.zoom_in_btn, metrics.zoom_size, metrics.zoom_size);
    lv_obj_set_size(s_widgets.zoom_out_btn, metrics.zoom_size, metrics.zoom_size);
    lv_obj_set_pos(s_widgets.zoom_in_btn, zoom_x, zoom_in_y);
    lv_obj_set_pos(s_widgets.zoom_out_btn, zoom_x, zoom_out_y);
    lv_obj_center(s_widgets.zoom_in_label);
    lv_obj_center(s_widgets.zoom_out_label);
}

void position_circle_center(lv_obj_t* obj, lv_coord_t center_x, lv_coord_t center_y)
{
    if (!obj)
    {
        return;
    }
    lv_obj_set_pos(obj,
                   center_x - (lv_obj_get_width(obj) / 2),
                   center_y - (lv_obj_get_height(obj) / 2));
}

void render_map_tiles(const GeoPoint& node_point, const ViewMetrics& metrics)
{
    for (std::size_t index = 0; index < kNodeInfoTileCount; ++index)
    {
        set_hidden(s_widgets.tile_images[index], true);
    }

    if (!node_point.valid)
    {
        return;
    }

    double node_world_x = 0.0;
    double node_world_y = 0.0;
    latlng_to_world_pixels(node_point.lat, node_point.lon, s_state.zoom, node_world_x, node_world_y);

    const int center_tile_x = static_cast<int>(std::floor(node_world_x / kTileSize));
    const int center_tile_y = static_cast<int>(std::floor(node_world_y / kTileSize));
    const double base_x = static_cast<double>(metrics.focus_x) - node_world_x;
    const double base_y = static_cast<double>(metrics.focus_y) - node_world_y;
    const int max_tile_y = (1 << s_state.zoom) - 1;
    const uint8_t map_source = sanitize_map_source(app::configFacade().getConfig().map_source);

    std::size_t tile_index = 0;
    for (int dy = -1; dy <= 1; ++dy)
    {
        for (int dx = -1; dx <= 1; ++dx)
        {
            if (tile_index >= kNodeInfoTileCount)
            {
                return;
            }

            const int tile_y = center_tile_y + dy;
            if (tile_y < 0 || tile_y > max_tile_y)
            {
                ++tile_index;
                continue;
            }

            const int draw_tile_x = center_tile_x + dx;
            const int tile_x = normalize_tile_x(draw_tile_x, s_state.zoom);
            const lv_coord_t draw_x =
                static_cast<lv_coord_t>(std::lround(base_x + static_cast<double>(draw_tile_x * kTileSize)));
            const lv_coord_t draw_y =
                static_cast<lv_coord_t>(std::lround(base_y + static_cast<double>(tile_y * kTileSize)));

            char path[96];
            if (!build_base_tile_path(s_state.zoom, tile_x, tile_y, map_source, path, sizeof(path)) ||
                !tile_exists(path))
            {
                ++tile_index;
                continue;
            }

            std::strncpy(s_state.tile_paths[tile_index], path, sizeof(s_state.tile_paths[tile_index]) - 1);
            s_state.tile_paths[tile_index][sizeof(s_state.tile_paths[tile_index]) - 1] = '\0';

            lv_obj_t* image = s_widgets.tile_images[tile_index];
            lv_image_set_src(image, s_state.tile_paths[tile_index]);
            lv_obj_set_size(image, kTileSize, kTileSize);
            lv_obj_set_pos(image, draw_x, draw_y);
            set_hidden(image, false);
            ++tile_index;
        }
    }
}

void clear_map_tiles()
{
    for (std::size_t index = 0; index < kNodeInfoTileCount; ++index)
    {
        if (s_widgets.tile_images[index])
        {
            set_hidden(s_widgets.tile_images[index], true);
        }
        s_state.tile_paths[index][0] = '\0';
    }
}

void render_connection_and_markers(const GeoPoint& node_point, const ViewMetrics& metrics)
{
    if (!node_point.valid)
    {
        set_hidden(s_widgets.connection_line, true);
        set_hidden(s_widgets.marker_node_ring, true);
        set_hidden(s_widgets.marker_node_dot, true);
        set_hidden(s_widgets.marker_self_ring, true);
        set_hidden(s_widgets.marker_self_dot, true);
        set_hidden(s_widgets.distance_label, true);
        return;
    }

    position_circle_center(s_widgets.marker_node_ring, metrics.focus_x, metrics.focus_y);
    position_circle_center(s_widgets.marker_node_dot, metrics.focus_x, metrics.focus_y);
    set_hidden(s_widgets.marker_node_ring, false);
    set_hidden(s_widgets.marker_node_dot, false);

    if (!s_state.self_point.valid)
    {
        set_hidden(s_widgets.connection_line, true);
        set_hidden(s_widgets.marker_self_ring, true);
        set_hidden(s_widgets.marker_self_dot, true);
        set_hidden(s_widgets.distance_label, true);
        return;
    }

    double node_world_x = 0.0;
    double node_world_y = 0.0;
    double self_world_x = 0.0;
    double self_world_y = 0.0;
    latlng_to_world_pixels(node_point.lat, node_point.lon, s_state.zoom, node_world_x, node_world_y);
    latlng_to_world_pixels(s_state.self_point.lat, s_state.self_point.lon, s_state.zoom, self_world_x, self_world_y);

    const double raw_x = static_cast<double>(metrics.focus_x) + (self_world_x - node_world_x);
    const double raw_y = static_cast<double>(metrics.focus_y) + (self_world_y - node_world_y);
    const lv_coord_t draw_x = static_cast<lv_coord_t>(std::lround(
        clamp_double(raw_x, static_cast<double>(metrics.pad + 6), static_cast<double>(metrics.right_x - 16))));
    const lv_coord_t draw_y = static_cast<lv_coord_t>(std::lround(
        clamp_double(raw_y, static_cast<double>(metrics.pad + 6), static_cast<double>(metrics.height - metrics.pad - 6))));

    s_state.link_points[0].x = static_cast<float>(metrics.focus_x);
    s_state.link_points[0].y = static_cast<float>(metrics.focus_y);
    s_state.link_points[1].x = static_cast<float>(draw_x);
    s_state.link_points[1].y = static_cast<float>(draw_y);
    lv_line_set_points(s_widgets.connection_line, s_state.link_points, 2);
    lv_obj_set_pos(s_widgets.connection_line, 0, 0);
    set_hidden(s_widgets.connection_line, false);

    position_circle_center(s_widgets.marker_self_ring, draw_x, draw_y);
    position_circle_center(s_widgets.marker_self_dot, draw_x, draw_y);
    set_hidden(s_widgets.marker_self_ring, false);
    set_hidden(s_widgets.marker_self_dot, false);

    char distance_buf[24];
    dashboard::format_distance(
        dashboard::haversine_m(node_point.lat, node_point.lon, s_state.self_point.lat, s_state.self_point.lon),
        distance_buf,
        sizeof(distance_buf));
    const float bearing = dashboard::bearing_between(
        s_state.self_point.lat, s_state.self_point.lon, node_point.lat, node_point.lon);
    char distance_text[40];
    std::snprintf(distance_text,
                  sizeof(distance_text),
                  "%s / %s",
                  distance_buf,
                  dashboard::compass_rose(bearing));
    set_label_text(s_widgets.distance_label, distance_text);
    lv_obj_update_layout(s_widgets.distance_label);

    lv_coord_t label_x = static_cast<lv_coord_t>((metrics.focus_x + draw_x) / 2);
    lv_coord_t label_y = static_cast<lv_coord_t>((metrics.focus_y + draw_y) / 2) - 14;
    if (std::abs(draw_x - metrics.focus_x) < 22 && std::abs(draw_y - metrics.focus_y) < 22)
    {
        label_y -= 18;
    }

    const lv_coord_t label_w = lv_obj_get_width(s_widgets.distance_label);
    const lv_coord_t label_h = lv_obj_get_height(s_widgets.distance_label);
    label_x = clamp_coord(label_x - (label_w / 2), metrics.pad + 2, metrics.right_x - label_w - 10);
    label_y = clamp_coord(label_y - (label_h / 2), metrics.pad + 2, metrics.height - label_h - metrics.pad);
    lv_obj_set_pos(s_widgets.distance_label, label_x, label_y);
    set_hidden(s_widgets.distance_label, false);
}

void update_zoom_button_state(bool enabled)
{
    if (!s_widgets.zoom_in_btn || !s_widgets.zoom_out_btn)
    {
        return;
    }

    if (enabled)
    {
        lv_obj_clear_state(s_widgets.zoom_in_btn, LV_STATE_DISABLED);
        lv_obj_clear_state(s_widgets.zoom_out_btn, LV_STATE_DISABLED);
    }
    else
    {
        lv_obj_add_state(s_widgets.zoom_in_btn, LV_STATE_DISABLED);
        lv_obj_add_state(s_widgets.zoom_out_btn, LV_STATE_DISABLED);
    }
}

void render_scene()
{
    position_overlay_widgets();
    update_overlay_text();

    if (!s_state.has_node)
    {
        set_hidden(s_widgets.no_position_label, false);
        update_zoom_button_state(false);
        return;
    }

    const GeoPoint node_point = node_point_from_info(s_state.node);
    const ViewMetrics metrics = view_metrics();
    if (s_state.map_ready)
    {
        render_map_tiles(node_point, metrics);
    }
    else
    {
        clear_map_tiles();
    }
    render_connection_and_markers(node_point, metrics);

    const bool has_position = node_point.valid;
    set_hidden(s_widgets.no_position_label, has_position);
    if (!has_position)
    {
        set_label_text(s_widgets.no_position_label, ::ui::i18n::tr("No position available"));
        set_hidden(s_widgets.connection_line, true);
        set_hidden(s_widgets.marker_node_ring, true);
        set_hidden(s_widgets.marker_node_dot, true);
        set_hidden(s_widgets.marker_self_ring, true);
        set_hidden(s_widgets.marker_self_dot, true);
        set_hidden(s_widgets.distance_label, true);
    }

    update_zoom_button_state(has_position);
}

void render_map_async(void* /*user_data*/)
{
    if (!s_widgets.root || !lv_obj_is_valid(s_widgets.root) || !s_state.has_node)
    {
        return;
    }

    lv_obj_update_layout(s_widgets.root);
    s_state.zoom = compute_initial_zoom(node_point_from_info(s_state.node), s_state.self_point, view_metrics());
    s_state.map_ready = true;
    render_scene();
}

void on_zoom_button_clicked(lv_event_t* e)
{
    if (!s_state.has_node)
    {
        return;
    }

    const intptr_t delta = reinterpret_cast<intptr_t>(lv_event_get_user_data(e));
    const GeoPoint node_point = node_point_from_info(s_state.node);
    if (!node_point.valid)
    {
        return;
    }

    const int next_zoom = select_zoom_after_delta(node_point, s_state.zoom, static_cast<int>(delta));
    if (next_zoom == s_state.zoom)
    {
        return;
    }

    s_state.zoom = next_zoom;
    render_scene();
}

} // namespace

NodeInfoWidgets create(lv_obj_t* parent)
{
    s_widgets = NodeInfoWidgets{};
    s_state = NodeInfoRuntimeState{};
    s_state.zoom = kDefaultZoom;

    s_widgets.root = layout::create_root(parent);
    s_widgets.header = layout::create_header(s_widgets.root);
    s_widgets.content = layout::create_content(s_widgets.root);

    lv_obj_set_style_bg_color(s_widgets.root, kColorBackdrop, 0);
    lv_obj_set_style_bg_opa(s_widgets.root, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_widgets.content, kColorBackdropAlt, 0);
    lv_obj_set_style_bg_opa(s_widgets.content, LV_OPA_COVER, 0);
    make_plain(s_widgets.root);
    make_plain(s_widgets.content);

    ::ui::widgets::TopBarConfig cfg;
    ::ui::widgets::top_bar_init(s_top_bar, s_widgets.header, cfg);
    s_widgets.back_btn = s_top_bar.back_btn;
    s_widgets.title_label = s_top_bar.title_label;
    s_widgets.battery_label = s_top_bar.right_label;
    if (s_widgets.back_btn)
    {
        s_widgets.back_label = lv_obj_get_child(s_widgets.back_btn, 0);
    }
    ::ui::widgets::top_bar_set_title(s_top_bar, ::ui::i18n::tr("NODE INFO"));
    ui_update_top_bar_battery(s_top_bar);
    apply_top_bar_style();

    s_widgets.map_stage = lv_obj_create(s_widgets.content);
    lv_obj_set_size(s_widgets.map_stage, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(s_widgets.map_stage, 0, 0);
    lv_obj_set_style_bg_color(s_widgets.map_stage, kColorBackdropAlt, 0);
    lv_obj_set_style_bg_opa(s_widgets.map_stage, LV_OPA_COVER, 0);
    make_plain(s_widgets.map_stage);
#ifdef LV_OBJ_FLAG_CLIP_CHILDREN
    lv_obj_add_flag(s_widgets.map_stage, LV_OBJ_FLAG_CLIP_CHILDREN);
#endif

    s_widgets.tile_layer = lv_obj_create(s_widgets.map_stage);
    lv_obj_set_size(s_widgets.tile_layer, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(s_widgets.tile_layer, 0, 0);
    lv_obj_set_style_bg_opa(s_widgets.tile_layer, LV_OPA_TRANSP, 0);
    make_plain(s_widgets.tile_layer);

    for (std::size_t index = 0; index < kNodeInfoTileCount; ++index)
    {
        s_widgets.tile_images[index] = lv_image_create(s_widgets.tile_layer);
        lv_obj_set_size(s_widgets.tile_images[index], kTileSize, kTileSize);
        lv_obj_set_style_border_width(s_widgets.tile_images[index], 0, 0);
        lv_obj_set_style_pad_all(s_widgets.tile_images[index], 0, 0);
        set_hidden(s_widgets.tile_images[index], true);
    }

    s_widgets.left_scrim = lv_obj_create(s_widgets.map_stage);
    s_widgets.right_scrim = lv_obj_create(s_widgets.map_stage);
    apply_scrim_style(s_widgets.left_scrim, lv_color_hex(0x071A27), 132);
    apply_scrim_style(s_widgets.right_scrim, lv_color_hex(0x25150D), 150);

    s_widgets.connection_line = lv_line_create(s_widgets.map_stage);
    lv_obj_set_style_line_color(s_widgets.connection_line, kColorLink, 0);
    lv_obj_set_style_line_width(s_widgets.connection_line, 2, 0);
    lv_obj_set_style_line_rounded(s_widgets.connection_line, true, 0);
    set_hidden(s_widgets.connection_line, true);

    s_widgets.marker_node_ring = lv_obj_create(s_widgets.map_stage);
    s_widgets.marker_node_dot = lv_obj_create(s_widgets.map_stage);
    s_widgets.marker_self_ring = lv_obj_create(s_widgets.map_stage);
    s_widgets.marker_self_dot = lv_obj_create(s_widgets.map_stage);
    apply_marker_style(s_widgets.marker_node_ring, 22, kColorNodeMarker, false);
    apply_marker_style(s_widgets.marker_node_dot, 10, kColorNodeMarker, true);
    apply_marker_style(s_widgets.marker_self_ring, 18, kColorSelfMarker, false);
    apply_marker_style(s_widgets.marker_self_dot, 8, kColorSelfMarker, true);
    set_hidden(s_widgets.marker_node_ring, true);
    set_hidden(s_widgets.marker_node_dot, true);
    set_hidden(s_widgets.marker_self_ring, true);
    set_hidden(s_widgets.marker_self_dot, true);

    s_widgets.id_label = create_label(s_widgets.map_stage, "ID !000000", font_montserrat_18_safe(), kColorId);
    lv_label_set_long_mode(s_widgets.id_label, LV_LABEL_LONG_DOT);

    s_widgets.lon_label = create_label(s_widgets.map_stage, "LON --", font_montserrat_14_safe(), kColorLon);
    s_widgets.lat_label = create_label(s_widgets.map_stage, "LAT --", font_montserrat_14_safe(), kColorLat);

    s_widgets.no_position_label =
        create_label(s_widgets.map_stage, "No position available", font_montserrat_16_safe(), kColorMuted);
    set_hidden(s_widgets.no_position_label, true);

    s_widgets.distance_label =
        create_label(s_widgets.map_stage, "", font_montserrat_12_safe(), kColorDistance);
    set_hidden(s_widgets.distance_label, true);

    for (std::size_t index = 0; index < kNodeInfoInfoLineCount; ++index)
    {
        s_widgets.info_labels[index] = create_label(
            s_widgets.map_stage,
            "",
            font_montserrat_12_safe(),
            kInfoLineColors[index]);
        lv_label_set_long_mode(s_widgets.info_labels[index], LV_LABEL_LONG_DOT);
        set_hidden(s_widgets.info_labels[index], true);
    }

    s_widgets.zoom_in_btn = lv_btn_create(s_widgets.map_stage);
    s_widgets.zoom_in_label = lv_label_create(s_widgets.zoom_in_btn);
    lv_label_set_text(s_widgets.zoom_in_label, "+");
    s_widgets.zoom_out_btn = lv_btn_create(s_widgets.map_stage);
    s_widgets.zoom_out_label = lv_label_create(s_widgets.zoom_out_btn);
    lv_label_set_text(s_widgets.zoom_out_label, "-");
    apply_zoom_button_style(s_widgets.zoom_in_btn, s_widgets.zoom_in_label);
    apply_zoom_button_style(s_widgets.zoom_out_btn, s_widgets.zoom_out_label);
    lv_obj_add_event_cb(s_widgets.zoom_in_btn,
                        on_zoom_button_clicked,
                        LV_EVENT_CLICKED,
                        reinterpret_cast<void*>(static_cast<intptr_t>(+1)));
    lv_obj_add_event_cb(s_widgets.zoom_out_btn,
                        on_zoom_button_clicked,
                        LV_EVENT_CLICKED,
                        reinterpret_cast<void*>(static_cast<intptr_t>(-1)));
    update_zoom_button_state(false);

    lv_obj_update_layout(s_widgets.root);
    position_overlay_widgets();

    return s_widgets;
}

void destroy()
{
    if (s_widgets.root && lv_obj_is_valid(s_widgets.root))
    {
        lv_obj_del(s_widgets.root);
    }

    s_widgets = NodeInfoWidgets{};
    s_state = NodeInfoRuntimeState{};
    s_top_bar = ::ui::widgets::TopBar{};
}

const NodeInfoWidgets& widgets()
{
    return s_widgets;
}

void set_node_info(const chat::contacts::NodeInfo& node)
{
    s_state.node = node;
    s_state.has_node = true;
    s_state.self_point = resolve_self_position();
    s_state.map_ready = false;
    s_state.zoom = kDefaultZoom;
    render_scene();
    lv_async_call(render_map_async, nullptr);
}

} // namespace ui
} // namespace node_info
