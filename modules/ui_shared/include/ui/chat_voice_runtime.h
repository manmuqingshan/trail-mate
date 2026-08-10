/**
 * @file chat_voice_runtime.h
 * @brief Narrow UI port for the isolated VMP voice-message feature.
 *
 * The shared chat UI depends on this port instead of a radio, MQTT, or chat
 * transport implementation. Registering an implementation is optional; on
 * devices without VMP support the compose screen simply has no voice action.
 *
 * A registered runtime and a runtime that is immediately ready to send are
 * intentionally different states.  The latter can be delayed while the
 * durable attachment inbox is restored after boot.  Compose uses the bound
 * state to keep its Voice control discoverable, and uses send readiness only
 * to report why a press cannot yet begin recording.
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

/** Local-only presentation state of one VMP voice attachment. */
enum class DeliveryState : uint8_t
{
    Received = 0U,
    Sending = 1U,
    Sent = 2U,
    Failed = 3U,
};

/** @brief A local-only summary of one VMP object for chat projection. */
struct MessageSummary
{
    uint64_t local_id = 0U;
    uint32_t sender_id = 0U;
    uint32_t target_id = 0U;
    uint32_t received_at_seconds = 0U;
    uint16_t duration_ms = 0U;
    bool private_message = false;
    bool source_unverified = false;
    bool outgoing = false;
    DeliveryState delivery = DeliveryState::Received;
    /** Stable local conversation binding; never VMP media bytes. */
    uint8_t presentation_protocol = 0U;
    uint8_t presentation_channel = 0U;
    bool read = true;
};

// One conversation projection holds eight summaries.  Keep this descriptor
// small and media-free: encoded audio belongs exclusively to the PSRAM VMP
// attachment store, never to the LVGL/controller working set.
static_assert(sizeof(MessageSummary) <= 32U,
              "Voice message UI summaries must remain within their fixed memory budget");

struct SendRequest
{
    uint32_t target_id = 0U;
    uint8_t presentation_protocol = 0U;
    uint8_t presentation_channel = 0U;
};

class IVoiceMessageRuntime
{
  public:
    virtual ~IVoiceMessageRuntime() = default;

    virtual bool isAvailable() const = 0;
    virtual bool canRecordAndSend() const = 0;
    virtual StartResult requestRecordAndSend(const SendRequest& request) = 0;
    /** Requests that the current press-to-talk capture stop after its current frame. */
    virtual bool requestStopRecording() = 0;
    /** True from accepted press-to-talk until its carrier attempt finishes. */
    virtual bool isOutboundActive() const = 0;
    /**
     * Returns newest-first local VMP messages in both directions.  The
     * presentation layer uses this to merge incoming and outgoing voice
     * attachment messages into one conversation timeline.
     */
    virtual std::size_t listMessages(MessageSummary* out_messages,
                                     std::size_t capacity) const = 0;
    virtual bool markConversationRead(uint8_t presentation_protocol,
                                      uint8_t presentation_channel,
                                      uint32_t peer_id,
                                      bool broadcast) = 0;
    virtual bool requestPlayback(uint64_t local_id) = 0;
};

/** @brief Binds the device-specific VMP service during platform startup. */
void setRuntime(IVoiceMessageRuntime* runtime);

/**
 * @brief True when this device has registered its isolated VMP integration.
 *
 * This is a UI affordance capability, not a readiness check: a bound Pager
 * keeps the compact Voice control visible while durable storage is restoring.
 */
bool isRuntimeBound();

/** @brief True only when this device has initialized an isolated VMP service. */
bool isAvailable();

/** @brief True when the active VMP carrier currently permits recording/sending. */
bool canRecordAndSend();

/** @brief Requests an asynchronous record-and-send operation through VMP only. */
StartResult requestRecordAndSend(const SendRequest& request);

/** @brief Ends the current press-to-talk capture without aborting a valid send. */
bool requestStopRecording();

/** @brief True while an accepted local voice capture/send is still active. */
bool isOutboundActive();

/**
 * @brief Retrieves newest-first local VMP summaries; never exposes audio bytes.
 *
 * The result contains both received and locally composed voice messages.  It
 * is intentionally named after a chat timeline, not an inbox, so future
 * attachment presenters do not mistake outgoing VMP records for unavailable
 * data.
 */
std::size_t listMessages(MessageSummary* out_messages, std::size_t capacity);

/** Persists read state for accepted VMP objects in one displayed thread. */
bool markConversationRead(uint8_t presentation_protocol,
                          uint8_t presentation_channel,
                          uint32_t peer_id,
                          bool broadcast);

/**
 * @deprecated Use listMessages().  Kept as a source-compatible wrapper for
 * older UI integrations; it also returns outgoing messages.
 */
std::size_t listReceivedMessages(MessageSummary* out_messages,
                                 std::size_t capacity);

/** @brief Requests asynchronous playback of a local VMP object. */
bool requestPlayback(uint64_t local_id);

} // namespace ui::chat_voice
