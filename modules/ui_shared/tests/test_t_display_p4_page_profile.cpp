#include "ui/page/page_profile.h"

#include <cassert>
#include <cstring>

const lv_font_t lv_font_montserrat_10{};
const lv_font_t lv_font_montserrat_12{};
const lv_font_t lv_font_montserrat_14{};
const lv_font_t lv_font_montserrat_16{};

lv_coord_t lv_display_get_physical_horizontal_resolution(lv_display_t*)
{
    return 720;
}

lv_coord_t lv_display_get_physical_vertical_resolution(lv_display_t*)
{
    return 720;
}

lv_coord_t lv_obj_get_width(lv_obj_t*)
{
    return 720;
}

lv_coord_t lv_obj_get_height(lv_obj_t*)
{
    return 720;
}

lv_obj_t* lv_screen_active()
{
    return nullptr;
}

int main()
{
    const auto& profile = ui::page_profile::current();
    assert(std::strcmp(profile.name, "t_display_p4") == 0);
    assert(profile.variant == ui::page_profile::LayoutVariant::HybridTouchLarge);
    assert(profile.large_touch_hitbox);
    assert(profile.filter_panel_width == 180);
    assert(profile.ime_keyboard_height == 260);
    assert(ui::page_profile::resolve_control_button_height() == 54);
    assert(ui::page_profile::resolve_control_button_min_width() == 120);
    return 0;
}
