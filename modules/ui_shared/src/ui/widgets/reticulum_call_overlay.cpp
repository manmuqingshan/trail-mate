/**
 * @file reticulum_call_overlay.cpp
 * @brief Top-level Reticulum call answer/hangup overlay.
 */

#include "ui/widgets/reticulum_call_overlay.h"

#include "lvgl.h"
#include "platform/ui/reticulum_call_runtime.h"
#include "ui/app_runtime.h"
#include "ui/assets/fonts/font_utils.h"
#include "ui/localization.h"
#include "ui/page/page_profile.h"
#include "ui/runtime/ui_feedback.h"

#include <cstdio>
#include <cstring>

namespace ui::widgets::reticulum_call_overlay
{
namespace
{

constexpr uint32_t kScrim = 0x1C1812;
constexpr uint32_t kPanelBg = 0xFAF0D8;
constexpr uint32_t kAmber = 0xEBA341;
constexpr uint32_t kAmberDark = 0xC98118;
constexpr uint32_t kLine = 0xE7C98F;
constexpr uint32_t kText = 0x6B4A1E;
constexpr uint32_t kTextDim = 0x8A6A3A;
constexpr uint32_t kWarn = 0xB94A2C;
constexpr uint32_t kOk = 0x3E7D3E;

struct OverlayState
{
    lv_obj_t* root = nullptr;
    lv_obj_t* panel = nullptr;
    lv_obj_t* title = nullptr;
    lv_obj_t* peer = nullptr;
    lv_obj_t* status = nullptr;
    lv_obj_t* answer_btn = nullptr;
    lv_obj_t* hangup_btn = nullptr;
    lv_obj_t* decline_btn = nullptr;
    ::platform::ui::reticulum_call::State last_state =
        ::platform::ui::reticulum_call::State::Idle;
};

OverlayState s_overlay;

const lv_font_t* body_font()
{
    return ::ui::page_profile::resolve_body_font();
}

const lv_font_t* caption_font()
{
    return ::ui::page_profile::resolve_caption_font();
}

void apply_label(lv_obj_t* label,
                 const char* text,
                 uint32_t color,
                 const lv_font_t* font)
{
    if (!label)
    {
        return;
    }
    const char* value = text ? text : "";
    lv_label_set_text(label, value);
    lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
    ::ui::fonts::apply_localized_font(label, value, font ? font : body_font());
}

void style_button(lv_obj_t* btn, uint32_t bg, uint32_t bg_pressed)
{
    lv_obj_set_style_radius(btn, 8, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, lv_color_hex(bg), LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, lv_color_hex(bg_pressed), LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(btn, lv_color_hex(bg_pressed), LV_STATE_FOCUSED);
    lv_obj_set_style_bg_color(btn, lv_color_hex(bg_pressed), LV_STATE_FOCUS_KEY);
    lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn, lv_color_hex(kLine), LV_PART_MAIN);
    lv_obj_set_style_outline_width(btn, 0, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_width(btn, 0, LV_STATE_FOCUS_KEY);
}

lv_obj_t* make_button(lv_obj_t* parent,
                      const char* text,
                      uint32_t bg,
                      uint32_t bg_pressed,
                      lv_event_cb_t cb)
{
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_size(btn,
                    ::ui::page_profile::resolve_control_button_min_width(),
                    ::ui::page_profile::resolve_control_button_height());
    style_button(btn, bg, bg_pressed);

    lv_obj_t* label = lv_label_create(btn);
    apply_label(label, text, kText, body_font());
    lv_obj_center(label);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_KEY, nullptr);
    if (app_g)
    {
        lv_group_add_obj(app_g, btn);
    }
    return btn;
}

bool activation_event(lv_event_t* event)
{
    if (!event)
    {
        return false;
    }
    if (lv_event_get_code(event) == LV_EVENT_CLICKED)
    {
        return true;
    }
    if (lv_event_get_code(event) != LV_EVENT_KEY)
    {
        return false;
    }
    const uint32_t key = lv_event_get_key(event);
    return key == LV_KEY_ENTER || key == LV_KEY_RIGHT;
}

void answer_cb(lv_event_t* event)
{
    if (!activation_event(event))
    {
        return;
    }
    if (!::platform::ui::reticulum_call::accept())
    {
        ::ui::feedback::show_notice(::ui::i18n::tr("Call unavailable"), 1800);
    }
}

void hangup_cb(lv_event_t* event)
{
    if (!activation_event(event))
    {
        return;
    }
    ::platform::ui::reticulum_call::hangup();
}

void decline_cb(lv_event_t* event)
{
    if (!activation_event(event))
    {
        return;
    }
    ::platform::ui::reticulum_call::reject();
}

void root_key_cb(lv_event_t* event)
{
    if (!event || lv_event_get_code(event) != LV_EVENT_KEY)
    {
        return;
    }
    const uint32_t key = lv_event_get_key(event);
    const auto snapshot = ::platform::ui::reticulum_call::snapshot();
    if ((key == LV_KEY_ESC || key == LV_KEY_BACKSPACE) &&
        snapshot.state != ::platform::ui::reticulum_call::State::Idle)
    {
        ::platform::ui::reticulum_call::hangup();
    }
}

void destroy_overlay()
{
    if (s_overlay.root)
    {
        lv_obj_del(s_overlay.root);
    }
    s_overlay = OverlayState{};
}

void ensure_overlay()
{
    if (s_overlay.root && lv_obj_is_valid(s_overlay.root))
    {
        return;
    }

    s_overlay.root = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_overlay.root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_overlay.root, lv_color_hex(kScrim), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_overlay.root, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_overlay.root, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_overlay.root, 8, LV_PART_MAIN);
    lv_obj_add_event_cb(s_overlay.root, root_key_cb, LV_EVENT_KEY, nullptr);
    lv_obj_add_flag(s_overlay.root, LV_OBJ_FLAG_CLICKABLE);

