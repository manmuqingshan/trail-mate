#pragma once
#include "lvgl.h"
// Device font loading is verified by the ESP build. This host adapter supplies
// a deterministic font to exercise the real PoiOverlay and LVGL draw/lifecycle.
namespace ui::fonts
{
inline const lv_font_t* ui_chrome_font() { return &lv_font_montserrat_14; }
inline const lv_font_t* content_font(const char*, const lv_font_t*) { return &lv_font_montserrat_14; }
} // namespace ui::fonts
