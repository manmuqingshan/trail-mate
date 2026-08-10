/**
 * @file chat_voice_runtime.h
 * @brief Narrow UI port for the isolated VMP voice-message feature.
 *
 * The shared chat UI depends on this port instead of a radio, MQTT, or chat
 * transport implementation. Registering an implementation is optional; on
 * devices without VMP support the compose screen simply has no voice action.
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace ui::chat_voice
{

enum class StartResult : uint8_t
{
    Queued = 1,
    Unsupported = 2,
    Busy = 3,
    PrivateContactUnverified = 4,
};

/** @brief A local-only summary of a received VMP object for chat projection. */
struct MessageSummary
{
    uint64_t local_id = 0U;
    uint32_t sender_id = 0U;
    uint32_t target_id = 0U;
    uint32_t received_at_seconds = 0U;
    bool private_message = false;
    bool source_unverified = false;
};

class IVoiceMessageRuntime
{
  public:
    virtual ~IVoiceMessageRuntime() = default;

    virtual bool isAvailable() const = 0;
    virtual bool canRecordAndSend() const = 0;
    virtual StartResult requestRecordAndSend(uint32_t target_id) = 0;
    virtual std::size_t listReceivedMessages(MessageSummary* out_messages,
                                             std::size_t capacity) const = 0;
    virtual bool requestPlayback(uint64_t local_id) = 0;
};

/** @brief Binds the device-specific VMP service during platform startup. */
void setRuntime(IVoiceMessageRuntime* runtime);

/** @brief True only when this device has initialized an isolated VMP service. */
bool isAvailable();

/** @brief True when the active VMP carrier currently permits recording/sending. */
bool canRecordAndSend();

/** @brief Requests an asynchronous record-and-send operation through VMP only. */
StartResult requestRecordAndSend(uint32_t target_id);

/** @brief Retrieves newest-first local VMP summaries; never exposes audio bytes. */
std::size_t listReceivedMessages(MessageSummary* out_messages,
                                 std::size_t capacity);

/** @brief Requests asynchronous playback of a local VMP object. */
bool requestPlayback(uint64_t local_id);

} // namespace ui::chat_voice
