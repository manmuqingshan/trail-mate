#include "ui/page/page_profile.h"
#include "ui/widgets/ime/ime_widget.h"
#include "ui_lvgl_ux_packs/common/input_layout.h"
#include "ui_lvgl_ux_packs/common/touch_text_editor.h"
#include "ui_lvgl_ux_packs/packs/deck_touch_ux_pack.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern bool test_pinyin_enabled;
namespace
{
uint16_t draw_buffer[320 * 40];
uint16_t framebuffer[320 * 240];

void flush(lv_display_t* display, const lv_area_t* area, uint8_t* pixels)
{
    const auto* source = reinterpret_cast<uint16_t*>(pixels);
    for (int y = area->y1; y <= area->y2; ++y)
        for (int x = area->x1; x <= area->x2; ++x)
            framebuffer[y * 320 + x] = *source++;
    lv_display_flush_ready(display);
}

lv_obj_t* find_class(lv_obj_t* root, const lv_obj_class_t* type)
{
    if (lv_obj_check_type(root, type)) return root;
    for (uint32_t index = 0; index < lv_obj_get_child_count(root); ++index)
        if (auto* found = find_class(lv_obj_get_child(root, index), type)) return found;
    return nullptr;
}

lv_obj_t* find_label(lv_obj_t* root, const char* text)
{
    if (lv_obj_check_type(root, &lv_label_class) && std::strcmp(lv_label_get_text(root), text) == 0) return root;
    for (uint32_t index = 0; index < lv_obj_get_child_count(root); ++index)
        if (auto* found = find_label(lv_obj_get_child(root, index), text)) return found;
    return nullptr;
}

void click_label(const char* text)
{
    auto* label = find_label(lv_layer_top(), text);
    assert(label);
    lv_obj_send_event(lv_obj_get_parent(label), LV_EVENT_CLICKED, nullptr);
}

void press_key(const char* token)
{
    auto* matrix = find_class(lv_layer_top(), &lv_buttonmatrix_class);
    assert(matrix);
    const char* const* map = lv_buttonmatrix_get_map(matrix);
    uint32_t button = 0;
    for (size_t index = 0; map[index][0]; ++index)
    {
        if (std::strcmp(map[index], "\n") == 0) continue;
        if (std::strcmp(map[index], token) == 0)
        {
            lv_obj_send_event(matrix, LV_EVENT_VALUE_CHANGED, &button);
            return;
        }
        ++button;
    }
    assert(false && "keyboard token missing");
}

void open(lv_obj_t* field)
{
    lv_obj_send_event(field, LV_EVENT_CLICKED, nullptr);
    lv_obj_update_layout(lv_layer_top());
    assert(lv_obj_get_child_count(lv_layer_top()) == 1);
    auto* matrix = find_class(lv_layer_top(), &lv_buttonmatrix_class);
    assert(matrix);
    lv_area_t area{};
    lv_obj_get_coords(matrix, &area);
    assert(area.x1 >= 0 && area.x2 < 320 && area.y1 >= 0 && area.y2 < 240);
    auto* ok = find_label(lv_layer_top(), "OK");
    assert(ok);
    lv_obj_get_coords(lv_obj_get_parent(ok), &area);
    assert(area.y1 >= 0 && area.y2 < 240);
}

void screenshot(lv_display_t* display)
{
    lv_refr_now(display);
    auto* file = std::fopen("wio-keyboard.ppm", "wb");
    assert(file);
    std::fprintf(file, "P6\n320 240\n255\n");
    for (uint16_t pixel : framebuffer)
    {
        const uint8_t rgb[] = {
            static_cast<uint8_t>(((pixel >> 11) & 31) * 255 / 31),
            static_cast<uint8_t>(((pixel >> 5) & 63) * 255 / 63),
            static_cast<uint8_t>((pixel & 31) * 255 / 31)};
        std::fwrite(rgb, 1, sizeof(rgb), file);
    }
    std::fclose(file);
}
} // namespace

int main()
{
#if defined(_MSC_VER)
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    lv_init();
    assert(!ui::page_profile::current().compact_touch_keyboard);
    ui_lvgl_ux::DeckTouchUxPack pack;
    ui_lvgl_ux::configureInputLayout(pack.profile());
    assert(ui::page_profile::current().compact_touch_keyboard);
    lv_display_t* display = lv_display_create(320, 240);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, draw_buffer, nullptr, sizeof(draw_buffer), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush);
    auto* form = lv_obj_create(lv_screen_active());
    lv_obj_set_size(form, 320, 240);
    auto* field = lv_textarea_create(form);
    lv_textarea_set_text(field, "");
    ui::widgets::ImeWidget owner;
    owner.init(form, field);
    assert(!find_class(form, &lv_buttonmatrix_class));

    open(field);
    press_key("q");
    press_key("Shift");
    press_key("A");
    screenshot(display);
    click_label("OK");
    assert(std::strcmp(lv_textarea_get_text(field), "qA") == 0);
    assert(lv_obj_get_child_count(lv_layer_top()) == 0);
    open(field);
    press_key("z");
    click_label("Cancel");
    assert(std::strcmp(lv_textarea_get_text(field), "qA") == 0);

    open(field);
    click_label("EN");
    press_key("_");
    press_key("Shift");
    press_key("}");
    click_label("OK");
    assert(std::strcmp(lv_textarea_get_text(field), "qA_}") == 0);

    auto* plain = lv_textarea_create(form);
    lv_textarea_set_text(plain, "");
    lv_textarea_set_one_line(plain, true);
    lv_textarea_set_max_length(plain, 3);
    ui::widgets::attach_touch_text_editor(plain);
    open(plain);
    press_key("a");
    press_key("b");
    press_key("c");
    press_key("d");
    click_label("OK");
    assert(std::strcmp(lv_textarea_get_text(plain), "abc") == 0);
    open(plain);
    press_key("d");
    press_key("Bksp");
    click_label("OK");
    assert(std::strcmp(lv_textarea_get_text(plain), "ab") == 0);

    open(plain);
    lv_obj_clean(lv_layer_top());
    open(plain);
    click_label("Cancel");

    test_pinyin_enabled = true;
    lv_textarea_set_text(plain, "");
    open(plain);
    click_label("EN");
    press_key("n");
    press_key("i");
    lv_obj_update_layout(lv_layer_top());
    auto* matrix = find_class(lv_layer_top(), &lv_buttonmatrix_class);
    lv_area_t area{};
    lv_obj_get_coords(matrix, &area);
    assert(area.y2 < 240);
    click_label("你");
    click_label("OK");
    assert(std::strcmp(lv_textarea_get_text(plain), "你") == 0);

    open(plain);
    lv_obj_delete(plain);
    assert(lv_obj_get_child_count(lv_layer_top()) == 0);
    open(field);
    owner.detach();
    assert(lv_obj_get_child_count(lv_layer_top()) == 0);
    lv_obj_delete(form);
    lv_display_delete(display);
    std::puts("Touch editor interaction, bounds and lifetime checks passed.");
    return 0;
}
