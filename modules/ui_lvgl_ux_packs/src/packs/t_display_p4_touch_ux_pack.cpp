#include "ui_lvgl_ux_packs/packs/t_display_p4_touch_ux_pack.h"

namespace ui_lvgl_ux
{

const char* TDisplayP4TouchUxPack::id() const
{
    return "t_display_p4_touch";
}

const DeviceUxProfile& TDisplayP4TouchUxPack::profile() const
{
    static const DeviceUxProfile profile{
        "t_display_p4_touch",
        ScreenClass::TouchTablet,
        InputModel::Touch,
        MapMode::Full,
        ChatMode::Full,
        true,
        true,
        true,
        true,
    };
    return profile;
}

const UxFeatureSet& TDisplayP4TouchUxPack::features() const
{
    static const UxFeatureSet features{
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
    };
    return features;
}

void TDisplayP4TouchUxPack::buildScreens(ScreenRegistry& out) const
{
    out.clear();
    (void)out.add({ScreenId::Dashboard, "Dashboard", true});
    (void)out.add({ScreenId::Chat, "Chat", true});
    (void)out.add({ScreenId::Contacts, "Contacts", true});
    (void)out.add({ScreenId::Map, "Map", true});
    (void)out.add({ScreenId::SkyPlot, "Sky Plot", true});
    (void)out.add({ScreenId::Gps, "GPS", true});
    (void)out.add({ScreenId::Team, "Team", true});
    (void)out.add({ScreenId::Tracker, "Tracker", true});
    (void)out.add({ScreenId::PcLink, "PC Link", true});
    (void)out.add({ScreenId::EnergySweep, "Energy Sweep", true});
    (void)out.add({ScreenId::WalkieTalkie, "Walkie", true});
    (void)out.add({ScreenId::Sstv, "SSTV", true});
    (void)out.add({ScreenId::Extensions, "Extensions", true});
    (void)out.add({ScreenId::Settings, "Settings", true});
}

void TDisplayP4TouchUxPack::buildInputBindings(InputBindingSet& out) const
{
    out.clear();
    (void)out.add({InputAction::Select, "Tap"});
    (void)out.add({InputAction::Back, "Bottom edge swipe"});
    (void)out.add({InputAction::Menu, "Menu gesture"});
    (void)out.add({InputAction::Compose, "Compose"});
    (void)out.add({InputAction::MapZoomIn, "Pinch zoom in"});
    (void)out.add({InputAction::MapZoomOut, "Pinch zoom out"});
}

} // namespace ui_lvgl_ux
