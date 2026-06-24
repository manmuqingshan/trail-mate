#include "ui/presentation_sources/runtime_chat_action_sink.h"

#include "chat_presentation_adapters/chat_conversation_mapper.h"

#include <string>

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
    if (message.text == nullptr || message.text_len == 0)
    {
        return ui::UiActionResult::fail(ui::UiActionFailure::InvalidInput);
    }

    ::chat::ConversationId core_id;
    if (!chat_presentation_adapters::toCoreConversationId(message.conversation,
                                                          core_id))
    {
        return ui::UiActionResult::fail(ui::UiActionFailure::Unsupported);
    }

    const std::string text(message.text, message.text_len);
    const ::chat::MeshSendResult result =
        chat_service_.sendTextToConversationDetailed(core_id, text);
    if (!result.ok || result.msg_id == 0)
    {
        return ui::UiActionResult::fail(mapMeshFailure(result.failure));
    }

    const ::chat::ChatMessage* sent = chat_service_.getMessage(result.msg_id);
    if (sent != nullptr && sent->status == ::chat::MessageStatus::Failed)
    {
        return ui::UiActionResult::fail(ui::UiActionFailure::Rejected);
    }
    return ui::UiActionResult::success();
}

ui::UiActionResult RuntimeChatActionSink::markRead(ui::chat::ConversationId id)
{
    ::chat::ConversationId core_id;
    if (!chat_presentation_adapters::toCoreConversationId(id, core_id))
    {
        return ui::UiActionResult::fail(ui::UiActionFailure::Unsupported);
    }

    chat_service_.markConversationRead(core_id);
    return ui::UiActionResult::success();
}

} // namespace ui::presentation_sources
