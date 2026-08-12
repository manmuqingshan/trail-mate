#pragma once

#include <stdint.h>

#include <lvgl.h>

class LilyGo_Display;

#if defined(ARDUINO) && \
    (!defined(TRAIL_MATE_LVGL_SD_FS_LETTER) || TRAIL_MATE_LVGL_SD_FS_LETTER != 'A')
#warning "TrailMate LVGL SD fs mismatch, A: paths may not resolve"
#endif

void beginLvglHelper(LilyGo_Display& display, bool debug = false);
void serviceLvglDisplay(uint32_t now_ms);
void lv_set_default_group(lv_group_t* group);
lv_indev_t* lv_get_touch_indev();
lv_indev_t* lv_get_keyboard_indev();
lv_indev_t* lv_get_encoder_indev();

bool lv_begin_external_font_load_fs_scope();
void lv_end_external_font_load_fs_scope();
bool lv_external_font_load_fs_was_busy();
