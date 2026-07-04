#pragma once

#include "lvgl.h"

namespace ui
{

enum class AppLaunchMode
{
    Screen,
    MenuOverlay,
};

} // namespace ui

class AppScreen
{
  public:
    virtual ~AppScreen() = default;
    virtual const char* stable_id() const = 0;
    virtual const char* name() const = 0;
    virtual const lv_image_dsc_t* icon() const = 0;
    virtual ui::AppLaunchMode launch_mode() const { return ui::AppLaunchMode::Screen; }
    virtual void enter(lv_obj_t* parent) = 0;
    virtual void exit(lv_obj_t* parent) = 0;
};
