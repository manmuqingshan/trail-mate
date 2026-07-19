#include "ui/presentation_sources/runtime_chat_action_sink.h"

#include "chat_presentation_adapters/chat_conversation_mapper.h"

#include <chrono>
#include <cstdio>
#include <string>

#ifndef CHAT_UI_SEND_TRACE_ENABLE
#define CHAT_UI_SEND_TRACE_ENABLE 1
#endif

#if CHAT_UI_SEND_TRACE_ENABLE
#define CHAT_ACTION_TRACE(...) std::printf(__VA_ARGS__)
#else
#define CHAT_ACTION_TRACE(...)
#endif

namespace ui::presentation_sources
{
namespace
{

ui::UiActionFailure mapMeshFailure(::chat::MeshOperationFailure failure)
{
    switch (failure)
    {
    case ::chat::MeshOperationFailure::InvalidInput:
        return ui::UiActionFailure::InvalidInput;
    case ::chat::MeshOperationFailure::Unsupported:
        return ui::UiActionFailure::Unsupported;
    case ::chat::MeshOperationFailure::NotReady:
        return ui::UiActionFailure::NotReady;
    case ::chat::MeshOperationFailure::TxDisabled:
        return ui::UiActionFailure::TxDisabled;
    case ::chat::MeshOperationFailure::RadioOffline:
        return ui::UiActionFailure::RadioOffline;
    case ::chat::MeshOperationFailure::DutyCycleLimited:
        return ui::UiActionFailure::DutyCycleLimited;
    case ::chat::MeshOperationFailure::LocalIdentityMissing:
        return ui::UiActionFailure::LocalIdentityMissing;
    case ::chat::MeshOperationFailure::PeerKeyMissing:
        return ui::UiActionFailure::PeerKeyMissing;
    case ::chat::MeshOperationFailure::ChannelKeyMissing:
        return ui::UiActionFailure::ChannelKeyMissing;
    case ::chat::MeshOperationFailure::Busy:
        return ui::UiActionFailure::Busy;
    case ::chat::MeshOperationFailure::RadioTxFailed:
        return ui::UiActionFailure::RadioTxFailed;
    case ::chat::MeshOperationFailure::EncodeFailed:
    case ::chat::MeshOperationFailure::CryptoFailed:
    case ::chat::MeshOperationFailure::Unknown:
    case ::chat::MeshOperationFailure::None:
    default:
        return ui::UiActionFailure::Rejected;
    }
}

} // namespace

RuntimeChatActionSink::RuntimeChatActionSink(::chat::ChatService& chat_service)
    : chat_service_(chat_service)
{
}

ui::UiActionResult RuntimeChatActionSink::selectConversation(
    ui::chat::ConversationId id)
{
    ::chat::ConversationId core_id;
    if (!chat_presentation_adapters::toCoreConversationId(id, core_id))
    {
        return ui::UiActionResult::fail(ui::UiActionFailure::Unsupported);
    }

    chat_service_.switchChannel(core_id.channel);
    return ui::UiActionResult::success();
}

ui::UiActionResult RuntimeChatActionSink::sendMessage(
    const ui::chat::SendMessageView& message)
{
    const auto started = std::chrono::steady_clock::now();
    CHAT_ACTION_TRACE("[ChatUiTrace] stage=runtime_sink begin kind=%u protocol=%u primary=%lu secondary=%lu text_len=%u\n",
                      static_cast<unsigned>(message.conversation.kind),
                      static_cast<unsigned>(message.conversation.protocol),
                      static_cast<unsigned long>(message.conversation.primary),
                      static_cast<unsigned long>(message.conversation.secondary),
                      static_cast<unsigned>(message.text_len));
    if (message.text == nullptr || message.text_len == 0)
    {
        CHAT_ACTION_TRACE("[ChatUiTrace] stage=runtime_sink reject reason=invalid_text elapsed_ms=%lld\n",
                          static_cast<long long>(
                              std::chrono::duration_cast<std::chrono::milliseconds>(
                                  std::chrono::steady_clock::now() - started)
                                  .count()));
        return ui::UiActionResult::fail(ui::UiActionFailure::InvalidInput);
    }

    ::chat::ConversationId core_id;
    if (!chat_presentation_adapters::toCoreConversationId(message.conversation,
                                                          core_id))
    {
        CHAT_ACTION_TRACE("[ChatUiTrace] stage=runtime_sink reject reason=conversation_map elapsed_ms=%lld\n",
                          static_cast<long long>(
                              std::chrono::duration_cast<std::chrono::milliseconds>(
                                  std::chrono::steady_clock::now() - started)
                                  .count()));
        return ui::UiActionResult::fail(ui::UiActionFailure::Unsupported);
    }
    CHAT_ACTION_TRACE("[ChatUiTrace] stage=runtime_sink mapped protocol=%u channel=%u peer=%08lX elapsed_ms=%lld\n",
                      static_cast<unsigned>(core_id.protocol),
                      static_cast<unsigned>(core_id.channel),
                      static_cast<unsigned long>(core_id.peer),
                      static_cast<long long>(
                          std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - started)
                              .count()));

