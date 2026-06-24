/**
 * @file ime_input_mode_descriptor.h
 * @brief Runtime descriptors that keep IME behavior separate from layout metadata.
 */

#pragma once

#include "ui/localization.h"

namespace ui
{
namespace widgets
{
namespace ime
{

enum class ScriptInputKind
{
    None,
    Pinyin,
    DirectKeyboard,
};

struct KeyboardLayoutDescriptor
{
    const char* layout_id = nullptr;
    const char* mode_label = nullptr;
    const char* touch_hint_key = nullptr;
    const char* const* touch_map = nullptr;
    const char* font_probe_text = nullptr;
};

struct ScriptInputDescriptor
{
    ScriptInputKind kind = ScriptInputKind::None;
    const char* ime_id = nullptr;
    const char* display_name = nullptr;
    const KeyboardLayoutDescriptor* keyboard_layout = nullptr;
};

const KeyboardLayoutDescriptor* keyboard_layout_for_id(const char* layout_id);
ScriptInputDescriptor describe_script_input(const ::ui::i18n::ImeInfo* ime);

} // namespace ime
} // namespace widgets
} // namespace ui
