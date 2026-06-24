#include "ui_lvgl_ux_packs/runtime/lvgl_runtime_adoption_probe.h"

namespace ui_lvgl_ux
{

bool LvglRuntimeAdoptionProbe::load(
    const product_composition::PresentationBundle& presentation)
{
    ready_ = adoption_.load(presentation);
    return ready_;
}

bool LvglRuntimeAdoptionProbe::ready() const
{
    return ready_;
}

std::size_t LvglRuntimeAdoptionProbe::menuCount() const
{
    return ready_ ? adoption_.menuCount() : 0;
}

std::size_t LvglRuntimeAdoptionProbe::screenCount() const
{
    return ready_ ? adoption_.screenCount() : 0;
}

const LvglRuntimeEntryAdoption& LvglRuntimeAdoptionProbe::adoption() const
{
    return adoption_;
}

} // namespace ui_lvgl_ux
