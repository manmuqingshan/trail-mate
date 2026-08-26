#pragma once

#include "lvgl.h"
#include "ui/page/page_host.h"

namespace calculator::ui::shell
{

using Host = ::ui::page::Host;

void enter(void* user_data, lv_obj_t* parent);
void exit(void* user_data, lv_obj_t* parent);

} // namespace calculator::ui::shell
