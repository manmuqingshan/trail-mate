#include "chat/infra/voice/vmp_private_crypto.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace
{

using namespace chat::voice::vmp;

void fill(uint8_t* out, std::size_t len, uint8_t seed)
{
    for (std::size_t index = 0; index < len; ++index)
    {
        out[index] = static_cast<uint8_t>(seed + index * 19U);
    }
}

bool allZero(const uint8_t* data, std::size_t len)
{
    uint8_t aggregate = 0;
    for (std::size_t index = 0; index < len; ++index)
    {
        aggregate |= data[index];
    }
    return aggregate == 0U;
}

void deriveTwoPeerKeys(PrivateSessionKeys* out_sender,
                       PrivateSessionKeys* out_receiver,
                       uint8_t out_nonce[kSessionNonceSize])
{
    EphemeralKeyPair sender{};
    EphemeralKeyPair receiver{};
    assert(generateEphemeralKeyPair(&sender));
    assert(generateEphemeralKeyPair(&receiver));

    uint8_t contact_secret[kPrivateKeySize] = {};
    fill(contact_secret, sizeof(contact_secret), 0xA4U);
    fill(out_nonce, kSessionNonceSize, 0x41U);
    constexpr uint64_t kSessionId = 0x1020304050607080ULL;
    assert(derivePrivateSessionKeys(contact_secret,
                                    sender.private_key,
                                    receiver.public_key,
                                    out_nonce,
                                    kSessionId,
                                    out_sender));
    assert(derivePrivateSessionKeys(contact_secret,
                                    receiver.private_key,
                                    sender.public_key,
                                    out_nonce,
                                    kSessionId,
                                    out_receiver));
    assert(allZero(sender.private_key, sizeof(sender.private_key)));
    assert(allZero(receiver.private_key, sizeof(receiver.private_key)));
    assert(std::memcmp(out_sender, out_receiver, sizeof(*out_sender)) == 0);
    std::memset(contact_secret, 0, sizeof(contact_secret));
}

void test_private_control_tags()
{
    PrivateSessionKeys sender_control{};
    PrivateSessionKeys receiver_control{};
    uint8_t nonce[kSessionNonceSize] = {};
    uint8_t contact_secret[kPrivateKeySize] = {};
    fill(contact_secret, sizeof(contact_secret), 0xA4U);
    fill(nonce, sizeof(nonce), 0x41U);
    constexpr uint64_t kSessionId = 0x1020304050607080ULL;
    assert(derivePrivateControlKey(contact_secret,
                                   nonce,
                                   kSessionId,
                                   sender_control.control_key));
    assert(derivePrivateControlKey(contact_secret,
                                   nonce,
                                   kSessionId,
                                   receiver_control.control_key));

    std::array<uint8_t, kControlFrameSize - kControlIntegrityTagSize> bytes{};
    fill(bytes.data(), bytes.size(), 0x01U);
    uint8_t tag[kControlIntegrityTagSize] = {};
    assert(tagPrivateControl(sender_control,
                             nonce,
                             ControlType::Offer,
                             PrivateFrameDirection::SenderToReceiver,
                             bytes.data(),
                             bytes.size(),
                             tag));
    assert(verifyPrivateControlTag(receiver_control,
                                   nonce,
                                   ControlType::Offer,
                                   PrivateFrameDirection::SenderToReceiver,
                                   bytes.data(),
                                   bytes.size(),
                                   tag));
    bytes[2] ^= 0x01U;
    assert(!verifyPrivateControlTag(receiver_control,
                                    nonce,
                                    ControlType::Offer,
                                    PrivateFrameDirection::SenderToReceiver,
                                    bytes.data(),
                                    bytes.size(),
                                    tag));
    clearPrivateSessionKeys(&sender_control);
    clearPrivateSessionKeys(&receiver_control);
    std::memset(contact_secret, 0, sizeof(contact_secret));
}

void test_ready_and_shard_protection()
{
    PrivateSessionKeys sender{};
    PrivateSessionKeys receiver{};
    uint8_t nonce[kSessionNonceSize] = {};
    deriveTwoPeerKeys(&sender, &receiver, nonce);

    DataHeader ready{};
    ready.type = DataType::ReadyProbe;
    ready.session_id = 99U;
    uint8_t ready_tag[kPrivateDataAuthTagSize] = {};
    assert(tagPrivateReady(sender,
                           nonce,
                           PrivateFrameDirection::SenderToReceiver,
                           ready,
                           ready_tag));
    assert(verifyPrivateReadyTag(receiver,
                                 nonce,
                                 PrivateFrameDirection::SenderToReceiver,
                                 ready,
                                 ready_tag));

    DataHeader shard{};
    shard.type = DataType::Shard;
    shard.session_id = ready.session_id;
    shard.block_index = 0;
    shard.shard_index = 4;
    shard.payload_len = 160;
    shard.flags = DataFlagFinalBlock;
    std::array<uint8_t, kMaxShardPayloadSize> plaintext{};
    std::array<uint8_t, kMaxShardPayloadSize> ciphertext{};
    std::array<uint8_t, kMaxShardPayloadSize> opened{};
    uint8_t tag[kPrivateDataAuthTagSize] = {};
    fill(plaintext.data(), plaintext.size(), 0xE1U);
    assert(sealPrivateShard(sender,
                            nonce,
                            PrivateFrameDirection::SenderToReceiver,
                            shard,
                            plaintext.data(),
                            plaintext.size(),
                            ciphertext.data(),
                            tag));
    assert(ciphertext != plaintext);
    assert(openPrivateShard(receiver,
                            nonce,
                            PrivateFrameDirection::SenderToReceiver,
                            shard,
                            ciphertext.data(),
                            ciphertext.size(),
                            tag,
                            opened.data()));
    assert(opened == plaintext);

    tag[0] ^= 0x80U;
    opened.fill(0xA5U);
    assert(!openPrivateShard(receiver,
                             nonce,
                             PrivateFrameDirection::SenderToReceiver,
                             shard,
                             ciphertext.data(),
                             ciphertext.size(),
                             tag,
                             opened.data()));
    assert(allZero(opened.data(), opened.size()));
    clearPrivateSessionKeys(&sender);
    clearPrivateSessionKeys(&receiver);
}

void test_private_mqtt_keys_are_static_contact_bound_and_distinct()
{
    uint8_t contact_secret[kPrivateKeySize] = {};
    uint8_t nonce[kSessionNonceSize] = {};
    fill(contact_secret, sizeof(contact_secret), 0x7AU);
    fill(nonce, sizeof(nonce), 0x13U);

    PrivateSessionKeys sender{};
    PrivateSessionKeys receiver{};
    constexpr uint64_t kSessionId = 0x9988776655443322ULL;
    assert(derivePrivateMqttSessionKeys(contact_secret, nonce, kSessionId, &sender));
    assert(derivePrivateMqttSessionKeys(contact_secret, nonce, kSessionId, &receiver));
    assert(std::memcmp(&sender, &receiver, sizeof(sender)) == 0);
    assert(std::memcmp(sender.control_key, sender.data_key, kPrivateKeySize) != 0);
    assert(std::memcmp(sender.data_key, sender.mqtt_key, kPrivateKeySize) != 0);

    nonce[0] ^= 0x80U;
    PrivateSessionKeys different_nonce{};
    assert(derivePrivateMqttSessionKeys(
        contact_secret, nonce, kSessionId, &different_nonce));
    assert(std::memcmp(sender.data_key,
                       different_nonce.data_key,
                       kPrivateKeySize) != 0);

    clearPrivateSessionKeys(&sender);
    clearPrivateSessionKeys(&receiver);
    clearPrivateSessionKeys(&different_nonce);
    std::memset(contact_secret, 0, sizeof(contact_secret));
}

void test_contact_secret_is_identity_family_and_pair_bound()
{
    uint8_t identity_shared_secret[kPrivateKeySize] = {};
    fill(identity_shared_secret, sizeof(identity_shared_secret), 0x55U);

    uint8_t forward[kPrivateKeySize] = {};
    uint8_t reverse[kPrivateKeySize] = {};
    uint8_t other_family[kPrivateKeySize] = {};
    assert(deriveVmpContactSecret(identity_shared_secret,
                                  ContactSecretIdentityFamily::Meshtastic,
                                  0x1001U,
                                  0x2002U,
                                  forward));
    assert(deriveVmpContactSecret(identity_shared_secret,
                                  ContactSecretIdentityFamily::Meshtastic,
                                  0x2002U,
                                  0x1001U,
                                  reverse));
    assert(deriveVmpContactSecret(identity_shared_secret,
                                  ContactSecretIdentityFamily::MeshCore,
                                  0x1001U,
                                  0x2002U,
                                  other_family));
    assert(std::memcmp(forward, reverse, sizeof(forward)) == 0);
    assert(std::memcmp(forward, other_family, sizeof(forward)) != 0);
    assert(!deriveVmpContactSecret(identity_shared_secret,
                                   ContactSecretIdentityFamily::Meshtastic,
                                   0x1001U,
                                   0x1001U,
                                   other_family));
    assert(allZero(other_family, sizeof(other_family)));

    std::memset(identity_shared_secret, 0, sizeof(identity_shared_secret));
    std::memset(forward, 0, sizeof(forward));
    std::memset(reverse, 0, sizeof(reverse));
}

} // namespace

int main()
{
    test_private_control_tags();
    test_ready_and_shard_protection();
    test_private_mqtt_keys_are_static_contact_bound_and_distinct();
    test_contact_secret_is_identity_family_and_pair_bound();
    return 0;
}
