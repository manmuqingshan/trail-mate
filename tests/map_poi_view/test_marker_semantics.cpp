#include "ui/widgets/map/poi_overlay.h"
#include <cassert>
#include <vector>

int main()
{
    lv_init();
    auto* display = lv_display_create(100, 60);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    std::vector<uint16_t> pixels(6000);
    lv_display_set_buffers(display, pixels.data(), nullptr, pixels.size() * 2, LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display, [](lv_display_t* d, const lv_area_t*, uint8_t*)
                            { lv_display_flush_ready(d); });
    auto* root = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, 100, 60);
    ui::widgets::map::PoiOverlay view;
    view.create(root);
    lv_obj_update_layout(root);
    ui::map::MapPoiItem item;
    item.marker = true;
    item.x = 30;
    item.y = 30;
    ui::copyText(item.category, "water");
    ui::map::MapPoiSnapshot snapshot;
    snapshot.enabled = snapshot.layout_ready = true;
    snapshot.items = &item;
    snapshot.item_count = snapshot.capacity = 1;
    view.update(snapshot);
    lv_refr_now(display);
    auto single = pixels;
    item.cluster_count = 3;
    view.update(snapshot);
    lv_refr_now(display);
    assert(pixels != single);
    auto three = pixels;
    item.cluster_count = 5;
    view.update(snapshot);
    lv_refr_now(display);
    assert(pixels != three);
    assert(lv_obj_get_child_count(lv_obj_get_child(root, -1)) == 0);
    view.clear();
    lv_obj_delete(root);
    lv_display_delete(display);
    lv_deinit();
}
