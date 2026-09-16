#include "ui/widgets/map/poi_overlay.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
    lv_init();
    if (argc > 2)
    {
        std::ifstream binary(argv[1], std::ios::binary);
        std::vector<char> bytes((std::istreambuf_iterator<char>(binary)), std::istreambuf_iterator<char>());
        assert(!bytes.empty());
        auto* font = lv_binfont_create_from_buffer(bytes.data(), static_cast<uint32_t>(bytes.size()));
        assert(font);
        std::ifstream ranges(argv[2]);
        std::string codepoint;
        std::size_t glyphs = 0;
        while (std::getline(ranges, codepoint, ','))
        {
            lv_font_glyph_dsc_t glyph{};
            assert(lv_font_get_glyph_dsc(font, &glyph, static_cast<uint32_t>(std::stoul(codepoint, nullptr, 16)), 0));
            assert(glyph.box_w > 0 && glyph.box_h > 0);
            ++glyphs;
        }
        assert(glyphs > 0);
        lv_binfont_destroy(font);
    }
    auto* display = lv_display_create(480, 222);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    std::vector<uint16_t> pixels(480 * 222);
    lv_display_set_buffers(display, pixels.data(), nullptr, pixels.size() * sizeof(uint16_t), LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display, [](lv_display_t* d, const lv_area_t*, uint8_t*)
                            { lv_display_flush_ready(d); });
    auto* root = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, 480, 222);
    lv_obj_set_style_bg_color(root, lv_color_hex(0x00FF00), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_update_layout(root);
    const auto initial = lv_obj_get_child_count(root);
    ui::widgets::map::PoiOverlay overlay;
    overlay.create(root);
    lv_obj_update_layout(root);
    std::vector<ui::map::MapPoiItem> items(48);
    ui::map::MapPoiSnapshot snapshot;
    snapshot.enabled = true;
    snapshot.layout_ready = true;
    snapshot.labels = true;
    snapshot.items = items.data();
    snapshot.capacity = items.size();
    snapshot.item_count = items.size();
    for (std::size_t i = 0; i < items.size(); ++i)
    {
        items[i].x = 20 + (i % 12) * 38;
        items[i].y = 25 + (i / 12) * 48;
        ui::copyText(items[i].category, "water");
        ui::copyText(items[i].label, "Water");
    }
    overlay.update(snapshot);
    lv_refr_now(display);
    assert(pixels[25 * 480 + 15] != 0x07E0);                                              // actual marker rasterisation
    assert(std::count(pixels.begin(), pixels.end(), static_cast<uint16_t>(0xFFBD)) == 0); // no cream label plate
    assert(lv_obj_get_child_count(root) == initial + 1);
    auto* layer = lv_obj_get_child(root, -1);
    assert(lv_obj_get_child_count(layer) == 0); // no hidden label/marker pool
    for (int i = 0; i < 100; ++i)
    {
        overlay.translate(2, -1);
        overlay.update(snapshot);
        lv_refr_now(display);
        assert(lv_obj_get_child_count(layer) == 0);
    }
    overlay.clear();
    lv_refr_now(display);
    assert(pixels[25 * 480 + 15] == 0x07E0);
    snapshot.enabled = false;
    overlay.update(snapshot);
    lv_refr_now(display);
    assert(lv_obj_has_flag(layer, LV_OBJ_FLAG_HIDDEN));
    lv_obj_delete(root); // parent-first deletion must detach the view safely
    overlay.clear();
    lv_display_delete(display);
    lv_deinit();
}
