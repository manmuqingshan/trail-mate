#include "ui/widgets/ime/ime_input_mode_descriptor.h"

#include <cassert>
#include <cstring>

namespace
{

ui::i18n::ImeInfo makeIme(const char* id,
                          const char* backend,
                          const char* layout)
{
    ui::i18n::ImeInfo ime;
    ime.id = id;
    ime.display_name = id;
    ime.backend = backend;
    ime.layout = layout;
    return ime;
}

} // namespace

int main()
{
    const ui::i18n::ImeInfo pinyin = makeIme("custom-pinyin", "builtin-pinyin", nullptr);
    const ui::widgets::ime::ScriptInputDescriptor pinyin_desc =
        ui::widgets::ime::describe_script_input(&pinyin);
    assert(pinyin_desc.kind == ui::widgets::ime::ScriptInputKind::Pinyin);
    assert(std::strcmp(pinyin_desc.ime_id, "custom-pinyin") == 0);

    const ui::i18n::ImeInfo cyrillic =
        makeIme("any-pack-id", "builtin-keyboard-layout", "ru-cyrillic");
    const ui::widgets::ime::ScriptInputDescriptor cyrillic_desc =
        ui::widgets::ime::describe_script_input(&cyrillic);
    assert(cyrillic_desc.kind == ui::widgets::ime::ScriptInputKind::DirectKeyboard);
    assert(cyrillic_desc.keyboard_layout != nullptr);
    assert(std::strcmp(cyrillic_desc.keyboard_layout->layout_id, "ru-cyrillic") == 0);
    assert(std::strcmp(cyrillic_desc.keyboard_layout->mode_label, "RU") == 0);
    assert(cyrillic_desc.keyboard_layout->touch_map != nullptr);
    assert(cyrillic_desc.keyboard_layout->font_probe_text != nullptr);

    const ui::i18n::ImeInfo unknown_layout =
        makeIme("unknown-layout", "builtin-keyboard-layout", "not-registered");
    const ui::widgets::ime::ScriptInputDescriptor unknown_desc =
        ui::widgets::ime::describe_script_input(&unknown_layout);
    assert(unknown_desc.kind == ui::widgets::ime::ScriptInputKind::None);

    const ui::i18n::ImeInfo legacy_picker =
        makeIme("symbol-picker", "builtin-candidate-picker", nullptr);
    const ui::widgets::ime::ScriptInputDescriptor legacy_desc =
        ui::widgets::ime::describe_script_input(&legacy_picker);
    assert(legacy_desc.kind == ui::widgets::ime::ScriptInputKind::None);
    return 0;
}
