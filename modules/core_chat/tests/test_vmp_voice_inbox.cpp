#include "chat/infra/voice/vmp_voice_inbox.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

namespace
{

using namespace chat::voice::vmp;

constexpr VoicePresentationProtocol kTestPresentation =
    VoicePresentationProtocol::Meshtastic;
constexpr uint8_t kTestChannel = 0U;

ControlFrame privateOffer(uint32_t sender, uint64_t session_id, uint16_t media_len)
{
    ControlFrame control{};
    control.type = ControlType::Offer;
    control.flags = ControlFlagPrivate;
    control.sender_id = sender;
    control.target_id = 7U;
    control.session_id = session_id;
    control.session_nonce[0] = 1U;
    control.phy_profile_id = 1U;
    control.encoded_media_len = media_len;
    control.total_blocks = 1U;
    control.data_start_delay_ms = 120U;
    control.object_fingerprint = static_cast<uint32_t>(session_id);
    control.ephemeral_public_key[0] = 2U;
    return control;
}

ControlFrame broadcastAnnounce(uint32_t sender, uint64_t session_id, uint16_t media_len)
{
    ControlFrame control{};
    control.type = ControlType::Announce;
    control.flags = ControlFlagBroadcast | ControlFlagPublicBroadcast;
    control.sender_id = sender;
    control.target_id = kBroadcastTargetId;
    control.session_id = session_id;
    control.session_nonce[0] = 3U;
    control.phy_profile_id = 1U;
    control.encoded_media_len = media_len;
    control.total_blocks = 1U;
    control.data_start_delay_ms = 700U;
    control.object_fingerprint = static_cast<uint32_t>(session_id);
    return control;
}

void test_store_local_only_voice_and_dedupe()
{
    VoiceMessageInbox inbox;
    const auto control = privateOffer(42U, 0x0102030405060708ULL, 7U);
    const std::array<uint8_t, 7> media = {1U, 2U, 3U, 4U, 5U, 6U, 7U};
    uint64_t local_id = 0U;

    assert(inbox.store(control, media.data(), media.size(), true, 123U,
                       kTestPresentation, kTestChannel, &local_id) ==
           VoiceInboxStoreResult::Stored);
    assert(local_id != 0U);
    assert(inbox.size() == 1U);

    VoiceMessageView view{};
    assert(inbox.get(local_id, &view));
    assert(view.metadata.sender_id == 42U);
    assert(voiceMessageComplete(view.metadata));
    assert(!voiceMessageSourceUnverified(view.metadata));
    assert(view.metadata.encoded_media_len == media.size());
    for (std::size_t index = 0U; index < media.size(); ++index)
    {
        assert(view.encoded_media[index] == media[index]);
    }
    assert(inbox.store(control, media.data(), media.size(), true, 124U,
                       kTestPresentation, kTestChannel, nullptr) ==
           VoiceInboxStoreResult::Duplicate);
}

void test_broadcast_is_explicitly_unverified()
{
    VoiceMessageInbox inbox;
    const auto control = broadcastAnnounce(9U, 0xAA55ULL, 7U);
    const std::array<uint8_t, 7> media = {7U, 6U, 5U, 4U, 3U, 2U, 1U};
    uint64_t local_id = 0U;
    assert(inbox.store(control, media.data(), media.size(), true, 0U,
                       kTestPresentation, kTestChannel, &local_id) ==
           VoiceInboxStoreResult::Stored);

    VoiceMessageView view{};
    assert(inbox.get(local_id, &view));
    assert(view.metadata.mode == DeliveryMode::Broadcast);
    assert(voiceMessageSourceUnverified(view.metadata));
}

void test_bad_media_is_never_persisted()
{
    VoiceMessageInbox inbox;
    const auto control = privateOffer(42U, 100U, 7U);
    const std::array<uint8_t, 6> too_short = {};
    assert(inbox.store(control, too_short.data(), too_short.size(), true, 0U,
                       kTestPresentation, kTestChannel, nullptr) ==
           VoiceInboxStoreResult::Invalid);
    assert(inbox.size() == 0U);
}

void test_metadata_lists_newest_first_without_media()
{
    VoiceMessageInbox inbox;
    const auto first = privateOffer(0x1001U, 0xA1U, 7U);
    const auto second = privateOffer(0x1002U, 0xA2U, 7U);
    const std::array<uint8_t, 7> first_media = {1U, 1U, 1U, 1U, 1U, 1U, 1U};
    const std::array<uint8_t, 7> second_media = {2U, 2U, 2U, 2U, 2U, 2U, 2U};
    assert(inbox.store(first, first_media.data(), first_media.size(), true, 10U,
                       kTestPresentation, kTestChannel, nullptr) ==
           VoiceInboxStoreResult::Stored);
    assert(inbox.store(second, second_media.data(), second_media.size(), true, 20U,
                       kTestPresentation, kTestChannel, nullptr) ==
           VoiceInboxStoreResult::Stored);

    VoiceMessageMetadata metadata[2] = {};
    assert(inbox.listMetadata(metadata, 2U) == 2U);
    assert(metadata[0].sender_id == second.sender_id);
    assert(metadata[0].received_at_seconds == 20U);
    assert(metadata[1].sender_id == first.sender_id);
    assert(inbox.listMetadata(nullptr, 2U) == 0U);
}

void test_restore_retains_playback_identity_and_deduplication()
{
    VoiceMessageInbox original;
    const auto control = privateOffer(0x11223344U, 0x1234567890ABCDEFULL, 7U);
    const std::array<uint8_t, 7> media = {9U, 8U, 7U, 6U, 5U, 4U, 3U};
    uint64_t local_id = 0U;
    assert(original.store(control,
                          media.data(),
                          media.size(),
                          true,
                          321U,
                          kTestPresentation,
                          kTestChannel,
                          &local_id) == VoiceInboxStoreResult::Stored);

    VoiceMessageView original_view{};
    assert(original.get(local_id, &original_view));

    VoiceMessageInbox restored;
    assert(restored.restore(original_view.metadata,
                            original_view.encoded_media,
                            original_view.metadata.encoded_media_len));
    VoiceMessageView restored_view{};
    assert(restored.get(local_id, &restored_view));
    assert(restored_view.metadata.local_id == local_id);
    assert(restored_view.metadata.received_at_seconds == 321U);
    assert(restored_view.metadata.object_fingerprint == control.object_fingerprint);
    for (std::size_t index = 0U; index < media.size(); ++index)
    {
        assert(restored_view.encoded_media[index] == media[index]);
    }
    assert(restored.store(control, media.data(), media.size(), true, 322U,
                          kTestPresentation, kTestChannel, nullptr) ==
           VoiceInboxStoreResult::Duplicate);

    VoiceMessageMetadata invalid = original_view.metadata;
    setVoiceMessageFlag(&invalid, VoiceMessageFlagComplete, false);
    assert(!VoiceMessageInbox{}.restore(invalid, media.data(), media.size()));
}

void test_outgoing_voice_keeps_local_delivery_lifecycle()
{
    VoiceMessageInbox inbox;
    const auto control = privateOffer(0x11223344U, 0x12345678U, 7U);
    const std::array<uint8_t, 7> media = {1U, 3U, 3U, 7U, 0U, 1U, 9U};
    uint64_t local_id = 0U;

    assert(inbox.storeOutgoing(control,
                               media.data(),
                               media.size(),
                               999U,
                               kTestPresentation,
                               kTestChannel,
                               &local_id) == VoiceInboxStoreResult::Stored);
    VoiceMessageView sending{};
    assert(inbox.get(local_id, &sending));
    assert(voiceMessageOutgoing(sending.metadata));
    assert(!voiceMessageSourceUnverified(sending.metadata));
    assert(sending.metadata.delivery == VoiceDeliveryState::Sending);
    assert(inbox.updateDeliveryState(local_id, VoiceDeliveryState::Sent));

    VoiceMessageView sent{};
    assert(inbox.get(local_id, &sent));
    assert(sent.metadata.delivery == VoiceDeliveryState::Sent);

    VoiceMessageInbox restored;
    assert(restored.restore(sent.metadata,
                            sent.encoded_media,
                            sent.metadata.encoded_media_len));
    VoiceMessageView restored_view{};
    assert(restored.get(local_id, &restored_view));
    assert(voiceMessageOutgoing(restored_view.metadata));
    assert(restored_view.metadata.delivery == VoiceDeliveryState::Sent);
    assert(!restored.updateDeliveryState(local_id, VoiceDeliveryState::Received));
}

void test_interrupted_outgoing_send_restores_as_failed()
{
    VoiceMessageInbox original;
    const auto control = broadcastAnnounce(0x11223344U, 0x55667788U, 7U);
    const std::array<uint8_t, 7> media = {9U, 8U, 7U, 6U, 5U, 4U, 3U};
    uint64_t local_id = 0U;
    assert(original.storeOutgoing(control,
                                  media.data(),
                                  media.size(),
                                  1234U,
                                  kTestPresentation,
                                  kTestChannel,
                                  &local_id) == VoiceInboxStoreResult::Stored);
    VoiceMessageView sending{};
    assert(original.get(local_id, &sending));

    VoiceMessageInbox restored;
    assert(restored.restore(sending.metadata,
                            sending.encoded_media,
                            sending.metadata.encoded_media_len));
    VoiceMessageView restored_view{};
    assert(restored.get(local_id, &restored_view));
    assert(restored_view.metadata.delivery == VoiceDeliveryState::Failed);
}

void test_conversation_binding_and_read_scope_are_local_only()
{
    VoiceMessageInbox inbox;
    const auto first = privateOffer(0x1111U, 0x111U, 7U);
    const auto second = privateOffer(0x2222U, 0x222U, 7U);
    const std::array<uint8_t, 7> media = {4U, 3U, 2U, 1U, 0U, 1U, 2U};
    uint64_t first_id = 0U;
    uint64_t second_id = 0U;
    assert(inbox.store(first,
                       media.data(),
                       media.size(),
                       true,
                       100U,
                       VoicePresentationProtocol::Meshtastic,
                       1U,
                       &first_id) == VoiceInboxStoreResult::Stored);
    assert(inbox.store(second,
                       media.data(),
                       media.size(),
                       true,
                       200U,
                       VoicePresentationProtocol::MeshCore,
                       1U,
                       &second_id) == VoiceInboxStoreResult::Stored);

    VoiceMessageView first_view{};
    VoiceMessageView second_view{};
    assert(inbox.get(first_id, &first_view));
    assert(inbox.get(second_id, &second_view));
    assert(first_view.metadata.presentation_protocol ==
           VoicePresentationProtocol::Meshtastic);
    assert(first_view.metadata.presentation_channel == 1U);
    assert(!voiceMessageRead(first_view.metadata));
    assert(!voiceMessageRead(second_view.metadata));

    assert(inbox.markConversationRead(VoicePresentationProtocol::Meshtastic,
                                      1U,
                                      first.sender_id,
                                      false));
    assert(inbox.get(first_id, &first_view));
    assert(inbox.get(second_id, &second_view));
    assert(voiceMessageRead(first_view.metadata));
    assert(!voiceMessageRead(second_view.metadata));
    assert(!inbox.markConversationRead(VoicePresentationProtocol::Meshtastic,
                                       1U,
                                       second.sender_id,
                                       false));
}

void test_snapshot_newest_first_restore_preserves_timeline_order()
{
    VoiceMessageInbox original;
    const auto older = privateOffer(0x11223344U, 0x101U, 7U);
    const auto newer = privateOffer(0x11223344U, 0x202U, 7U);
    const std::array<uint8_t, 7> older_media = {1U, 1U, 1U, 1U, 1U, 1U, 1U};
    const std::array<uint8_t, 7> newer_media = {2U, 2U, 2U, 2U, 2U, 2U, 2U};
    assert(original.store(older,
                          older_media.data(),
                          older_media.size(),
                          true,
                          100U,
                          kTestPresentation,
                          kTestChannel,
                          nullptr) == VoiceInboxStoreResult::Stored);
    assert(original.store(newer,
                          newer_media.data(),
                          newer_media.size(),
                          true,
                          200U,
                          kTestPresentation,
                          kTestChannel,
                          nullptr) == VoiceInboxStoreResult::Stored);

    VoiceMessageMetadata snapshot[2] = {};
    assert(original.listMetadata(snapshot, 2U) == 2U);
    assert(snapshot[0].session_id == newer.session_id);
    assert(snapshot[1].session_id == older.session_id);

    VoiceMessageInbox restored;
    for (std::size_t index = 0U; index < 2U; ++index)
    {
        VoiceMessageView view{};
        assert(original.get(snapshot[index].local_id, &view));
        assert(restored.restore(snapshot[index],
                                view.encoded_media,
                                view.metadata.encoded_media_len,
                                static_cast<uint64_t>(2U - index)));
    }

    VoiceMessageMetadata listed[2] = {};
    assert(restored.listMetadata(listed, 2U) == 2U);
    assert(listed[0].session_id == newer.session_id);
    assert(listed[1].session_id == older.session_id);
}

} // namespace

int main()
{
    test_store_local_only_voice_and_dedupe();
    test_broadcast_is_explicitly_unverified();
    test_bad_media_is_never_persisted();
    test_metadata_lists_newest_first_without_media();
    test_restore_retains_playback_identity_and_deduplication();
    test_outgoing_voice_keeps_local_delivery_lifecycle();
    test_interrupted_outgoing_send_restores_as_failed();
    test_conversation_binding_and_read_scope_are_local_only();
    test_snapshot_newest_first_restore_preserves_timeline_order();
    return 0;
}
