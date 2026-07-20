#include "ui_presentation/chat/chat_workspace_model.h"

#include <chrono>
#include <cstdio>
#include <cstring>

#ifndef CHAT_UI_SEND_TRACE_ENABLE
#define CHAT_UI_SEND_TRACE_ENABLE 0
#endif

#if CHAT_UI_SEND_TRACE_ENABLE
#define CHAT_WORKSPACE_TRACE(...) std::printf(__VA_ARGS__)
#else
#define CHAT_WORKSPACE_TRACE(...)
#endif

namespace ui::chat
{

ChatWorkspaceModel::ChatWorkspaceModel(IChatPresentationSource& source,
                                       IChatActionSink& sink)
    : source_(source),
      sink_(sink)
{
}

bool ChatWorkspaceModel::buildSnapshot(ChatWorkspaceSnapshot& out) const
{
    const auto started = std::chrono::steady_clock::now();
    ChatWorkspaceRequest request;
    request.selected = selected_;
    request.conversation_offset = conversation_offset_;
    request.message_offset = message_offset_;
    CHAT_WORKSPACE_TRACE("[ChatUiTrace] stage=workspace_snapshot begin kind=%u protocol=%u primary=%lu secondary=%lu conv_offset=%u msg_offset=%u\n",
                         static_cast<unsigned>(selected_.kind),
                         static_cast<unsigned>(selected_.protocol),
                         static_cast<unsigned long>(selected_.primary),
                         static_cast<unsigned long>(selected_.secondary),
                         static_cast<unsigned>(conversation_offset_),
                         static_cast<unsigned>(message_offset_));

    if (!source_.buildChatWorkspaceSnapshot(request, out))
    {
        resetChatWorkspaceSnapshot(out);
        out.header.valid = false;
        CHAT_WORKSPACE_TRACE("[ChatUiTrace] stage=workspace_snapshot reject source=0 elapsed_ms=%lld\n",
                             static_cast<long long>(
                                 std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now() - started)
                                     .count()));
        return false;
    }
    CHAT_WORKSPACE_TRACE("[ChatUiTrace] stage=workspace_snapshot end valid=%u conversations=%u messages=%u total=%u elapsed_ms=%lld\n",
                         out.header.valid ? 1U : 0U,
                         static_cast<unsigned>(out.conversation_count),
                         static_cast<unsigned>(out.message_count),
                         static_cast<unsigned>(out.message_total_count),
                         static_cast<long long>(
                             std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - started)
                                 .count()));
    return out.header.valid;
}

ChatWorkspaceSnapshot ChatWorkspaceModel::snapshot() const
{
    ChatWorkspaceSnapshot out{};
    (void)buildSnapshot(out);
    return out;
}

ui::UiActionResult ChatWorkspaceModel::selectConversation(ConversationId id)
{
    if (!id.isValid())
    {
        return ui::UiActionResult::fail(ui::UiActionFailure::InvalidInput);
    }

    selected_ = id;
    message_offset_ = 0;
    return sink_.selectConversation(id);
}

ui::UiActionResult ChatWorkspaceModel::sendMessage(const char* text)
{
    const auto started = std::chrono::steady_clock::now();
    CHAT_WORKSPACE_TRACE("[ChatUiTrace] stage=workspace_send begin selected=%u kind=%u protocol=%u primary=%lu secondary=%lu text_len=%u\n",
                         selected_.isValid() ? 1U : 0U,
                         static_cast<unsigned>(selected_.kind),
                         static_cast<unsigned>(selected_.protocol),
                         static_cast<unsigned long>(selected_.primary),
                         static_cast<unsigned long>(selected_.secondary),
                         text ? static_cast<unsigned>(std::strlen(text)) : 0U);
    if (!selected_.isValid() || text == nullptr || text[0] == '\0')
    {
        CHAT_WORKSPACE_TRACE("[ChatUiTrace] stage=workspace_send reject reason=invalid_input elapsed_ms=%lld\n",
                             static_cast<long long>(
                                 std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now() - started)
                                     .count()));
        return ui::UiActionResult::fail(ui::UiActionFailure::InvalidInput);
    }

    SendMessageView message;
    message.conversation = selected_;
    message.text = text;
    message.text_len = std::strlen(text);
    const ui::UiActionResult result = sink_.sendMessage(message);
    CHAT_WORKSPACE_TRACE("[ChatUiTrace] stage=workspace_send end ok=%u failure=%u elapsed_ms=%lld\n",
                         result.ok ? 1U : 0U,
                         static_cast<unsigned>(result.failure),
                         static_cast<long long>(
                             std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - started)
                                 .count()));
    return result;
}

ui::UiActionResult ChatWorkspaceModel::markRead(ConversationId id)
{
    if (!id.isValid())
    {
        return ui::UiActionResult::fail(ui::UiActionFailure::InvalidInput);
    }
    return sink_.markRead(id);
}

ConversationId ChatWorkspaceModel::selectedConversation() const
{
    return selected_;
}

void ChatWorkspaceModel::setConversationOffset(uint16_t offset)
{
    conversation_offset_ = offset;
}

void ChatWorkspaceModel::setMessageOffset(uint16_t offset)
{
    message_offset_ = offset;
}

uint16_t ChatWorkspaceModel::conversationOffset() const
{
    return conversation_offset_;
}

uint16_t ChatWorkspaceModel::messageOffset() const
{
    return message_offset_;
}

} // namespace ui::chat
