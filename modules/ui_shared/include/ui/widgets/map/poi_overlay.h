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
    void prepare_text(const char* text);
    uint64_t font_signature() const;
    static bool measure_text(void* view, const char* text, std::size_t bytes, bool ellipsis, int16_t& width, int16_t& height);

  private:
    lv_obj_t* layer_ = nullptr;
    const ui::map::MapPoiSnapshot* snapshot_ = nullptr; // Viewport-owned PSRAM data.
    const lv_font_t* font_ = nullptr;
    static void on_delete(lv_event_t* event);
    static void on_draw(lv_event_t* event);
};
static_assert(sizeof(PoiOverlay) <= 64, "POI view must not retain widget pools or bulk buffers");
} // namespace ui::widgets::map
