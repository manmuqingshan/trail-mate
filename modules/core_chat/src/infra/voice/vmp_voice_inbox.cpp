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

bool validOutboundControl(const ControlFrame& control, DeliveryMode* out_mode)
{
    if (!out_mode || !isValidControlFrame(control) ||
        !deliveryModeFor(control, out_mode))
    {
        return false;
    }
    return (*out_mode == DeliveryMode::Private &&
            control.type == ControlType::Offer) ||
           (*out_mode == DeliveryMode::Broadcast &&
            control.type == ControlType::Announce);
}

bool validDeliveryState(VoiceDeliveryState delivery)
{
    return delivery == VoiceDeliveryState::Received ||
           delivery == VoiceDeliveryState::Sending ||
           delivery == VoiceDeliveryState::Sent ||
           delivery == VoiceDeliveryState::Failed;
}

bool validRestoredMetadata(const VoiceMessageMetadata& metadata,
                           std::size_t encoded_media_len)
{
    if (metadata.local_id == 0U || !voiceMessageComplete(metadata) ||
        encoded_media_len == 0U || encoded_media_len > kMaxEncodedMediaSize ||
        encoded_media_len != metadata.encoded_media_len ||
        (metadata.presentation_protocol != VoicePresentationProtocol::Unknown &&
         !isValidVoicePresentationBinding(metadata.presentation_protocol,
                                          metadata.presentation_channel)))
    {
        return false;
    }

    if (metadata.codec != Codec::Codec2_1300 ||
        !validDeliveryState(metadata.delivery))
    {
        return false;
    }

    if (voiceMessageOutgoing(metadata))
    {
        if (voiceMessageSourceUnverified(metadata) ||
            metadata.delivery == VoiceDeliveryState::Received)
        {
            return false;
        }
        if (metadata.mode == DeliveryMode::Broadcast)
        {
            return metadata.sender_id != 0U &&
                   metadata.target_id == kBroadcastTargetId;
        }
        return metadata.mode == DeliveryMode::Private &&
               metadata.sender_id != 0U && metadata.target_id != 0U &&
               metadata.target_id != kBroadcastTargetId;
    }

    if (metadata.delivery != VoiceDeliveryState::Received)
    {
        return false;
    }

    if (metadata.mode == DeliveryMode::Broadcast)
    {
        return metadata.target_id == kBroadcastTargetId &&
               voiceMessageSourceUnverified(metadata);
    }
    if (metadata.mode == DeliveryMode::Private)
    {
        return metadata.sender_id != 0U && metadata.target_id != 0U &&
               metadata.target_id != kBroadcastTargetId &&
               !voiceMessageSourceUnverified(metadata);
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
                                               VoicePresentationProtocol presentation_protocol,
                                               uint8_t presentation_channel,
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
        !validInboundControl(control, &mode) ||
        !isValidVoicePresentationBinding(presentation_protocol,
                                         presentation_channel))
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
    destination->metadata.flags = 0U;
    setVoiceMessageFlag(&destination->metadata,
                        VoiceMessageFlagSourceUnverified,
                        mode == DeliveryMode::Broadcast);
    setVoiceMessageFlag(&destination->metadata, VoiceMessageFlagComplete, complete);
    destination->metadata.presentation_protocol = presentation_protocol;
    destination->metadata.presentation_channel = presentation_channel;
    destination->metadata.delivery = VoiceDeliveryState::Received;
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

VoiceInboxStoreResult VoiceMessageInbox::storeOutgoing(
    const ControlFrame& control,
    const uint8_t* encoded_media,
    std::size_t encoded_media_len,
    uint32_t created_at_seconds,
    VoicePresentationProtocol presentation_protocol,
    uint8_t presentation_channel,
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
        !validOutboundControl(control, &mode) ||
        !isValidVoicePresentationBinding(presentation_protocol,
                                         presentation_channel))
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
    destination->metadata.received_at_seconds = created_at_seconds;
    destination->metadata.encoded_media_len =
        static_cast<uint16_t>(encoded_media_len);
    destination->metadata.codec = control.codec;
    destination->metadata.mode = mode;
    destination->metadata.flags = 0U;
    setVoiceMessageFlag(&destination->metadata, VoiceMessageFlagComplete, true);
    setVoiceMessageFlag(&destination->metadata, VoiceMessageFlagOutgoing, true);
    setVoiceMessageFlag(&destination->metadata, VoiceMessageFlagRead, true);
    destination->metadata.presentation_protocol = presentation_protocol;
    destination->metadata.presentation_channel = presentation_channel;
    destination->metadata.delivery = VoiceDeliveryState::Sending;
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

bool VoiceMessageInbox::updateDeliveryState(uint64_t local_id,
                                            VoiceDeliveryState delivery)
{
    if (local_id == 0U || delivery == VoiceDeliveryState::Received ||
        !validDeliveryState(delivery))
    {
        return false;
    }
    for (Slot& slot : slots_)
    {
        if (slot.occupied && slot.metadata.local_id == local_id &&
            voiceMessageOutgoing(slot.metadata))
        {
            slot.metadata.delivery = delivery;
            return true;
        }
    }
    return false;
}

bool VoiceMessageInbox::markConversationRead(VoicePresentationProtocol protocol,
                                             uint8_t channel,
                                             uint32_t peer_id,
                                             bool broadcast)
{
    if (!isValidVoicePresentationBinding(protocol, channel))
    {
        return false;
    }
    bool changed = false;
    for (Slot& slot : slots_)
    {
        VoiceMessageMetadata& metadata = slot.metadata;
        if (!slot.occupied || voiceMessageOutgoing(metadata) ||
            voiceMessageRead(metadata) || metadata.presentation_protocol != protocol ||
            metadata.presentation_channel != channel ||
            (broadcast ? metadata.mode != DeliveryMode::Broadcast
                       : (metadata.mode != DeliveryMode::Private ||
                          metadata.sender_id != peer_id)))
        {
            continue;
        }
        setVoiceMessageFlag(&metadata, VoiceMessageFlagRead, true);
        changed = true;
    }
    return changed;
}

bool VoiceMessageInbox::bindUnassignedMessages(
    VoicePresentationProtocol protocol,
    uint8_t channel,
    bool mark_incoming_read)
{
    if (!isValidVoicePresentationBinding(protocol, channel))
    {
        return false;
    }
    bool changed = false;
    for (Slot& slot : slots_)
    {
        VoiceMessageMetadata& metadata = slot.metadata;
        if (!slot.occupied ||
            metadata.presentation_protocol != VoicePresentationProtocol::Unknown)
        {
            continue;
        }
        metadata.presentation_protocol = protocol;
        metadata.presentation_channel = channel;
        if (mark_incoming_read && !voiceMessageOutgoing(metadata))
        {
            setVoiceMessageFlag(&metadata, VoiceMessageFlagRead, true);
        }
        changed = true;
    }
    return changed;
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
                                std::size_t encoded_media_len,
                                uint64_t insertion_sequence)
{
    if (!encoded_media || !validRestoredMetadata(metadata, encoded_media_len))
    {
        return false;
    }

    VoiceMessageMetadata restored_metadata = metadata;
    // A VMP carrier attempt cannot survive reboot and VMP has no resume or
    // retry queue. Never present a stale `Sending` bubble as live work after
    // storage hydration; it is an interrupted local send.
    if (voiceMessageOutgoing(restored_metadata) &&
        restored_metadata.delivery == VoiceDeliveryState::Sending)
    {
        restored_metadata.delivery = VoiceDeliveryState::Failed;
    }

    for (const Slot& slot : slots_)
    {
        if (!slot.occupied)
        {
            continue;
        }
        if (slot.metadata.local_id == restored_metadata.local_id)
        {
            return slot.metadata.sender_id == restored_metadata.sender_id &&
                   slot.metadata.session_id == restored_metadata.session_id &&
                   slot.metadata.object_fingerprint ==
                       restored_metadata.object_fingerprint &&
                   slot.metadata.encoded_media_len == encoded_media_len;
        }
        if (slot.metadata.sender_id == restored_metadata.sender_id &&
            slot.metadata.session_id == restored_metadata.session_id)
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

    destination->metadata = restored_metadata;
    std::memcpy(destination->encoded_media, encoded_media, encoded_media_len);
    // V1 snapshots are written newest-first.  The caller supplies the
    // descending historical sequence for that representation so an SD
    // hydrate preserves the same newest-first list and oldest-slot eviction
    // behavior as the live inbox.  Ad-hoc restore users retain the ordinary
    // append-to-newest behavior with the default zero value.
    if (insertion_sequence == 0U)
    {
        insertion_sequence = next_insertion_sequence_;
        next_insertion_sequence_ = nextNonZero(next_insertion_sequence_);
    }
    else if (insertion_sequence >= next_insertion_sequence_)
    {
        next_insertion_sequence_ = nextNonZero(insertion_sequence);
    }
    destination->insertion_sequence = insertion_sequence;
    destination->occupied = true;
    if (!replacing)
    {
        ++size_;
    }
    if (restored_metadata.local_id >= next_local_id_)
    {
        next_local_id_ = nextNonZero(restored_metadata.local_id);
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
