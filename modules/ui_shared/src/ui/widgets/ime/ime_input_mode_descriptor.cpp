/**
 * @file ime_input_mode_descriptor.cpp
 * @brief Runtime descriptors for script IME behavior and direct keyboard layouts.
 */

#include "ui/widgets/ime/ime_input_mode_descriptor.h"

#include <cstring>

namespace ui
{
namespace widgets
{
namespace ime
{
namespace
{

static const char* kTouchRuCyrillicMap[] = {
    "й", "ц", "у", "к", "е", "н", "г", "ш", "щ", "з", "х", "ъ", "Bksp", "\n",
    "ф", "ы", "в", "а", "п", "р", "о", "л", "д", "ж", "э", "Enter", "\n",
    "я", "ч", "с", "м", "и", "т", "ь", "б", "ю", ",", ".", "?", "\n",
    "Space", ""};

static constexpr KeyboardLayoutDescriptor kKeyboardLayouts[] = {{
    "ru-cyrillic",
    "RU",
    "Cyrillic keyboard",
    kTouchRuCyrillicMap,
    "йцукенгшщзхъфывапролджэячсмитьбю",
}};

bool streq(const char* lhs, const char* rhs)
{
    return lhs != nullptr && rhs != nullptr && std::strcmp(lhs, rhs) == 0;
}

} // namespace

const KeyboardLayoutDescriptor* keyboard_layout_for_id(const char* layout_id)
{
    if (layout_id == nullptr || layout_id[0] == '\0')
    {
        return nullptr;
    }
    for (const KeyboardLayoutDescriptor& layout : kKeyboardLayouts)
    {
        if (streq(layout.layout_id, layout_id))
        {
            return &layout;
        }
    }
    return nullptr;
}

ScriptInputDescriptor describe_script_input(const ::ui::i18n::ImeInfo* ime)
{
    if (ime == nullptr || ime->id == nullptr || ime->backend == nullptr)
    {
        return {};
    }

    const char* display_name = ime->display_name != nullptr ? ime->display_name : ime->id;
    if (streq(ime->backend, "builtin-pinyin"))
    {
        return {ScriptInputKind::Pinyin, ime->id, display_name, nullptr};
    }
    if (streq(ime->backend, "builtin-keyboard-layout"))
    {
        const KeyboardLayoutDescriptor* layout = keyboard_layout_for_id(ime->layout);
        if (layout == nullptr)
        {
            return {};
        }
        return {ScriptInputKind::DirectKeyboard, ime->id, display_name, layout};
    }
    return {};
}

} // namespace ime
} // namespace widgets
} // namespace ui
