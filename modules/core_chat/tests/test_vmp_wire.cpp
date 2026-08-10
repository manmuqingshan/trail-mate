#include "chat/infra/voice/vmp_wire.h"

#include <array>
#include <cassert>
#include <cstring>

namespace
{

using namespace chat::voice::vmp;

ControlFrame makePrivateOffer()
{
    ControlFrame frame{};
    frame.type = ControlType::Offer;
    frame.flags = ControlFlagPrivate;
    frame.key_or_profile_id = 7;
    frame.sender_id = 0x12345678U;
    frame.target_id = 0x90ABCDEFU;
    frame.session_id = 0x0123456789ABCDEFULL;
    for (std::size_t index = 0; index < sizeof(frame.session_nonce); ++index)
    {
        frame.session_nonce[index] = static_cast<uint8_t>(index + 1U);
    }
    for (std::size_t index = 0; index < sizeof(frame.ephemeral_public_key); ++index)
    {
        frame.ephemeral_public_key[index] = static_cast<uint8_t>(0x40U + index);
    }
    frame.phy_profile_id = 1;
    frame.channel_index = 6;
    frame.encoded_media_len = 813;
    frame.codec = Codec::Codec2_1300;
    frame.total_blocks = 1;
    frame.data_start_delay_ms = 120;
    frame.object_fingerprint = 0x7AA55A77U;
    for (std::size_t index = 0; index < sizeof(frame.integrity_tag); ++index)
    {
        frame.integrity_tag[index] = static_cast<uint8_t>(0xA0U + index);
    }
    return frame;
}

void testPrivateControlRoundTrip()
{
    const ControlFrame expected = makePrivateOffer();
    std::array<uint8_t, kControlFrameSize> bytes{};
    std::size_t len = bytes.size();
    assert(encodeControlFrame(expected, bytes.data(), &len));
    assert(len == bytes.size());

    ControlFrame actual{};
    assert(decodeControlFrame(bytes.data(), bytes.size(), &actual));
    assert(actual.type == expected.type);
    assert(actual.flags == expected.flags);
    assert(actual.sender_id == expected.sender_id);
    assert(actual.target_id == expected.target_id);
    assert(actual.session_id == expected.session_id);
    assert(actual.encoded_media_len == expected.encoded_media_len);
    assert(actual.total_blocks == expected.total_blocks);
    assert(actual.object_fingerprint == expected.object_fingerprint);
    assert(std::memcmp(actual.session_nonce,
                       expected.session_nonce,
                       sizeof(expected.session_nonce)) == 0);
    assert(std::memcmp(actual.ephemeral_public_key,
                       expected.ephemeral_public_key,
                       sizeof(expected.ephemeral_public_key)) == 0);
    assert(std::memcmp(actual.integrity_tag,
                       expected.integrity_tag,
                       sizeof(expected.integrity_tag)) == 0);

    DeliveryMode mode = DeliveryMode::Broadcast;
    assert(deliveryModeFor(actual, &mode));
    assert(mode == DeliveryMode::Private);
}

void testBroadcastAndInvalidControlConstraints()
{
    ControlFrame broadcast = makePrivateOffer();
    broadcast.type = ControlType::Announce;
    broadcast.flags = ControlFlagBroadcast | ControlFlagPublicBroadcast;
    broadcast.key_or_profile_id = 0;
    std::memset(broadcast.ephemeral_public_key,
                0,
                sizeof(broadcast.ephemeral_public_key));
    broadcast.target_id = kBroadcastTargetId;
    broadcast.data_start_delay_ms = 700;
    assert(isValidControlFrame(broadcast));

    ControlFrame invalid_mode = broadcast;
    invalid_mode.flags = ControlFlagPrivate | ControlFlagBroadcast;
    assert(!isValidControlFrame(invalid_mode));

    ControlFrame invalid_broadcast_target = broadcast;
    invalid_broadcast_target.target_id = 42;
    assert(!isValidControlFrame(invalid_broadcast_target));

    ControlFrame invalid_broadcast_key = broadcast;
    invalid_broadcast_key.key_or_profile_id = 1;
    assert(!isValidControlFrame(invalid_broadcast_key));

    ControlFrame invalid_broadcast_ephemeral = broadcast;
    invalid_broadcast_ephemeral.ephemeral_public_key[0] = 1;
    assert(!isValidControlFrame(invalid_broadcast_ephemeral));

    ControlFrame invalid_accept = broadcast;
    invalid_accept.type = ControlType::Accept;
    assert(!isValidControlFrame(invalid_accept));

    ControlFrame invalid_layout = makePrivateOffer();
    invalid_layout.total_blocks = 2;
    assert(!isValidControlFrame(invalid_layout));
}

void testControlDecodeRejectsCorruption()
{
    const ControlFrame expected = makePrivateOffer();
    std::array<uint8_t, kControlFrameSize> bytes{};
    std::size_t len = bytes.size();
    assert(encodeControlFrame(expected, bytes.data(), &len));

    bytes[0] = 'X';
    ControlFrame decoded{};
    assert(!decodeControlFrame(bytes.data(), bytes.size(), &decoded));

    bytes[0] = 'V';
    bytes[4] |= 0x80U;
    assert(!decodeControlFrame(bytes.data(), bytes.size(), &decoded));
}

void testMediaLayoutBoundaries()
{
    MediaLayout layout{};
    assert(!planMediaLayout(0, &layout));
    assert(planMediaLayout(1, &layout));
    assert(layout.source_shard_count == 1);
    assert(layout.block_count == 1);
    assert(layout.data_frame_count == 10);
    assert(sourceShardPayloadSize(layout, 0, 0) == 1);
    assert(sourceShardPayloadSize(layout, 0, 1) == 0);

    assert(planMediaLayout(160, &layout));
    assert(layout.source_shard_count == 1);
    assert(sourceShardPayloadSize(layout, 0, 0) == 160);

    assert(planMediaLayout(161, &layout));
    assert(layout.source_shard_count == 2);
    assert(sourceShardPayloadSize(layout, 0, 0) == 160);
    assert(sourceShardPayloadSize(layout, 0, 1) == 1);

    assert(planMediaLayout(1280, &layout));
    assert(layout.source_shard_count == 8);
    assert(layout.block_count == 1);
    assert(layout.data_frame_count == 10);
    assert(sourceShardPayloadSize(layout, 0, 7) == 160);
    assert(!planMediaLayout(1281, &layout));
}

void testDataHeaderRoundTripAndConstraints()
{
    DataHeader expected{};
    expected.type = DataType::Shard;
    expected.session_id = 0x0102030405060708ULL;
    expected.block_index = 0;
    expected.shard_index = 7;
    expected.payload_len = 53;
    expected.flags = DataFlagFinalBlock | DataFlagPartialSource;

    std::array<uint8_t, kDataHeaderSize> bytes{};
    std::size_t len = bytes.size();
    assert(encodeDataHeader(expected, bytes.data(), &len));
    assert(len == bytes.size());

    DataHeader actual{};
    assert(decodeDataHeader(bytes.data(), bytes.size(), &actual));
    assert(actual.type == expected.type);
    assert(actual.session_id == expected.session_id);
    assert(actual.block_index == expected.block_index);
    assert(actual.shard_index == expected.shard_index);
    assert(actual.payload_len == expected.payload_len);
    assert(actual.flags == expected.flags);

    DataHeader invalid_parity = expected;
    invalid_parity.shard_index = 8;
    assert(!isValidDataHeader(invalid_parity));

    DataHeader unknown{};
    unknown.type = static_cast<DataType>(4);
    unknown.session_id = expected.session_id;
    assert(!isValidDataHeader(unknown));
}

void testReadyHeadersNeverContainVoiceBytes()
{
    DataHeader probe{};
    probe.type = DataType::ReadyProbe;
    probe.session_id = 0x0A0B0C0D0E0F1011ULL;
    assert(isValidDataHeader(probe));

    std::array<uint8_t, kDataHeaderSize> bytes{};
    std::size_t len = bytes.size();
    assert(encodeDataHeader(probe, bytes.data(), &len));

    DataHeader decoded{};
    assert(decodeDataHeader(bytes.data(), bytes.size(), &decoded));
    assert(decoded.type == DataType::ReadyProbe);
    assert(decoded.payload_len == 0);

    DataHeader ready = probe;
    ready.type = DataType::Ready;
    assert(isValidDataHeader(ready));

    ready.payload_len = 1;
    assert(!isValidDataHeader(ready));
    ready.payload_len = 0;
    ready.block_index = 1;
    assert(!isValidDataHeader(ready));
}

} // namespace

int main()
{
    testPrivateControlRoundTrip();
    testBroadcastAndInvalidControlConstraints();
    testControlDecodeRejectsCorruption();
    testMediaLayoutBoundaries();
    testDataHeaderRoundTripAndConstraints();
    testReadyHeadersNeverContainVoiceBytes();
    return 0;
}
