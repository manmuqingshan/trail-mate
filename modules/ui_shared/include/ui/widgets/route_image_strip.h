#pragma once

#include "lvgl.h"

#include <cstddef>
#include <string>
#include <vector>

namespace ui::widgets::route_image_strip
{

struct Item
{
    std::string local_path{};
    std::string preview_path{};
    std::string view_path{};
    bool downloaded = false;
};

struct Config
{
    lv_coord_t width = 200;
    lv_coord_t item_height = 120;
    lv_coord_t inset = 2;
};

using SelectionCallback = void (*)(std::size_t index, void* user_data);

struct Widget
{
    lv_obj_t* root = nullptr;
    lv_obj_t* list = nullptr;
    lv_obj_t* slot_button = nullptr;
    lv_obj_t* slot_image = nullptr;
    lv_obj_t* slot_placeholder = nullptr;
    lv_obj_t* slot_caption = nullptr;
    lv_obj_t* fullscreen_root = nullptr;
    Config config{};
    std::vector<Item> items{};
    std::vector<std::string> image_sources{};
    std::vector<std::string> fullscreen_sources{};
    std::vector<lv_obj_t*> item_buttons{};
    std::string rendered_source{};
    std::string fullscreen_source{};
    std::size_t selected_index = 0;
    bool visible = false;
    bool fullscreen_visible = false;
    SelectionCallback on_select = nullptr;
    void* user_data = nullptr;
};

void reset(Widget& widget);
void destroy(Widget& widget);
void create(lv_obj_t* parent, Widget& widget, const Config& config = Config{});
void set_selection_callback(Widget& widget, SelectionCallback callback, void* user_data);
void set_items(Widget& widget, const Item* items, std::size_t item_count);
void set_selected(Widget& widget, std::size_t index, bool notify);
void set_hidden(Widget& widget, bool hidden);
bool is_visible(const Widget& widget);
bool handle_key(Widget& widget, uint32_t key);
void refresh(Widget& widget);

} // namespace ui::widgets::route_image_strip
