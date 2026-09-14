#pragma once
#include "lvgl.h"
#include "ui_presentation/map/map_poi_snapshot.h"

namespace ui::widgets::map
{
// Pure LVGL view: accepts a prepared snapshot, owns no filesystem or parser.
class PoiOverlay
{
  public:
    PoiOverlay() = default;
    ~PoiOverlay();
    PoiOverlay(const PoiOverlay&) = delete;
    PoiOverlay& operator=(const PoiOverlay&) = delete;
    void create(lv_obj_t* parent);
    void update(const ui::map::MapPoiSnapshot& snapshot);
    void clear();
    void translate(int dx, int dy);

  private:
    static constexpr std::size_t kLabels = 20;
    struct Rect
    {
        int x, y, w, h;
    };
    lv_obj_t* layer_ = nullptr;
    const ui::map::MapPoiSnapshot* snapshot_ = nullptr; // Viewport-owned PSRAM data.
    const lv_font_t* font_ = nullptr;
    uint64_t label_mask_ = 0;
    uint64_t sides_low_ = 0;
    uint32_t sides_high_ = 0;
    static void on_delete(lv_event_t* event);
    static void on_draw(lv_event_t* event);
    static bool overlaps(const Rect& a, const Rect& b);
    bool label_fits(const Rect& rect, const ui::map::MapPoiSnapshot& snapshot, std::size_t count, int width, int height) const;
    bool label_rect(const ui::map::MapPoiItem& item, unsigned side, int width, Rect& out) const;
    unsigned side_for(std::size_t index) const;
};
static_assert(sizeof(PoiOverlay) <= 64, "POI view must not retain widget pools or bulk buffers");
} // namespace ui::widgets::map
