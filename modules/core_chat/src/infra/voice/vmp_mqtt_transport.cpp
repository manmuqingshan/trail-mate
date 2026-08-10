/**
 * @file vmp_mqtt_transport.cpp
 * @brief Bounded VMP object carrier for MQTT.
 */

#include "chat/infra/voice/vmp_mqtt_transport.h"

#include <cstring>

namespace chat::voice::vmp
{
namespace
{

constexpr uint8_t kEnvelopeMagic0 = 'V';
constexpr uint8_t kEnvelopeMagic1 = 'Q';
constexpr uint8_t kEnvelopeVersion = 1U;

void secureClear(uint8_t* bytes, std::size_t size)
{
    volatile uint8_t* cursor = bytes;
    while (cursor && size-- != 0U)
    {
        *cursor++ = 0U;
    }
}

bool isKnownEnvelopeKind(MqttEnvelopeKind kind)
{
    return kind == MqttEnvelopeKind::Control || kind == MqttEnvelopeKind::Shard;
}

bool isPrivateOffer(const ControlFrame& control)
{
    DeliveryMode mode = DeliveryMode::Private;
    return control.type == ControlType::Offer && deliveryModeFor(control, &mode) &&
           mode == DeliveryMode::Private;
}

bool isPublicAnnounce(const ControlFrame& control)
{
    DeliveryMode mode = DeliveryMode::Private;
    return control.type == ControlType::Announce && deliveryModeFor(control, &mode) &&
           mode == DeliveryMode::Broadcast;
}

} // namespace

bool buildMqttEnvelope(MqttEnvelopeKind kind,
                       const uint8_t* payload,
                       std::size_t payload_len,
                       uint8_t* out,
                       std::size_t* inout_len)
{
    if (!payload || !out || !inout_len || !isKnownEnvelopeKind(kind) ||
        payload_len == 0U || payload_len > kPrivateShardFrameSize)
    {
        return false;
    }
    const std::size_t needed = kMqttEnvelopePrefixSize + payload_len;
    if (*inout_len < needed)
    {
        *inout_len = needed;
        return false;
    }
    out[0] = kEnvelopeMagic0;
    out[1] = kEnvelopeMagic1;
    out[2] = kEnvelopeVersion;
    out[3] = static_cast<uint8_t>(kind);
    std::memcpy(out + kMqttEnvelopePrefixSize, payload, payload_len);
    *inout_len = needed;
    return true;
}

bool parseMqttEnvelope(const uint8_t* data,
                       std::size_t len,
                       MqttEnvelopeView* out_view)
{
    if (!data || !out_view || len <= kMqttEnvelopePrefixSize ||
        data[0] != kEnvelopeMagic0 || data[1] != kEnvelopeMagic1 ||
        data[2] != kEnvelopeVersion)
    {
        return false;
    }
    const MqttEnvelopeKind kind = static_cast<MqttEnvelopeKind>(data[3]);
    if (!isKnownEnvelopeKind(kind))
    {
        return false;
    }
    const std::size_t payload_len = len - kMqttEnvelopePrefixSize;
    if (payload_len > kPrivateShardFrameSize)
    {
        return false;
    }
    out_view->kind = kind;
    out_view->payload = data + kMqttEnvelopePrefixSize;
    out_view->payload_len = payload_len;
    return true;
}

bool MqttTransmitTransfer::prepareCommon(const ControlFrame& control,
                                         const uint8_t* encoded_media,
                                         std::size_t encoded_media_len)
{
    if (!isValidControlFrame(control) || !encoded_media ||
        encoded_media_len != control.encoded_media_len ||
        !transmit_block_.prepare(encoded_media, encoded_media_len))
    {
        return false;
    }
    control_ = control;
    return true;
}

bool MqttTransmitTransfer::preparePrivate(
    const ControlFrame& control,
    const uint8_t verified_contact_secret[kPrivateKeySize],
    const uint8_t* encoded_media,
    std::size_t encoded_media_len)
{
    clear();
    if (!verified_contact_secret || !isPrivateOffer(control) ||
        !prepareCommon(control, encoded_media, encoded_media_len) ||
        !derivePrivateMqttSessionKeys(verified_contact_secret,
                                      control.session_nonce,
                                      control.session_id,
                                      &keys_))
    {
        clear();
        return false;
    }
    std::size_t control_len = sizeof(control_wire_);
    if (!encodePrivateControlFrame(control_,
                                   keys_,
                                   PrivateFrameDirection::SenderToReceiver,
                                   control_wire_,
                                   &control_len) ||
        control_len != sizeof(control_wire_))
    {
        clear();
        return false;
    }
    private_mode_ = true;
    prepared_ = true;
    return true;
}

bool MqttTransmitTransfer::prepareBroadcast(const ControlFrame& control,
                                            const uint8_t* encoded_media,
                                            std::size_t encoded_media_len)
{
    clear();
    if (!isPublicAnnounce(control) || !prepareCommon(control, encoded_media, encoded_media_len))
    {
        clear();
        return false;
    }
    std::size_t control_len = sizeof(control_wire_);
    if (!encodePublicControlFrame(control_, control_wire_, &control_len) ||
        control_len != sizeof(control_wire_))
    {
        clear();
        return false;
    }
    private_mode_ = false;
    prepared_ = true;
    return true;
}

bool MqttTransmitTransfer::copyNextEnvelope(uint8_t* out, std::size_t* inout_len)
{
    if (!out || !inout_len || !hasNext())
    {
        return false;
    }
    if (next_index_ == 0U)
    {
        const bool ok = buildMqttEnvelope(MqttEnvelopeKind::Control,
                                          control_wire_,
                                          sizeof(control_wire_),
                                          out,
                                          inout_len);
        return ok;
    }

    const uint8_t shard_index = static_cast<uint8_t>(next_index_ - 1U);
    std::size_t frame_len = sizeof(frame_wire_);
    const bool frame_ok = private_mode_
                              ? transmit_block_.buildPrivateShardFrame(
                                    keys_,
                                    control_.session_nonce,
                                    control_.session_id,
                                    shard_index,
                                    frame_wire_,
                                    &frame_len)
                              : transmit_block_.buildPublicShardFrame(
                                    control_.session_id,
                                    shard_index,
                                    frame_wire_,
                                    &frame_len);
    if (!frame_ok)
    {
        return false;
    }
    const bool envelope_ok = buildMqttEnvelope(
        MqttEnvelopeKind::Shard, frame_wire_, frame_len, out, inout_len);
    return envelope_ok;
}

bool MqttTransmitTransfer::commitNextEnvelope()
{
    if (!hasNext())
    {
        return false;
    }
    ++next_index_;
    return true;
}

bool MqttTransmitTransfer::nextEnvelope(uint8_t* out, std::size_t* inout_len)
{
    return copyNextEnvelope(out, inout_len) && commitNextEnvelope();
}

void MqttTransmitTransfer::clear()
{
    control_ = {};
    clearPrivateSessionKeys(&keys_);
    transmit_block_.clear();
    secureClear(control_wire_, sizeof(control_wire_));
    secureClear(frame_wire_, sizeof(frame_wire_));
    next_index_ = 0U;
    private_mode_ = false;
    prepared_ = false;
}

MqttTransferResult MqttReceiveTransfer::acceptEnvelope(
    const uint8_t* envelope,
    std::size_t envelope_len,
    uint32_t self_node_id,
    const IVerifiedContactSecretProvider& contacts)
{
    MqttEnvelopeView view{};
    if (!parseMqttEnvelope(envelope, envelope_len, &view))
    {
        return MqttTransferResult::Rejected;
    }
    return view.kind == MqttEnvelopeKind::Control
               ? acceptControl(view.payload, view.payload_len, self_node_id, contacts)
               : acceptShard(view.payload, view.payload_len);
}

MqttTransferResult MqttReceiveTransfer::acceptControl(
    const uint8_t* control_wire,
    std::size_t control_len,
    uint32_t self_node_id,
    const IVerifiedContactSecretProvider& contacts)
{
    clear();
    if (!control_wire || control_len != kControlFrameSize || self_node_id == 0U ||
        !decodeControlFrame(control_wire, control_len, &candidate_control_))
    {
        return MqttTransferResult::Rejected;
    }

    DeliveryMode mode = DeliveryMode::Private;
    if (!deliveryModeFor(candidate_control_, &mode))
    {
        return MqttTransferResult::Rejected;
    }
    if (mode == DeliveryMode::Private)
    {
        if (!isPrivateOffer(candidate_control_) ||
            candidate_control_.target_id != self_node_id ||
            !contacts.lookupVerifiedContactSecret(candidate_control_.sender_id,
                                                  contact_secret_) ||
            !derivePrivateMqttSessionKeys(contact_secret_,
                                          candidate_control_.session_nonce,
                                          candidate_control_.session_id,
                                          &keys_) ||
            !decodePrivateControlFrame(control_wire,
                                       control_len,
                                       keys_,
                                       PrivateFrameDirection::SenderToReceiver,
                                       &control_))
        {
            clear();
            return MqttTransferResult::Rejected;
        }
        private_mode_ = true;
    }
    else
    {
        if (!isPublicAnnounce(candidate_control_) ||
            !decodePublicControlFrame(control_wire, control_len, &control_))
        {
            clear();
            return MqttTransferResult::Rejected;
        }
        private_mode_ = false;
    }

    MediaLayout layout{};
    if (!planMediaLayout(control_.encoded_media_len, &layout) ||
        !receive_block_.begin(layout))
    {
        clear();
        return MqttTransferResult::Rejected;
    }
    active_ = true;
    return MqttTransferResult::Accepted;
}

MqttTransferResult MqttReceiveTransfer::acceptShard(const uint8_t* frame,
                                                    std::size_t frame_len)
{
    if (!active_ || !frame || complete_)
    {
        return MqttTransferResult::Rejected;
    }

    const uint8_t* shard = nullptr;
    if (private_mode_)
    {
        if (frame_len != kPrivateShardFrameSize ||
            !decodeDataHeader(frame, kDataHeaderSize, &data_header_) ||
            data_header_.session_id != control_.session_id ||
            !openPrivateShard(keys_,
                              control_.session_nonce,
                              PrivateFrameDirection::SenderToReceiver,
                              data_header_,
                              frame + kDataHeaderSize,
                              kMaxShardPayloadSize,
                              frame + kDataHeaderSize + kMaxShardPayloadSize,
                              plaintext_))
        {
            return MqttTransferResult::Rejected;
        }
        shard = plaintext_;
    }
    else
    {
        if (frame_len != kPublicShardFrameSize ||
            !parsePublicShardFrame(frame, frame_len, &data_header_, &shard) ||
            data_header_.session_id != control_.session_id)
        {
            return MqttTransferResult::Rejected;
        }
    }

    const ReceiveBlockResult accepted = receive_block_.accept(
        data_header_, shard, kMaxShardPayloadSize);
    if (accepted == ReceiveBlockResult::Duplicate)
    {
        return MqttTransferResult::Duplicate;
    }
    if (accepted == ReceiveBlockResult::Invalid)
    {
        return MqttTransferResult::Rejected;
    }
    if (accepted == ReceiveBlockResult::Complete)
    {
        complete_ = true;
        return MqttTransferResult::Complete;
    }
    return MqttTransferResult::Accepted;
}

bool MqttReceiveTransfer::recover(uint8_t* out_media,
                                  std::size_t out_capacity,
                                  std::size_t* out_media_len)
{
    return complete_ && receive_block_.recover(out_media, out_capacity, out_media_len);
}

void MqttReceiveTransfer::clear()
{
    candidate_control_ = {};
    control_ = {};
    data_header_ = {};
    clearPrivateSessionKeys(&keys_);
    receive_block_.clear();
    secureClear(contact_secret_, sizeof(contact_secret_));
    secureClear(plaintext_, sizeof(plaintext_));
    private_mode_ = false;
    active_ = false;
    complete_ = false;
}

} // namespace chat::voice::vmp
