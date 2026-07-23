#include "ui_lvgl_ux_packs/packs/uconsole_desktop_ux_pack.h"

#include <cassert>
#include <cstring>

int main()
{
    ui_lvgl_ux::UConsoleDesktopUxPack pack;
    assert(std::strcmp(pack.id(), "uconsole_desktop") == 0);
    assert(pack.profile().screen_class == ui_lvgl_ux::ScreenClass::Desktop);
    assert(pack.profile().input_model ==
           ui_lvgl_ux::InputModel::DesktopKeyboardMouse);
    assert(pack.profile().map_mode == ui_lvgl_ux::MapMode::Desktop);
    assert(pack.profile().chat_mode == ui_lvgl_ux::ChatMode::Full);
    assert(pack.features().chat);
    assert(pack.features().contacts);
    assert(pack.features().walkie);
    assert(pack.features().sstv);
    assert(pack.features().extensions);

    ui_lvgl_ux::ScreenRegistry screens;
    pack.buildScreens(screens);
    assert(screens.size() == 12);
    assert(screens.items()[0].id == ui_lvgl_ux::ScreenId::Dashboard);
    assert(screens.items()[1].id == ui_lvgl_ux::ScreenId::Chat);
    assert(screens.items()[2].id == ui_lvgl_ux::ScreenId::Contacts);
    assert(screens.items()[7].id == ui_lvgl_ux::ScreenId::EnergySweep);
    assert(screens.items()[8].id == ui_lvgl_ux::ScreenId::WalkieTalkie);
    assert(screens.items()[9].id == ui_lvgl_ux::ScreenId::Sstv);
    assert(screens.items()[10].id == ui_lvgl_ux::ScreenId::Extensions);
    assert(screens.items()[11].id == ui_lvgl_ux::ScreenId::Settings);
    return 0;
}
