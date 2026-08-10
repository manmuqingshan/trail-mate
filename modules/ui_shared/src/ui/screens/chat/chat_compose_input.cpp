#if !defined(ARDUINO_T_WATCH_S3)
#include "ui/screens/chat/chat_compose_input.h"

namespace chat::ui::compose::input
{

void setup_default_group_focus(const layout::Widgets& w)
{
    // ChatComposeScreen owns the complete rotary order once IME, optional
    // actions, and the top-bar Back control all exist. Registering controls as
    // they are constructed makes the cycle depend on creation timing.
    lv_obj_add_state(w.textarea, LV_STATE_FOCUSED);
}

void bind_textarea_events(const layout::Widgets& w, void* user_data,
                          lv_event_cb_t key_cb, lv_event_cb_t text_cb)
{
    lv_obj_add_event_cb(w.textarea, text_cb, LV_EVENT_VALUE_CHANGED, user_data);
    lv_obj_add_event_cb(w.textarea, key_cb, LV_EVENT_KEY, user_data);
}

} // namespace chat::ui::compose::input

#endif
