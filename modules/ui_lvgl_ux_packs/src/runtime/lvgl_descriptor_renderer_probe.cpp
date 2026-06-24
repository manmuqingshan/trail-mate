#include "ui_lvgl_ux_packs/runtime/lvgl_descriptor_renderer_probe.h"

namespace ui_lvgl_ux
{

bool LvglDescriptorRendererProbe::render(
    const LvglPrimaryScreenGraphRuntime& runtime)
{
    return load(runtime);
}

bool LvglDescriptorRendererProbe::load(
    const LvglPrimaryScreenGraphRuntime& runtime)
{
    ready_ = false;
    primary_ = false;

    if (runtime.usingPrimaryScreenGraph())
    {
        ready_ = model_.load(runtime);
        primary_ = ready_;
        return ready_;
    }

    return false;
}

bool LvglDescriptorRendererProbe::ready() const noexcept
{
    return ready_;
}

bool LvglDescriptorRendererProbe::usingPrimaryScreenGraph() const noexcept
{
    return primary_;
}

bool LvglDescriptorRendererProbe::usedPrimaryScreenGraph() const noexcept
{
    return primary_;
}

std::size_t LvglDescriptorRendererProbe::itemCount() const noexcept
{
    return ready_ ? model_.itemCount() : 0;
}

const LvglDescriptorMenuItem* LvglDescriptorRendererProbe::items()
    const noexcept
{
    return ready_ ? model_.items() : nullptr;
}

} // namespace ui_lvgl_ux
