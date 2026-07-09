#include "ui/widgets/route_image_strip.h"

#include "ui/support/lvgl_fs_utils.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>

namespace ui::widgets::route_image_strip
{
namespace
{

struct SlotMetrics
{
    lv_coord_t width = 0;
    lv_coord_t height = 0;
    lv_coord_t center_y = 0;
    lv_opa_t opacity = LV_OPA_COVER;
};

bool valid_obj(lv_obj_t* obj)
{
    return obj && lv_obj_is_valid(obj);
}

lv_coord_t panel_width(const Widget& widget)
{
    return std::max<lv_coord_t>(80, std::min<lv_coord_t>(200, widget.config.width));
}

lv_coord_t content_width(const Widget& widget)
{
    return std::max<lv_coord_t>(60, panel_width(widget) - (widget.config.inset * 2));
}

lv_coord_t center_item_height(const Widget& widget)
{
    return std::max<lv_coord_t>(54, widget.config.item_height);
}

lv_coord_t side_item_height(const Widget& widget)
{
    return std::max<lv_coord_t>(24, (center_item_height(widget) * 72) / 100);
}

lv_coord_t outer_item_height(const Widget& widget)
{
    return std::max<lv_coord_t>(18, (center_item_height(widget) * 55) / 100);
}

lv_coord_t thumbnail_width_for_slot(const SlotMetrics& metrics)
{
    return std::max<lv_coord_t>(48, metrics.width);
}

lv_coord_t thumbnail_height_for_slot(const SlotMetrics& metrics)
{
    return std::max<lv_coord_t>(36, metrics.height);
}

Widget* widget_from_event(lv_event_t* e)
{
    return e ? static_cast<Widget*>(lv_event_get_user_data(e)) : nullptr;
}

std::size_t clamp_index(const Widget& widget, std::size_t index)
{
    if (widget.items.empty())
    {
        return 0;
    }
    return std::min<std::size_t>(index, widget.items.size() - 1);
}

bool selected_image_saved(const Widget& widget)
{
    return widget.selected_index < widget.image_sources.size() &&
           !widget.image_sources[widget.selected_index].empty();
}

void close_fullscreen(Widget& widget)
{
    if (valid_obj(widget.fullscreen_root))
    {
        lv_obj_del(widget.fullscreen_root);
    }
    widget.fullscreen_root = nullptr;
    widget.fullscreen_source.clear();
    widget.fullscreen_visible = false;
}

void fullscreen_key_cb(lv_event_t* e)
{
    Widget* widget = widget_from_event(e);
    if (!widget)
    {
        return;
    }
    const uint32_t key = lv_event_get_key(e);
    if (key == LV_KEY_ENTER || key == LV_KEY_ESC || key == LV_KEY_BACKSPACE)
    {
        close_fullscreen(*widget);
        lv_event_stop_bubbling(e);
        lv_event_stop_processing(e);
    }
}

void open_fullscreen(Widget& widget)
{
    if (widget.items.empty())
    {
        return;
    }

    close_fullscreen(widget);
    lv_obj_t* parent = lv_layer_top();
    if (!parent)
    {
        parent = lv_screen_active();
    }
    if (!parent)
    {
        return;
    }

    widget.fullscreen_root = lv_obj_create(parent);
    widget.fullscreen_visible = true;
    lv_obj_set_size(widget.fullscreen_root, LV_PCT(100), LV_PCT(100));
    lv_obj_align(widget.fullscreen_root, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(widget.fullscreen_root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(widget.fullscreen_root, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_set_style_bg_color(widget.fullscreen_root, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(widget.fullscreen_root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(widget.fullscreen_root, 0, 0);
    lv_obj_set_style_radius(widget.fullscreen_root, 0, 0);
    lv_obj_set_style_pad_all(widget.fullscreen_root, 3, 0);
    lv_obj_clear_flag(widget.fullscreen_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(widget.fullscreen_root, fullscreen_key_cb, LV_EVENT_KEY, &widget);

    char title_text[24]{};
    std::snprintf(title_text,
                  sizeof(title_text),
                  "%u/%u",
                  static_cast<unsigned>(widget.selected_index + 1),
                  static_cast<unsigned>(widget.items.size()));
    lv_obj_t* title = lv_label_create(widget.fullscreen_root);
    lv_label_set_text(title, title_text);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFF3DF), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 4, 2);

    if (selected_image_saved(widget))
    {
        widget.fullscreen_source = widget.image_sources[widget.selected_index];
        lv_obj_t* image = lv_image_create(widget.fullscreen_root);
        lv_obj_set_size(image, LV_PCT(100), LV_PCT(100));
        lv_image_set_inner_align(image, LV_IMAGE_ALIGN_CONTAIN);
        lv_image_set_src(image, widget.fullscreen_source.c_str());
        lv_obj_center(image);
    }
    else
    {
        lv_obj_t* label = lv_label_create(widget.fullscreen_root);
        lv_label_set_text(label, "Image not saved");
        lv_obj_set_width(label, LV_PCT(100));
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(0xFFF3DF), 0);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(label);
    }

    lv_obj_move_foreground(title);
    lv_obj_move_foreground(widget.fullscreen_root);
}

void apply_button_style(lv_obj_t* button,
                        bool selected,
                        lv_opa_t opacity)
{
    if (!valid_obj(button))
    {
        return;
    }
    lv_obj_set_style_radius(button, selected ? 5 : 4, 0);
    lv_obj_set_style_bg_color(button,
                              selected ? lv_color_hex(0x17130F)
                                       : lv_color_hex(0x1C1812),
                              0);
    lv_obj_set_style_bg_opa(button, opacity, 0);
    lv_obj_set_style_border_width(button, selected ? 2 : 1, 0);
    lv_obj_set_style_border_color(button,
                                  selected ? lv_color_hex(0xF2A13A)
                                           : lv_color_hex(0x8A6E43),
                                  0);
    lv_obj_set_style_pad_all(button, 0, 0);
    lv_obj_set_style_pad_row(button, 0, 0);
    lv_obj_set_style_text_color(button,
                                selected ? lv_color_hex(0xFFF3DF)
                                         : lv_color_hex(0xFFF3DF),
                                0);
}

void add_caption(lv_obj_t* button,
                 std::size_t index,
                 std::size_t total,
                 bool downloaded,
                 bool selected)
{
    char text[20]{};
    if (selected)
    {
        std::snprintf(text,
                      sizeof(text),
                      "%u/%u%s",
                      static_cast<unsigned>(index + 1),
                      static_cast<unsigned>(total),
                      downloaded ? "" : " off");
    }
    else
    {
        std::snprintf(text,
                      sizeof(text),
                      "%02u%s",
                      static_cast<unsigned>(index + 1),
                      downloaded ? "" : " off");
    }
    lv_obj_t* caption = lv_label_create(button);
    lv_obj_add_flag(caption, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_size(caption, selected ? 46 : 32, 13);
    lv_obj_set_style_bg_color(caption, lv_color_hex(0x17130F), 0);
    lv_obj_set_style_bg_opa(caption, LV_OPA_70, 0);
    lv_obj_set_style_radius(caption, 3, 0);
    lv_obj_set_style_pad_all(caption, 1, 0);
    lv_label_set_long_mode(caption, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(caption, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(caption, lv_color_hex(0xFFF3DF), 0);
    lv_obj_set_style_text_align(caption, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(caption, text);
    lv_obj_align(caption, LV_ALIGN_BOTTOM_MID, 0, -2);
}

void add_placeholder(lv_obj_t* button,
                     const SlotMetrics& metrics,
                     std::size_t index)
{
    lv_obj_t* placeholder = lv_obj_create(button);
    lv_obj_add_flag(placeholder, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_size(placeholder,
                    thumbnail_width_for_slot(metrics),
                    thumbnail_height_for_slot(metrics));
    lv_obj_set_pos(placeholder, 0, 0);
    lv_obj_set_style_bg_color(placeholder, lv_color_hex(0x3A2B1E), 0);
    lv_obj_set_style_bg_opa(placeholder, LV_OPA_70, 0);
    lv_obj_set_style_border_width(placeholder, 1, 0);
    lv_obj_set_style_border_color(placeholder, lv_color_hex(0x8A6E43), 0);
    lv_obj_set_style_radius(placeholder, 3, 0);
    lv_obj_set_style_pad_all(placeholder, 0, 0);
    lv_obj_clear_flag(placeholder, LV_OBJ_FLAG_SCROLLABLE);

    char text[8]{};
    std::snprintf(text, sizeof(text), "%u", static_cast<unsigned>(index + 1));
    lv_obj_t* label = lv_label_create(placeholder);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFF3DF), 0);
    lv_obj_center(label);
}

void add_thumbnail(lv_obj_t* button,
                   const Widget& widget,
                   const SlotMetrics& metrics,
                   std::size_t index)
{
    if (index >= widget.image_sources.size() || widget.image_sources[index].empty())
    {
        add_placeholder(button, metrics, index);
        return;
    }

    lv_obj_t* image = lv_image_create(button);
    lv_obj_add_flag(image, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_size(image,
                    thumbnail_width_for_slot(metrics),
                    thumbnail_height_for_slot(metrics));
    lv_image_set_src(image, widget.image_sources[index].c_str());
    lv_image_set_inner_align(image, LV_IMAGE_ALIGN_COVER);
    lv_image_set_antialias(image, false);
    lv_obj_set_pos(image, 0, 0);
    lv_obj_clear_flag(image, LV_OBJ_FLAG_SCROLLABLE);
}

SlotMetrics slot_metrics_for_offset(const Widget& widget,
                                    lv_coord_t viewport_height,
                                    int offset)
{
    const lv_coord_t full_width = content_width(widget);
    const lv_coord_t center_h = center_item_height(widget);
    const lv_coord_t side_h = side_item_height(widget);
    const lv_coord_t outer_h = outer_item_height(widget);
    const lv_coord_t center_y = std::max<lv_coord_t>(center_h / 2 + 1, viewport_height / 2);
    const lv_coord_t step_one = center_h / 2 + side_h / 2 + 4;
    const lv_coord_t step_two = step_one + side_h / 2 + outer_h / 2 + 3;

    SlotMetrics metrics{};
    if (offset == 0)
    {
        metrics.width = full_width;
        metrics.height =
            std::min<lv_coord_t>(center_h, std::max<lv_coord_t>(36, viewport_height - 2));
        metrics.center_y = std::max<lv_coord_t>(metrics.height / 2 + 1, viewport_height / 2);
        metrics.opacity = LV_OPA_90;
        return metrics;
    }

    const int abs_offset = offset < 0 ? -offset : offset;
    if (abs_offset == 1)
    {
        metrics.width = std::max<lv_coord_t>(32, (full_width * 78) / 100);
        metrics.height = side_h;
        metrics.center_y = center_y + (offset < 0 ? -step_one : step_one);
        metrics.opacity = LV_OPA_60;
        return metrics;
    }

    metrics.width = std::max<lv_coord_t>(28, (full_width * 62) / 100);
    metrics.height = outer_h;
    metrics.center_y = center_y + (offset < 0 ? -step_two : step_two);
    metrics.opacity = LV_OPA_40;
    return metrics;
}

void render_items(Widget& widget);
void select_item(Widget& widget,
                 std::size_t index,
                 bool notify,
                 bool defer_render = false);

void render_items_async(void* user_data)
{
    auto* widget = static_cast<Widget*>(user_data);
    if (!widget || !valid_obj(widget->root))
    {
        return;
    }
    render_items(*widget);
}

void request_render_items(Widget& widget)
{
    if (!valid_obj(widget.root))
    {
        return;
    }
    (void)lv_async_call(render_items_async, &widget);
}

void item_clicked_cb(lv_event_t* e)
{
    Widget* widget = widget_from_event(e);
    lv_obj_t* target = e ? static_cast<lv_obj_t*>(lv_event_get_target(e)) : nullptr;
    if (!widget || !valid_obj(target))
    {
        return;
    }
    const auto raw =
        static_cast<std::size_t>(reinterpret_cast<uintptr_t>(lv_obj_get_user_data(target)));
    if (raw == 0)
    {
        return;
    }
    select_item(*widget, raw - 1, true, true);
}

void panel_key_cb(lv_event_t* e)
{
    Widget* widget = widget_from_event(e);
    if (!widget)
    {
        return;
    }
    const uint32_t key = lv_event_get_key(e);
    if (handle_key(*widget, key))
    {
        lv_event_stop_bubbling(e);
        lv_event_stop_processing(e);
    }
}

void render_slot(Widget& widget,
                 std::size_t index,
                 int offset,
                 lv_coord_t viewport_height)
{
    if (!valid_obj(widget.list))
    {
        return;
    }

    const SlotMetrics metrics = slot_metrics_for_offset(widget, viewport_height, offset);
    const lv_coord_t x = std::max<lv_coord_t>(0, (content_width(widget) - metrics.width) / 2);
    const lv_coord_t y = metrics.center_y - (metrics.height / 2);
    if (y >= viewport_height || y + metrics.height <= 0)
    {
        return;
    }

    const bool selected = offset == 0;
    lv_obj_t* button = lv_btn_create(widget.list);
    lv_obj_add_flag(button, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_size(button, metrics.width, metrics.height);
    lv_obj_set_pos(button, x, y);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_user_data(button, reinterpret_cast<void*>(index + 1));
    lv_obj_add_event_cb(button, item_clicked_cb, LV_EVENT_CLICKED, &widget);
    apply_button_style(button, selected, metrics.opacity);
    add_thumbnail(button, widget, metrics, index);
    add_caption(button,
                index,
                widget.items.size(),
                index < widget.items.size() ? widget.items[index].downloaded : false,
                selected);
    widget.item_buttons.push_back(button);
}

void render_items(Widget& widget)
{
    if (!valid_obj(widget.list))
    {
        return;
    }

    lv_obj_clean(widget.list);
    widget.item_buttons.clear();
    if (widget.items.empty())
    {
        lv_obj_t* label = lv_label_create(widget.list);
        lv_obj_set_width(label, LV_PCT(100));
        lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(0xFFF3DF), 0);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(label, "No images");
        lv_obj_center(label);
        return;
    }

    widget.selected_index = clamp_index(widget, widget.selected_index);
    lv_obj_update_layout(widget.root);
    const lv_coord_t content_height = lv_obj_get_content_height(widget.list);
    const lv_coord_t viewport_height =
        content_height > 0 ? content_height : center_item_height(widget);

    render_slot(widget, widget.selected_index, 0, viewport_height);
}

void select_item(Widget& widget,
                 std::size_t index,
                 bool notify,
                 bool defer_render)
{
    if (widget.items.empty())
    {
        widget.selected_index = 0;
        if (defer_render)
        {
            request_render_items(widget);
        }
        else
        {
            render_items(widget);
        }
        return;
    }
    widget.selected_index = clamp_index(widget, index);
    if (defer_render)
    {
        request_render_items(widget);
    }
    else
    {
        render_items(widget);
    }
    if (notify && widget.on_select)
    {
        widget.on_select(widget.selected_index, widget.user_data);
    }
}

} // namespace

void reset(Widget& widget)
{
    widget.root = nullptr;
    widget.list = nullptr;
    widget.fullscreen_root = nullptr;
    widget.config = Config{};
    widget.items.clear();
    widget.image_sources.clear();
    widget.item_buttons.clear();
    widget.fullscreen_source.clear();
    widget.selected_index = 0;
    widget.visible = false;
    widget.fullscreen_visible = false;
    widget.on_select = nullptr;
    widget.user_data = nullptr;
}

void destroy(Widget& widget)
{
    close_fullscreen(widget);
    if (valid_obj(widget.root))
    {
        lv_obj_del(widget.root);
    }
    widget.root = nullptr;
    widget.list = nullptr;
    widget.item_buttons.clear();
    widget.visible = false;
}

void create(lv_obj_t* parent, Widget& widget, const Config& config)
{
    if (!valid_obj(parent))
    {
        destroy(widget);
        return;
    }

    widget.config = config;
    const lv_coord_t width = panel_width(widget);
    if (valid_obj(widget.root) && valid_obj(widget.list))
    {
        lv_obj_set_size(widget.root, width, LV_PCT(100));
        lv_obj_set_style_bg_opa(widget.root, widget.config.opacity, 0);
        return;
    }

    widget.root = lv_obj_create(parent);
    lv_obj_set_size(widget.root, width, LV_PCT(100));
    lv_obj_align(widget.root, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_add_flag(widget.root, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_add_flag(widget.root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(widget.root, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_add_flag(widget.root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_user_data(widget.root, &widget);
    lv_obj_set_style_bg_color(widget.root, lv_color_hex(0x17130F), 0);
    lv_obj_set_style_bg_opa(widget.root, widget.config.opacity, 0);
    lv_obj_set_style_border_width(widget.root, 1, 0);
    lv_obj_set_style_border_color(widget.root, lv_color_hex(0xFFF3DF), 0);
    lv_obj_set_style_radius(widget.root, 0, 0);
    lv_obj_set_style_pad_all(widget.root, widget.config.inset, 0);
    lv_obj_clear_flag(widget.root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(widget.root, panel_key_cb, LV_EVENT_KEY, &widget);

    widget.list = lv_obj_create(widget.root);
    lv_obj_set_size(widget.list, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(widget.list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(widget.list, 0, 0);
    lv_obj_set_style_radius(widget.list, 0, 0);
    lv_obj_set_style_pad_all(widget.list, 0, 0);
    lv_obj_clear_flag(widget.list, LV_OBJ_FLAG_SCROLLABLE);
}

void set_selection_callback(Widget& widget, SelectionCallback callback, void* user_data)
{
    widget.on_select = callback;
    widget.user_data = user_data;
}

void set_items(Widget& widget, const Item* items, std::size_t item_count)
{
    widget.items.clear();
    widget.image_sources.clear();
    widget.item_buttons.clear();
    if (items && item_count > 0)
    {
        widget.items.assign(items, items + item_count);
    }
    widget.selected_index = clamp_index(widget, widget.selected_index);

    widget.image_sources.reserve(widget.items.size());
    for (const auto& item : widget.items)
    {
        if (item.downloaded && !item.local_path.empty())
        {
            widget.image_sources.push_back(::ui::fs::normalize_path(item.local_path.c_str()));
        }
        else
        {
            widget.image_sources.emplace_back();
        }
    }
    if (is_visible(widget))
    {
        render_items(widget);
    }
}

void set_selected(Widget& widget, std::size_t index, bool notify)
{
    const std::size_t next = clamp_index(widget, index);
    if (!notify && next == widget.selected_index)
    {
        return;
    }
    select_item(widget, index, notify);
}

void set_hidden(Widget& widget, bool hidden)
{
    if (!valid_obj(widget.root))
    {
        widget.visible = false;
        close_fullscreen(widget);
        return;
    }

    const bool next_visible = !hidden;
    if (next_visible == widget.visible)
    {
        if (next_visible)
        {
            lv_obj_move_foreground(widget.root);
        }
        else
        {
            close_fullscreen(widget);
        }
        return;
    }

    widget.visible = next_visible;
    if (hidden)
    {
        close_fullscreen(widget);
        lv_obj_add_flag(widget.root, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_obj_clear_flag(widget.root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_x(widget.root, 0);
    render_items(widget);
    lv_obj_move_foreground(widget.root);
}

bool is_visible(const Widget& widget)
{
    return widget.visible && valid_obj(widget.root);
}

bool handle_key(Widget& widget, uint32_t key)
{
    if (widget.fullscreen_visible)
    {
        if (key == LV_KEY_ENTER || key == LV_KEY_ESC || key == LV_KEY_BACKSPACE)
        {
            close_fullscreen(widget);
        }
        return true;
    }

    if (!is_visible(widget) || widget.items.empty())
    {
        return false;
    }

    if (key == LV_KEY_ENTER)
    {
        open_fullscreen(widget);
        return true;
    }
    if (key == LV_KEY_UP || key == 'w' || key == 'W')
    {
        const std::size_t next = widget.selected_index == 0
                                     ? 0
                                     : widget.selected_index - 1;
        select_item(widget, next, true);
        return true;
    }
    if (key == LV_KEY_DOWN || key == 's' || key == 'S')
    {
        const std::size_t next =
            std::min<std::size_t>(widget.items.size() - 1, widget.selected_index + 1);
        select_item(widget, next, true);
        return true;
    }
    return false;
}

void refresh(Widget& widget)
{
    render_items(widget);
    if (is_visible(widget))
    {
        lv_obj_move_foreground(widget.root);
    }
}

} // namespace ui::widgets::route_image_strip
