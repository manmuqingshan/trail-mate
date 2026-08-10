/**
 * @file chat_voice_runtime.cpp
 * @brief Global binding for the optional isolated VMP voice UI port.
 */

#include "ui/chat_voice_runtime.h"

namespace ui::chat_voice
{
namespace
{

IVoiceMessageRuntime* s_runtime = nullptr;

} // namespace

void setRuntime(IVoiceMessageRuntime* runtime)
{
    s_runtime = runtime;
}

bool isRuntimeBound()
{
    return s_runtime != nullptr;
}

bool isAvailable()
{
    return s_runtime && s_runtime->isAvailable();
}

bool canRecordAndSend()
{
    return s_runtime && s_runtime->canRecordAndSend();
}

StartResult requestRecordAndSend(const SendRequest& request)
{
    return s_runtime ? s_runtime->requestRecordAndSend(request)
                     : StartResult::Unsupported;
}

bool requestStopRecording()
{
    return s_runtime && s_runtime->requestStopRecording();
}

bool isOutboundActive()
{
    return s_runtime && s_runtime->isOutboundActive();
}

std::size_t listMessages(MessageSummary* out_messages, std::size_t capacity)
{
    return s_runtime ? s_runtime->listMessages(out_messages, capacity) : 0U;
}

bool markConversationRead(uint8_t presentation_protocol,
                          uint8_t presentation_channel,
                          uint32_t peer_id,
                          bool broadcast)
{
    return s_runtime && s_runtime->markConversationRead(presentation_protocol,
                                                        presentation_channel,
                                                        peer_id,
                                                        broadcast);
}

std::size_t listReceivedMessages(MessageSummary* out_messages,
                                 std::size_t capacity)
{
    return listMessages(out_messages, capacity);
}

bool requestPlayback(uint64_t local_id)
{
    return s_runtime && s_runtime->requestPlayback(local_id);
}

} // namespace ui::chat_voice
