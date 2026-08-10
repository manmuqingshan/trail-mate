#include "chat/infra/voice/vmp_control_auth.h"

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
        out[index] = static_cast<uint8_t>(seed + index * 23U);
    }
}

ControlFrame makeBroadcastAnnounce()
{
    ControlFrame frame{};
    frame.type = ControlType::Announce;
    frame.flags = ControlFlagBroadcast | ControlFlagPublicBroadcast;
    frame.sender_id = 42U;
    frame.target_id = kBroadcastTargetId;
    frame.session_id = 0x9988776655443322ULL;
    fill(frame.session_nonce, sizeof(frame.session_nonce), 0x33U);
    frame.phy_profile_id = 1U;
    frame.channel_index = 6U;
    frame.encoded_media_len = 875U;
    frame.codec = Codec::Codec2_1300;
    frame.fec_layout = kFecLayoutRs10_8;
    frame.total_blocks = 1U;
    frame.data_start_delay_ms = 700U;
    frame.object_fingerprint = 0x12345678U;
    return frame;
}

void test_public_control_corruption_is_rejected()
{
    const ControlFrame frame = makeBroadcastAnnounce();
    std::array<uint8_t, kControlFrameSize> encoded{};
    std::size_t encoded_len = encoded.size();
    assert(encodePublicControlFrame(frame, encoded.data(), &encoded_len));
    assert(encoded_len == encoded.size());

    ControlFrame decoded{};
    assert(decodePublicControlFrame(encoded.data(), encoded.size(), &decoded));
    assert(decoded.sender_id == frame.sender_id);
    assert(decoded.session_id == frame.session_id);
    encoded[36] ^= 0x01U;
    assert(!decodePublicControlFrame(encoded.data(), encoded.size(), &decoded));
}

void test_broadcast_cannot_use_private_encoder()
{
    const ControlFrame frame = makeBroadcastAnnounce();
    PrivateSessionKeys keys{};
    std::array<uint8_t, kControlFrameSize> encoded{};
    std::size_t encoded_len = encoded.size();
    assert(!encodePrivateControlFrame(frame,
                                      keys,
                                      PrivateFrameDirection::SenderToReceiver,
                                      encoded.data(),
                                      &encoded_len));
}

} // namespace

int main()
{
    test_public_control_corruption_is_rejected();
    test_broadcast_cannot_use_private_encoder();
    return 0;
}
