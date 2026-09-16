#include "ui/localization.h"
#include <cstdio>
bool test_pinyin_enabled = false;
namespace ui::i18n
{
const char* tr(const char* text) { return text; }
const char* active_ime_pack_id() { return test_pinyin_enabled ? "pinyin" : ""; }
std::size_t ime_count() { return test_pinyin_enabled ? 1 : 0; }
const ImeInfo* ime_at(std::size_t index)
{
    static const ImeInfo info{"pinyin", "Pinyin", "builtin-pinyin", nullptr, true};
    return index == 0 && test_pinyin_enabled ? &info : nullptr;
}
bool ime_enabled(const char*) { return test_pinyin_enabled; }
void set_label_text(lv_obj_t* label, const char* text) { lv_label_set_text(label, text); }
void set_content_label_text_raw(lv_obj_t* label, const char* text) { lv_label_set_text(label, text); }
std::string format(const char* fmt, ...)
{
    char buffer[256]{};
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    return buffer;
}
} // namespace ui::i18n
