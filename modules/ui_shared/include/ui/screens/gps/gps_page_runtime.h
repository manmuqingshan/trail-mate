#pragma once

#include "ui/screens/gps/gps_page_shell.h"

#include <cstdint>

namespace gps::ui::runtime
{

bool is_available();
void enter(const shell::Host* host,
           lv_obj_t* parent,
           shell::Projection projection = shell::Projection::Map);
void exit(lv_obj_t* parent);
void remember_gps_view_state();
bool restore_gps_view_state();
uint32_t selected_map_member_id();
bool load_map_track_file(const char* path, bool show_fail_toast);

} // namespace gps::ui::runtime
