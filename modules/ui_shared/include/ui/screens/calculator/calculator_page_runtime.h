#pragma once

#include "lvgl.h"
#include "ui/page/page_host.h"

namespace calculator::ui::runtime
{

void enter(const ::ui::page::Host* host, lv_obj_t* parent);
void exit(lv_obj_t* parent);

} // namespace calculator::ui::runtime
