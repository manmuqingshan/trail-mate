#include "ui/components/screen_saver_overlay.h"

#include <cstdio>

#include "lvgl.h"
#include "sys/clock.h"
#include "ui/localization.h"

#if !defined(LV_FONT_MONTSERRAT_36) || !LV_FONT_MONTSERRAT_36
#if defined(LV_FONT_MONTSERRAT_28) && LV_FONT_MONTSERRAT_28
#define lv_font_montserrat_36 lv_font_montserrat_28
#elif defined(LV_FONT_MONTSERRAT_20) && LV_FONT_MONTSERRAT_20
#define lv_font_montserrat_36 lv_font_montserrat_20
#else
#define lv_font_montserrat_36 lv_font_montserrat_14
#endif
#endif

#if !defined(LV_FONT_MONTSERRAT_20) || !LV_FONT_MONTSERRAT_20
#define lv_font_montserrat_20 lv_font_montserrat_14
#endif

namespace ui::components::screen_saver_overlay
{
namespace
{

constexpr uint32_t kColorWarmBg = 0xF6E6C6;
constexpr uint32_t kColorText = 0x6B4A1E;
constexpr uint32_t kColorTextDim = 0x8A6A3A;

Hooks s_hooks{};
lv_obj_t* s_root = nullptr;
lv_obj_t* s_time_label = nullptr;
lv_obj_t* s_unread_label = nullptr;
lv_obj_t* s_hint_label = nullptr;

void set_label_texts(int unread)
{
    if (s_unread_label != nullptr)
    {
        ::ui::i18n::set_label_text_fmt(s_unread_label, "Unread: %d", unread);
    }
    if (s_hint_label != nullptr)
    {
        ::ui::i18n::set_label_text(s_hint_label, "Press SPACE to resume");
    }
}

void create_if_needed()
{
    if (s_root != nullptr)
    {
        return;
    }

    lv_obj_t* parent = lv_screen_active();
    if (parent == nullptr)
    {
        return;
    }

    s_root = lv_obj_create(parent);
    lv_obj_set_size(s_root, LV_PCT(100), LV_PCT(100));
    lv_obj_align(s_root, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(s_root, lv_color_hex(kColorWarmBg), 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_root, 0, 0);
    lv_obj_set_style_radius(s_root, 0, 0);
    lv_obj_set_style_pad_all(s_root, 0, 0);
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_CLICKABLE);

    s_time_label = lv_label_create(s_root);
    lv_obj_set_style_text_color(s_time_label, lv_color_hex(kColorText), 0);
    lv_obj_set_style_text_font(s_time_label, &lv_font_montserrat_36, 0);
    lv_label_set_text(s_time_label, "--:--");
    lv_obj_align(s_time_label, LV_ALIGN_CENTER, 0, -26);

    s_unread_label = lv_label_create(s_root);
    lv_obj_set_style_text_color(s_unread_label, lv_color_hex(kColorText), 0);
    lv_obj_set_style_text_font(s_unread_label, &lv_font_montserrat_20, 0);
    lv_obj_align(s_unread_label, LV_ALIGN_CENTER, 0, 10);

    s_hint_label = lv_label_create(s_root);
    lv_obj_set_style_text_color(s_hint_label, lv_color_hex(kColorTextDim), 0);
    lv_obj_set_style_text_font(s_hint_label, &lv_font_montserrat_14, 0);
    lv_obj_align(s_hint_label, LV_ALIGN_CENTER, 0, 40);

    set_label_texts(0);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
}

} // namespace

void init(const Hooks& hooks)
{
    s_hooks = hooks;
    create_if_needed();
}

void refresh()
{
    create_if_needed();
    if (s_root == nullptr || s_time_label == nullptr || s_unread_label == nullptr)
    {
        return;
    }

    char time_buf[16] = "--:--";
    if (s_hooks.format_time == nullptr ||
        !s_hooks.format_time(time_buf, sizeof(time_buf)))
    {
        std::snprintf(time_buf, sizeof(time_buf), "--:--");
    }
    lv_label_set_text(s_time_label, time_buf);

    const int unread = s_hooks.read_unread_count != nullptr ? s_hooks.read_unread_count() : 0;
    set_label_texts(unread);
}

void show()
{
    create_if_needed();
    if (s_root == nullptr)
    {
        return;
    }
    refresh();
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_root);
}

void hide()
{
    if (s_root == nullptr)
    {
        return;
    }
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
}

bool is_visible()
{
    return s_root != nullptr && !lv_obj_has_flag(s_root, LV_OBJ_FLAG_HIDDEN);
}

void present_now(std::uint8_t frame_count, std::uint32_t frame_delay_ms)
{
    if (frame_count == 0)
    {
        frame_count = 1;
    }
    for (std::uint8_t frame = 0; frame < frame_count; ++frame)
    {
        if (s_root != nullptr)
        {
            lv_obj_invalidate(s_root);
        }
        if (lv_obj_t* top = lv_layer_top())
        {
            lv_obj_invalidate(top);
        }
        lv_refr_now(nullptr);
        if (frame + 1U < frame_count && frame_delay_ms != 0)
        {
            sys::sleep_ms(frame_delay_ms);
        }
    }
}

} // namespace ui::components::screen_saver_overlay
