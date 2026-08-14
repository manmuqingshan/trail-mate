#include "screen_app_internal.h"

#include "platform/ui/gps_runtime.h"

#include <cmath>
#include <cstdio>

namespace ui::mono::screens::screen_240x320::detail
{
namespace
{

constexpr lv_coord_t kSkyTop = kHeaderRuleY + 2;
constexpr lv_coord_t kSkyHeight = kActionTop - kSkyTop - 2;
constexpr lv_coord_t kSkyCenterX = kScreenWidth / 2;
constexpr lv_coord_t kSkyCenterY = 91;
constexpr lv_coord_t kSkyOuterRadius = 82;
constexpr size_t kSkyMaxSats = ::gps::kMaxGnssSats;

struct SkyPlotPageState
{
    lv_obj_t* stage = nullptr;
    lv_obj_t* ring_outer = nullptr;
    lv_obj_t* ring_mid = nullptr;
    lv_obj_t* ring_inner = nullptr;
    lv_obj_t* axis_ns = nullptr;
    lv_obj_t* axis_ew = nullptr;
    lv_obj_t* center = nullptr;
    lv_obj_t* status = nullptr;
    lv_obj_t* empty = nullptr;
    lv_obj_t* satellite_dot[kSkyMaxSats]{};
    lv_obj_t* satellite_label[kSkyMaxSats]{};
    lv_point_precise_t axis_ns_points[2]{};
    lv_point_precise_t axis_ew_points[2]{};
    ::gps::GnssSatInfo satellites[kSkyMaxSats]{};
    ::gps::GnssStatus gnss_status{};
    lv_timer_t* refresh_timer = nullptr;
    size_t satellite_count = 0;
};

SkyPlotPageState s_sky_plot;

bool sky_stage_active()
{
    return valid(s_sky_plot.stage) && s_state.adapter != nullptr &&
           s_state.adapter->page_kind() == PageKind::SkyPlot;
}

void style_sky_shape(lv_obj_t* object, bool filled)
{
    lv_obj_set_style_bg_color(object, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(object, filled ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_color(object, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_width(object, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(object, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(object, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(object, 0, LV_PART_MAIN);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
}

void style_sky_line(lv_obj_t* object)
{
    lv_obj_set_style_line_color(object, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_line_width(object, 1, LV_PART_MAIN);
    lv_obj_set_style_line_rounded(object, false, LV_PART_MAIN);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
}

const char* constellation_prefix(::gps::GnssSystem system)
{
    switch (system)
    {
    case ::gps::GnssSystem::GPS:
        return "G";
    case ::gps::GnssSystem::GLN:
        return "R";
    case ::gps::GnssSystem::GAL:
        return "E";
    case ::gps::GnssSystem::BD:
        return "C";
    case ::gps::GnssSystem::UNKNOWN:
    default:
        return "?";
    }
}

const char* fix_label(::gps::GnssFix fix)
{
    switch (fix)
    {
    case ::gps::GnssFix::FIX3D:
        return "3D FIX";
    case ::gps::GnssFix::FIX2D:
        return "2D FIX";
    case ::gps::GnssFix::NOFIX:
    default:
        return "NO FIX";
    }
}

void set_satellite_visible(size_t index, bool visible)
{
    if (index >= kSkyMaxSats)
    {
        return;
    }
    if (valid(s_sky_plot.satellite_dot[index]))
    {
        if (visible)
            lv_obj_clear_flag(s_sky_plot.satellite_dot[index], LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(s_sky_plot.satellite_dot[index], LV_OBJ_FLAG_HIDDEN);
    }
    if (valid(s_sky_plot.satellite_label[index]))
    {
        if (visible)
            lv_obj_clear_flag(s_sky_plot.satellite_label[index], LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(s_sky_plot.satellite_label[index], LV_OBJ_FLAG_HIDDEN);
    }
}

void render_satellites()
{
    if (!sky_stage_active())
    {
        return;
    }

    for (size_t index = 0; index < kSkyMaxSats; ++index)
    {
        if (index >= s_sky_plot.satellite_count)
        {
            set_satellite_visible(index, false);
            continue;
        }

        const ::gps::GnssSatInfo& satellite = s_sky_plot.satellites[index];
        const float elevation = satellite.elevation > 90U ? 90.0F : static_cast<float>(satellite.elevation);
        const float radius = static_cast<float>(kSkyOuterRadius) * (1.0F - (elevation / 90.0F));
        constexpr float kPi = 3.14159265358979323846F;
        const float angle = static_cast<float>(satellite.azimuth % 360U) * kPi / 180.0F;
        const lv_coord_t x = static_cast<lv_coord_t>(
            std::lround(static_cast<float>(kSkyCenterX) + std::sin(angle) * radius));
        const lv_coord_t y = static_cast<lv_coord_t>(
            std::lround(static_cast<float>(kSkyCenterY) - std::cos(angle) * radius));
        const lv_coord_t dot_size = satellite.snr >= 30 ? 10 : satellite.snr >= 15 ? 8
                                                                                   : 6;

        lv_obj_t* const dot = s_sky_plot.satellite_dot[index];
        lv_obj_t* const label = s_sky_plot.satellite_label[index];
        if (!valid(dot) || !valid(label))
        {
            continue;
        }
        lv_obj_set_size(dot, dot_size, dot_size);
        lv_obj_set_pos(dot, x - (dot_size / 2), y - (dot_size / 2));
        lv_obj_set_style_bg_color(dot, satellite.used ? lv_color_black() : lv_color_white(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(dot, 1, LV_PART_MAIN);

        char text[12] = {};
        std::snprintf(text,
                      sizeof(text),
                      "%s%u",
                      constellation_prefix(satellite.sys),
                      static_cast<unsigned>(satellite.id));
        set_text(label, text);
        lv_obj_set_pos(label, x + (dot_size / 2) + 1, y - 7);
        set_satellite_visible(index, true);
    }

    if (valid(s_sky_plot.empty))
    {
        if (s_sky_plot.satellite_count == 0)
            lv_obj_clear_flag(s_sky_plot.empty, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(s_sky_plot.empty, LV_OBJ_FLAG_HIDDEN);
    }
    if (valid(s_sky_plot.status))
    {
        char status[72] = {};
        std::snprintf(status,
                      sizeof(status),
                      "%s  %u/%u SAT  HDOP %.1f",
                      fix_label(s_sky_plot.gnss_status.fix),
                      static_cast<unsigned>(s_sky_plot.gnss_status.sats_in_use),
                      static_cast<unsigned>(s_sky_plot.gnss_status.sats_in_view),
                      static_cast<double>(s_sky_plot.gnss_status.hdop));
        set_text(s_sky_plot.status, status);
    }
}

void refresh_sky_data()
{
    if (!sky_stage_active())
    {
        return;
    }
    s_sky_plot.satellite_count = 0;
    s_sky_plot.gnss_status = {};
    (void)::platform::ui::gps::get_gnss_snapshot(s_sky_plot.satellites,
                                                 kSkyMaxSats,
                                                 &s_sky_plot.satellite_count,
                                                 &s_sky_plot.gnss_status);
    render_satellites();
}

void on_sky_refresh(lv_timer_t*)
{
    refresh_sky_data();
}

void create_sky_stage()
{
    if (sky_stage_active())
    {
        return;
    }

    s_sky_plot.stage = lv_obj_create(s_state.root);
    lv_obj_set_pos(s_sky_plot.stage, 0, kSkyTop);
    lv_obj_set_size(s_sky_plot.stage, kScreenWidth, kSkyHeight);
    style_paper(s_sky_plot.stage);

    s_sky_plot.ring_outer = lv_obj_create(s_sky_plot.stage);
    lv_obj_set_pos(s_sky_plot.ring_outer, kSkyCenterX - kSkyOuterRadius, kSkyCenterY - kSkyOuterRadius);
    lv_obj_set_size(s_sky_plot.ring_outer, kSkyOuterRadius * 2, kSkyOuterRadius * 2);
    style_sky_shape(s_sky_plot.ring_outer, false);

    s_sky_plot.ring_mid = lv_obj_create(s_sky_plot.stage);
    lv_obj_set_pos(s_sky_plot.ring_mid, kSkyCenterX - 55, kSkyCenterY - 55);
    lv_obj_set_size(s_sky_plot.ring_mid, 110, 110);
    style_sky_shape(s_sky_plot.ring_mid, false);

    s_sky_plot.ring_inner = lv_obj_create(s_sky_plot.stage);
    lv_obj_set_pos(s_sky_plot.ring_inner, kSkyCenterX - 27, kSkyCenterY - 27);
    lv_obj_set_size(s_sky_plot.ring_inner, 54, 54);
    style_sky_shape(s_sky_plot.ring_inner, false);

    s_sky_plot.axis_ns_points[0] = {static_cast<lv_value_precise_t>(kSkyCenterX),
                                    static_cast<lv_value_precise_t>(kSkyCenterY - kSkyOuterRadius)};
    s_sky_plot.axis_ns_points[1] = {static_cast<lv_value_precise_t>(kSkyCenterX),
                                    static_cast<lv_value_precise_t>(kSkyCenterY + kSkyOuterRadius)};
    s_sky_plot.axis_ns = lv_line_create(s_sky_plot.stage);
    lv_line_set_points(s_sky_plot.axis_ns, s_sky_plot.axis_ns_points, 2);
    style_sky_line(s_sky_plot.axis_ns);

    s_sky_plot.axis_ew_points[0] = {static_cast<lv_value_precise_t>(kSkyCenterX - kSkyOuterRadius),
                                    static_cast<lv_value_precise_t>(kSkyCenterY)};
    s_sky_plot.axis_ew_points[1] = {static_cast<lv_value_precise_t>(kSkyCenterX + kSkyOuterRadius),
                                    static_cast<lv_value_precise_t>(kSkyCenterY)};
    s_sky_plot.axis_ew = lv_line_create(s_sky_plot.stage);
    lv_line_set_points(s_sky_plot.axis_ew, s_sky_plot.axis_ew_points, 2);
    style_sky_line(s_sky_plot.axis_ew);

    s_sky_plot.center = lv_obj_create(s_sky_plot.stage);
    lv_obj_set_pos(s_sky_plot.center, kSkyCenterX - 3, kSkyCenterY - 3);
    lv_obj_set_size(s_sky_plot.center, 6, 6);
    style_sky_shape(s_sky_plot.center, true);

    const struct
    {
        const char* text;
        lv_coord_t x;
        lv_coord_t y;
    } directions[] = {
        {"N", kSkyCenterX - 3, kSkyCenterY - kSkyOuterRadius - 12},
        {"E", kSkyCenterX + kSkyOuterRadius + 4, kSkyCenterY - 7},
        {"S", kSkyCenterX - 3, kSkyCenterY + kSkyOuterRadius + 2},
        {"W", kSkyCenterX - kSkyOuterRadius - 11, kSkyCenterY - 7},
    };
    for (const auto& direction : directions)
    {
        lv_obj_t* label = create_text(s_sky_plot.stage, 12, LV_TEXT_ALIGN_CENTER);
        lv_obj_set_pos(label, direction.x, direction.y);
        set_text(label, direction.text);
    }

    for (size_t index = 0; index < kSkyMaxSats; ++index)
    {
        s_sky_plot.satellite_dot[index] = lv_obj_create(s_sky_plot.stage);
        lv_obj_set_size(s_sky_plot.satellite_dot[index], 6, 6);
        style_sky_shape(s_sky_plot.satellite_dot[index], false);
        lv_obj_add_flag(s_sky_plot.satellite_dot[index], LV_OBJ_FLAG_HIDDEN);

        s_sky_plot.satellite_label[index] = create_text(s_sky_plot.stage, 26);
        lv_obj_set_style_text_font(s_sky_plot.satellite_label[index], LV_FONT_DEFAULT, LV_PART_MAIN);
        lv_obj_add_flag(s_sky_plot.satellite_label[index], LV_OBJ_FLAG_HIDDEN);
    }

    s_sky_plot.empty = create_text(s_sky_plot.stage, kContentWidth, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_pos(s_sky_plot.empty, kMargin, kSkyCenterY - 7);
    set_text(s_sky_plot.empty, "WAITING FOR SATELLITES");

    s_sky_plot.status = create_text(s_sky_plot.stage, kContentWidth, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_pos(s_sky_plot.status, kMargin, kSkyHeight - 17);

    s_sky_plot.refresh_timer = lv_timer_create(on_sky_refresh, 1000, nullptr);
}

} // namespace

void reset_sky_plot_page_state()
{
    destroy_sky_plot_page();
    s_sky_plot = SkyPlotPageState{};
}

void destroy_sky_plot_page()
{
    if (s_sky_plot.refresh_timer)
    {
        lv_timer_del(s_sky_plot.refresh_timer);
        s_sky_plot.refresh_timer = nullptr;
    }
    if (valid(s_sky_plot.stage))
    {
        lv_obj_del(s_sky_plot.stage);
    }
    s_sky_plot.stage = nullptr;
    s_sky_plot.ring_outer = nullptr;
    s_sky_plot.ring_mid = nullptr;
    s_sky_plot.ring_inner = nullptr;
    s_sky_plot.axis_ns = nullptr;
    s_sky_plot.axis_ew = nullptr;
    s_sky_plot.center = nullptr;
    s_sky_plot.status = nullptr;
    s_sky_plot.empty = nullptr;
    s_sky_plot.satellite_count = 0;
}

void render_sky_plot()
{
    set_text(s_state.title, "SKY PLOT");
    set_text(s_state.subtitle, "GNSS");
    clear_lines_from(0);
    create_sky_stage();
    refresh_sky_data();
}

void add_sky_plot_actions()
{
    add_action("GPS ON/OFF", Action::ToggleGps, kMargin, kActionTop, 118);
}

bool handle_sky_plot_action(Action action)
{
    switch (action)
    {
    case Action::ToggleGps:
    {
        const bool enabled = app::configFacade().readConfig().gps_enabled;
        ::ui::settings::SettingsPatchView patch{};
        ::ui::copyText(patch.key, "gps_enabled");
        ::ui::copyText(patch.value, enabled ? "OFF" : "ON");
        set_notice(::ui::presentation_sources::runtime_settings_action_sink().applySetting(patch).ok
                       ? (enabled ? "GPS DISABLED" : "GPS ENABLED")
                       : "GPS CHANGE REJECTED");
        return true;
    }

    default:
        return false;
    }
}

} // namespace ui::mono::screens::screen_240x320::detail
