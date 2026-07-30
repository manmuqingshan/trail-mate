#pragma once

#include "lvgl.h"

#include <cstddef>
#include <cstdint>

namespace ui::components::shortcut_help_modal
{

struct Row
{
    const char* primary = nullptr;
    const char* secondary = nullptr;
    const char* description = nullptr;
};

struct Config
{
    const char* title = "Help";
    const Row* rows = nullptr;
    std::size_t row_count = 0;
    lv_coord_t width = 304;
    lv_coord_t height = 176;
    lv_group_t* restore_group = nullptr;
};

struct State
{
    lv_obj_t* overlay = nullptr;
    lv_obj_t* panel = nullptr;
    lv_obj_t* body = nullptr;
    lv_group_t* group = nullptr;
    lv_group_t* previous_group = nullptr;
};

bool is_open(const State& state);
bool open(State& state, lv_obj_t* parent, const Config& config);
void close(State& state);
void focus(State& state);

} // namespace ui::components::shortcut_help_modal
