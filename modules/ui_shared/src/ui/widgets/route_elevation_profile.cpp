#include "ui/widgets/route_elevation_profile.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace ui::widgets::route_elevation_profile
{
namespace
{

bool valid_obj(lv_obj_t* obj)
{
    return obj && lv_obj_is_valid(obj);
}

bool valid_widget(const Widget& widget)
{
    return valid_obj(widget.panel) && valid_obj(widget.label) && valid_obj(widget.line);
}

void set_label_text(lv_obj_t* label, const char* text)
{
    if (!valid_obj(label))
    {
        return;
    }
    lv_label_set_text(label, text ? text : "");
}

const Sample* altitude_sample_at_ordinal(const Sample* samples,
                                         std::size_t sample_count,
                                         std::size_t target_ordinal)
{
    std::size_t ordinal = 0;
    for (std::size_t index = 0; index < sample_count; ++index)
    {
        const auto& sample = samples[index];
        if (!sample.has_altitude || !std::isfinite(sample.altitude_m))
        {
            continue;
        }
        if (ordinal == target_ordinal)
        {
            return &sample;
        }
        ++ordinal;
    }
    return nullptr;
}

} // namespace

void reset(Widget& widget)
{
    widget.panel = nullptr;
    widget.label = nullptr;
    widget.line = nullptr;
    widget.points.clear();
}

void create(lv_obj_t* parent, Widget& widget, const Config& config)
{
    if (!valid_obj(parent))
    {
        reset(widget);
        return;
    }
    if (valid_widget(widget))
    {
        return;
    }

    widget.panel = lv_obj_create(parent);
    lv_obj_set_size(widget.panel, config.min_width, config.height);
    lv_obj_add_flag(widget.panel, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_add_flag(widget.panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(widget.panel, lv_color_hex(0x25170D), 0);
    lv_obj_set_style_bg_opa(widget.panel, LV_OPA_80, 0);
    lv_obj_set_style_border_width(widget.panel, 1, 0);
    lv_obj_set_style_border_color(widget.panel, lv_color_hex(0xF8E6C3), 0);
    lv_obj_set_style_radius(widget.panel, 4, 0);
    lv_obj_set_style_pad_all(widget.panel, 0, 0);
    lv_obj_clear_flag(widget.panel, LV_OBJ_FLAG_SCROLLABLE);

    widget.label = lv_label_create(widget.panel);
    lv_label_set_text(widget.label, "Elev --");
    lv_obj_set_pos(widget.label, 5, 3);
    lv_obj_set_width(widget.label, std::max<lv_coord_t>(1, config.min_width - 10));
    lv_obj_set_style_text_font(widget.label, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(widget.label, lv_color_hex(0xFFF3DF), 0);
    lv_label_set_long_mode(widget.label, LV_LABEL_LONG_DOT);

    widget.line = lv_line_create(widget.panel);
    lv_obj_set_pos(widget.line, 0, 0);
    lv_obj_set_style_line_color(widget.line, lv_color_hex(0x7FD8BE), 0);
    lv_obj_set_style_line_width(widget.line, 2, 0);
    lv_obj_set_style_line_rounded(widget.line, true, 0);
}

void set_hidden(Widget& widget, bool hidden)
{
    if (!valid_obj(widget.panel))
    {
        return;
    }
    if (hidden)
    {
        lv_obj_add_flag(widget.panel, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_clear_flag(widget.panel, LV_OBJ_FLAG_HIDDEN);
    }
}

bool update(Widget& widget,
            const Config& config,
            const Sample* samples,
            std::size_t sample_count,
            const Metrics& metrics,
            bool visible,
            lv_coord_t bottom_inset)
{
    if (!valid_widget(widget))
    {
        return false;
    }

    if (!visible || !samples || sample_count < 2 || metrics.altitude_count < 2)
    {
        set_hidden(widget, true);
        return false;
    }

    std::size_t altitude_count = 0;
    for (std::size_t index = 0; index < sample_count; ++index)
    {
        if (samples[index].has_altitude && std::isfinite(samples[index].altitude_m))
        {
            ++altitude_count;
        }
    }
    if (altitude_count < 2)
    {
        set_hidden(widget, true);
        return false;
    }

    lv_obj_t* parent = lv_obj_get_parent(widget.panel);
    if (!valid_obj(parent))
    {
        set_hidden(widget, true);
        return false;
    }

    lv_obj_update_layout(parent);
    const lv_coord_t viewport_width = lv_obj_get_content_width(parent);
    lv_coord_t panel_width = config.min_width;
    if (viewport_width > config.inset * 2)
    {
        panel_width = viewport_width - (config.inset * 2);
    }
    panel_width = std::max<lv_coord_t>(80, panel_width);

    lv_obj_set_size(widget.panel, panel_width, config.height);
    lv_obj_align(widget.panel, LV_ALIGN_BOTTOM_MID, 0, -bottom_inset);

    char label[48]{};
    std::snprintf(label,
                  sizeof(label),
                  "Elev %.0f-%.0fm +%.0f/-%.0f",
                  metrics.min_altitude_m,
                  metrics.max_altitude_m,
                  metrics.ascent_m,
                  metrics.descent_m);
    set_label_text(widget.label, label);
    lv_obj_set_width(widget.label, std::max<lv_coord_t>(1, panel_width - 10));

    const lv_coord_t plot_width = std::max<lv_coord_t>(
        1, panel_width - (config.plot_pad * 2));
    const lv_coord_t plot_height = std::max<lv_coord_t>(
        1, config.height - config.plot_top - config.plot_pad);
    const double range = std::max(1.0, metrics.max_altitude_m - metrics.min_altitude_m);

    const std::size_t draw_count =
        std::max<std::size_t>(2, std::min<std::size_t>(config.max_points, altitude_count));
    widget.points.clear();
    widget.points.reserve(draw_count);
    const double total_distance =
        std::max(1.0, std::isfinite(metrics.distance_m) ? metrics.distance_m : 0.0);
    for (std::size_t index = 0; index < draw_count; ++index)
    {
        const std::size_t src_ordinal =
            draw_count <= 1 ? 0 : (index * (altitude_count - 1)) / (draw_count - 1);
        const Sample* sample = altitude_sample_at_ordinal(samples, sample_count, src_ordinal);
        if (!sample)
        {
            continue;
        }
        double x_ratio = sample->distance_m / total_distance;
        if (!std::isfinite(x_ratio))
        {
            x_ratio = 0.0;
        }
        x_ratio = std::max(0.0, std::min(1.0, x_ratio));
        const double y_ratio = (metrics.max_altitude_m - sample->altitude_m) / range;
        lv_point_precise_t point{};
        point.x = static_cast<float>(config.plot_pad + std::lround(x_ratio * plot_width));
        point.y = static_cast<float>(config.plot_top + std::lround(y_ratio * plot_height));
        widget.points.push_back(point);
    }

    if (widget.points.size() < 2)
    {
        set_hidden(widget, true);
        return false;
    }

    lv_line_set_points(widget.line,
                       widget.points.data(),
                       static_cast<std::uint16_t>(widget.points.size()));
    set_hidden(widget, false);
    lv_obj_move_foreground(widget.panel);
    return true;
}

} // namespace ui::widgets::route_elevation_profile
