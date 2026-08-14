#include "screen_app_internal.h"

#include <cstdio>
#include <cstring>

namespace ui::mono::screens::screen_240x320::detail
{
namespace
{

struct ComposePageState
{
    lv_obj_t* body = nullptr;
    lv_obj_t* recipient = nullptr;
    lv_obj_t* help = nullptr;
    lv_obj_t* textarea = nullptr;
    ChatFlowState* flow = nullptr;
    bool back_scheduled = false;
};

ComposePageState s_compose_page;

void destroy_compose_widgets()
{
    if (app_g != nullptr && valid(s_compose_page.textarea) &&
        lv_obj_get_group(s_compose_page.textarea) == app_g)
    {
        lv_group_remove_obj(s_compose_page.textarea);
    }
    if (valid(s_compose_page.body))
    {
        lv_obj_del(s_compose_page.body);
    }
    s_compose_page = ComposePageState{};
}

void compose_back_async(void*)
{
    s_compose_page.back_scheduled = false;
    if (s_compose_page.flow == nullptr)
    {
        return;
    }

    ChatFlowState& flow = *s_compose_page.flow;
    leave_chat_compose_page(flow, true);
    flow.route = ChatRoute::Conversation;
    set_notice("DRAFT SAVED");
    refresh_page();
}

void on_compose_key(lv_event_t* event)
{
    if (lv_event_get_code(event) != LV_EVENT_KEY ||
        lv_event_get_key(event) != LV_KEY_ESC || s_compose_page.back_scheduled)
    {
        return;
    }

    s_compose_page.back_scheduled = true;
    if (lv_async_call(compose_back_async, nullptr) != LV_RESULT_OK)
    {
        s_compose_page.back_scheduled = false;
        set_notice("SELECT CANCEL TO RETURN");
    }
    lv_event_stop_processing(event);
}

void style_compose_textarea(lv_obj_t* textarea)
{
    lv_obj_set_style_text_font(textarea, LV_FONT_DEFAULT, LV_PART_MAIN);
    lv_obj_set_style_text_color(textarea, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(textarea, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(textarea, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(textarea, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(textarea, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_radius(textarea, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_left(textarea, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_right(textarea, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_top(textarea, 3, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(textarea, 3, LV_PART_MAIN);
}

} // namespace

void enter_chat_compose_page(ChatFlowState& flow)
{
    flow.route = ChatRoute::Compose;
}

void leave_chat_compose_page(ChatFlowState& flow, bool preserve_draft)
{
    if (preserve_draft && s_compose_page.flow == &flow && valid(s_compose_page.textarea))
    {
        std::snprintf(flow.draft, sizeof(flow.draft), "%s", lv_textarea_get_text(s_compose_page.textarea));
    }
    else if (!preserve_draft)
    {
        flow.draft[0] = '\0';
    }

    if (s_compose_page.flow == &flow)
    {
        destroy_compose_widgets();
    }
}

bool chat_compose_page_active()
{
    return valid(s_compose_page.body);
}

const char* chat_compose_page_text()
{
    return valid(s_compose_page.textarea) ? lv_textarea_get_text(s_compose_page.textarea) : "";
}

void clear_chat_compose_page_text()
{
    if (valid(s_compose_page.textarea))
    {
        lv_textarea_set_text(s_compose_page.textarea, "");
    }
    if (s_compose_page.flow != nullptr)
    {
        s_compose_page.flow->draft[0] = '\0';
    }
}

void render_chat_compose_page(ChatFlowState& flow, const char* title_prefix)
{
    if (s_compose_page.flow != &flow)
    {
        destroy_compose_widgets();
    }

    set_text(s_state.title, title_prefix ? title_prefix : "CHAT");
    set_text(s_state.subtitle, "COMPOSE");
    clear_lines_from(0);
    set_line(9, "CANCEL SAVES DRAFT  SEND QUEUES");

    if (!valid(s_compose_page.body))
    {
        s_compose_page.flow = &flow;
        s_compose_page.body = lv_obj_create(s_state.root);
        lv_obj_set_pos(s_compose_page.body, 0, kHeaderRuleY + 1);
        lv_obj_set_size(s_compose_page.body, kScreenWidth, kActionTop - kHeaderRuleY - 3);
        style_paper(s_compose_page.body);

        s_compose_page.recipient = create_text(s_compose_page.body, kContentWidth);
        lv_obj_set_pos(s_compose_page.recipient, kMargin, 8);

        s_compose_page.textarea = lv_textarea_create(s_compose_page.body);
        lv_obj_set_pos(s_compose_page.textarea, kMargin, 30);
        lv_obj_set_size(s_compose_page.textarea, kContentWidth, 112);
        style_compose_textarea(s_compose_page.textarea);
        lv_textarea_set_max_length(s_compose_page.textarea, 120);
        lv_textarea_set_placeholder_text(s_compose_page.textarea, "TYPE MESSAGE");
        lv_textarea_set_text(s_compose_page.textarea, flow.draft);
        lv_obj_add_event_cb(s_compose_page.textarea, on_compose_key, LV_EVENT_KEY, nullptr);

        s_compose_page.help = create_text(s_compose_page.body, kContentWidth);
        lv_obj_set_pos(s_compose_page.help, kMargin, 150);
        set_text(s_compose_page.help, "TAP CANCEL TO SAVE DRAFT");

        if (app_g != nullptr)
        {
            lv_group_add_obj(app_g, s_compose_page.textarea);
            lv_group_focus_obj(s_compose_page.textarea);
        }
    }

    const char* recipient = flow.snapshot.workspace_title.empty() ? "RECIPIENT" : flow.snapshot.workspace_title.c_str();
    std::snprintf(s_state.scratch, sizeof(s_state.scratch), "TO: %s", recipient);
    set_text(s_compose_page.recipient, s_state.scratch);
}

void configure_chat_compose_actions(ChatFlowState&, const char*)
{
    if (s_state.action_count < 3)
    {
        return;
    }

    set_action(0, "CANCEL", Action::Back);
    set_action(1, "CLEAR", Action::ChatDiscard);
    set_action(2, "SEND", Action::ChatSend);
    for (size_t index = 0; index < 3; ++index)
    {
        set_action_visible(index, true);
    }
    for (size_t index = 3; index < s_state.action_count; ++index)
    {
        set_action_visible(index, false);
    }
}

bool handle_chat_compose_action(Action action,
                                ChatFlowState& flow,
                                ::ui::chat::ChatWorkspaceModel* model,
                                const char* return_notice)
{
    switch (action)
    {
    case Action::Back:
        leave_chat_compose_page(flow, true);
        flow.route = ChatRoute::Conversation;
        set_notice("DRAFT SAVED");
        return true;
    case Action::ChatDiscard:
        clear_chat_compose_page_text();
        set_notice("DRAFT CLEARED");
        return true;
    case Action::ChatSend:
    {
        const char* const text = chat_compose_page_text();
        if (model == nullptr || text == nullptr || text[0] == '\0')
        {
            set_notice("TYPE A MESSAGE FIRST");
            return true;
        }
        if (!model->sendMessage(text).ok)
        {
            set_notice("SEND REJECTED");
            return true;
        }

        leave_chat_compose_page(flow, false);
        flow.route = ChatRoute::Conversation;
        set_notice(return_notice ? return_notice : "MESSAGE QUEUED");
        return true;
    }
    default:
        return false;
    }
}

} // namespace ui::mono::screens::screen_240x320::detail
