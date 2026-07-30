#include "ui/widgets/text_candidate_picker.h"

#include "ui/assets/fonts/font_utils.h"
#include "ui/localization.h"
#include "ui/page/page_profile.h"
#include "ui/ui_common.h"
#include "ui_lvgl_core/lvgl_focus_group.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace ui::widgets
{
namespace
{

constexpr std::size_t kCandidateRows = 4;
constexpr std::size_t kCategoryColumns = 3;
constexpr std::size_t kCategoryRows = 3;
constexpr std::size_t kMaxCandidateButtons = 10 * kCandidateRows;
constexpr lv_coord_t kHeaderHeightPx = 30;
constexpr lv_coord_t kFooterHeightPx = 30;
constexpr lv_coord_t kHeaderCloseButtonHeightPx = 24;
constexpr lv_coord_t kHeaderActionButtonWidthPx = 68;
constexpr lv_coord_t kFooterButtonHeightPx = 24;
constexpr lv_coord_t kFooterButtonWidthPx = 82;
constexpr lv_coord_t kShortcutKeyWidthPx = 18;
constexpr lv_coord_t kShortcutKeyHeightPx = 18;
constexpr lv_coord_t kShortcutKeyOffsetPx = 3;
constexpr lv_coord_t kShortcutActionOffsetPx = 25;
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

enum class PickerView : uint8_t
{
    Candidates,
    Categories,
};

struct PickerState
{
    lv_obj_t* root = nullptr;
    lv_obj_t* textarea = nullptr;
    lv_group_t* group = nullptr;
    lv_group_t* previous_group = nullptr;
    lv_obj_t* grid = nullptr;
    lv_obj_t* hint_label = nullptr;
    lv_obj_t* category_btn = nullptr;
    lv_obj_t* previous_btn = nullptr;
    lv_obj_t* next_btn = nullptr;
    lv_obj_t* page_label = nullptr;
    text_candidates::CandidateSet set = text_candidates::CandidateSet::Symbols;
    PickerView view = PickerView::Candidates;
    std::array<lv_obj_t*, kMaxCandidateButtons> buttons{};
    std::size_t button_count = 0;
    std::size_t active = 0;
    std::size_t category = 0;
    std::size_t page = 0;
    std::size_t page_capacity = 1;
    int columns = 1;
    const lv_font_t* candidate_font = nullptr;
};

PickerState s_picker;

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

void apply_shortcut_button_style(lv_obj_t* button)
{
    if (!button)
    {
        return;
    }
    lv_obj_set_style_radius(button, 6, LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, lv_color_hex(kWarmBg), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(button, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(button, lv_color_hex(kLine), LV_PART_MAIN);
    lv_obj_set_style_pad_all(button, 0, LV_PART_MAIN);
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
    lv_obj_clear_flag(label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_center(label);

    if (lv_obj_t* category_label = lv_obj_get_child(button, 1))
    {
        lv_obj_add_flag(category_label, LV_OBJ_FLAG_HIDDEN);
    }
}

void set_category_button_label(lv_obj_t* button,
                               const text_candidates::EmojiCategoryInfo& category)
{
    if (!button)
    {
        return;
    }

    lv_obj_t* title = lv_obj_get_child(button, 0);
    if (!title)
    {
        title = lv_label_create(button);
    }

    ::ui::i18n::set_label_text_raw(title, category.title ? category.title : "");
    lv_obj_set_width(title, LV_SIZE_CONTENT);
    lv_label_set_long_mode(title, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_hex(kText), LV_PART_MAIN);
    lv_obj_set_style_text_font(title,
                               ::ui::page_profile::resolve_caption_font(),
                               LV_PART_MAIN);
    lv_obj_clear_flag(title, LV_OBJ_FLAG_HIDDEN);
    lv_obj_center(title);

    if (lv_obj_t* secondary_label = lv_obj_get_child(button, 1))
    {
        lv_obj_add_flag(secondary_label, LV_OBJ_FLAG_HIDDEN);
    }
}

void set_shortcut_button_label(lv_obj_t* button,
                               const char* key,
                               const char* action)
{
    if (!button)
    {
        return;
    }
    lv_obj_t* key_label = lv_obj_get_child(button, 0);
    if (!key_label)
    {
        key_label = lv_label_create(button);
    }
    lv_obj_t* action_label = lv_obj_get_child(button, 1);
    if (!action_label)
    {
        action_label = lv_label_create(button);
    }

    ::ui::i18n::set_label_text_raw(key_label, key ? key : "");
    lv_obj_set_size(key_label, kShortcutKeyWidthPx, kShortcutKeyHeightPx);
    lv_label_set_long_mode(key_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(key_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(key_label,
                               ::ui::page_profile::resolve_caption_font(),
                               LV_PART_MAIN);
    lv_obj_set_style_text_color(key_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_bg_color(key_label, lv_color_hex(kAmberDark), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(key_label, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(key_label, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_all(key_label, 0, LV_PART_MAIN);
    lv_obj_align(key_label, LV_ALIGN_LEFT_MID, kShortcutKeyOffsetPx, 0);

    ::ui::i18n::set_label_text_raw(action_label, action ? action : "");
    lv_obj_set_width(action_label, LV_SIZE_CONTENT);
    lv_label_set_long_mode(action_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(action_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    lv_obj_set_style_text_font(action_label,
                               ::ui::page_profile::resolve_caption_font(),
                               LV_PART_MAIN);
    lv_obj_set_style_text_color(action_label, lv_color_hex(kText), LV_PART_MAIN);
    lv_obj_align(action_label, LV_ALIGN_LEFT_MID, kShortcutActionOffsetPx, 0);
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

bool is_emoji_picker()
{
    return s_picker.set == text_candidates::CandidateSet::Emoji;
}

const text_candidates::EmojiCategoryInfo* current_category()
{
    if (!is_emoji_picker())
    {
        return nullptr;
    }
    const std::size_t count = text_candidates::emoji_category_count();
    if (count == 0)
    {
        return nullptr;
    }
    if (s_picker.category >= count)
    {
        s_picker.category = count - 1U;
    }
    return text_candidates::emoji_category_at(s_picker.category);
}

std::size_t current_candidate_count()
{
    if (const auto* category = current_category())
    {
        return category->count;
    }
    return text_candidates::count(s_picker.set);
}

std::size_t current_page_count()
{
    const std::size_t capacity = std::max<std::size_t>(1, s_picker.page_capacity);
    const std::size_t candidates = current_candidate_count();
    return std::max<std::size_t>(1, (candidates + capacity - 1U) / capacity);
}

std::size_t current_page_candidate_count()
{
    const std::size_t candidates = current_candidate_count();
    const std::size_t capacity = std::max<std::size_t>(1, s_picker.page_capacity);
    const std::size_t pages = current_page_count();
    if (s_picker.page >= pages)
    {
        s_picker.page = pages - 1U;
    }
    const std::size_t offset = s_picker.page * capacity;
    return offset < candidates ? std::min(capacity, candidates - offset) : 0;
}

const char* candidate_text_at(std::size_t slot)
{
    const std::size_t visible = current_page_candidate_count();
    if (slot >= visible)
    {
        return nullptr;
    }
    const std::size_t index = s_picker.page * s_picker.page_capacity + slot;
    if (is_emoji_picker())
    {
        return text_candidates::emoji_at(s_picker.category, index);
    }
    return text_candidates::at(s_picker.set, index);
}

void refresh_active_button()
{
    for (std::size_t i = 0; i < s_picker.button_count; ++i)
    {
        apply_candidate_button_style(s_picker.buttons[i], i == s_picker.active);
    }
}

void set_footer_status_layout(bool use_full_width)
{
    if (!s_picker.page_label)
    {
        return;
    }
    lv_obj_t* footer = lv_obj_get_parent(s_picker.page_label);
    if (!footer)
    {
        return;
    }
    const lv_coord_t footer_width = lv_obj_get_width(footer);
    const lv_coord_t inset = use_full_width
                                 ? kPickerOuterPaddingPx
                                 : kPickerOuterPaddingPx + kFooterButtonWidthPx + kGridGapPx;
    lv_obj_set_pos(s_picker.page_label, inset, 0);
    lv_obj_set_size(s_picker.page_label,
                    std::max<lv_coord_t>(1, footer_width - static_cast<lv_coord_t>(inset * 2)),
                    lv_obj_get_height(footer));
}

void refresh_header_and_footer()
{
    if (s_picker.hint_label)
    {
        char title[96]{};
        if (s_picker.view == PickerView::Categories)
        {
            std::snprintf(title, sizeof(title), "Emoji categories");
        }
        else if (const auto* category = current_category())
        {
            std::snprintf(title,
                          sizeof(title),
                          "%s / %s  %u/%u",
                          text_candidates::title(s_picker.set),
                          category->title,
                          static_cast<unsigned>(s_picker.page + 1U),
                          static_cast<unsigned>(current_page_count()));
        }
        else
        {
            std::snprintf(title,
                          sizeof(title),
                          "%s  %u/%u",
                          text_candidates::title(s_picker.set),
                          static_cast<unsigned>(s_picker.page + 1U),
                          static_cast<unsigned>(current_page_count()));
        }
        ::ui::i18n::set_label_text_raw(s_picker.hint_label, title);
    }

    if (s_picker.category_btn)
    {
        if (is_emoji_picker())
        {
            lv_obj_clear_flag(s_picker.category_btn, LV_OBJ_FLAG_HIDDEN);
            set_shortcut_button_label(s_picker.category_btn,
                                      "C",
                                      s_picker.view == PickerView::Categories ? "Back" : "Cats");
        }
        else
        {
            lv_obj_add_flag(s_picker.category_btn, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (s_picker.view == PickerView::Categories)
    {
        if (s_picker.previous_btn)
        {
            lv_obj_add_flag(s_picker.previous_btn, LV_OBJ_FLAG_HIDDEN);
        }
        if (s_picker.next_btn)
        {
            lv_obj_add_flag(s_picker.next_btn, LV_OBJ_FLAG_HIDDEN);
        }
        if (s_picker.page_label)
        {
            set_footer_status_layout(true);
            ::ui::i18n::set_label_text_raw(s_picker.page_label, "WASD Move   E Select");
        }
        return;
    }

    const std::size_t pages = current_page_count();
    if (s_picker.previous_btn)
    {
        lv_obj_clear_flag(s_picker.previous_btn, LV_OBJ_FLAG_HIDDEN);
        if (s_picker.page == 0)
        {
            lv_obj_add_state(s_picker.previous_btn, LV_STATE_DISABLED);
        }
        else
        {
            lv_obj_clear_state(s_picker.previous_btn, LV_STATE_DISABLED);
        }
    }
    if (s_picker.next_btn)
    {
        lv_obj_clear_flag(s_picker.next_btn, LV_OBJ_FLAG_HIDDEN);
        if (s_picker.page + 1U >= pages)
        {
            lv_obj_add_state(s_picker.next_btn, LV_STATE_DISABLED);
        }
        else
        {
            lv_obj_clear_state(s_picker.next_btn, LV_STATE_DISABLED);
        }
    }
    if (s_picker.page_label)
    {
        set_footer_status_layout(false);
        char page_text[32]{};
        std::snprintf(page_text,
                      sizeof(page_text),
                      "Page %u / %u",
                      static_cast<unsigned>(s_picker.page + 1U),
                      static_cast<unsigned>(pages));
        ::ui::i18n::set_label_text_raw(s_picker.page_label, page_text);
    }
}

void layout_visible_buttons()
{
    if (!s_picker.grid)
    {
        return;
    }
    lv_obj_update_layout(s_picker.grid);
    const int columns = s_picker.view == PickerView::Categories ? kCategoryColumns : s_picker.columns;
    const std::size_t rows =
        s_picker.view == PickerView::Categories ? kCategoryRows : kCandidateRows;
    const lv_coord_t grid_width = lv_obj_get_width(s_picker.grid);
    const lv_coord_t grid_height = lv_obj_get_height(s_picker.grid);
    const lv_coord_t content_width =
        std::max<lv_coord_t>(1, grid_width - static_cast<lv_coord_t>(kPickerOuterPaddingPx * 2));
    const lv_coord_t content_height = std::max<lv_coord_t>(
        1,
        grid_height - kGridTopPaddingPx - kGridBottomPaddingPx -
            static_cast<lv_coord_t>((rows - 1U) * kGridGapPx));
    const lv_coord_t cell_width = std::max<lv_coord_t>(
        1,
        (content_width - static_cast<lv_coord_t>((columns - 1) * kGridGapPx)) / columns);
    const lv_coord_t cell_height =
        std::max<lv_coord_t>(1, content_height / static_cast<lv_coord_t>(rows));

    for (std::size_t slot = 0; slot < s_picker.button_count; ++slot)
    {
        lv_obj_t* button = s_picker.buttons[slot];
        if (!button)
        {
            continue;
        }
        const lv_coord_t col = static_cast<lv_coord_t>(slot % static_cast<std::size_t>(columns));
        const lv_coord_t row = static_cast<lv_coord_t>(slot / static_cast<std::size_t>(columns));
        lv_obj_set_pos(button,
                       kPickerOuterPaddingPx + col * (cell_width + kGridGapPx),
                       kGridTopPaddingPx + row * (cell_height + kGridGapPx));
        lv_obj_set_size(button, cell_width, cell_height);
    }
}

void refresh_focus_group()
{
    if (!s_picker.group)
    {
        return;
    }
    lv_group_remove_all_objs(s_picker.group);
    for (std::size_t slot = 0; slot < s_picker.button_count; ++slot)
    {
        if (s_picker.buttons[slot])
        {
            lv_group_add_obj(s_picker.group, s_picker.buttons[slot]);
        }
    }
}

void refresh_candidates()
{
    if (s_picker.view == PickerView::Categories)
    {
        s_picker.button_count = std::min<std::size_t>(text_candidates::emoji_category_count(),
                                                      s_picker.buttons.size());
        s_picker.active = s_picker.button_count == 0
                              ? 0
                              : std::min(s_picker.active, s_picker.button_count - 1U);
        for (std::size_t slot = 0; slot < s_picker.buttons.size(); ++slot)
        {
            lv_obj_t* button = s_picker.buttons[slot];
            if (!button)
            {
                continue;
            }
            const auto* category = text_candidates::emoji_category_at(slot);
            if (slot >= s_picker.button_count || !category)
            {
                lv_obj_add_flag(button, LV_OBJ_FLAG_HIDDEN);
                continue;
            }
            lv_obj_clear_flag(button, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_user_data(button, reinterpret_cast<void*>(static_cast<intptr_t>(slot)));
            set_category_button_label(button, *category);
            apply_candidate_button_style(button, slot == s_picker.active);
        }
    }
    else
    {
        s_picker.button_count = std::min(current_page_candidate_count(), s_picker.buttons.size());
        s_picker.active = s_picker.button_count == 0
                              ? 0
                              : std::min(s_picker.active, s_picker.button_count - 1U);
        for (std::size_t slot = 0; slot < s_picker.buttons.size(); ++slot)
        {
            lv_obj_t* button = s_picker.buttons[slot];
            if (!button)
            {
                continue;
            }
            const char* candidate = candidate_text_at(slot);
            if (slot >= s_picker.button_count || !candidate || candidate[0] == '\0')
            {
                lv_obj_add_flag(button, LV_OBJ_FLAG_HIDDEN);
                continue;
            }
            lv_obj_clear_flag(button, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_user_data(button, reinterpret_cast<void*>(static_cast<intptr_t>(slot)));
            set_candidate_button_label(button, candidate, s_picker.candidate_font);
            apply_candidate_button_style(button, slot == s_picker.active);
        }
    }

    layout_visible_buttons();
    refresh_focus_group();
    refresh_header_and_footer();
}

void focus_candidate(std::size_t index)
{
    if (s_picker.button_count == 0)
    {
        return;
    }
    s_picker.active = std::min(index, s_picker.button_count - 1U);
    refresh_active_button();
    if (lv_obj_t* button = s_picker.buttons[s_picker.active])
    {
        lv_group_focus_obj(button);
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

int navigation_columns()
{
    return s_picker.view == PickerView::Categories ? kCategoryColumns : s_picker.columns;
}

void move_active(int delta)
{
    if (s_picker.button_count == 0)
    {
        return;
    }
    int next = static_cast<int>(s_picker.active) + delta;
    if (next < 0)
    {
        next = 0;
    }
    const int last = static_cast<int>(s_picker.button_count - 1U);
    if (next > last)
    {
        next = last;
    }
    focus_candidate(static_cast<std::size_t>(next));
}

void change_page(int delta)
{
    if (s_picker.view != PickerView::Candidates)
    {
        return;
    }
    const int last = static_cast<int>(current_page_count() - 1U);
    int next = static_cast<int>(s_picker.page) + delta;
    next = std::clamp(next, 0, last);
    if (static_cast<std::size_t>(next) == s_picker.page)
    {
        return;
    }
    s_picker.page = static_cast<std::size_t>(next);
    s_picker.active = 0;
    refresh_candidates();
    focus_candidate(0);
}

void toggle_category_view()
{
    if (!is_emoji_picker())
    {
        return;
    }
    s_picker.view = s_picker.view == PickerView::Categories ? PickerView::Candidates
                                                            : PickerView::Categories;
    s_picker.active = s_picker.view == PickerView::Categories ? s_picker.category : 0;
    refresh_candidates();
    focus_candidate(s_picker.active);
}

void select_active_item()
{
    if (s_picker.view == PickerView::Categories)
    {
        if (s_picker.active >= text_candidates::emoji_category_count())
        {
            return;
        }
        s_picker.category = s_picker.active;
        s_picker.page = 0;
        s_picker.active = 0;
        s_picker.view = PickerView::Candidates;
        refresh_candidates();
        focus_candidate(0);
        return;
    }
    commit_active_candidate();
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
        select_active_item();
        lv_event_stop_processing(e);
        return;
    }
    if ((key == 'c' || key == 'C') && is_emoji_picker())
    {
        toggle_category_view();
        lv_event_stop_processing(e);
        return;
    }
    if ((key == 'b' || key == 'B') && s_picker.view == PickerView::Candidates)
    {
        change_page(-1);
        lv_event_stop_processing(e);
        return;
    }
    if ((key == 'n' || key == 'N') && s_picker.view == PickerView::Candidates)
    {
        change_page(1);
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
        move_active(-navigation_columns());
        lv_event_stop_processing(e);
        return;
    }
    if (key == LV_KEY_DOWN || key == 's' || key == 'S')
    {
        move_active(navigation_columns());
        lv_event_stop_processing(e);
    }
}

void on_candidate_clicked(lv_event_t* e)
{
    lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
    const intptr_t raw = reinterpret_cast<intptr_t>(lv_obj_get_user_data(target));
    if (raw < 0 || static_cast<std::size_t>(raw) >= s_picker.button_count)
    {
        return;
    }
    s_picker.active = static_cast<std::size_t>(raw);
    select_active_item();
}

void on_candidate_focused(lv_event_t* e)
{
    lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
    const intptr_t raw = reinterpret_cast<intptr_t>(lv_obj_get_user_data(target));
    if (raw >= 0 && static_cast<std::size_t>(raw) < s_picker.button_count)
    {
        s_picker.active = static_cast<std::size_t>(raw);
        refresh_active_button();
    }
}

void on_close_clicked(lv_event_t*)
{
    close_picker(true);
}

void on_previous_page_clicked(lv_event_t*)
{
    change_page(-1);
}

void on_next_page_clicked(lv_event_t*)
{
    change_page(1);
}

void on_category_clicked(lv_event_t*)
{
    toggle_category_view();
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

    // This full-screen modal must belong to the active screen: the firmware's
    // screenshot helper snapshots that screen and LVGL does not include sibling
    // objects from lv_layer_top() in such a snapshot.
    lv_obj_t* parent = lv_screen_active();
    if (!parent)
    {
        parent = lv_layer_top();
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
    const lv_coord_t footer_h = std::min(kFooterHeightPx,
                                         std::max<lv_coord_t>(0, screen_h - header_h));
    const lv_coord_t body_h = std::max<lv_coord_t>(1, screen_h - header_h - footer_h);

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
    lv_obj_set_style_pad_column(header, 6, LV_PART_MAIN);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    s_picker.hint_label = lv_label_create(header);
    lv_obj_set_flex_grow(s_picker.hint_label, 1);
    lv_obj_set_height(s_picker.hint_label, LV_PCT(100));
    lv_label_set_long_mode(s_picker.hint_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(s_picker.hint_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_picker.hint_label,
                               ::ui::page_profile::resolve_caption_font(),
                               LV_PART_MAIN);
    lv_obj_set_style_text_color(s_picker.hint_label, lv_color_hex(kTextDim), LV_PART_MAIN);

    s_picker.category_btn = lv_btn_create(header);
    lv_obj_set_size(s_picker.category_btn, kHeaderActionButtonWidthPx, kHeaderCloseButtonHeightPx);
    apply_shortcut_button_style(s_picker.category_btn);
    lv_obj_clear_flag(s_picker.category_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_picker.category_btn, on_category_clicked, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(s_picker.category_btn, on_picker_key, LV_EVENT_KEY, nullptr);

    lv_obj_t* close_btn = lv_btn_create(header);
    lv_obj_set_size(close_btn, kHeaderActionButtonWidthPx, kHeaderCloseButtonHeightPx);
    apply_shortcut_button_style(close_btn);
    set_shortcut_button_label(close_btn, "Q", "Close");
    lv_obj_clear_flag(close_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(close_btn, on_close_clicked, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(close_btn, on_picker_key, LV_EVENT_KEY, nullptr);

    s_picker.grid = lv_obj_create(s_picker.root);
    lv_obj_set_pos(s_picker.grid, 0, header_h);
    lv_obj_set_size(s_picker.grid, screen_w, body_h);
    lv_obj_set_layout(s_picker.grid, LV_LAYOUT_NONE);
    lv_obj_clear_flag(s_picker.grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(s_picker.grid, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_picker.grid, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_picker.grid, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(s_picker.grid, on_picker_key, LV_EVENT_KEY, nullptr);

    lv_obj_t* footer = lv_obj_create(s_picker.root);
    lv_obj_set_pos(footer, 0, header_h + body_h);
    lv_obj_set_size(footer, screen_w, footer_h);
    lv_obj_set_layout(footer, LV_LAYOUT_NONE);
    lv_obj_set_style_bg_color(footer, lv_color_hex(kPanelBg), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(footer, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(footer, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(footer, 0, LV_PART_MAIN);
    lv_obj_clear_flag(footer, LV_OBJ_FLAG_SCROLLABLE);

    s_picker.previous_btn = lv_btn_create(footer);
    lv_obj_set_pos(s_picker.previous_btn, kPickerOuterPaddingPx, 3);
    lv_obj_set_size(s_picker.previous_btn, kFooterButtonWidthPx, kFooterButtonHeightPx);
    apply_shortcut_button_style(s_picker.previous_btn);
    set_shortcut_button_label(s_picker.previous_btn, "B", "Prev");
    lv_obj_clear_flag(s_picker.previous_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_picker.previous_btn, on_previous_page_clicked, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(s_picker.previous_btn, on_picker_key, LV_EVENT_KEY, nullptr);

    s_picker.next_btn = lv_btn_create(footer);
    lv_obj_set_pos(s_picker.next_btn,
                   std::max<lv_coord_t>(kPickerOuterPaddingPx,
                                        screen_w - kPickerOuterPaddingPx - kFooterButtonWidthPx),
                   3);
    lv_obj_set_size(s_picker.next_btn, kFooterButtonWidthPx, kFooterButtonHeightPx);
    apply_shortcut_button_style(s_picker.next_btn);
    set_shortcut_button_label(s_picker.next_btn, "N", "Next");
    lv_obj_clear_flag(s_picker.next_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_picker.next_btn, on_next_page_clicked, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(s_picker.next_btn, on_picker_key, LV_EVENT_KEY, nullptr);

    s_picker.page_label = lv_label_create(footer);
    lv_obj_set_pos(s_picker.page_label,
                   kPickerOuterPaddingPx + kFooterButtonWidthPx + kGridGapPx,
                   0);
    lv_obj_set_size(s_picker.page_label,
                    std::max<lv_coord_t>(1,
                                         screen_w -
                                             static_cast<lv_coord_t>(
                                                 2 * (kPickerOuterPaddingPx +
                                                      kFooterButtonWidthPx + kGridGapPx))),
                    footer_h);
    lv_label_set_long_mode(s_picker.page_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(s_picker.page_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_picker.page_label,
                               ::ui::page_profile::resolve_caption_font(),
                               LV_PART_MAIN);
    lv_obj_set_style_text_color(s_picker.page_label, lv_color_hex(kTextDim), LV_PART_MAIN);

    s_picker.columns = picker_columns();
    s_picker.page_capacity = static_cast<std::size_t>(s_picker.columns) * kCandidateRows;
    s_picker.candidate_font = ::ui::fonts::localized_font(::ui::fonts::FontScope::Content,
                                                          nullptr,
                                                          ::ui::fonts::ui_chrome_font());

    for (std::size_t slot = 0; slot < s_picker.buttons.size(); ++slot)
    {
        lv_obj_t* button = lv_btn_create(s_picker.grid);
        lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_user_data(button, reinterpret_cast<void*>(static_cast<intptr_t>(slot)));
        apply_candidate_button_style(button, false);
        set_candidate_button_label(button, "", s_picker.candidate_font);
        lv_obj_add_event_cb(button, on_candidate_clicked, LV_EVENT_CLICKED, nullptr);
        lv_obj_add_event_cb(button, on_candidate_focused, LV_EVENT_FOCUSED, nullptr);
        lv_obj_add_event_cb(button, on_picker_key, LV_EVENT_KEY, nullptr);
        s_picker.buttons[slot] = button;
    }

#if TRAIL_MATE_TEXT_CANDIDATE_LAYOUT_LOG
    std::printf("[TEXT_CANDIDATE] layout set=%s screen=%dx%d header=%d body=%d footer=%d columns=%d capacity=%u\n",
                text_candidates::title(set),
                static_cast<int>(screen_w),
                static_cast<int>(screen_h),
                static_cast<int>(header_h),
                static_cast<int>(body_h),
                static_cast<int>(footer_h),
                s_picker.columns,
                static_cast<unsigned>(s_picker.page_capacity));
#endif

    refresh_candidates();
    lv_obj_update_layout(s_picker.root);
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
