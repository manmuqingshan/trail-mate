#include "ui/widgets/text_candidate_picker.h"

#include "ui/assets/fonts/font_utils.h"
#include "ui/localization.h"
#include "ui/page/page_profile.h"
#include "ui/ui_common.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace ui::widgets
{
namespace
{

constexpr std::size_t kMaxCandidateButtons = text_candidates::kMaxBuiltinTextCandidates;
constexpr lv_coord_t kHeaderHeightPx = 30;
constexpr lv_coord_t kHeaderCloseButtonHeightPx = 24;
constexpr lv_coord_t kPickerOuterPaddingPx = 6;
constexpr lv_coord_t kGridGapPx = 6;
constexpr lv_coord_t kGridTopPaddingPx = 4;
constexpr lv_coord_t kGridBottomPaddingPx = 2;

#ifndef TRAIL_MATE_TEXT_CANDIDATE_LAYOUT_LOG
#define TRAIL_MATE_TEXT_CANDIDATE_LAYOUT_LOG 0
#endif

constexpr uint32_t kAmber = 0xEBA341;
constexpr uint32_t kAmberDark = 0xC98118;
constexpr uint32_t kWarmBg = 0xF6E6C6;
constexpr uint32_t kPanelBg = 0xFAF0D8;
constexpr uint32_t kLine = 0xE7C98F;
constexpr uint32_t kText = 0x6B4A1E;
constexpr uint32_t kTextDim = 0x8A6A3A;

struct PickerState
{
    lv_obj_t* root = nullptr;
    lv_obj_t* textarea = nullptr;
    lv_group_t* group = nullptr;
    lv_group_t* previous_group = nullptr;
    text_candidates::CandidateSet set = text_candidates::CandidateSet::Symbols;
    std::array<lv_obj_t*, kMaxCandidateButtons> buttons{};
    std::array<std::size_t, text_candidates::kMaxBuiltinTextCandidates> candidate_indices{};
    std::size_t candidate_count = 0;
    std::size_t active = 0;
    int columns = 1;
    const lv_font_t* candidate_font = nullptr;
};

PickerState s_picker;

bool decode_next_utf8(const unsigned char*& ptr, uint32_t& out_codepoint)
{
    if (!ptr || *ptr == 0)
    {
        return false;
    }

    const unsigned char lead = *ptr++;
    if ((lead & 0x80U) == 0)
    {
        out_codepoint = lead;
        return true;
    }

    int extra_bytes = 0;
    uint32_t codepoint = 0;
    if ((lead & 0xE0U) == 0xC0U)
    {
        extra_bytes = 1;
        codepoint = static_cast<uint32_t>(lead & 0x1FU);
    }
    else if ((lead & 0xF0U) == 0xE0U)
    {
        extra_bytes = 2;
        codepoint = static_cast<uint32_t>(lead & 0x0FU);
    }
    else if ((lead & 0xF8U) == 0xF0U)
    {
        extra_bytes = 3;
        codepoint = static_cast<uint32_t>(lead & 0x07U);
    }
    else
    {
        out_codepoint = 0xFFFD;
        return true;
    }

    for (int i = 0; i < extra_bytes; ++i)
    {
        const unsigned char next = *ptr;
        if ((next & 0xC0U) != 0x80U)
        {
            out_codepoint = 0xFFFD;
            return true;
        }
        ++ptr;
        codepoint = (codepoint << 6U) | static_cast<uint32_t>(next & 0x3FU);
    }

    out_codepoint = codepoint;
    return true;
}

bool codepoint_requires_no_glyph(uint32_t codepoint)
{
    return (codepoint >= 0xFE00U && codepoint <= 0xFE0FU) ||
           codepoint == 0x200DU;
}

bool font_supports_text(const lv_font_t* font, const char* text)
{
    if (!font || !text || text[0] == '\0')
    {
        return false;
    }

    const unsigned char* ptr = reinterpret_cast<const unsigned char*>(text);
    while (*ptr != 0)
    {
        uint32_t codepoint = 0;
        if (!decode_next_utf8(ptr, codepoint))
        {
            return false;
        }
        if (codepoint_requires_no_glyph(codepoint))
        {
            continue;
        }
        lv_font_glyph_dsc_t glyph{};
        if (!lv_font_get_glyph_dsc(font, &glyph, codepoint, 0))
        {
            return false;
        }
    }
    return true;
}

void apply_candidate_button_style(lv_obj_t* button, bool active)
{
    if (!button)
    {
        return;
    }
    lv_obj_set_style_radius(button, 8, LV_PART_MAIN);
    lv_obj_set_style_bg_color(button,
                              active ? lv_color_hex(kAmber) : lv_color_hex(kPanelBg),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(button, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(button,
                                  active ? lv_color_hex(kAmberDark) : lv_color_hex(kLine),
                                  LV_PART_MAIN);
    lv_obj_set_style_outline_width(button, active ? 2 : 0, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_color(button, lv_color_hex(kAmberDark), LV_STATE_FOCUSED);
    lv_obj_set_style_outline_pad(button, 1, LV_STATE_FOCUSED);
}

void apply_ime_button_style(lv_obj_t* button)
{
    if (!button)
    {
        return;
    }
    lv_obj_set_style_radius(button, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, lv_color_hex(kPanelBg), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(button, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(button, lv_color_hex(kLine), LV_PART_MAIN);
    lv_obj_set_style_outline_width(button, 0, LV_STATE_FOCUSED);
}

void copy_toolbar_button_style(lv_obj_t* button, lv_obj_t* reference)
{
    if (!button || !reference || !lv_obj_is_valid(reference))
    {
        apply_ime_button_style(button);
        return;
    }

    lv_obj_set_style_radius(button,
                            lv_obj_get_style_radius(reference, LV_PART_MAIN),
                            LV_PART_MAIN);
    lv_obj_set_style_bg_color(button,
                              lv_obj_get_style_bg_color(reference, LV_PART_MAIN),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button,
                            lv_obj_get_style_bg_opa(reference, LV_PART_MAIN),
                            LV_PART_MAIN);
    lv_obj_set_style_border_width(button,
                                  lv_obj_get_style_border_width(reference, LV_PART_MAIN),
                                  LV_PART_MAIN);
    lv_obj_set_style_border_color(button,
                                  lv_obj_get_style_border_color(reference, LV_PART_MAIN),
                                  LV_PART_MAIN);
    lv_obj_set_style_border_opa(button,
                                lv_obj_get_style_border_opa(reference, LV_PART_MAIN),
                                LV_PART_MAIN);
    lv_obj_set_style_pad_top(button,
                             lv_obj_get_style_pad_top(reference, LV_PART_MAIN),
                             LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(button,
                                lv_obj_get_style_pad_bottom(reference, LV_PART_MAIN),
                                LV_PART_MAIN);
    lv_obj_set_style_pad_left(button,
                              lv_obj_get_style_pad_left(reference, LV_PART_MAIN),
                              LV_PART_MAIN);
    lv_obj_set_style_pad_right(button,
                               lv_obj_get_style_pad_right(reference, LV_PART_MAIN),
                               LV_PART_MAIN);
    lv_obj_set_style_outline_width(button,
                                   lv_obj_get_style_outline_width(
                                       reference,
                                       static_cast<lv_part_t>(LV_STATE_FOCUSED)),
                                   LV_STATE_FOCUSED);
    lv_obj_set_style_outline_color(button,
                                   lv_obj_get_style_outline_color(
                                       reference,
                                       static_cast<lv_part_t>(LV_STATE_FOCUSED)),
                                   LV_STATE_FOCUSED);
    lv_obj_set_style_outline_pad(button,
                                 lv_obj_get_style_outline_pad(
                                     reference,
                                     static_cast<lv_part_t>(LV_STATE_FOCUSED)),
                                 LV_STATE_FOCUSED);
}

lv_obj_t* first_child(lv_obj_t* obj)
{
    return obj && lv_obj_is_valid(obj) ? lv_obj_get_child(obj, 0) : nullptr;
}

const lv_font_t* button_label_font(lv_obj_t* button)
{
    if (lv_obj_t* label = first_child(button))
    {
        return lv_obj_get_style_text_font(label, LV_PART_MAIN);
    }
    return button && lv_obj_is_valid(button)
               ? lv_obj_get_style_text_font(button, LV_PART_MAIN)
               : nullptr;
}

lv_color_t button_label_color(lv_obj_t* button)
{
    if (lv_obj_t* label = first_child(button))
    {
        return lv_obj_get_style_text_color(label, LV_PART_MAIN);
    }
    return button && lv_obj_is_valid(button)
               ? lv_obj_get_style_text_color(button, LV_PART_MAIN)
               : lv_color_hex(kText);
}

lv_coord_t object_width_hint(lv_obj_t* obj)
{
    if (!obj || !lv_obj_is_valid(obj))
    {
        return 0;
    }
    lv_coord_t width = lv_obj_get_width(obj);
    if (width > 0)
    {
        return width;
    }
    width = static_cast<lv_coord_t>(lv_obj_get_style_width(obj, LV_PART_MAIN));
    return width > 0 ? width : 0;
}

lv_coord_t object_height_hint(lv_obj_t* obj)
{
    if (!obj || !lv_obj_is_valid(obj))
    {
        return 0;
    }
    lv_coord_t height = lv_obj_get_height(obj);
    if (height > 0)
    {
        return height;
    }
    height = static_cast<lv_coord_t>(lv_obj_get_style_height(obj, LV_PART_MAIN));
    return height > 0 ? height : 0;
}

void set_candidate_button_label(lv_obj_t* button,
                                const char* text,
                                const lv_font_t* font = nullptr,
                                lv_color_t color = lv_color_hex(kText))
{
    if (!button)
    {
        return;
    }
    lv_obj_t* label = lv_obj_get_child(button, 0);
    if (!label)
    {
        label = lv_label_create(button);
    }
    ::ui::i18n::set_content_label_text_raw(label, text ? text : "");
    lv_obj_set_width(label, LV_SIZE_CONTENT);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, color, LV_PART_MAIN);
    lv_obj_set_style_text_font(label,
                               font ? font : ::ui::fonts::localized_font(::ui::fonts::ui_chrome_font()),
                               LV_PART_MAIN);
    lv_obj_center(label);
}

void set_toolbar_button_label(lv_obj_t* button,
                              const char* text,
                              const lv_font_t* font = nullptr,
                              lv_color_t color = lv_color_hex(kText))
{
    if (!button)
    {
        return;
    }
    lv_obj_t* label = lv_obj_get_child(button, 0);
    if (!label)
    {
        label = lv_label_create(button);
    }
    ::ui::i18n::set_label_text_raw(label, text ? text : "");
    lv_obj_set_width(label, LV_SIZE_CONTENT);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, color, LV_PART_MAIN);
    lv_obj_set_style_text_font(label,
                               font ? font : ::ui::fonts::localized_font(::ui::fonts::ui_chrome_font()),
                               LV_PART_MAIN);
    lv_obj_center(label);
}

void close_picker(bool restore_focus)
{
    lv_obj_t* textarea = s_picker.textarea;
    lv_group_t* group = s_picker.group;
    lv_group_t* previous_group = s_picker.previous_group;
    lv_obj_t* root = s_picker.root;

    s_picker = PickerState{};

    if (previous_group != nullptr)
    {
        set_default_group(previous_group);
    }
    else if (lv_group_get_default() == group)
    {
        set_default_group(nullptr);
    }
    if (group)
    {
        lv_group_del(group);
    }
    if (root)
    {
        lv_obj_del_async(root);
    }
    if (restore_focus && textarea && lv_obj_is_valid(textarea))
    {
        lv_obj_add_state(textarea, LV_STATE_FOCUSED);
        if (lv_group_t* g = lv_group_get_default())
        {
            lv_group_focus_obj(textarea);
            lv_group_set_editing(g, false);
        }
    }
}

void refresh_active_button()
{
    for (std::size_t i = 0; i < s_picker.candidate_count; ++i)
    {
        apply_candidate_button_style(s_picker.buttons[i], i == s_picker.active);
    }
}

const char* candidate_text_at(std::size_t filtered_index)
{
    if (filtered_index >= s_picker.candidate_count)
    {
        return nullptr;
    }
    return text_candidates::at(
        s_picker.set,
        s_picker.candidate_indices[filtered_index]);
}

void refresh_candidates()
{
    if (s_picker.active >= s_picker.candidate_count)
    {
        s_picker.active = s_picker.candidate_count == 0 ? 0 : s_picker.candidate_count - 1U;
    }

    for (std::size_t slot = 0; slot < s_picker.buttons.size(); ++slot)
    {
        lv_obj_t* button = s_picker.buttons[slot];
        if (!button)
        {
            continue;
        }
        if (slot >= s_picker.candidate_count)
        {
            lv_obj_add_flag(button, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        const char* candidate = candidate_text_at(slot);
        if (!candidate || candidate[0] == '\0')
        {
            lv_obj_add_flag(button, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_clear_flag(button, LV_OBJ_FLAG_HIDDEN);
        set_candidate_button_label(button, candidate, s_picker.candidate_font);
        apply_candidate_button_style(button, slot == s_picker.active);
    }
}

void focus_candidate(std::size_t index)
{
    if (s_picker.candidate_count == 0)
    {
        return;
    }
    if (index >= s_picker.candidate_count)
    {
        index = s_picker.candidate_count - 1;
    }
    s_picker.active = index;
    refresh_active_button();
    const std::size_t slot = s_picker.active;
    if (slot < s_picker.candidate_count && s_picker.buttons[slot])
    {
        lv_group_focus_obj(s_picker.buttons[slot]);
        if (lv_obj_t* grid = lv_obj_get_parent(s_picker.buttons[slot]))
        {
            if (static_cast<int>(slot) < s_picker.columns)
            {
                lv_obj_scroll_to_y(grid, 0, LV_ANIM_OFF);
            }
            else
            {
                lv_obj_scroll_to_view(s_picker.buttons[slot], LV_ANIM_OFF);
            }
        }
    }
}

void commit_active_candidate()
{
    if (!s_picker.textarea || !lv_obj_is_valid(s_picker.textarea))
    {
        close_picker(false);
        return;
    }
    const char* candidate = candidate_text_at(s_picker.active);
    if (!candidate || candidate[0] == '\0')
    {
        return;
    }
    lv_textarea_add_text(s_picker.textarea, candidate);
    const char* text = lv_textarea_get_text(s_picker.textarea);
    ::ui::fonts::apply_content_font(s_picker.textarea,
                                    text ? text : "",
                                    ::ui::fonts::ui_chrome_font());
    lv_obj_send_event(s_picker.textarea, LV_EVENT_VALUE_CHANGED, nullptr);
    close_picker(true);
}

int picker_columns()
{
    lv_coord_t width = 0;
    if (s_picker.root)
    {
        lv_obj_update_layout(s_picker.root);
        width = lv_obj_get_width(s_picker.root);
    }
    if (width >= 700)
    {
        return 10;
    }
    if (width >= 420)
    {
        return 8;
    }
    if (width >= 300)
    {
        return 6;
    }
    return 5;
}

void move_active(int delta)
{
    if (s_picker.candidate_count == 0)
    {
        return;
    }
    int next = static_cast<int>(s_picker.active) + delta;
    if (next < 0)
    {
        next = 0;
    }
    const int last = static_cast<int>(s_picker.candidate_count - 1U);
    if (next > last)
    {
        next = last;
    }
    focus_candidate(static_cast<std::size_t>(next));
}

void on_picker_key(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_KEY)
    {
        return;
    }
    const uint32_t key = lv_event_get_key(e);
    if (key == LV_KEY_ESC || key == LV_KEY_BACKSPACE || key == 'q' || key == 'Q')
    {
        close_picker(true);
        lv_event_stop_processing(e);
        return;
    }
    if (key == LV_KEY_ENTER || key == 'e' || key == 'E' || key == ' ')
    {
        commit_active_candidate();
        lv_event_stop_processing(e);
        return;
    }
    if (key == LV_KEY_LEFT || key == 'a' || key == 'A')
    {
        move_active(-1);
        lv_event_stop_processing(e);
        return;
    }
    if (key == LV_KEY_RIGHT || key == 'd' || key == 'D')
    {
        move_active(1);
        lv_event_stop_processing(e);
        return;
    }
    if (key == LV_KEY_UP || key == 'w' || key == 'W')
    {
        move_active(-picker_columns());
        lv_event_stop_processing(e);
        return;
    }
    if (key == LV_KEY_DOWN || key == 's' || key == 'S')
    {
        move_active(picker_columns());
        lv_event_stop_processing(e);
    }
}

void on_candidate_clicked(lv_event_t* e)
{
    lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
    const intptr_t raw = reinterpret_cast<intptr_t>(lv_obj_get_user_data(target));
    if (raw < 0 || static_cast<std::size_t>(raw) >= s_picker.candidate_count)
    {
        return;
    }
    s_picker.active = static_cast<std::size_t>(raw);
    commit_active_candidate();
}

void on_candidate_focused(lv_event_t* e)
{
    lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
    const intptr_t raw = reinterpret_cast<intptr_t>(lv_obj_get_user_data(target));
    if (raw >= 0 && static_cast<std::size_t>(raw) < s_picker.candidate_count)
    {
        s_picker.active = static_cast<std::size_t>(raw);
        refresh_active_button();
    }
}

void on_close_clicked(lv_event_t*)
{
    close_picker(true);
}

void on_toolbar_button_clicked(lv_event_t* e)
{
    lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
    lv_obj_t* textarea = static_cast<lv_obj_t*>(lv_obj_get_user_data(target));
    const auto set = static_cast<text_candidates::CandidateSet>(
        static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e))));
    open_text_candidate_picker(textarea, set);
}

void style_toolbar_button(lv_obj_t* button, lv_obj_t* reference = nullptr)
{
    copy_toolbar_button_style(button, reference);
}

} // namespace

void open_text_candidate_picker(lv_obj_t* textarea,
                                text_candidates::CandidateSet set)
{
    if (!textarea || !lv_obj_is_valid(textarea))
    {
        return;
    }
    if (s_picker.root)
    {
        close_picker(false);
    }

    lv_obj_t* parent = lv_layer_top();
    if (!parent)
    {
        parent = lv_screen_active();
    }
    if (!parent)
    {
        return;
    }

    s_picker.textarea = textarea;
    s_picker.set = set;
    s_picker.previous_group = lv_group_get_default();
    s_picker.group = lv_group_create();
    set_default_group(s_picker.group);

    lv_coord_t screen_w = lv_display_get_horizontal_resolution(nullptr);
    lv_coord_t screen_h = lv_display_get_vertical_resolution(nullptr);
    if (screen_w <= 0)
    {
        screen_w = 1;
    }
    if (screen_h <= 0)
    {
        screen_h = 1;
    }
    const lv_coord_t header_h = std::min(kHeaderHeightPx, screen_h);
    const lv_coord_t body_h = std::max<lv_coord_t>(1, screen_h - header_h);

    s_picker.root = lv_obj_create(parent);
    lv_obj_set_pos(s_picker.root, 0, 0);
    lv_obj_set_size(s_picker.root, screen_w, screen_h);
    lv_obj_set_style_bg_color(s_picker.root, lv_color_hex(kWarmBg), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_picker.root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_picker.root, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_picker.root, 0, LV_PART_MAIN);
    lv_obj_set_layout(s_picker.root, LV_LAYOUT_NONE);
    lv_obj_clear_flag(s_picker.root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_picker.root, on_picker_key, LV_EVENT_KEY, nullptr);

    const lv_coord_t content_w =
        std::max<lv_coord_t>(1, screen_w - static_cast<lv_coord_t>(kPickerOuterPaddingPx * 2));

    lv_obj_t* header = lv_obj_create(s_picker.root);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_size(header, screen_w, header_h);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header,
                          LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(header, lv_color_hex(kPanelBg), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(header, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_top(header, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(header, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_left(header, kPickerOuterPaddingPx, LV_PART_MAIN);
    lv_obj_set_style_pad_right(header, kPickerOuterPaddingPx, LV_PART_MAIN);
    lv_obj_set_style_pad_column(header, 8, LV_PART_MAIN);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* hint_label = lv_label_create(header);
    ::ui::i18n::set_label_text_raw(hint_label, "WASD move  Q close  E pick");
    lv_obj_set_flex_grow(hint_label, 1);
    lv_obj_set_height(hint_label, LV_PCT(100));
    lv_label_set_long_mode(hint_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(hint_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(hint_label,
                               ::ui::page_profile::resolve_caption_font(),
                               LV_PART_MAIN);
    lv_obj_set_style_text_color(hint_label, lv_color_hex(kTextDim), LV_PART_MAIN);

    lv_obj_t* close_btn = lv_btn_create(header);
    lv_obj_set_size(close_btn,
                    ::ui::page_profile::resolve_compact_button_min_width(),
                    kHeaderCloseButtonHeightPx);
    style_toolbar_button(close_btn);
    set_toolbar_button_label(close_btn, "Close");
    lv_obj_clear_flag(close_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(close_btn, on_close_clicked, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(close_btn, on_picker_key, LV_EVENT_KEY, nullptr);

    lv_obj_t* grid = lv_obj_create(s_picker.root);
    lv_obj_set_pos(grid, 0, header_h);
    lv_obj_set_size(grid, screen_w, body_h);
    lv_obj_set_layout(grid, LV_LAYOUT_NONE);
    lv_obj_set_scroll_dir(grid, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(grid, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(grid, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_left(grid, kPickerOuterPaddingPx, LV_PART_MAIN);
    lv_obj_set_style_pad_right(grid, kPickerOuterPaddingPx, LV_PART_MAIN);
    lv_obj_set_style_pad_top(grid, kGridTopPaddingPx, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(grid, kGridBottomPaddingPx, LV_PART_MAIN);
    lv_obj_set_style_pad_row(grid, kGridGapPx, LV_PART_MAIN);
    lv_obj_set_style_pad_column(grid, kGridGapPx, LV_PART_MAIN);
    lv_obj_add_event_cb(grid, on_picker_key, LV_EVENT_KEY, nullptr);

    const int columns = picker_columns();
    const lv_coord_t cell_w =
        std::max<lv_coord_t>(34,
                             (content_w -
                              ((columns - 1) * kGridGapPx)) /
                                 columns);
    const lv_coord_t cell_h = std::max<lv_coord_t>(
        ::ui::page_profile::resolve_control_button_height(),
        set == text_candidates::CandidateSet::Emoji ? 42 : 32);
    const lv_font_t* button_font =
        ::ui::fonts::localized_font(::ui::fonts::FontScope::Content,
                                    nullptr,
                                    ::ui::fonts::ui_chrome_font());
    s_picker.candidate_font = button_font;
    s_picker.columns = columns;

    const std::size_t raw_candidate_count = std::min<std::size_t>(
        text_candidates::count(set),
        text_candidates::kMaxBuiltinTextCandidates);
    for (std::size_t index = 0; index < raw_candidate_count; ++index)
    {
        const char* candidate = text_candidates::at(set, index);
        if (!candidate || candidate[0] == '\0')
        {
            continue;
        }
        if (!font_supports_text(button_font, candidate))
        {
            continue;
        }
        s_picker.candidate_indices[s_picker.candidate_count++] = index;
    }

#if TRAIL_MATE_TEXT_CANDIDATE_LAYOUT_LOG
    std::printf("[TEXT_CANDIDATE] layout set=%s screen=%dx%d header=%d body_y=%d body_h=%d columns=%d raw=%u filtered=%u\n",
                text_candidates::title(set),
                static_cast<int>(screen_w),
                static_cast<int>(screen_h),
                static_cast<int>(header_h),
                static_cast<int>(header_h),
                static_cast<int>(body_h),
                columns,
                static_cast<unsigned>(raw_candidate_count),
                static_cast<unsigned>(s_picker.candidate_count));
#endif

    for (std::size_t slot = 0; slot < s_picker.candidate_count; ++slot)
    {
        lv_obj_t* button = lv_btn_create(grid);
        const lv_coord_t col = static_cast<lv_coord_t>(slot % static_cast<std::size_t>(columns));
        const lv_coord_t row = static_cast<lv_coord_t>(slot / static_cast<std::size_t>(columns));
        lv_obj_set_pos(button,
                       kPickerOuterPaddingPx + col * (cell_w + kGridGapPx),
                       kGridTopPaddingPx + row * (cell_h + kGridGapPx));
        lv_obj_set_size(button, cell_w, cell_h);
        lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_user_data(button, reinterpret_cast<void*>(static_cast<intptr_t>(slot)));
        apply_candidate_button_style(button, slot == 0);
        set_candidate_button_label(button, "", button_font);
        lv_obj_add_event_cb(button, on_candidate_clicked, LV_EVENT_CLICKED, nullptr);
        lv_obj_add_event_cb(button, on_candidate_focused, LV_EVENT_FOCUSED, nullptr);
        lv_obj_add_event_cb(button, on_picker_key, LV_EVENT_KEY, nullptr);
        lv_group_add_obj(s_picker.group, button);
        s_picker.buttons[slot] = button;
    }

    refresh_candidates();
    lv_obj_update_layout(s_picker.root);
    lv_obj_scroll_to_y(grid, 0, LV_ANIM_OFF);
    focus_candidate(0);
}

lv_obj_t* add_text_candidate_button(lv_obj_t* toolbar,
                                    lv_obj_t* textarea,
                                    text_candidates::CandidateSet set,
                                    lv_group_t* group,
                                    lv_obj_t* reference_button)
{
    if (!toolbar || !textarea)
    {
        return nullptr;
    }
    const auto& profile = ::ui::page_profile::current();
    lv_coord_t height = object_height_hint(reference_button);
    if (height <= 0)
    {
        height = std::max(profile.ime_toggle_height,
                          ::ui::page_profile::resolve_control_button_height());
    }
    lv_coord_t width = object_width_hint(reference_button);
    if (width <= 0)
    {
        width = std::max<lv_coord_t>(48, profile.ime_toggle_width);
    }
    width = set == text_candidates::CandidateSet::Emoji
                ? std::max<lv_coord_t>(width + 24, 76)
                : std::max<lv_coord_t>(width + 10, 58);

    lv_obj_t* button = lv_btn_create(toolbar);
    lv_obj_set_size(button, width, height);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_user_data(button, textarea);
    style_toolbar_button(button, reference_button);
    set_toolbar_button_label(button,
                             text_candidates::button_label(set),
                             button_label_font(reference_button),
                             button_label_color(reference_button));
    lv_obj_add_event_cb(button,
                        on_toolbar_button_clicked,
                        LV_EVENT_CLICKED,
                        reinterpret_cast<void*>(
                            static_cast<intptr_t>(static_cast<int>(set))));
    if (group)
    {
        lv_group_add_obj(group, button);
    }
    return button;
}

} // namespace ui::widgets
