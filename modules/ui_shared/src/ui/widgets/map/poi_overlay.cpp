#include "ui/widgets/map/poi_overlay.h"
#include "ui/assets/fonts/font_utils.h"
#include <algorithm>
#include <cstring>

namespace ui::widgets::map
{
namespace
{
constexpr int kMarker = 18;
const char* symbol(const char* category)
{
    if (std::strcmp(category, "water") == 0) return "W";
    if (std::strcmp(category, "camp") == 0) return "C";
    if (std::strcmp(category, "shelter") == 0) return "H";
    if (std::strcmp(category, "peak") == 0) return "^";
    if (std::strcmp(category, "viewpoint") == 0) return "V";
    if (std::strcmp(category, "parking") == 0) return "P";
    if (std::strcmp(category, "toilet") == 0) return "T";
    if (std::strcmp(category, "emergency") == 0) return "+";
    if (std::strcmp(category, "trailhead") == 0) return "h";
    return "i";
}
lv_color_t color(const char* category)
{
    if (std::strcmp(category, "water") == 0) return lv_color_hex(0x246BB2);
    if (std::strcmp(category, "emergency") == 0) return lv_color_hex(0xBC3B35);
    if (std::strcmp(category, "camp") == 0 || std::strcmp(category, "shelter") == 0) return lv_color_hex(0x347544);
    return lv_color_hex(0x705337);
}
void draw_background(lv_layer_t* layer, const lv_area_t& area, lv_color_t fill, bool marker)
{
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = fill;
    dsc.bg_opa = LV_OPA_COVER;
    dsc.radius = marker ? LV_RADIUS_CIRCLE : 2;
    dsc.border_width = marker ? 1 : 0;
    dsc.border_color = lv_color_hex(0xFFFFFF);
    dsc.border_opa = LV_OPA_COVER;
    lv_draw_rect(layer, &dsc, &area);
}
void draw_text(lv_layer_t* layer, const lv_area_t& area, const char* text, const lv_font_t* font, bool marker)
{
    lv_draw_label_dsc_t dsc;
    lv_draw_label_dsc_init(&dsc);
    dsc.font = font;
    dsc.text = text;
    dsc.align = marker ? LV_TEXT_ALIGN_CENTER : LV_TEXT_ALIGN_LEFT;
    dsc.color = lv_color_hex(marker ? 0xFFFFFF : 0x3A2A1A);
    dsc.flag = LV_TEXT_FLAG_EXPAND;
    // LVGL owns a short transient copy if a viewport refresh releases the PSRAM snapshot.
    dsc.text_local = !marker;
    dsc.text_static = marker;
    lv_draw_label(layer, &dsc, &area);
}
} // namespace

PoiOverlay::~PoiOverlay()
{
    if (layer_) lv_obj_del(layer_);
}
void PoiOverlay::on_delete(lv_event_t* event)
{
    auto* self = static_cast<PoiOverlay*>(lv_event_get_user_data(event));
    self->layer_ = nullptr;
    self->snapshot_ = nullptr;
}
void PoiOverlay::create(lv_obj_t* parent)
{
    if (layer_) lv_obj_del(layer_);
    layer_ = lv_obj_create(parent);
    lv_obj_remove_style_all(layer_);
    lv_obj_set_size(layer_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(layer_, 0, 0);
    lv_obj_clear_flag(layer_, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_add_event_cb(layer_, on_delete, LV_EVENT_DELETE, this);
    lv_obj_add_event_cb(layer_, on_draw, LV_EVENT_DRAW_MAIN, this);
    clear();
}
bool PoiOverlay::overlaps(const Rect& a, const Rect& b)
{
    return a.x < b.x + b.w && a.x + a.w > b.x && a.y < b.y + b.h && a.y + a.h > b.y;
}
unsigned PoiOverlay::side_for(std::size_t index) const
{
    return index < 32 ? static_cast<unsigned>((sides_low_ >> (index * 2)) & 3U)
                      : static_cast<unsigned>((sides_high_ >> ((index - 32) * 2)) & 3U);
}
bool PoiOverlay::label_rect(const ui::map::MapPoiItem& item, unsigned side, int width, Rect& out) const
{
    if (!font_ || item.label.empty()) return false;
    lv_point_t size{};
    lv_text_get_size(&size, item.label.c_str(), font_, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    // Long names never enlarge a marker or create a scrolling/wrapping widget.
    if (size.x + 2 > std::min(128, width / 2) || size.y > font_->line_height) return false;
    out = {item.x + 12, item.y - size.y / 2, size.x + 2, size.y};
    if (side == 1) out.x = item.x - 12 - out.w;
    if (side == 2)
    {
        out.x = item.x - out.w / 2;
        out.y = item.y - 12 - out.h;
    }
    if (side == 3)
    {
        out.x = item.x - out.w / 2;
        out.y = item.y + 12;
    }
    return true;
}
bool PoiOverlay::label_fits(const Rect& rect, const ui::map::MapPoiSnapshot& snapshot, std::size_t count, int width, int height) const
{
    if (rect.x < 0 || rect.y < 0 || rect.x + rect.w > width || rect.y + rect.h > height) return false;
    for (std::size_t i = 0; i < count; ++i)
    {
        const auto& item = snapshot.items[i];
        if (overlaps(rect, {item.x - kMarker / 2, item.y - kMarker / 2, kMarker, kMarker})) return false;
        Rect previous{};
        if ((label_mask_ & (uint64_t{1} << i)) && label_rect(item, side_for(i), width, previous) && overlaps(rect, previous)) return false;
    }
    return true;
}
void PoiOverlay::update(const ui::map::MapPoiSnapshot& snapshot)
{
    if (!layer_) return;
    clear();
    lv_obj_set_pos(layer_, 0, 0);
    if (!snapshot.enabled || !snapshot.items || snapshot.item_count == 0) return;
    snapshot_ = &snapshot;
    const auto count = std::min(std::min(snapshot.item_count, snapshot.capacity), ui::map::MapPoiSnapshot::kMaxItems);
    font_ = ui::fonts::ui_chrome_font();
    if (snapshot.labels)
    {
        // Resolve the shared content font during preparation, never from DRAW_MAIN.
        for (std::size_t i = 0; i < count; ++i)
            if (!snapshot.items[i].label.empty()) font_ = ui::fonts::content_font(snapshot.items[i].label.c_str(), ui::fonts::ui_chrome_font());
        const int width = lv_obj_get_width(layer_), height = lv_obj_get_height(layer_);
        std::size_t placed = 0;
        for (std::size_t i = 0; i < count && placed < kLabels; ++i)
        {
            for (unsigned side = 0; side < 4; ++side)
            {
                Rect rect{};
                if (!label_rect(snapshot.items[i], side, width, rect) || !label_fits(rect, snapshot, count, width, height)) continue;
                label_mask_ |= uint64_t{1} << i;
                if (i < 32) sides_low_ |= uint64_t{side} << (i * 2);
                else sides_high_ |= uint32_t{side} << ((i - 32) * 2);
                ++placed;
                break;
            }
        }
    }
    lv_obj_clear_flag(layer_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_invalidate(layer_);
}
void PoiOverlay::on_draw(lv_event_t* event)
{
    const auto* self = static_cast<const PoiOverlay*>(lv_event_get_user_data(event));
    if (!self->snapshot_ || !self->snapshot_->items) return;
    const auto& snapshot = *self->snapshot_;
    const auto count = std::min(std::min(snapshot.item_count, snapshot.capacity), ui::map::MapPoiSnapshot::kMaxItems);
    auto* layer = lv_event_get_layer(event);
    lv_area_t origin{};
    lv_obj_get_coords(self->layer_, &origin);
    const auto* marker_font = &lv_font_montserrat_14;
    for (std::size_t i = 0; i < count; ++i)
    {
        const auto& item = snapshot.items[i];
        lv_area_t area{origin.x1 + item.x - kMarker / 2, origin.y1 + item.y - kMarker / 2,
                       origin.x1 + item.x + kMarker / 2 - 1, origin.y1 + item.y + kMarker / 2 - 1};
        draw_background(layer, area, color(item.category.c_str()), true);
        area.y1 += (kMarker - marker_font->line_height) / 2;
        draw_text(layer, area, symbol(item.category.c_str()), marker_font, true);
        if (!(self->label_mask_ & (uint64_t{1} << i))) continue;
        Rect rect{};
        if (!self->label_rect(item, self->side_for(i), lv_obj_get_width(self->layer_), rect)) continue;
        area = {origin.x1 + rect.x, origin.y1 + rect.y, origin.x1 + rect.x + rect.w - 1, origin.y1 + rect.y + rect.h - 1};
        draw_background(layer, area, lv_color_hex(0xFFF7E9), false);
        draw_text(layer, area, item.label.c_str(), self->font_, false);
    }
}
void PoiOverlay::clear()
{
    snapshot_ = nullptr;
    label_mask_ = sides_low_ = 0;
    sides_high_ = 0;
    if (layer_) lv_obj_add_flag(layer_, LV_OBJ_FLAG_HIDDEN);
}
void PoiOverlay::translate(int dx, int dy)
{
    if (layer_) lv_obj_set_pos(layer_, lv_obj_get_x(layer_) + dx, lv_obj_get_y(layer_) + dy);
}
} // namespace ui::widgets::map
