#include "ui_lvgl_ux_packs/common/input_layout.h"
#include "ui/page/page_profile.h"

namespace ui_lvgl_ux
{
void configureInputLayout(const DeviceUxProfile& profile)
{
    if (profile.screen_class != ScreenClass::DeckLandscape || profile.input_model != InputModel::Touch)
    {
        ui::page_profile::set_active_profile(nullptr);
        return;
    }
    static const ui::page_profile::PageLayoutProfile layout = []()
    {
        auto value = ui::page_profile::make_tdeck_profile();
        value.name = "deck_touch";
        value.compact_touch_keyboard = true;
        value.ime_bar_height = 22;
        value.ime_toggle_width = 44;
        value.ime_toggle_height = 20;
        value.ime_candidate_button_height = 22;
        value.ime_keyboard_height = 108;
        return value;
    }();
    ui::page_profile::set_active_profile(&layout);
}
} // namespace ui_lvgl_ux
