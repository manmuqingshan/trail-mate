#include "screen_app_internal.h"

#include <cstdio>
#include <cstdlib>
#include <new>

#if defined(ESP_PLATFORM)
#include <esp_heap_caps.h>
#endif

namespace ui::mono::screens::screen_240x320::detail
{
namespace
{

struct ChatRuntime
{
    ::chat::ChatService* service = nullptr;
    std::unique_ptr<::ui::presentation_sources::ChatPresentationSource> source;
    std::unique_ptr<::ui::presentation_sources::RuntimeChatActionSink> sink;
    std::unique_ptr<::ui::chat::ChatWorkspaceModel> model;

    ::ui::chat::ChatWorkspaceModel* ensure()
    {
        if (!::app::hasAppFacade())
        {
            return nullptr;
        }

        auto& facade = ::app::messagingFacade();
        ::chat::ChatService& current_service = facade.getChatService();
        if (model && service == &current_service)
        {
            return model.get();
        }

        service = &current_service;
        source = std::unique_ptr<::ui::presentation_sources::ChatPresentationSource>(
            new ::ui::presentation_sources::ChatPresentationSource(
                current_service,
                &facade.getContactService(),
                nullptr,
                facade.getMeshAdapter()));
        sink = std::unique_ptr<::ui::presentation_sources::RuntimeChatActionSink>(
            new ::ui::presentation_sources::RuntimeChatActionSink(current_service));
        model = std::unique_ptr<::ui::chat::ChatWorkspaceModel>(
            new ::ui::chat::ChatWorkspaceModel(*source, *sink));
        return model.get();
    }
};

struct PendingChatRoute
{
    ::ui::chat::ConversationId conversation{};
    bool pending = false;
};

ChatRuntime s_chat_runtime;
ChatFlowState* s_direct_chat_flow = nullptr;
ChatFlowState* s_team_chat_flow = nullptr;
bool s_chat_flow_allocation_failed_logged = false;
PendingChatRoute s_pending_chat_route;

ChatFlowState* allocate_chat_flow()
{
#if defined(ESP_PLATFORM)
    void* const storage = heap_caps_malloc(sizeof(ChatFlowState), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    void* const storage = std::malloc(sizeof(ChatFlowState));
#endif
    return storage ? new (storage) ChatFlowState{} : nullptr;
}

bool ensure_chat_flow(ChatFlowState*& flow, const char* owner)
{
    if (flow != nullptr)
    {
        return true;
    }

    flow = allocate_chat_flow();
    if (flow != nullptr)
    {
        return true;
    }

    if (!s_chat_flow_allocation_failed_logged)
    {
        s_chat_flow_allocation_failed_logged = true;
        std::printf("[UI][Chat] page enter denied owner=%s reason=psram_state_alloc bytes=%u\n",
                    owner ? owner : "unknown",
                    static_cast<unsigned>(sizeof(ChatFlowState)));
    }
    return false;
}

void release_chat_flow(ChatFlowState*& flow)
{
    if (flow == nullptr)
    {
        return;
    }

    leave_chat_compose_page(*flow, false);
    flow->~ChatFlowState();
#if defined(ESP_PLATFORM)
    heap_caps_free(flow);
#else
    std::free(flow);
#endif
    flow = nullptr;
}

void render_chat_unavailable()
{
    set_text(s_state.title, "CHAT");
    set_text(s_state.subtitle, "OFFLINE");
    set_line(0, "CHAT SERVICE UNAVAILABLE");
    set_line(1, "WAIT FOR MESSAGING RUNTIME");
    set_line(2, s_state.notice[0] != '\0' ? s_state.notice : "BACK TO RETURN");
    clear_lines_from(3);
}

bool build_chat_snapshot(ChatFlowState& flow, ::ui::chat::ChatWorkspaceModel* model)
{
    return model != nullptr && model->buildSnapshot(flow.snapshot);
}

void render_chat_message_list_page(ChatFlowState& flow)
{
    const ::ui::chat::ChatWorkspaceSnapshot& snapshot = flow.snapshot;
    set_text(s_state.title, "CHAT");
    set_text(s_state.subtitle, "LIST");
    if (snapshot.conversation_count == 0)
    {
        set_line(0, "NO CONVERSATIONS");
        set_line(1, "RADIO MESSAGES APPEAR HERE");
        set_line(2, s_state.notice[0] != '\0' ? s_state.notice : "OPEN AFTER A MESSAGE ARRIVES");
        clear_lines_from(3);
        return;
    }

    if (flow.selected_index >= snapshot.conversation_count)
    {
        flow.selected_index = snapshot.conversation_count - 1U;
    }
    set_linef(0,
              "CONVERSATIONS %u  SELECT %u",
              static_cast<unsigned>(snapshot.conversation_count),
              static_cast<unsigned>(flow.selected_index + 1U));

    size_t line = 1;
    for (size_t index = 0; index < snapshot.conversation_count && line < 9; ++index)
    {
        const auto& conversation = snapshot.conversations[index];
        set_linef(line++,
                  "%c %02u %s %s%s",
                  index == flow.selected_index ? '>' : ' ',
                  static_cast<unsigned>(index + 1U),
                  conversation.title.c_str(),
                  conversation.unread_count > 0 ? "NEW " : "",
                  conversation.subtitle.c_str());
    }
    while (line < 9)
    {
        set_line(line++, "");
    }
    set_line(9, s_state.notice[0] != '\0' ? s_state.notice : "PREV/NEXT SELECT  OPEN READ");
}

bool open_selected_chat_conversation(ChatFlowState& flow, ::ui::chat::ChatWorkspaceModel* model)
{
    if (model == nullptr || flow.snapshot.conversation_count == 0 ||
        flow.selected_index >= flow.snapshot.conversation_count)
    {
        set_notice("NO CONVERSATION SELECTED");
        return false;
    }

    const ::ui::chat::ConversationId conversation = flow.snapshot.conversations[flow.selected_index].id;
    if (!model->selectConversation(conversation).ok)
    {
        set_notice("CONVERSATION UNAVAILABLE");
        return false;
    }
    (void)model->markRead(conversation);
    flow.route = ChatRoute::Conversation;
    set_notice("CONVERSATION OPEN");
    return true;
}

} // namespace

ChatFlowState& direct_chat_flow()
{
    return *s_direct_chat_flow;
}

ChatFlowState& team_chat_flow()
{
    return *s_team_chat_flow;
}

bool ensure_team_chat_flow()
{
    // Team owns an independent snapshot/navigation flow so direct-chat
    // selection and drafts cannot leak into a team conversation.  The flow is
    // retained only while Team is open.
    return ensure_chat_flow(s_team_chat_flow, "team");
}

void reset_direct_chat_flow()
{
    release_chat_flow(s_direct_chat_flow);
}

void reset_team_chat_flow()
{
    release_chat_flow(s_team_chat_flow);
}

void render_chat()
{
    reset_chat_conversation_page();
    if (!ensure_chat_flow(s_direct_chat_flow, "direct"))
    {
        set_text(s_state.title, "CHAT");
        set_text(s_state.subtitle, "MEMORY");
        set_line(0, "PSRAM REQUIRED FOR CHAT");
        set_line(1, "RETURN AFTER MEMORY RECOVERS");
        clear_lines_from(2);
        return;
    }
    ChatFlowState& flow = direct_chat_flow();
    ::ui::chat::ChatWorkspaceModel* const model = s_chat_runtime.ensure();
    if (model != nullptr && s_pending_chat_route.pending)
    {
        const ::ui::chat::ConversationId route = s_pending_chat_route.conversation;
        s_pending_chat_route = PendingChatRoute{};
        if (model->selectConversation(route).ok)
        {
            (void)model->markRead(route);
            flow.route = ChatRoute::Conversation;
            set_notice("CONTACT CONVERSATION OPEN");
        }
        else
        {
            set_notice("CONVERSATION UNAVAILABLE");
        }
    }

    if (!build_chat_snapshot(flow, model))
    {
        render_chat_unavailable();
        return;
    }

    switch (flow.route)
    {
    case ChatRoute::ConversationList:
        render_chat_message_list_page(flow);
        break;
    case ChatRoute::Conversation:
        render_chat_conversation_page(flow, "CHAT");
        break;
    case ChatRoute::Compose:
        render_chat_compose_page(flow, "CHAT");
        break;
    }
}

void configure_chat_actions()
{
    if (s_state.adapter == nullptr || s_state.adapter->page_kind() != PageKind::Chat ||
        s_state.action_count < 5 || !ensure_chat_flow(s_direct_chat_flow, "direct"))
    {
        return;
    }

    ChatFlowState& flow = direct_chat_flow();
    switch (flow.route)
    {
    case ChatRoute::ConversationList:
        set_action(0, "PREV", Action::ChatPrevious);
        set_action(1, "NEXT", Action::ChatNext);
        set_action(2, "OPEN", Action::ChatOpen);
        set_action(3, "SYNC", Action::Refresh);
        set_action(4, "BACK", Action::Back);
        break;
    case ChatRoute::Conversation:
        set_action(0, "OLDER", Action::ChatPrevious);
        set_action(1, "NEWER", Action::ChatNext);
        set_action(2, "COMPOSE", Action::ChatType);
        set_action(3, "SYNC", Action::Refresh);
        set_action(4, "BACK", Action::Back);
        break;
    case ChatRoute::Compose:
        configure_chat_compose_actions(flow, "CANCEL");
        return;
    }

    for (size_t index = 0; index < 5; ++index)
    {
        set_action_visible(index, true);
    }
}

void add_chat_actions()
{
    add_action("PREV", Action::ChatPrevious, kMargin, kActionTop, 48);
    add_action("NEXT", Action::ChatNext, 62, kActionTop, 48);
    add_action("OPEN", Action::ChatOpen, 116, kActionTop, 54);
    add_action("SYNC", Action::Refresh, 176, kActionTop, 56);
    add_action("BACK", Action::Back, kMargin, kActionTop + 22, 48);
}

bool queue_chat_conversation(const ::ui::chat::ConversationId& conversation)
{
    if (!conversation.isValid())
    {
        return false;
    }
    s_pending_chat_route.conversation = conversation;
    s_pending_chat_route.pending = true;
    return true;
}

void cancel_queued_chat_conversation()
{
    s_pending_chat_route = PendingChatRoute{};
}

bool handle_chat_action(Action action)
{
    if (!ensure_chat_flow(s_direct_chat_flow, "direct"))
    {
        set_notice("CHAT MEMORY UNAVAILABLE");
        return action != Action::Back;
    }
    ChatFlowState& flow = direct_chat_flow();
    ::ui::chat::ChatWorkspaceModel* const model = s_chat_runtime.ensure();
    if (flow.route == ChatRoute::Compose)
    {
        return handle_chat_compose_action(action, flow, model, "MESSAGE QUEUED");
    }

    switch (action)
    {
    case Action::Back:
        if (flow.route == ChatRoute::Conversation)
        {
            flow.route = ChatRoute::ConversationList;
            set_notice("CONVERSATION LIST");
            return true;
        }
        return false;
    case Action::ChatPrevious:
    case Action::ChatNext:
        if (model == nullptr)
        {
            set_notice("CHAT SERVICE UNAVAILABLE");
            return true;
        }
        if (flow.route == ChatRoute::Conversation)
        {
            const uint16_t offset = model->messageOffset();
            const uint16_t next = action == Action::ChatPrevious
                                      ? (offset < 10U ? 0U : static_cast<uint16_t>(offset - 10U))
                                      : static_cast<uint16_t>(offset + 10U);
            model->setMessageOffset(next);
            set_notice(action == Action::ChatPrevious ? "OLDER MESSAGES" : "NEWER MESSAGES");
            return true;
        }
        if (flow.snapshot.conversation_count == 0)
        {
            set_notice("NO CONVERSATIONS");
            return true;
        }
        flow.selected_index = action == Action::ChatPrevious
                                  ? (flow.selected_index == 0
                                         ? flow.snapshot.conversation_count - 1U
                                         : flow.selected_index - 1U)
                                  : (flow.selected_index + 1U) % flow.snapshot.conversation_count;
        set_notice("");
        return true;
    case Action::ChatOpen:
        return open_selected_chat_conversation(flow, model) || true;
    case Action::ChatType:
        if (flow.route != ChatRoute::Conversation || !flow.snapshot.composer_enabled)
        {
            set_notice("COMPOSER UNAVAILABLE");
            return true;
        }
        enter_chat_compose_page(flow);
        set_notice("TYPE MESSAGE");
        return true;
    case Action::ChatList:
        flow.route = ChatRoute::ConversationList;
        set_notice("CONVERSATION LIST");
        return true;
    default:
        return false;
    }
}

} // namespace ui::mono::screens::screen_240x320::detail
