/**
 * @file vmp_voice_inbox.h
 * @brief Fixed local-only VMP voice-object inbox.
 *
 * This deliberately stores already-validated encoded media and has no mesh,
 * radio, MQTT, or forwarding API.  A receive-side VMP implementation can only
 * persist, present, or play an object through this type; it cannot re-originate
 * it onto any air interface.
 */

#pragma once

#include "chat/infra/voice/vmp_wire.h"

#include <cstddef>
#include <cstdint>

namespace chat::voice::vmp
{

inline constexpr std::size_t kVoiceInboxCapacity = 8U;

struct VoiceMessageMetadata
{
    uint64_t local_id = 0U;
    uint32_t sender_id = 0U;
    uint32_t target_id = 0U;
    uint64_t session_id = 0U;
    uint32_t object_fingerprint = 0U;
    uint32_t received_at_seconds = 0U;
    uint16_t encoded_media_len = 0U;
    Codec codec = Codec::Codec2_1300;
    DeliveryMode mode = DeliveryMode::Private;
    bool source_unverified = false;
    bool complete = false;
};

struct VoiceMessageView
{
    VoiceMessageMetadata metadata{};
    const uint8_t* encoded_media = nullptr;
};

enum class VoiceInboxStoreResult : uint8_t
{
    Stored = 1,
    Duplicate = 2,
    Invalid = 3,
};

/**
 * @brief Bounded, replacement-on-oldest local VMP inbox.
 *
 * ESP callers must own this as static/runtime storage because each slot holds
 * one encoded voice object.  Replacing an old slot securely clears its encoded
 * media first.  Caller-owned durable storage may mirror accepted entries.
 */
class VoiceMessageInbox final
{
  public:
    VoiceInboxStoreResult store(const ControlFrame& control,
                                const uint8_t* encoded_media,
                                std::size_t encoded_media_len,
                                bool complete,
                                uint32_t received_at_seconds,
                                uint64_t* out_local_id = nullptr);

    bool get(uint64_t local_id, VoiceMessageView* out_view) const;

    /**
     * @brief Copies metadata newest first without exposing encoded media.
     *
     * This is intentionally a presentation-only operation. The returned
     * metadata cannot be used to forward, serialize, or recreate a VMP radio
     * frame.
     */
    std::size_t listMetadata(VoiceMessageMetadata* out_metadata,
                             std::size_t capacity) const;

    /**
     * Restore one authenticated, completed local object from durable local
     * storage. This is intentionally not a wire ingress API: callers must
     * have already validated the record checksum and the object must have
     * originated from a previously accepted inbox entry.
     *
     * The original local ID is retained so a chat projection's playback
     * target survives a reboot. Restored records participate in the ordinary
     * duplicate check and bounded oldest-entry replacement policy.
     */
    bool restore(const VoiceMessageMetadata& metadata,
                 const uint8_t* encoded_media,
                 std::size_t encoded_media_len);

    bool erase(uint64_t local_id);
    void clear();

    [[nodiscard]] std::size_t size() const { return size_; }

  private:
    struct Slot
    {
        VoiceMessageMetadata metadata{};
        uint8_t encoded_media[kMaxEncodedMediaSize] = {};
        uint64_t insertion_sequence = 0U;
        bool occupied = false;
    };

    bool isDuplicate(const ControlFrame& control) const;
    Slot* selectDestination();
    static void clearSlot(Slot* slot);

    Slot slots_[kVoiceInboxCapacity] = {};
    std::size_t size_ = 0U;
    uint64_t next_local_id_ = 1U;
    uint64_t next_insertion_sequence_ = 1U;
};

} // namespace chat::voice::vmp