    s_overlay.panel = lv_obj_create(s_overlay.root);
    const bool dense = ::ui::page_profile::current().dense;
    lv_obj_set_size(s_overlay.panel, dense ? 220 : 260, LV_SIZE_CONTENT);
    lv_obj_align(s_overlay.panel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_flex_flow(s_overlay.panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_overlay.panel,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(s_overlay.panel, lv_color_hex(kPanelBg), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_overlay.panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_overlay.panel, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_overlay.panel, lv_color_hex(kLine), LV_PART_MAIN);
    lv_obj_set_style_radius(s_overlay.panel, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_overlay.panel, dense ? 8 : 12, LV_PART_MAIN);
    lv_obj_set_style_pad_row(s_overlay.panel, dense ? 4 : 6, LV_PART_MAIN);

    s_overlay.title = lv_label_create(s_overlay.panel);
    lv_obj_set_width(s_overlay.title, LV_PCT(100));
    lv_label_set_long_mode(s_overlay.title, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(s_overlay.title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    s_overlay.peer = lv_label_create(s_overlay.panel);
    lv_obj_set_width(s_overlay.peer, LV_PCT(100));
    lv_label_set_long_mode(s_overlay.peer, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(s_overlay.peer, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    s_overlay.status = lv_label_create(s_overlay.panel);
    lv_obj_set_width(s_overlay.status, LV_PCT(100));
    lv_label_set_long_mode(s_overlay.status, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(s_overlay.status, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    lv_obj_t* actions = lv_obj_create(s_overlay.panel);
    lv_obj_set_width(actions, LV_PCT(100));
    lv_obj_set_height(actions,
                      ::ui::page_profile::resolve_control_button_height() + 4);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(actions,
                          LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(actions, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(actions, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(actions, 0, LV_PART_MAIN);

    s_overlay.answer_btn = make_button(actions, "Answer", kOk, 0x2F662F, answer_cb);
    s_overlay.hangup_btn = make_button(actions, "Hang up", kWarn, 0x95351F, hangup_cb);
    s_overlay.decline_btn = make_button(actions, "Decline", kAmber, kAmberDark, decline_cb);
}

void format_peer_name(const ::platform::ui::reticulum_call::Snapshot& snapshot,
                      char* out,
                      std::size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }
    if (snapshot.peer_name[0] != '\0')
    {
        std::snprintf(out, out_len, "%s", snapshot.peer_name);
        return;
    }
    std::snprintf(out,
                  out_len,
                  "%.2X%.2X%.2X%.2X",
                  static_cast<unsigned>(snapshot.peer_destination_hash[0]),
                  static_cast<unsigned>(snapshot.peer_destination_hash[1]),
                  static_cast<unsigned>(snapshot.peer_destination_hash[2]),
                  static_cast<unsigned>(snapshot.peer_destination_hash[3]));
}

void update_overlay(const ::platform::ui::reticulum_call::Snapshot& snapshot)
{
    ensure_overlay();
    const auto state = snapshot.state;
    const bool incoming = state == ::platform::ui::reticulum_call::State::Incoming;
    const bool active = state == ::platform::ui::reticulum_call::State::Active;
    const bool outgoing = state == ::platform::ui::reticulum_call::State::Outgoing;

    apply_label(s_overlay.title,
                incoming ? "Incoming call" : (outgoing ? "Calling" : "Reticulum call"),
                kText,
                body_font());

    char peer_name[48] = {};
    format_peer_name(snapshot, peer_name, sizeof(peer_name));
    apply_label(s_overlay.peer, peer_name, kText, body_font());

    char status[96] = {};
    if (!snapshot.wifi_ready)
    {
        std::snprintf(status, sizeof(status), "%s", "Wi-Fi gateway unavailable");
    }
    else if (!snapshot.media_supported)
    {
        std::snprintf(status, sizeof(status), "%s", "Audio hardware unavailable");
    }
    else if (active)
    {
        std::snprintf(status,
                      sizeof(status),
                      "RX %lu  TX %lu",
                      static_cast<unsigned long>(snapshot.rx_packets),
                      static_cast<unsigned long>(snapshot.tx_packets));
    }
    else
    {
        std::snprintf(status, sizeof(status), "%s", "Wi-Fi call.audio");
    }
    apply_label(s_overlay.status, status, kTextDim, caption_font());

    if (incoming)
    {
        lv_obj_clear_flag(s_overlay.answer_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_overlay.decline_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_overlay.hangup_btn, LV_OBJ_FLAG_HIDDEN);
        if (app_g)
        {
            lv_group_focus_obj(s_overlay.answer_btn);
        }
    }
    else
    {
        lv_obj_add_flag(s_overlay.answer_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_overlay.decline_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_overlay.hangup_btn, LV_OBJ_FLAG_HIDDEN);
        if (app_g)
        {
            lv_group_focus_obj(s_overlay.hangup_btn);
        }
    }
    s_overlay.last_state = state;
}

} // namespace

void tick()
{
    const auto snapshot = ::platform::ui::reticulum_call::snapshot();
    if (snapshot.state == ::platform::ui::reticulum_call::State::Idle ||
        snapshot.state == ::platform::ui::reticulum_call::State::Ended ||
        snapshot.state == ::platform::ui::reticulum_call::State::Failed)
    {
        destroy_overlay();
        return;
    }
    update_overlay(snapshot);
}

bool visible()
{
    return s_overlay.root && lv_obj_is_valid(s_overlay.root);
}

} // namespace ui::widgets::reticulum_call_overlay
