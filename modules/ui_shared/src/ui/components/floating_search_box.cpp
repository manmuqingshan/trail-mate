#include "ui/components/floating_search_box.h"

#include "ui/app_runtime.h"
#include "ui/components/two_pane_styles.h"
#include "ui/localization.h"
#include "ui/page/page_profile.h"

#include <cstdio>

namespace ui::components::floating_search_box
{
namespace
{

constexpr uint32_t kScrim = 0x18212F;
constexpr uint16_t kMaxSearchText = 63;

bool activation_key(lv_event_t* event)
{
    if (!event || lv_event_get_code(event) != LV_EVENT_KEY)
    {
        return false;
    }
    const uint32_t key = lv_event_get_key(event);
    return key == LV_KEY_ENTER || key == LV_KEY_RIGHT;
}

lv_obj_t* create_button(lv_obj_t* parent,
                        const char* text,
                        lv_event_cb_t callback,
                        State* state)
{
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_size(btn,
                    ::ui::page_profile::resolve_control_button_min_width(),
                    ::ui::page_profile::resolve_control_button_height());
    ::ui::components::two_pane_styles::apply_btn_basic(btn);

    lv_obj_t* label = lv_label_create(btn);
    ::ui::i18n::set_label_text(label, text);
    ::ui::components::two_pane_styles::apply_label_primary(label);
    lv_obj_center(label);

    lv_obj_add_event_cb(btn, callback, LV_EVENT_CLICKED, state);
    lv_obj_add_event_cb(btn, callback, LV_EVENT_KEY, state);
    return btn;
}

void invoke_apply(State& state)
{
    char text[kMaxSearchText + 1U] = {};
    if (state.textarea)
    {
        const char* raw = lv_textarea_get_text(state.textarea);
        std::snprintf(text, sizeof(text), "%s", raw ? raw : "");
    }
    const auto callback = state.callbacks.apply;
    void* user_data = state.callbacks.user_data;
    close(state);
    if (callback)
    {
        callback(text, user_data);
    }
}

void invoke_clear(State& state)
{
    const auto callback = state.callbacks.clear;
    void* user_data = state.callbacks.user_data;
    close(state);
    if (callback)
    {
        callback(user_data);
    }
}

void invoke_cancel(State& state)
{
    const auto callback = state.callbacks.cancel;
    void* user_data = state.callbacks.user_data;
    close(state);
    if (callback)
    {
        callback(user_data);
    }
}

void on_textarea_key(lv_event_t* event)
{
    auto* state = static_cast<State*>(lv_event_get_user_data(event));
    if (!state || lv_event_get_code(event) != LV_EVENT_KEY)
    {
        return;
    }
    const uint32_t key = lv_event_get_key(event);
    if (key == LV_KEY_ENTER || key == LV_KEY_RIGHT)
    {
        invoke_apply(*state);
    }
    else if (key == LV_KEY_ESC)
    {
        invoke_cancel(*state);
    }
}

void on_apply(lv_event_t* event)
{
    auto* state = static_cast<State*>(lv_event_get_user_data(event));
    if (!state)
    {
        return;
    }
    if (lv_event_get_code(event) == LV_EVENT_CLICKED || activation_key(event))
    {
        invoke_apply(*state);
    }
}

void on_clear(lv_event_t* event)
{
    auto* state = static_cast<State*>(lv_event_get_user_data(event));
    if (!state)
    {
        return;
    }
    if (lv_event_get_code(event) == LV_EVENT_CLICKED || activation_key(event))
    {
        invoke_clear(*state);
    }
}

void on_cancel(lv_event_t* event)
{
    auto* state = static_cast<State*>(lv_event_get_user_data(event));
    if (!state)
    {
        return;
    }
    if (lv_event_get_code(event) == LV_EVENT_CLICKED || activation_key(event))
    {
        invoke_cancel(*state);
    }
}

} // namespace

bool is_open(const State& state)
{
    return state.overlay && lv_obj_is_valid(state.overlay);
}

void focus(State& state)
{
    if (is_open(state) && state.textarea)
    {
        lv_group_focus_obj(state.textarea);
    }
}

bool open(State& state, lv_obj_t* parent, const Config& config)
{
    if (is_open(state))
    {
        focus(state);
        return true;
    }

    lv_obj_t* host = parent ? parent : lv_screen_active();
    if (!host)
    {
        return false;
    }

    state.callbacks = config.callbacks;
    state.previous_group = config.restore_group ? config.restore_group : lv_group_get_default();
    state.group = lv_group_create();
    if (state.group)
    {
        set_default_group(state.group);
    }

    state.overlay = lv_obj_create(host);
    lv_obj_set_size(state.overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(state.overlay, lv_color_hex(kScrim), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(state.overlay, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_border_width(state.overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(state.overlay, 0, LV_PART_MAIN);
    lv_obj_add_flag(state.overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(state.overlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* panel = lv_obj_create(state.overlay);
    lv_obj_set_size(panel, config.width, config.height);
    lv_obj_center(panel);
    ::ui::components::two_pane_styles::apply_panel_main(panel);
    lv_obj_set_style_border_width(panel, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(panel,
                                  lv_color_hex(::ui::components::two_pane_styles::kBorder),
                                  LV_PART_MAIN);
    lv_obj_set_style_radius(panel, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_all(panel, 8, LV_PART_MAIN);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title = lv_label_create(panel);
    ::ui::i18n::set_label_text(title, config.title ? config.title : "Search");
    ::ui::components::two_pane_styles::apply_label_primary(title);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    state.textarea = lv_textarea_create(panel);
    lv_textarea_set_one_line(state.textarea, true);
    lv_textarea_set_max_length(state.textarea,
                               config.max_length < kMaxSearchText ? config.max_length
                                                                  : kMaxSearchText);
    lv_textarea_set_text(state.textarea, config.initial_text ? config.initial_text : "");
    lv_obj_set_width(state.textarea, LV_PCT(100));
    lv_obj_align(state.textarea, LV_ALIGN_TOP_MID, 0, 34);
    lv_obj_add_event_cb(state.textarea, on_textarea_key, LV_EVENT_KEY, &state);

    lv_obj_t* btn_row = lv_obj_create(panel);
    lv_obj_set_size(btn_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_align(btn_row, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row,
                          LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(btn_row, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_row, 0, LV_PART_MAIN);
    lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* apply_btn = create_button(btn_row, "Apply", on_apply, &state);
    lv_obj_t* clear_btn = create_button(btn_row, "Clear", on_clear, &state);
    lv_obj_t* cancel_btn = create_button(btn_row, "Cancel", on_cancel, &state);

    if (state.group)
    {
        lv_group_add_obj(state.group, state.textarea);
        lv_group_add_obj(state.group, apply_btn);
        lv_group_add_obj(state.group, clear_btn);
        lv_group_add_obj(state.group, cancel_btn);
        lv_group_focus_obj(state.textarea);
    }
    return true;
}

void close(State& state)
{
    if (state.overlay && lv_obj_is_valid(state.overlay))
    {
        lv_obj_del(state.overlay);
    }
    state.overlay = nullptr;
    state.textarea = nullptr;
    if (state.previous_group)
    {
        set_default_group(state.previous_group);
    }
    if (state.group)
    {
        lv_group_del(state.group);
    }
    state.group = nullptr;
    state.previous_group = nullptr;
    state.callbacks = Callbacks{};
}

} // namespace ui::components::floating_search_box