    const std::string text(message.text, message.text_len);
    CHAT_ACTION_TRACE("[ChatUiTrace] stage=runtime_sink service begin elapsed_ms=%lld\n",
                      static_cast<long long>(
                          std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - started)
                              .count()));
    const ::chat::MeshSendResult result =
        chat_service_.sendTextToConversationDetailed(core_id, text);
    CHAT_ACTION_TRACE("[ChatUiTrace] stage=runtime_sink service done ok=%u msg=%lu failure=%u elapsed_ms=%lld\n",
                      result.ok ? 1U : 0U,
                      static_cast<unsigned long>(result.msg_id),
                      static_cast<unsigned>(result.failure),
                      static_cast<long long>(
                          std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - started)
                              .count()));
    if (!result.ok || result.msg_id == 0)
    {
        CHAT_ACTION_TRACE("[ChatUiTrace] stage=runtime_sink reject reason=service failure=%u elapsed_ms=%lld\n",
                          static_cast<unsigned>(result.failure),
                          static_cast<long long>(
                              std::chrono::duration_cast<std::chrono::milliseconds>(
                                  std::chrono::steady_clock::now() - started)
                                  .count()));
        return ui::UiActionResult::fail(mapMeshFailure(result.failure));
    }

    CHAT_ACTION_TRACE("[ChatUiTrace] stage=runtime_sink lookup begin msg=%lu elapsed_ms=%lld\n",
                      static_cast<unsigned long>(result.msg_id),
                      static_cast<long long>(
                          std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - started)
                              .count()));
    const ::chat::ChatMessage* sent = chat_service_.getMessage(result.msg_id);
    CHAT_ACTION_TRACE("[ChatUiTrace] stage=runtime_sink lookup done found=%u status=%u elapsed_ms=%lld\n",
                      sent ? 1U : 0U,
                      sent ? static_cast<unsigned>(sent->status) : 0U,
                      static_cast<long long>(
                          std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - started)
                              .count()));
    if (sent != nullptr && sent->status == ::chat::MessageStatus::Failed)
    {
        CHAT_ACTION_TRACE("[ChatUiTrace] stage=runtime_sink reject reason=stored_failed elapsed_ms=%lld\n",
                          static_cast<long long>(
                              std::chrono::duration_cast<std::chrono::milliseconds>(
                                  std::chrono::steady_clock::now() - started)
                                  .count()));
        return ui::UiActionResult::fail(ui::UiActionFailure::Rejected);
    }
    CHAT_ACTION_TRACE("[ChatUiTrace] stage=runtime_sink end elapsed_ms=%lld\n",
                      static_cast<long long>(
                          std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - started)
                              .count()));
    return ui::UiActionResult::success();
}

ui::UiActionResult RuntimeChatActionSink::markRead(ui::chat::ConversationId id)
{
    ::chat::ConversationId core_id;
    if (!chat_presentation_adapters::toCoreConversationId(id, core_id))
    {
        return ui::UiActionResult::fail(ui::UiActionFailure::Unsupported);
    }

    if (!chat_service_.markConversationRead(core_id))
    {
        return ui::UiActionResult::fail(ui::UiActionFailure::Rejected);
    }
    return ui::UiActionResult::success();
}

} // namespace ui::presentation_sources
