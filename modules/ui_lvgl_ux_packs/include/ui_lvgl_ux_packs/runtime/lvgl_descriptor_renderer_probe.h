#pragma once

#include "ui_lvgl_ux_packs/runtime/lvgl_descriptor_menu_model.h"

#include <cstddef>

namespace ui_lvgl_ux
{

class LvglDescriptorRendererProbe
{
  public:
    bool render(const LvglPrimaryScreenGraphRuntime& runtime);
    bool load(const LvglPrimaryScreenGraphRuntime& runtime);

    bool ready() const noexcept;
    bool usingPrimaryScreenGraph() const noexcept;
    bool usedPrimaryScreenGraph() const noexcept;
    std::size_t itemCount() const noexcept;
    const LvglDescriptorMenuItem* items() const noexcept;

  private:
    LvglDescriptorMenuModel model_{};
    bool ready_ = false;
    bool primary_ = false;
};

} // namespace ui_lvgl_ux
