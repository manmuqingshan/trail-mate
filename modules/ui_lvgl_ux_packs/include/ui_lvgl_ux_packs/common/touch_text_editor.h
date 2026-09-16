#pragma once

#include "lvgl.h"

namespace ui::widgets
{
class ImeWidget;

// No-op unless the active page profile selects a compact touch-only keyboard.
// An existing IME owner receives committed text so its composition buffer
// stays synchronized. Plain text fields use the same editor with no owner.
void attach_touch_text_editor(lv_obj_t* textarea, ImeWidget* owner = nullptr);
} // namespace ui::widgets
