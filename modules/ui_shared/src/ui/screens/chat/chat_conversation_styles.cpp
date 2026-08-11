#if !defined(ARDUINO_T_WATCH_S3)
#include "ui/screens/chat/chat_conversation_styles.h"
#include "ui/assets/fonts/font_utils.h"
#include "ui/page/page_profile.h"

namespace chat::ui::conversation::styles
{

static bool inited = false;

static lv_style_t s_root;
static lv_style_t s_msg_list;
static lv_style_t s_action_bar;

static lv_style_t s_reply_btn;
static lv_style_t s_reply_btn_focused;
static lv_style_t s_reply_label;

static lv_style_t s_row;

static lv_style_t s_bubble_base;
static lv_style_t s_bubble_self;
static lv_style_t s_bubble_other;
static lv_style_t s_bubble_unverified;
static lv_style_t s_bubble_selected;
static lv_style_t s_bubble_text;
static lv_style_t s_bubble_time;

static constexpr lv_coord_t kPadX = 8;
static constexpr lv_coord_t kPadY = 6;
static constexpr lv_coord_t kGapY = 6;
static constexpr lv_coord_t kBubblePadX = 10;
static constexpr lv_coord_t kBubblePadY = 6;
static constexpr lv_coord_t kBubbleRadius = 12;

static const lv_color_t kBubbleOther = lv_color_hex(0xFFF8E8);
static const lv_color_t kBubbleSelf = lv_color_hex(0xDDF3EA);
static const lv_color_t kTextColor = lv_color_hex(0x3A2A1A);

void init_once()
{
    if (inited) return;
    inited = true;
    const bool dense = ::ui::page_profile::is_dense();
    const lv_coord_t pad_x = dense ? 4 : kPadX;
    const lv_coord_t pad_y = dense ? 3 : kPadY;
    const lv_coord_t gap_y = dense ? 2 : kGapY;
    const lv_coord_t bubble_pad_x = dense ? 6 : kBubblePadX;
    const lv_coord_t bubble_pad_y = dense ? 3 : kBubblePadY;
    const lv_coord_t bubble_radius = dense ? 7 : kBubbleRadius;
    const lv_font_t* body_font = ::ui::page_profile::resolve_body_font();
    const lv_font_t* meta_font = ::ui::page_profile::resolve_caption_font();

    lv_style_init(&s_root);
    lv_style_set_bg_color(&s_root, lv_color_hex(0xFFF3DF));
    lv_style_set_bg_opa(&s_root, LV_OPA_COVER);
    lv_style_set_border_width(&s_root, 0);
    lv_style_set_pad_all(&s_root, 0);
    lv_style_set_radius(&s_root, 0);

    lv_style_init(&s_msg_list);
    lv_style_set_bg_color(&s_msg_list, lv_color_hex(0xFFF3DF));
    lv_style_set_bg_opa(&s_msg_list, LV_OPA_COVER);
    lv_style_set_border_width(&s_msg_list, 0);
    lv_style_set_pad_left(&s_msg_list, pad_x);
    lv_style_set_pad_right(&s_msg_list, pad_x);
    lv_style_set_pad_top(&s_msg_list, pad_y);
    lv_style_set_pad_bottom(&s_msg_list, pad_y);
    lv_style_set_pad_row(&s_msg_list, gap_y);
    lv_style_set_radius(&s_msg_list, 0);

    lv_style_init(&s_action_bar);
    lv_style_set_bg_color(&s_action_bar, lv_color_hex(0xFFF0D3));
    lv_style_set_bg_opa(&s_action_bar, LV_OPA_COVER);
    lv_style_set_border_width(&s_action_bar, 0);
    lv_style_set_pad_left(&s_action_bar, dense ? 4 : 10);
    lv_style_set_pad_right(&s_action_bar, dense ? 4 : 10);
    lv_style_set_pad_top(&s_action_bar, dense ? 1 : 4);
    lv_style_set_pad_bottom(&s_action_bar, dense ? 1 : 4);

    lv_style_init(&s_reply_btn);
    lv_style_set_bg_color(&s_reply_btn, lv_color_hex(0xFFF7E9));
    lv_style_set_bg_opa(&s_reply_btn, LV_OPA_COVER);
    lv_style_set_border_width(&s_reply_btn, 1);
    lv_style_set_border_color(&s_reply_btn, lv_color_hex(0xD9B06A));
    lv_style_set_radius(&s_reply_btn, 6);
    lv_style_set_text_color(&s_reply_btn, lv_color_hex(0x3A2A1A));

    lv_style_init(&s_reply_btn_focused);
    lv_style_set_bg_color(&s_reply_btn_focused, lv_color_hex(0xEBA341));
    lv_style_set_outline_width(&s_reply_btn_focused, 0);

    lv_style_init(&s_reply_label);
    lv_style_set_text_color(&s_reply_label, lv_color_hex(0x3A2A1A));
    lv_style_set_text_font(&s_reply_label, ::ui::fonts::localized_font(::ui::fonts::ui_chrome_font()));

    lv_style_init(&s_row);
    lv_style_set_bg_opa(&s_row, LV_OPA_TRANSP);
    lv_style_set_border_width(&s_row, 0);
    lv_style_set_pad_top(&s_row, gap_y / 2);
    lv_style_set_pad_bottom(&s_row, gap_y / 2);
    lv_style_set_pad_left(&s_row, 0);
    lv_style_set_pad_right(&s_row, 0);
    lv_style_set_radius(&s_row, 0);
    lv_style_set_pad_column(&s_row, dense ? 3 : 6);

    lv_style_init(&s_bubble_base);
    lv_style_set_bg_opa(&s_bubble_base, LV_OPA_COVER);
    lv_style_set_border_width(&s_bubble_base, 1);
    lv_style_set_border_color(&s_bubble_base, lv_color_hex(0xD7B979));
    lv_style_set_radius(&s_bubble_base, bubble_radius);
    lv_style_set_pad_left(&s_bubble_base, bubble_pad_x);
    lv_style_set_pad_right(&s_bubble_base, bubble_pad_x);
    lv_style_set_pad_top(&s_bubble_base, bubble_pad_y);
    lv_style_set_pad_bottom(&s_bubble_base, bubble_pad_y);
    lv_style_set_pad_row(&s_bubble_base, dense ? 1 : 2);
    lv_style_set_pad_column(&s_bubble_base, 0);
    lv_style_set_bg_grad_dir(&s_bubble_base, LV_GRAD_DIR_NONE);

    lv_style_init(&s_bubble_self);
    lv_style_set_bg_color(&s_bubble_self, kBubbleSelf);
    lv_style_set_border_color(&s_bubble_self, lv_color_hex(0x8FCDB9));

    lv_style_init(&s_bubble_other);
    lv_style_set_bg_color(&s_bubble_other, kBubbleOther);
    lv_style_set_border_color(&s_bubble_other, lv_color_hex(0xE2C487));

    lv_style_init(&s_bubble_unverified);
    lv_style_set_bg_color(&s_bubble_unverified, lv_color_hex(0xF4E1DE));
    lv_style_set_border_color(&s_bubble_unverified, lv_color_hex(0xC47D70));

    // This style is shared by every bubble.  W/S selection only toggles the
    // LV_STATE_USER_1 bit; it does not add bubbles to an LVGL group or create
    // any per-message focus bookkeeping/allocation.
    lv_style_init(&s_bubble_selected);
    lv_style_set_outline_width(&s_bubble_selected, dense ? 1 : 2);
    lv_style_set_outline_pad(&s_bubble_selected, dense ? 1 : 2);
    lv_style_set_outline_color(&s_bubble_selected, lv_color_hex(0xE57B1F));
    lv_style_set_outline_opa(&s_bubble_selected, LV_OPA_COVER);

    lv_style_init(&s_bubble_text);
    lv_style_set_text_color(&s_bubble_text, kTextColor);
    lv_style_set_text_align(&s_bubble_text, LV_TEXT_ALIGN_LEFT);
    lv_style_set_text_font(&s_bubble_text, ::ui::fonts::localized_font(body_font));

    lv_style_init(&s_bubble_time);
    lv_style_set_text_color(&s_bubble_time, lv_color_hex(0x6A5646));
    lv_style_set_text_align(&s_bubble_time, LV_TEXT_ALIGN_LEFT);
    lv_style_set_text_font(&s_bubble_time, meta_font);
}

void apply_root(lv_obj_t* root)
{
    init_once();
    lv_obj_add_style(root, &s_root, 0);
}

void apply_msg_list(lv_obj_t* msg_list)
{
    init_once();
    lv_obj_add_style(msg_list, &s_msg_list, 0);
}

void apply_action_bar(lv_obj_t* action_bar)
{
    init_once();
    lv_obj_add_style(action_bar, &s_action_bar, 0);
}

void apply_reply_btn(lv_obj_t* btn)
{
    init_once();
    lv_obj_add_style(btn, &s_reply_btn, LV_PART_MAIN);
    lv_obj_add_style(btn, &s_reply_btn_focused, LV_STATE_FOCUSED);
}

void apply_reply_label(lv_obj_t* label)
{
    init_once();
    lv_obj_add_style(label, &s_reply_label, 0);
}

void apply_message_row(lv_obj_t* row)
{
    init_once();
    lv_obj_add_style(row, &s_row, 0);
}

void apply_bubble(lv_obj_t* bubble, bool is_self, bool source_unverified)
{
    init_once();
    lv_obj_add_style(bubble, &s_bubble_base, LV_PART_MAIN);
    lv_style_t* message_style =
        is_self ? &s_bubble_self
                : (source_unverified ? &s_bubble_unverified : &s_bubble_other);
    lv_obj_add_style(bubble, message_style, LV_PART_MAIN);
    lv_obj_add_style(bubble, &s_bubble_selected, LV_PART_MAIN | LV_STATE_USER_1);
}

void apply_bubble_text(lv_obj_t* label)
{
    init_once();
    lv_obj_add_style(label, &s_bubble_text, 0);
}

void apply_bubble_time(lv_obj_t* label)
{
    init_once();
    lv_obj_add_style(label, &s_bubble_time, 0);
}

} // namespace chat::ui::conversation::styles

#endif
