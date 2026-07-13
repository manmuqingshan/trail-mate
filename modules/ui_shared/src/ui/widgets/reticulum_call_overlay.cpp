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
constexpr int kEncoderKeyRotateUp = 19;
constexpr int kEncoderKeyRotateDown = 20;
constexpr lv_coord_t kFallbackScreenW = 320;
constexpr lv_coord_t kFallbackScreenH = 240;

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
    lv_group_t* group = nullptr;
    lv_group_t* previous_group = nullptr;
    AppScreen* owner_app = nullptr;
    bool needs_present = false;
    ::platform::ui::reticulum_call::State last_state =
        ::platform::ui::reticulum_call::State::Idle;
};

OverlayState s_overlay;

lv_coord_t display_width()
{
    const lv_coord_t width = lv_display_get_horizontal_resolution(nullptr);
    if (width > 0)
    {
        return width;
    }
    if (lv_obj_t* screen = lv_scr_act())
    {
        const lv_coord_t screen_width = lv_obj_get_width(screen);
        if (screen_width > 0)
        {
            return screen_width;
        }
    }
    return kFallbackScreenW;
}

lv_coord_t display_height()
{
    const lv_coord_t height = lv_display_get_vertical_resolution(nullptr);
    if (height > 0)
    {
        return height;
    }
    if (lv_obj_t* screen = lv_scr_act())
    {
        const lv_coord_t screen_height = lv_obj_get_height(screen);
        if (screen_height > 0)
        {
            return screen_height;
        }
    }
    return kFallbackScreenH;
}

lv_coord_t clamp_panel_width(lv_coord_t preferred, lv_coord_t screen_width)
{
    const lv_coord_t max_width = screen_width > 24 ? screen_width - 16 : screen_width;
    if (max_width > 0 && preferred > max_width)
    {
        return max_width;
    }
    return preferred;
}

void cover_display(lv_obj_t* obj)
{
    if (!obj || !lv_obj_is_valid(obj))
    {
        return;
    }
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, display_width(), display_height());
}

void normalize_top_layer()
{
    if (lv_obj_t* top = lv_layer_top())
    {
        cover_display(top);
    }
}

void present_overlay_now()
{
    if (s_overlay.root && lv_obj_is_valid(s_overlay.root))
    {
        lv_obj_invalidate(s_overlay.root);
    }
    if (lv_obj_t* top = lv_layer_top())
    {
        lv_obj_invalidate(top);
    }
    lv_refr_now(nullptr);
}

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

void stop_input_event(lv_event_t* event)
{
    if (!event)
    {
        return;
    }
    lv_event_stop_bubbling(event);
    lv_event_stop_processing(event);
    if (lv_indev_t* indev = lv_event_get_indev(event))
    {
        lv_indev_stop_processing(indev);
    }
}

void swallow_event_cb(lv_event_t* event)
{
    stop_input_event(event);
}

