#pragma once

#include "lvgl.h"

namespace ui::mono::screens::screen_240x320::cellular_page
{

// A reusable 240x320 monochrome cellular UI.  It contains no board identity,
// pin knowledge, transport commands, or persistence implementation.
void enter(lv_obj_t* parent);
void exit(lv_obj_t* parent);

} // namespace ui::mono::screens::screen_240x320::cellular_page
