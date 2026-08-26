#pragma once

#include "lvgl.h"

namespace calculator::ui::layout
{

// Geometry belongs to the view adapter, not to the calculation model.  All
// fields are coordinates only, so resolving a layout adds no heap allocation
// or screen-sized backing store.
struct Geometry
{
    lv_coord_t width = 0;
    lv_coord_t height = 0;
    lv_coord_t content_x = 0;
    lv_coord_t content_width = 0;
    lv_coord_t outer_margin = 0;
    lv_coord_t top_bar_height = 0;
    lv_coord_t display_y = 0;
    lv_coord_t display_height = 0;
    lv_coord_t function_y = 0;
    lv_coord_t function_height = 0;
    lv_coord_t function_gap = 0;
    lv_coord_t keypad_y = 0;
    lv_coord_t keypad_button_height = 0;
    lv_coord_t keypad_gap = 0;
    lv_coord_t footer_y = 0;
    lv_coord_t footer_height = 0;
    bool large_touch = false;
};

// Resolves the Pager, T-Deck, and touch-P4 projections from the actual LVGL
// root dimensions.  This keeps device layout policy independent from the
// keypad interaction and calculation state.
Geometry resolve(lv_obj_t* parent);

} // namespace calculator::ui::layout
