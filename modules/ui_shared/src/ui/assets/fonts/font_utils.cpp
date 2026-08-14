#include "ui/assets/fonts/font_utils.h"

#if defined(ESP_PLATFORM)
#include "esp_heap_caps.h"
#endif

#include <cstdio>
#include <cstdlib>
#include <new>

namespace ui::fonts
{
namespace
{

constexpr std::size_t kMaxLocalizedFontBindings = 24;
LocalizedFontBinding* s_localized_font_bindings = nullptr;
bool s_localized_font_binding_allocation_failed_logged = false;

LocalizedFontBinding* ensure_localized_font_binding_storage()
{
    if (s_localized_font_bindings)
    {
        return s_localized_font_bindings;
    }

    const std::size_t bytes = sizeof(LocalizedFontBinding) * kMaxLocalizedFontBindings;
#if defined(ESP_PLATFORM)
    void* storage = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    void* storage = std::malloc(bytes);
#endif
    if (!storage)
    {
        if (!s_localized_font_binding_allocation_failed_logged)
        {
            std::printf("[I18N] localized_font_bindings allocation_failed "
                        "memory=psram bytes=%u\n",
                        static_cast<unsigned>(bytes));
            s_localized_font_binding_allocation_failed_logged = true;
        }
        return nullptr;
    }

    auto* bindings = static_cast<LocalizedFontBinding*>(storage);
    for (std::size_t index = 0; index < kMaxLocalizedFontBindings; ++index)
    {
        new (&bindings[index]) LocalizedFontBinding{};
    }
    s_localized_font_bindings = bindings;
    std::printf("[I18N] localized_font_bindings allocated memory=psram bytes=%u\n",
                static_cast<unsigned>(bytes));
    return s_localized_font_bindings;
}

} // namespace

LocalizedFontBinding* localized_font_binding_storage()
{
    return ensure_localized_font_binding_storage();
}

std::size_t localized_font_binding_storage_size()
{
    return localized_font_binding_storage() ? kMaxLocalizedFontBindings : 0;
}

} // namespace ui::fonts
