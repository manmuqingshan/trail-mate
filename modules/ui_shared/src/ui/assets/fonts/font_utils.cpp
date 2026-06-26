#include "ui/assets/fonts/font_utils.h"

#if defined(ESP_PLATFORM)
#include "esp_attr.h"
#define UI_FONT_STATE_RAM_ATTR EXT_RAM_ATTR
#else
#define UI_FONT_STATE_RAM_ATTR
#endif

namespace ui::fonts
{
namespace
{

constexpr std::size_t kMaxLocalizedFontBindings = 24;
UI_FONT_STATE_RAM_ATTR LocalizedFontBinding s_localized_font_bindings[kMaxLocalizedFontBindings]{};

} // namespace

LocalizedFontBinding* localized_font_binding_storage()
{
    return s_localized_font_bindings;
}

std::size_t localized_font_binding_storage_size()
{
    return kMaxLocalizedFontBindings;
}

} // namespace ui::fonts
