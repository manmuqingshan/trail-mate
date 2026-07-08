#pragma once

#include "lvgl.h"

#include <cstdint>

namespace ui::components::floating_search_box
{

using ApplyCallback = void (*)(const char* text, void* user_data);
using SimpleCallback = void (*)(void* user_data);

struct Callbacks
{
    ApplyCallback apply = nullptr;
    SimpleCallback clear = nullptr;
    SimpleCallback cancel = nullptr;
    void* user_data = nullptr;
};

struct Config
{
    const char* title = "Search";
    const char* initial_text = "";
    uint16_t max_length = 31;
    lv_coord_t width = 280;
    lv_coord_t height = 150;
    lv_group_t* restore_group = nullptr;
    Callbacks callbacks{};
};

struct State
{
    lv_obj_t* overlay = nullptr;
    lv_obj_t* textarea = nullptr;
    lv_group_t* group = nullptr;
    lv_group_t* previous_group = nullptr;
    Callbacks callbacks{};
};

bool is_open(const State& state);
bool open(State& state, lv_obj_t* parent, const Config& config);
void close(State& state);
void focus(State& state);

} // namespace ui::components::floating_search_box
