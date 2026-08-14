/**
 * @file vmp_control_auth.cpp
 * @brief Integrity encoding/verification for VMP Sub-GHz control envelopes.
 */

#include "chat/infra/voice/vmp_control_auth.h"

#include <cstring>

namespace chat::voice::vmp
{
namespace
{

constexpr std::size_t kAuthenticatedControlBytes =
    kControlFrameSize - kControlIntegrityTagSize;

uint64_t crc64Ecma(const uint8_t* data, std::size_t len)
{
    if (!data && len != 0U)
    {
        return 0U;
    }
    uint64_t crc = 0U;
    for (std::size_t index = 0; index < len; ++index)
    {
        crc ^= static_cast<uint64_t>(data[index]) << 56U;
        for (uint8_t bit = 0; bit < 8U; ++bit)
        {
            crc = (crc & (1ULL << 63U)) != 0U
                      ? (crc << 1U) ^ 0x42F0E1EBA9EA3693ULL
                      : crc << 1U;
        }
    }
    return crc;
}

void writeU64(uint64_t value, uint8_t out[8])
{
    for (std::size_t index = 0; index < sizeof(value); ++index)
    {
        out[index] = static_cast<uint8_t>(
            value >> ((sizeof(value) - 1U - index) * 8U));
    }
}

uint64_t readU64(const uint8_t data[8])
{
    uint64_t value = 0U;
    for (std::size_t index = 0; index < sizeof(value); ++index)
    {
        value = (value << 8U) | data[index];
    }
    return value;
}

bool allZero(const uint8_t* data, std::size_t len)
{
    if (!data)
    {
        return false;
    }
    uint8_t aggregate = 0U;
    for (std::size_t index = 0; index < len; ++index)
    {
        aggregate |= data[index];
    }
    return aggregate == 0U;
}

bool isPrivate(const ControlFrame& frame)
{
    DeliveryMode mode = DeliveryMode::Broadcast;
    return deliveryModeFor(frame, &mode) && mode == DeliveryMode::Private;
}

bool isPublicBroadcast(const ControlFrame& frame)
{
    DeliveryMode mode = DeliveryMode::Private;
    return deliveryModeFor(frame, &mode) && mode == DeliveryMode::Broadcast;
}

} // namespace

bool encodePrivateControlFrame(const ControlFrame& frame,
                               const PrivateSessionKeys& keys,
                               PrivateFrameDirection direction,
                               uint8_t* out,
                               std::size_t* inout_len)
{
    if (!out || !inout_len || !isPrivate(frame))
    {
        return false;
    }
    ControlFrame unsigned_frame = frame;
    std::memset(unsigned_frame.integrity_tag, 0, sizeof(unsigned_frame.integrity_tag));
    if (!encodeControlFrame(unsigned_frame, out, inout_len))
    {
        return false;
    }
    return tagPrivateControl(keys,
                             unsigned_frame.session_nonce,
                             unsigned_frame.type,
                             direction,
                             out,
                             kAuthenticatedControlBytes,
                             out + kAuthenticatedControlBytes);
}

bool decodePrivateControlFrame(const uint8_t* data,
                               std::size_t len,
                               const PrivateSessionKeys& keys,
                               PrivateFrameDirection direction,
                               ControlFrame* out_frame)
{
    if (!data || !out_frame || len != kControlFrameSize)
    {
        return false;
    }
    ControlFrame frame{};
    if (!decodeControlFrame(data, len, &frame) || !isPrivate(frame) ||
        !verifyPrivateControlTag(keys,
                                 frame.session_nonce,
                                 frame.type,
                                 direction,
                                 data,
                                 kAuthenticatedControlBytes,
                                 data + kAuthenticatedControlBytes))
    {
        return false;
    }
    *out_frame = frame;
    return true;
}

bool encodePublicControlFrame(const ControlFrame& frame,
                              uint8_t* out,
                              std::size_t* inout_len)
{
    if (!out || !inout_len || !isPublicBroadcast(frame))
    {
        return false;
    }
    ControlFrame unchecked_frame = frame;
    std::memset(unchecked_frame.integrity_tag, 0, sizeof(unchecked_frame.integrity_tag));
    if (!encodeControlFrame(unchecked_frame, out, inout_len))
    {
        return false;
    }
    writeU64(crc64Ecma(out, kAuthenticatedControlBytes),
             out + kAuthenticatedControlBytes);
    std::memset(out + kAuthenticatedControlBytes + sizeof(uint64_t),
                0,
                sizeof(uint64_t));
    return true;
}

bool decodePublicControlFrame(const uint8_t* data,
                              std::size_t len,
                              ControlFrame* out_frame)
{
    if (!data || !out_frame || len != kControlFrameSize ||
        !allZero(data + kAuthenticatedControlBytes + sizeof(uint64_t),
                 sizeof(uint64_t)) ||
        crc64Ecma(data, kAuthenticatedControlBytes) !=
            readU64(data + kAuthenticatedControlBytes))
    {
        return false;
    }
    ControlFrame frame{};
    if (!decodeControlFrame(data, len, &frame) || !isPublicBroadcast(frame))
    {
        return false;
    }
    *out_frame = frame;
    return true;
}

} // namespace chat::voice::vmp
