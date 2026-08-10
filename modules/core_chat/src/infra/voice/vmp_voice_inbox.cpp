/**
 * @file vmp_voice_inbox.cpp
 * @brief Fixed local-only VMP voice-object inbox.
 */

#include "chat/infra/voice/vmp_voice_inbox.h"

#include <cstring>

namespace chat::voice::vmp
{
namespace
{

void secureClear(uint8_t* bytes, std::size_t size)
{
    volatile uint8_t* cursor = bytes;
    while (cursor && size-- != 0U)
    {
        *cursor++ = 0U;
    }
}

bool validInboundControl(const ControlFrame& control, DeliveryMode* out_mode)
{
    return out_mode && isValidControlFrame(control) &&
           control.type != ControlType::Cancel &&
           deliveryModeFor(control, out_mode);
}

bool validRestoredMetadata(const VoiceMessageMetadata& metadata,
                           std::size_t encoded_media_len)
{
    if (metadata.local_id == 0U || !metadata.complete ||
        encoded_media_len == 0U || encoded_media_len > kMaxEncodedMediaSize ||
        encoded_media_len != metadata.encoded_media_len)
    {
        return false;
    }

    if (metadata.codec != Codec::Codec2_1300)
    {
        return false;
    }

    if (metadata.mode == DeliveryMode::Broadcast)
    {
        return metadata.target_id == kBroadcastTargetId &&
               metadata.source_unverified;
    }
    if (metadata.mode == DeliveryMode::Private)
    {
        return metadata.sender_id != 0U && metadata.target_id != 0U &&
               metadata.target_id != kBroadcastTargetId &&
               !metadata.source_unverified;
    }
    return false;
}

uint64_t nextNonZero(uint64_t value)
{
    ++value;
    return value == 0U ? 1U : value;
}

} // namespace

VoiceInboxStoreResult VoiceMessageInbox::store(const ControlFrame& control,
                                               const uint8_t* encoded_media,
                                               std::size_t encoded_media_len,
                                               bool complete,
                                               uint32_t received_at_seconds,
                                               uint64_t* out_local_id)
{
    if (out_local_id)
    {
        *out_local_id = 0U;
    }

    DeliveryMode mode = DeliveryMode::Private;
    if (!encoded_media || encoded_media_len == 0U ||
        encoded_media_len > kMaxEncodedMediaSize ||
        encoded_media_len != control.encoded_media_len ||
        !validInboundControl(control, &mode))
    {
        return VoiceInboxStoreResult::Invalid;
    }
    if (isDuplicate(control))
    {
        return VoiceInboxStoreResult::Duplicate;
    }

    Slot* const destination = selectDestination();
    if (!destination)
    {
        return VoiceInboxStoreResult::Invalid;
    }
    const bool replacing = destination->occupied;
    if (replacing)
    {
        clearSlot(destination);
    }

    destination->metadata.local_id = next_local_id_++;
    if (next_local_id_ == 0U)
    {
        next_local_id_ = 1U;
    }
    destination->metadata.sender_id = control.sender_id;
    destination->metadata.target_id = control.target_id;
    destination->metadata.session_id = control.session_id;
    destination->metadata.object_fingerprint = control.object_fingerprint;
    destination->metadata.received_at_seconds = received_at_seconds;
    destination->metadata.encoded_media_len =
        static_cast<uint16_t>(encoded_media_len);
    destination->metadata.codec = control.codec;
    destination->metadata.mode = mode;
    destination->metadata.source_unverified = mode == DeliveryMode::Broadcast;
    destination->metadata.complete = complete;
    std::memcpy(destination->encoded_media, encoded_media, encoded_media_len);
    destination->insertion_sequence = next_insertion_sequence_++;
    if (next_insertion_sequence_ == 0U)
    {
        next_insertion_sequence_ = 1U;
    }
    destination->occupied = true;
    if (!replacing)
    {
        ++size_;
    }
    if (out_local_id)
    {
        *out_local_id = destination->metadata.local_id;
    }
    return VoiceInboxStoreResult::Stored;
}

bool VoiceMessageInbox::get(uint64_t local_id, VoiceMessageView* out_view) const
{
    if (!out_view || local_id == 0U)
    {
        return false;
    }
    for (const Slot& slot : slots_)
    {
        if (slot.occupied && slot.metadata.local_id == local_id)
        {
            out_view->metadata = slot.metadata;
            out_view->encoded_media = slot.encoded_media;
            return true;
        }
    }
    return false;
}

std::size_t VoiceMessageInbox::listMetadata(VoiceMessageMetadata* out_metadata,
                                            std::size_t capacity) const
{
    if (!out_metadata || capacity == 0U)
    {
        return 0U;
    }

    std::size_t written = 0U;
    uint64_t preceding_sequence = ~uint64_t{0};
    while (written < capacity)
    {
        const Slot* next = nullptr;
        for (const Slot& slot : slots_)
        {
            if (!slot.occupied || slot.insertion_sequence >= preceding_sequence)
            {
                continue;
            }
            if (!next || slot.insertion_sequence > next->insertion_sequence)
            {
                next = &slot;
            }
        }
        if (!next)
        {
            break;
        }
        out_metadata[written++] = next->metadata;
        preceding_sequence = next->insertion_sequence;
    }
    return written;
}

bool VoiceMessageInbox::restore(const VoiceMessageMetadata& metadata,
                                const uint8_t* encoded_media,
                                std::size_t encoded_media_len)
{
    if (!encoded_media || !validRestoredMetadata(metadata, encoded_media_len))
    {
        return false;
    }

    for (const Slot& slot : slots_)
    {
        if (!slot.occupied)
        {
            continue;
        }
        if (slot.metadata.local_id == metadata.local_id)
        {
            return slot.metadata.sender_id == metadata.sender_id &&
                   slot.metadata.session_id == metadata.session_id &&
                   slot.metadata.object_fingerprint ==
                       metadata.object_fingerprint &&
                   slot.metadata.encoded_media_len == encoded_media_len;
        }
        if (slot.metadata.sender_id == metadata.sender_id &&
            slot.metadata.session_id == metadata.session_id)
        {
            return true;
        }
    }

    Slot* const destination = selectDestination();
    if (!destination)
    {
        return false;
    }
    const bool replacing = destination->occupied;
    if (replacing)
    {
        clearSlot(destination);
    }

    destination->metadata = metadata;
    std::memcpy(destination->encoded_media, encoded_media, encoded_media_len);
    destination->insertion_sequence = next_insertion_sequence_;
    next_insertion_sequence_ = nextNonZero(next_insertion_sequence_);
    destination->occupied = true;
    if (!replacing)
    {
        ++size_;
    }
    if (metadata.local_id >= next_local_id_)
    {
        next_local_id_ = nextNonZero(metadata.local_id);
    }
    return true;
}

bool VoiceMessageInbox::erase(uint64_t local_id)
{
    if (local_id == 0U)
    {
        return false;
    }
    for (Slot& slot : slots_)
    {
        if (slot.occupied && slot.metadata.local_id == local_id)
        {
            clearSlot(&slot);
            --size_;
            return true;
        }
    }
    return false;
}

void VoiceMessageInbox::clear()
{
    for (Slot& slot : slots_)
    {
        clearSlot(&slot);
    }
    size_ = 0U;
}

bool VoiceMessageInbox::isDuplicate(const ControlFrame& control) const
{
    for (const Slot& slot : slots_)
    {
        if (slot.occupied && slot.metadata.sender_id == control.sender_id &&
            slot.metadata.session_id == control.session_id)
        {
            return true;
        }
    }
    return false;
}

VoiceMessageInbox::Slot* VoiceMessageInbox::selectDestination()
{
    Slot* oldest = nullptr;
    for (Slot& slot : slots_)
    {
        if (!slot.occupied)
        {
            return &slot;
        }
        if (!oldest || slot.insertion_sequence < oldest->insertion_sequence)
        {
            oldest = &slot;
        }
    }
    return oldest;
}

void VoiceMessageInbox::clearSlot(Slot* slot)
{
    if (!slot)
    {
        return;
    }
    secureClear(slot->encoded_media, sizeof(slot->encoded_media));
    slot->metadata = {};
    slot->insertion_sequence = 0U;
    slot->occupied = false;
}

} // namespace chat::voice::vmp
