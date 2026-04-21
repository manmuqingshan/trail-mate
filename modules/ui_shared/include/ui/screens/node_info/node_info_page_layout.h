/**
 * @file node_info_page_layout.h
 * @brief Node info page layout helpers
 */

#pragma once

#include "lvgl.h"

namespace node_info
{
namespace ui
{
namespace layout
{

lv_obj_t* create_root(lv_obj_t* parent);
lv_obj_t* create_header(lv_obj_t* root);
lv_obj_t* create_content(lv_obj_t* root);

} // namespace layout
} // namespace ui
} // namespace node_info
