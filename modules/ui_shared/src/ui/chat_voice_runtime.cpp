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

bool isAvailable()
{
    return s_runtime && s_runtime->isAvailable();
}

bool canRecordAndSend()
{
    return s_runtime && s_runtime->canRecordAndSend();
}

StartResult requestRecordAndSend(uint32_t target_id)
{
    return s_runtime ? s_runtime->requestRecordAndSend(target_id)
                     : StartResult::Unsupported;
}

std::size_t listReceivedMessages(MessageSummary* out_messages,
                                 std::size_t capacity)
{
    return s_runtime ? s_runtime->listReceivedMessages(out_messages, capacity) : 0U;
}

bool requestPlayback(uint64_t local_id)
{
    return s_runtime && s_runtime->requestPlayback(local_id);
}

} // namespace ui::chat_voice
