#pragma once
#include "ui_lvgl_ux_packs/ux/device_ux_profile.h"

namespace ui_lvgl_ux
{
// Called by the app shell after selecting a UX pack, before creating its pages.
void configureInputLayout(const DeviceUxProfile& profile);
} // namespace ui_lvgl_ux