void add_swallow_input_callbacks(lv_obj_t* obj)
{
    if (!obj)
    {
        return;
    }

    lv_obj_add_event_cb(obj, swallow_event_cb, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(obj, swallow_event_cb, LV_EVENT_PRESSING, nullptr);
    lv_obj_add_event_cb(obj, swallow_event_cb, LV_EVENT_PRESS_LOST, nullptr);
    lv_obj_add_event_cb(obj, swallow_event_cb, LV_EVENT_RELEASED, nullptr);
    lv_obj_add_event_cb(obj, swallow_event_cb, LV_EVENT_SHORT_CLICKED, nullptr);
    lv_obj_add_event_cb(obj, swallow_event_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(obj, swallow_event_cb, LV_EVENT_LONG_PRESSED, nullptr);
    lv_obj_add_event_cb(obj, swallow_event_cb, LV_EVENT_LONG_PRESSED_REPEAT, nullptr);
    lv_obj_add_event_cb(obj, swallow_event_cb, LV_EVENT_GESTURE, nullptr);
    lv_obj_add_event_cb(obj, swallow_event_cb, LV_EVENT_KEY, nullptr);
}

bool focus_previous_key(uint32_t key)
{
    return key == LV_KEY_UP || key == LV_KEY_LEFT || key == LV_KEY_PREV ||
           key == static_cast<uint32_t>(kEncoderKeyRotateUp);
}

bool focus_next_key(uint32_t key)
{
    return key == LV_KEY_DOWN || key == LV_KEY_RIGHT || key == LV_KEY_NEXT ||
           key == static_cast<uint32_t>(kEncoderKeyRotateDown);
}

bool handle_focus_key(lv_event_t* event)
{
    if (!event || lv_event_get_code(event) != LV_EVENT_KEY || !s_overlay.group)
    {
        return false;
    }
    const uint32_t key = lv_event_get_key(event);
    if (focus_previous_key(key))
    {
        lv_group_focus_prev(s_overlay.group);
        lv_group_set_editing(s_overlay.group, false);
        stop_input_event(event);
        return true;
    }
    if (focus_next_key(key))
    {
        lv_group_focus_next(s_overlay.group);
        lv_group_set_editing(s_overlay.group, false);
        stop_input_event(event);
        return true;
    }
    return false;
}

bool visible_action(lv_obj_t* obj, bool incoming)
{
    if (!obj || !lv_obj_is_valid(obj) || lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN))
    {
        return false;
    }
    if (incoming)
    {
        return obj == s_overlay.answer_btn || obj == s_overlay.decline_btn;
    }
    return obj == s_overlay.hangup_btn;
}

void focus_default_action(bool incoming, bool force)
{
    if (!s_overlay.group)
    {
        return;
    }
    lv_obj_t* focused = lv_group_get_focused(s_overlay.group);
    if (!force && visible_action(focused, incoming))
    {
        return;
    }
    lv_obj_t* target = incoming ? s_overlay.answer_btn : s_overlay.hangup_btn;
    if (target && lv_obj_is_valid(target) &&
        !lv_obj_has_flag(target, LV_OBJ_FLAG_HIDDEN))
    {
        lv_group_focus_obj(target);
        lv_group_set_editing(s_overlay.group, false);
    }
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
    lv_obj_add_event_cb(btn, swallow_event_cb, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(btn, swallow_event_cb, LV_EVENT_PRESSING, nullptr);
    lv_obj_add_event_cb(btn, swallow_event_cb, LV_EVENT_PRESS_LOST, nullptr);
    lv_obj_add_event_cb(btn, swallow_event_cb, LV_EVENT_RELEASED, nullptr);
    if (s_overlay.group)
    {
        lv_group_add_obj(s_overlay.group, btn);
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
    return key == LV_KEY_ENTER;
}

void answer_cb(lv_event_t* event)
{
    if (handle_focus_key(event))
    {
        return;
    }
    if (!activation_event(event))
    {
        return;
    }
    stop_input_event(event);
    if (!::platform::ui::reticulum_call::accept())
    {
        ::ui::feedback::show_notice(::ui::i18n::tr("Call unavailable"), 1800);
    }
}

void hangup_cb(lv_event_t* event)
{
    if (handle_focus_key(event))
    {
        return;
    }
    if (!activation_event(event))
    {
        return;
    }
    stop_input_event(event);
    ::platform::ui::reticulum_call::hangup();
}

void decline_cb(lv_event_t* event)
{
    if (handle_focus_key(event))
    {
        return;
    }
    if (!activation_event(event))
    {
        return;
    }
    stop_input_event(event);
    ::platform::ui::reticulum_call::reject();
}

void root_key_cb(lv_event_t* event)
{
    if (!event || lv_event_get_code(event) != LV_EVENT_KEY)
    {
        return;
    }
    if (handle_focus_key(event))
    {
        return;
    }
    const uint32_t key = lv_event_get_key(event);
    const auto snapshot = ::platform::ui::reticulum_call::snapshot();
    if ((key == LV_KEY_ESC || key == LV_KEY_BACKSPACE) &&
        snapshot.state != ::platform::ui::reticulum_call::State::Idle)
    {
        stop_input_event(event);
        ::platform::ui::reticulum_call::hangup();
    }
}

void destroy_overlay()
{
    if (!s_overlay.root && !s_overlay.group)
    {
        if (ui_is_overlay_active())
        {
            ui_set_overlay_active(false);
        }
        return;
    }

    lv_group_t* group = s_overlay.group;
    lv_group_t* previous_group = s_overlay.previous_group;
    AppScreen* owner_app = s_overlay.owner_app;
    if (s_overlay.root)
    {
        lv_obj_del(s_overlay.root);
    }
    s_overlay = OverlayState{};

    // Page-owned LVGL groups are deleted during page cleanup; only restore within
    // the same active app.
    const bool overlay_still_owns_input = group && lv_group_get_default() == group;
    const bool previous_group_still_owned =
        previous_group && owner_app == ui_get_active_app();
    if (overlay_still_owns_input && previous_group_still_owned)
    {
        set_default_group(previous_group);
    }
    else if (overlay_still_owns_input)
    {
        set_default_group(nullptr);
    }
    if (group)
    {
        lv_group_del(group);
    }
    ui_set_overlay_active(false);
}

void ensure_overlay()
{
    normalize_top_layer();
    if (s_overlay.root && lv_obj_is_valid(s_overlay.root))
    {
        ui_set_overlay_active(true);
        cover_display(s_overlay.root);
        lv_obj_move_foreground(s_overlay.root);
        return;
    }

    s_overlay.previous_group = lv_group_get_default();
    s_overlay.owner_app = ui_get_active_app();
    s_overlay.group = lv_group_create();
    if (s_overlay.group)
    {
        set_default_group(s_overlay.group);
    }
    ui_set_overlay_active(true);

    s_overlay.root = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_overlay.root);
    cover_display(s_overlay.root);
    lv_obj_set_style_bg_color(s_overlay.root, lv_color_hex(kScrim), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_overlay.root, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_overlay.root, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_overlay.root, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_overlay.root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_overlay.root, root_key_cb, LV_EVENT_KEY, nullptr);
    lv_obj_add_flag(s_overlay.root, LV_OBJ_FLAG_CLICKABLE);
    add_swallow_input_callbacks(s_overlay.root);

    s_overlay.panel = lv_obj_create(s_overlay.root);
    const bool dense = ::ui::page_profile::current().dense;
    const lv_coord_t screen_w = display_width();
    lv_obj_set_size(s_overlay.panel,
                    clamp_panel_width(dense ? 220 : 260, screen_w),
                    LV_SIZE_CONTENT);
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
    lv_obj_clear_flag(s_overlay.panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_overlay.panel, LV_OBJ_FLAG_CLICKABLE);
    add_swallow_input_callbacks(s_overlay.panel);

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
    lv_obj_clear_flag(actions, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(actions, LV_OBJ_FLAG_CLICKABLE);
    add_swallow_input_callbacks(actions);

    s_overlay.answer_btn = make_button(actions, "Answer", kOk, 0x2F662F, answer_cb);
    s_overlay.hangup_btn = make_button(actions, "Hang up", kWarn, 0x95351F, hangup_cb);
    s_overlay.decline_btn = make_button(actions, "Decline", kAmber, kAmberDark, decline_cb);
    lv_obj_move_foreground(s_overlay.root);
    s_overlay.needs_present = true;
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
    const auto phase = snapshot.realtime_phase;
    const bool incoming =
        phase == ::platform::ui::reticulum_call::RealtimePhase::IncomingRinging;
    const bool starting =
        phase == ::platform::ui::reticulum_call::RealtimePhase::AcceptedStarting;
    const bool active =
        phase == ::platform::ui::reticulum_call::RealtimePhase::ActiveCall;
    const bool closing =
        phase == ::platform::ui::reticulum_call::RealtimePhase::ClosingCall;
    const bool outgoing = state == ::platform::ui::reticulum_call::State::Outgoing;
    const bool state_changed = s_overlay.last_state != state;

    apply_label(s_overlay.title,
                incoming ? "Incoming call"
                         : (closing ? "Closing call"
                                    : (outgoing ? "Calling" : "Reticulum call")),
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
    else if (closing)
    {
        std::snprintf(status, sizeof(status), "%s", "Hanging up");
    }
    else if (starting)
    {
        std::snprintf(status, sizeof(status), "%s", "Connecting call.audio");
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
        focus_default_action(true, state_changed);
    }
    else if (closing)
    {
        lv_obj_add_flag(s_overlay.answer_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_overlay.decline_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_overlay.hangup_btn, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_add_flag(s_overlay.answer_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_overlay.decline_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_overlay.hangup_btn, LV_OBJ_FLAG_HIDDEN);
        focus_default_action(false, state_changed);
    }
    s_overlay.last_state = state;
    if (s_overlay.needs_present || state_changed)
    {
        s_overlay.needs_present = false;
        present_overlay_now();
    }
}

} // namespace

void tick()
{
    const auto snapshot = ::platform::ui::reticulum_call::snapshot();
    if (snapshot.realtime_phase ==
        ::platform::ui::reticulum_call::RealtimePhase::Idle)
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
