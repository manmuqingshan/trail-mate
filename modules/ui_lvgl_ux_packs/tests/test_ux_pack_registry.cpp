#include "ui_lvgl_ux_packs/ux/ux_pack_registry.h"

#include <cassert>
#include <cstring>

int main()
{
    const ui_lvgl_ux::IUxPack* compatibility =
        ui_lvgl_ux::findUxPackById("compatibility");
    const ui_lvgl_ux::IUxPack* uconsole =
        ui_lvgl_ux::findUxPackById("uconsole_desktop");
    const ui_lvgl_ux::IUxPack* tiny =
        ui_lvgl_ux::findUxPackById("tiny_node_status");
    const ui_lvgl_ux::IUxPack* cardputer =
        ui_lvgl_ux::findUxPackById("cardputer_compact");
    const ui_lvgl_ux::IUxPack* simulator =
        ui_lvgl_ux::findUxPackById("simulator_full");
    const ui_lvgl_ux::IUxPack* t_display_p4 =
        ui_lvgl_ux::findUxPackById("t_display_p4_touch");

    assert(compatibility != nullptr);
    assert(uconsole != nullptr);
    assert(tiny != nullptr);
    assert(cardputer != nullptr);
    assert(simulator != nullptr);
    assert(t_display_p4 != nullptr);
    assert(std::strcmp(compatibility->id(), "compatibility") == 0);
    assert(std::strcmp(uconsole->id(), "uconsole_desktop") == 0);
    assert(std::strcmp(tiny->id(), "tiny_node_status") == 0);
    assert(std::strcmp(cardputer->id(), "cardputer_compact") == 0);
    assert(std::strcmp(simulator->id(), "simulator_full") == 0);
    assert(std::strcmp(t_display_p4->id(), "t_display_p4_touch") == 0);
    assert(ui_lvgl_ux::findUxPackById("missing") == nullptr);
    assert(ui_lvgl_ux::findUxPackById(nullptr) == nullptr);

    ui_lvgl_ux::ScreenRegistry screens;
    uconsole->buildScreens(screens);
    assert(screens.size() == 7);

    t_display_p4->buildScreens(screens);
    assert(screens.size() == 14);
    return 0;
}
