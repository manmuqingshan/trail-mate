#include "chat/infra/voice/vmp_media_frames.h"
#include "chat/infra/voice/vmp_receive_block.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace
{

using namespace chat::voice::vmp;

void fillMedia(uint8_t* media, std::size_t len)
{
    for (std::size_t index = 0; index < len; ++index)
    {
        media[index] = static_cast<uint8_t>((index * 17U) ^ (index >> 3U));
    }
}

void test_exactly_ten_public_frames_recover_encoded_object()
{
    std::array<uint8_t, 875> media{};
    fillMedia(media.data(), media.size());
    TransmitBlock sender{};
    assert(sender.prepare(media.data(), media.size()));
    assert(sender.layout().data_frame_count == kTotalShardsPerBlock);

    ReceiveBlock receiver{};
    assert(receiver.begin(sender.layout()));
    std::array<uint8_t, kPublicShardFrameSize> frame{};
    for (uint8_t index = 0; index < kTotalShardsPerBlock; ++index)
    {
        std::size_t frame_len = frame.size();
        assert(sender.buildPublicShardFrame(0xAABBCCDDEEFF0011ULL,
                                            index,
                                            frame.data(),
                                            &frame_len));
        assert(frame_len == frame.size());
        DataHeader header{};
        const uint8_t* shard = nullptr;
        assert(parsePublicShardFrame(frame.data(), frame_len, &header, &shard));
        const ReceiveBlockResult result = receiver.accept(
            header, shard, kMaxShardPayloadSize);
        assert(result == ReceiveBlockResult::Accepted ||
               result == ReceiveBlockResult::Complete);
    }

    std::array<uint8_t, kMaxEncodedMediaSize> recovered{};
    std::size_t recovered_len = 0;
    assert(receiver.recover(recovered.data(), recovered.size(), &recovered_len));
    assert(recovered_len == media.size());
    assert(std::memcmp(recovered.data(), media.data(), media.size()) == 0);
}

void test_broadcast_crc_rejects_tampering()
{
    std::array<uint8_t, 160> media{};
    fillMedia(media.data(), media.size());
    TransmitBlock sender{};
    assert(sender.prepare(media.data(), media.size()));
    std::array<uint8_t, kPublicShardFrameSize> frame{};
    std::size_t frame_len = frame.size();
    assert(sender.buildPublicShardFrame(8U, 0U, frame.data(), &frame_len));
    frame[kDataHeaderSize + 10U] ^= 0x80U;
    DataHeader header{};
    const uint8_t* shard = nullptr;
    assert(!parsePublicShardFrame(frame.data(), frame_len, &header, &shard));
}

void test_partial_source_flag_is_metadata_not_short_air_frame()
{
    std::array<uint8_t, 161> media{};
    fillMedia(media.data(), media.size());
    TransmitBlock sender{};
    assert(sender.prepare(media.data(), media.size()));
    std::array<uint8_t, kPublicShardFrameSize> frame{};
    std::size_t frame_len = frame.size();
    assert(sender.buildPublicShardFrame(9U, 1U, frame.data(), &frame_len));
    DataHeader header{};
    const uint8_t* shard = nullptr;
    assert(parsePublicShardFrame(frame.data(), frame_len, &header, &shard));
    assert(header.payload_len == kMaxShardPayloadSize);
    assert((header.flags & DataFlagPartialSource) != 0U);
    assert(shard[1] == 0U);
}

void test_public_ready_probe_has_no_media_and_detects_corruption()
{
    DataHeader header{};
    header.type = DataType::ReadyProbe;
    header.session_id = 0x0102030405060708ULL;
    std::array<uint8_t, kPublicReadyFrameSize> frame{};
    std::size_t frame_len = frame.size();
    assert(buildPublicReadyFrame(header, frame.data(), &frame_len));
    assert(frame_len == frame.size());

    DataHeader decoded{};
    assert(parsePublicReadyFrame(frame.data(), frame.size(), &decoded));
    assert(decoded.type == DataType::ReadyProbe);
    assert(decoded.payload_len == 0U);
    frame[4] ^= 0x01U;
    assert(!parsePublicReadyFrame(frame.data(), frame.size(), &decoded));
}

} // namespace

int main()
{
    test_exactly_ten_public_frames_recover_encoded_object();
    test_broadcast_crc_rejects_tampering();
    test_partial_source_flag_is_metadata_not_short_air_frame();
    test_public_ready_probe_has_no_media_and_detects_corruption();
    return 0;
}
