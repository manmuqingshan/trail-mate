#include "chat/infra/voice/vmp_mqtt_transport.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace
{

using namespace chat::voice::vmp;

ControlFrame publicControl(uint16_t encoded_media_len)
{
    ControlFrame control{};
    control.type = ControlType::Announce;
    control.flags = ControlFlagBroadcast | ControlFlagPublicBroadcast;
    control.sender_id = 0x1010U;
    control.target_id = kBroadcastTargetId;
    control.session_id = 0x1020304050607080ULL;
    for (std::size_t index = 0U; index < kSessionNonceSize; ++index)
    {
        control.session_nonce[index] = static_cast<uint8_t>(index + 1U);
    }
    control.phy_profile_id = 1U;
    control.channel_index = 3U;
    control.encoded_media_len = encoded_media_len;
    control.codec = Codec::Codec2_1300;
    control.fec_layout = kFecLayoutRs10_8;
    control.total_blocks = 1U;
    control.data_start_delay_ms = 700U;
    control.object_fingerprint = 0xCC33AA55U;
    return control;
}

ControlFrame privateControl(uint16_t encoded_media_len)
{
    ControlFrame control{};
    control.type = ControlType::Offer;
    control.flags = ControlFlagPrivate;
    control.sender_id = 0x1010U;
    control.target_id = 0x2020U;
    control.session_id = 0x0A0B0C0D0E0F1011ULL;
    for (std::size_t index = 0U; index < kSessionNonceSize; ++index)
    {
        control.session_nonce[index] = static_cast<uint8_t>(0x80U + index);
    }
    for (std::size_t index = 0U; index < kEphemeralPublicKeySize; ++index)
    {
        control.ephemeral_public_key[index] = static_cast<uint8_t>(0x31U + index);
    }
    control.phy_profile_id = 1U;
    control.channel_index = 7U;
    control.encoded_media_len = encoded_media_len;
    control.codec = Codec::Codec2_1300;
    control.fec_layout = kFecLayoutRs10_8;
    control.total_blocks = 1U;
    control.data_start_delay_ms = 120U;
    control.object_fingerprint = 0xF0E1D2C3U;
    return control;
}

class NoContacts final : public IVerifiedContactSecretProvider
{
  public:
    bool lookupVerifiedContactSecret(uint32_t,
                                     uint8_t[kPrivateKeySize]) const override
    {
        return false;
    }
};

class OneContact final : public IVerifiedContactSecretProvider
{
  public:
    OneContact(uint32_t peer_id, const uint8_t secret[kPrivateKeySize])
        : peer_id_(peer_id)
    {
        std::memcpy(secret_.data(), secret, secret_.size());
    }

    bool lookupVerifiedContactSecret(uint32_t peer_id,
                                     uint8_t out_secret[kPrivateKeySize]) const override
    {
        if (peer_id != peer_id_ || !out_secret)
        {
            return false;
        }
        std::memcpy(out_secret, secret_.data(), secret_.size());
        return true;
    }

  private:
    uint32_t peer_id_ = 0U;
    std::array<uint8_t, kPrivateKeySize> secret_{};
};

void test_envelope_is_bounded_and_strict()
{
    const std::array<uint8_t, 3> payload = {1U, 2U, 3U};
    std::array<uint8_t, kMaxMqttEnvelopeSize> bytes{};
    std::size_t len = bytes.size();
    assert(buildMqttEnvelope(
        MqttEnvelopeKind::Control, payload.data(), payload.size(), bytes.data(), &len));
    assert(len == kMqttEnvelopePrefixSize + payload.size());

    MqttEnvelopeView view{};
    assert(parseMqttEnvelope(bytes.data(), len, &view));
    assert(view.kind == MqttEnvelopeKind::Control);
    assert(view.payload_len == payload.size());
    assert(std::memcmp(view.payload, payload.data(), payload.size()) == 0);

    bytes[0] ^= 0x01U;
    assert(!parseMqttEnvelope(bytes.data(), len, &view));
}

void test_public_transfer_reassembles_without_radio_api()
{
    constexpr std::size_t kMediaSize = 875U;
    std::array<uint8_t, kMediaSize> media{};
    for (std::size_t index = 0U; index < media.size(); ++index)
    {
        media[index] = static_cast<uint8_t>((index * 29U) ^ 0x5AU);
    }

    MqttTransmitTransfer transmitter{};
    const ControlFrame control = publicControl(static_cast<uint16_t>(media.size()));
    assert(transmitter.prepareBroadcast(control, media.data(), media.size()));

    MqttReceiveTransfer receiver{};
    NoContacts contacts{};
    std::array<uint8_t, kMaxMqttEnvelopeSize> envelope{};
    unsigned emitted = 0U;
    MqttTransferResult result = MqttTransferResult::Rejected;
    while (transmitter.hasNext())
    {
        std::size_t envelope_len = envelope.size();
        assert(transmitter.nextEnvelope(envelope.data(), &envelope_len));
        result = receiver.acceptEnvelope(
            envelope.data(), envelope_len, 0x2020U, contacts);
        assert(result == MqttTransferResult::Accepted ||
               result == MqttTransferResult::Complete);
        ++emitted;
        if (result == MqttTransferResult::Complete)
        {
            break;
        }
    }
    // The RS(10,8) plan still publishes ten shards, but a receiver may end
    // local processing as soon as any eight valid shards complete recovery.
    assert(emitted == kSourceShardsPerBlock + 1U);
    assert(result == MqttTransferResult::Complete);
    assert(receiver.complete());
    assert(receiver.control().sender_id == control.sender_id);
    assert(receiver.control().target_id == kBroadcastTargetId);

    std::array<uint8_t, kMaxEncodedMediaSize> restored{};
    std::size_t restored_len = 0U;
    assert(receiver.recover(restored.data(), restored.size(), &restored_len));
    assert(restored_len == media.size());
    assert(std::memcmp(restored.data(), media.data(), media.size()) == 0);

    transmitter.clear();
    receiver.clear();
}

void test_private_transfer_is_end_to_end_encrypted_and_authenticated()
{
    constexpr std::size_t kMediaSize = 875U;
    std::array<uint8_t, kMediaSize> media{};
    std::array<uint8_t, kPrivateKeySize> contact_secret{};
    for (std::size_t index = 0U; index < media.size(); ++index)
    {
        media[index] = static_cast<uint8_t>((index * 11U) ^ 0xC7U);
    }
    for (std::size_t index = 0U; index < contact_secret.size(); ++index)
    {
        contact_secret[index] = static_cast<uint8_t>(0x44U + index);
    }

    const ControlFrame control = privateControl(static_cast<uint16_t>(media.size()));
    MqttTransmitTransfer transmitter{};
    assert(transmitter.preparePrivate(control,
                                      contact_secret.data(),
                                      media.data(),
                                      media.size()));

    OneContact contacts{control.sender_id, contact_secret.data()};
    MqttReceiveTransfer receiver{};
    std::array<uint8_t, kMaxMqttEnvelopeSize> envelope{};

    std::size_t envelope_len = envelope.size();
    assert(transmitter.copyNextEnvelope(envelope.data(), &envelope_len));
    assert(receiver.acceptEnvelope(envelope.data(), envelope_len, control.target_id, contacts) ==
           MqttTransferResult::Accepted);
    assert(transmitter.commitNextEnvelope());

    // A third party that changes an encrypted data envelope cannot create a
    // usable voice shard.  The valid original remains accepted afterwards.
    envelope_len = envelope.size();
    assert(transmitter.copyNextEnvelope(envelope.data(), &envelope_len));
    std::array<uint8_t, kMaxMqttEnvelopeSize> altered = envelope;
    altered[envelope_len - 1U] ^= 0x80U;
    assert(receiver.acceptEnvelope(altered.data(), envelope_len, control.target_id, contacts) ==
           MqttTransferResult::Rejected);
    assert(receiver.acceptEnvelope(envelope.data(), envelope_len, control.target_id, contacts) ==
           MqttTransferResult::Accepted);
    assert(transmitter.commitNextEnvelope());

    MqttTransferResult result = MqttTransferResult::Accepted;
    while (transmitter.hasNext() && result != MqttTransferResult::Complete)
    {
        envelope_len = envelope.size();
        assert(transmitter.nextEnvelope(envelope.data(), &envelope_len));
        result = receiver.acceptEnvelope(
            envelope.data(), envelope_len, control.target_id, contacts);
        assert(result == MqttTransferResult::Accepted ||
               result == MqttTransferResult::Complete);
    }
    assert(result == MqttTransferResult::Complete);

    std::array<uint8_t, kMaxEncodedMediaSize> restored{};
    std::size_t restored_len = 0U;
    assert(receiver.recover(restored.data(), restored.size(), &restored_len));
    assert(restored_len == media.size());
    assert(std::memcmp(restored.data(), media.data(), media.size()) == 0);

    transmitter.clear();
    receiver.clear();
}

} // namespace

int main()
{
    test_envelope_is_bounded_and_strict();
    test_public_transfer_reassembles_without_radio_api();
    test_private_transfer_is_end_to_end_encrypted_and_authenticated();
    return 0;
}
