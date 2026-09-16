#include "ui/widgets/map/poi_overlay.h"
#include "ui_map_runtime/map_poi/annotation_frame.h"
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <vector>

int main()
{
    lv_init();
    auto* display = lv_display_create(480, 222);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    std::vector<uint16_t> pixels(480 * 222);
    lv_display_set_buffers(display, pixels.data(), nullptr, pixels.size() * 2, LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display, [](lv_display_t* d, const lv_area_t*, uint8_t*)
                            { lv_display_flush_ready(d); });
    auto* root = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, 480, 222);
    lv_obj_set_style_bg_color(root, lv_color_hex(0x00FF00), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    ui::widgets::map::PoiOverlay view;
    view.create(root);
    lv_obj_update_layout(root);
    ui::map_poi::AnnotationFrame frame(
        [](std::size_t size, void*)
        { return std::malloc(size); },
        [](void* data, void*)
        { std::free(data); });
    ui::map::MapPoiSnapshot metadata;
    metadata.enabled = metadata.header.valid = true;
    metadata.width = 480;
    metadata.height = 222;
    metadata.view_key = 1;
    metadata.compatibility_key = 1;
    ui::map_poi::AnnotationCandidate road;
    road.key = road.feature_key = 1;
    road.kind = ui::map::AnnotationKind::Road;
    road.name = "People Road";
    road.category = "residential";
    road.priority = 175;
    road.x = -1000;
    road.y = 80;
    road.path_points = 2;
    road.path[0] = -32;
    road.path[1] = 80;
    road.path[2] = 288;
    road.path[3] = 80;
    assert(frame.begin(1, metadata));
    frame.add(road);
    view.prepare_text(road.name);
    ui::map_poi::AnnotationLayoutOptions options;
    options.width = 480;
    options.height = 222;
    assert(frame.finish(options, ui::widgets::map::PoiOverlay::measure_text, &view));
    assert(frame.result().road_labels == 1);
    const auto& item = frame.snapshot().items[0];
    assert(!item.marker && item.text_width > 0);
    view.update(frame.snapshot());
    lv_refr_now(display);
    int ink = 0, transparent = 0;
    for (int y = item.text_y; y < item.text_y + item.text_height; ++y)
        for (int x = item.text_x; x < item.text_x + item.text_width; ++x)
        {
            const auto pixel = pixels[y * 480 + x];
            if (pixel == 0x07E0) ++transparent;
            else ++ink;
            assert(pixel != 0xFFFF && pixel != 0xFFBD);
        }
    assert(ink > 10 && transparent > 10);
    assert(lv_obj_get_child_count(lv_obj_get_child(root, -1)) == 0);
    view.clear();
    frame.clear();
    lv_refr_now(display);
    assert(std::all_of(pixels.begin(), pixels.end(), [](uint16_t p)
                       { return p == 0x07E0; }));
    lv_obj_delete(root);
    lv_display_delete(display);
    lv_deinit();
}
