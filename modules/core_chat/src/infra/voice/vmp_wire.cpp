/**
 * @file vmp_wire.cpp
 * @brief Trail Mate Voice Message Protocol (VMP) v1 binary framing.
 */

#include "chat/infra/voice/vmp_wire.h"

#include <cstring>

namespace chat::voice::vmp
{
namespace
{

constexpr uint8_t kControlMagic0 = 'V';
constexpr uint8_t kControlMagic1 = 'M';
constexpr uint8_t kDataMagic0 = 'V';
constexpr uint8_t kDataMagic1 = 'D';
constexpr std::size_t kControlEphemeralKeyOffset = 47;
constexpr std::size_t kControlIntegrityTagOffset =
    kControlEphemeralKeyOffset + kEphemeralPublicKeySize;

void writeU16(uint16_t value, uint8_t* out)
{
    out[0] = static_cast<uint8_t>(value >> 8U);
    out[1] = static_cast<uint8_t>(value);
}

void writeU32(uint32_t value, uint8_t* out)
{
    out[0] = static_cast<uint8_t>(value >> 24U);
    out[1] = static_cast<uint8_t>(value >> 16U);
    out[2] = static_cast<uint8_t>(value >> 8U);
    out[3] = static_cast<uint8_t>(value);
}

void writeU64(uint64_t value, uint8_t* out)
{
    for (std::size_t index = 0; index < sizeof(value); ++index)
    {
        const std::size_t shift = (sizeof(value) - 1U - index) * 8U;
        out[index] = static_cast<uint8_t>(value >> shift);
    }
}

uint16_t readU16(const uint8_t* data)
{
    return (static_cast<uint16_t>(data[0]) << 8U) |
           static_cast<uint16_t>(data[1]);
}

uint32_t readU32(const uint8_t* data)
{
    return (static_cast<uint32_t>(data[0]) << 24U) |
           (static_cast<uint32_t>(data[1]) << 16U) |
           (static_cast<uint32_t>(data[2]) << 8U) |
           static_cast<uint32_t>(data[3]);
}

uint64_t readU64(const uint8_t* data)
{
    uint64_t value = 0;
    for (std::size_t index = 0; index < sizeof(value); ++index)
    {
        value = (value << 8U) | data[index];
    }
    return value;
}

bool isKnownControlType(ControlType type)
{
    return type == ControlType::Offer || type == ControlType::Accept ||
           type == ControlType::Announce || type == ControlType::Cancel;
}

bool isKnownCodec(Codec codec)
{
    return codec == Codec::Codec2_1300 || codec == Codec::Codec2_1200;
}

bool isAllZero(const uint8_t* data, std::size_t len)
{
    if (!data)
    {
        return true;
    }
    for (std::size_t index = 0; index < len; ++index)
    {
        if (data[index] != 0)
        {
            return false;
        }
    }
    return true;
}

bool hasOnlyKnownControlFlags(uint8_t flags)
{
    constexpr uint8_t kKnownFlags = ControlFlagPrivate |
                                    ControlFlagBroadcast |
                                    ControlFlagPublicBroadcast |
                                    ControlFlagReticulumCarrierHint;
    return (flags & ~kKnownFlags) == 0;
}

bool hasOnlyKnownDataFlags(uint8_t flags)
{
    constexpr uint8_t kKnownFlags = DataFlagFinalBlock |
                                    DataFlagPartialSource;
    return (flags & ~kKnownFlags) == 0;
}

} // namespace

bool deliveryModeFor(const ControlFrame& frame, DeliveryMode* out_mode)
{
    if (!out_mode)
    {
        return false;
    }

    const bool is_private = (frame.flags & ControlFlagPrivate) != 0;
    const bool is_broadcast = (frame.flags & ControlFlagBroadcast) != 0;
    if (is_private == is_broadcast)
    {
        return false;
    }

    *out_mode = is_private ? DeliveryMode::Private : DeliveryMode::Broadcast;
    return true;
}

bool planMediaLayout(uint16_t encoded_media_len, MediaLayout* out_layout)
{
    if (!out_layout || encoded_media_len == 0 ||
        encoded_media_len > kMaxEncodedMediaSize)
    {
        return false;
    }

    const std::size_t source_shard_count =
        (static_cast<std::size_t>(encoded_media_len) + kMaxShardPayloadSize - 1U) /
        kMaxShardPayloadSize;
    const std::size_t block_count =
        (source_shard_count + kSourceShardsPerBlock - 1U) /
        kSourceShardsPerBlock;
    if (source_shard_count == 0 || block_count == 0 || block_count > kMaxBlocks)
    {
        return false;
    }

    *out_layout = MediaLayout{};
    out_layout->encoded_media_len = encoded_media_len;
    out_layout->source_shard_count = static_cast<uint8_t>(source_shard_count);
    out_layout->block_count = static_cast<uint8_t>(block_count);
    out_layout->data_frame_count = static_cast<uint8_t>(
        block_count * kTotalShardsPerBlock);
    return true;
}

std::size_t sourceShardPayloadSize(const MediaLayout& layout,
                                   uint8_t block_index,
                                   uint8_t shard_index)
{
    if (layout.encoded_media_len == 0 ||
        layout.source_shard_count == 0 ||
        layout.block_count == 0 ||
        block_index >= layout.block_count ||
        shard_index >= kSourceShardsPerBlock)
    {
        return 0;
    }

    const std::size_t source_index =
        static_cast<std::size_t>(block_index) * kSourceShardsPerBlock + shard_index;
    if (source_index >= layout.source_shard_count)
    {
        return 0;
    }

    const std::size_t byte_offset = source_index * kMaxShardPayloadSize;
    const std::size_t bytes_remaining =
        static_cast<std::size_t>(layout.encoded_media_len) - byte_offset;
    return bytes_remaining < kMaxShardPayloadSize ? bytes_remaining
                                                  : kMaxShardPayloadSize;
}

bool isValidControlFrame(const ControlFrame& frame)
{
    if (!isKnownControlType(frame.type) || !hasOnlyKnownControlFlags(frame.flags) ||
        frame.sender_id == 0 || frame.session_id == 0 ||
        isAllZero(frame.session_nonce, sizeof(frame.session_nonce)))
    {
        return false;
    }

    DeliveryMode mode = DeliveryMode::Private;
    if (!deliveryModeFor(frame, &mode))
    {
        return false;
    }

    if (mode == DeliveryMode::Private)
    {
        if (frame.target_id == 0 || frame.target_id == kBroadcastTargetId ||
            (frame.flags & ControlFlagPublicBroadcast) != 0)
        {
            return false;
        }
        if (frame.type != ControlType::Offer && frame.type != ControlType::Accept &&
            frame.type != ControlType::Cancel)
        {
            return false;
        }
    }
    else
    {
        if (frame.target_id != kBroadcastTargetId || frame.type == ControlType::Accept ||
            frame.key_or_profile_id != 0 ||
            (frame.flags & ControlFlagPublicBroadcast) == 0)
        {
            return false;
        }
    }

    if (frame.type == ControlType::Cancel)
    {
        return frame.encoded_media_len == 0 && frame.total_blocks == 0 &&
               (mode == DeliveryMode::Private ||
                isAllZero(frame.ephemeral_public_key,
                          sizeof(frame.ephemeral_public_key)));
    }

    if (!isKnownCodec(frame.codec) || frame.fec_layout != kFecLayoutRs10_8 ||
        frame.data_start_delay_ms == 0)
    {
        return false;
    }

    MediaLayout layout{};
    if (!planMediaLayout(frame.encoded_media_len, &layout) ||
        layout.block_count != frame.total_blocks)
    {
        return false;
    }

    return mode == DeliveryMode::Private
               ? !isAllZero(frame.ephemeral_public_key,
                            sizeof(frame.ephemeral_public_key))
               : isAllZero(frame.ephemeral_public_key,
                           sizeof(frame.ephemeral_public_key));
}

bool encodeControlFrame(const ControlFrame& frame,
                        uint8_t* out,
                        std::size_t* inout_len)
{
    if (!out || !inout_len || !isValidControlFrame(frame))
    {
        return false;
    }
    if (*inout_len < kControlFrameSize)
    {
        *inout_len = kControlFrameSize;
        return false;
    }

    out[0] = kControlMagic0;
    out[1] = kControlMagic1;
    out[2] = kVersion;
    out[3] = static_cast<uint8_t>(frame.type);
    out[4] = frame.flags;
    out[5] = frame.key_or_profile_id;
    writeU32(frame.sender_id, out + 6);
    writeU32(frame.target_id, out + 10);
    writeU64(frame.session_id, out + 14);
    std::memcpy(out + 22, frame.session_nonce, sizeof(frame.session_nonce));
    out[34] = frame.phy_profile_id;
    out[35] = frame.channel_index;
    writeU16(frame.encoded_media_len, out + 36);
    out[38] = static_cast<uint8_t>(frame.codec);
    out[39] = frame.fec_layout;
    out[40] = frame.total_blocks;
    writeU16(frame.data_start_delay_ms, out + 41);
    writeU32(frame.object_fingerprint, out + 43);
    std::memcpy(out + kControlEphemeralKeyOffset,
                frame.ephemeral_public_key,
                sizeof(frame.ephemeral_public_key));
    std::memcpy(out + kControlIntegrityTagOffset,
                frame.integrity_tag,
                sizeof(frame.integrity_tag));
    *inout_len = kControlFrameSize;
    return true;
}

bool decodeControlFrame(const uint8_t* data,
                        std::size_t len,
                        ControlFrame* out_frame)
{
    if (!data || !out_frame || len != kControlFrameSize ||
        data[0] != kControlMagic0 || data[1] != kControlMagic1 ||
        data[2] != kVersion)
    {
        return false;
    }

    ControlFrame frame{};
    frame.type = static_cast<ControlType>(data[3]);
    frame.flags = data[4];
    frame.key_or_profile_id = data[5];
    frame.sender_id = readU32(data + 6);
    frame.target_id = readU32(data + 10);
    frame.session_id = readU64(data + 14);
    std::memcpy(frame.session_nonce, data + 22, sizeof(frame.session_nonce));
    frame.phy_profile_id = data[34];
    frame.channel_index = data[35];
    frame.encoded_media_len = readU16(data + 36);
    frame.codec = static_cast<Codec>(data[38]);
    frame.fec_layout = data[39];
    frame.total_blocks = data[40];
    frame.data_start_delay_ms = readU16(data + 41);
    frame.object_fingerprint = readU32(data + 43);
    std::memcpy(frame.ephemeral_public_key,
                data + kControlEphemeralKeyOffset,
                sizeof(frame.ephemeral_public_key));
    std::memcpy(frame.integrity_tag,
                data + kControlIntegrityTagOffset,
                sizeof(frame.integrity_tag));
    if (!isValidControlFrame(frame))
    {
        return false;
    }

    *out_frame = frame;
    return true;
}

bool isValidDataHeader(const DataHeader& header)
{
    if (header.session_id == 0 || !hasOnlyKnownDataFlags(header.flags))
    {
        return false;
    }

    if (header.type == DataType::ReadyProbe || header.type == DataType::Ready)
    {
        return header.payload_len == 0 && header.block_index == 0 &&
               header.shard_index == 0 && header.flags == 0;
    }

    if (header.type != DataType::Shard || header.block_index >= kMaxBlocks ||
        header.shard_index >= kTotalShardsPerBlock || header.payload_len == 0 ||
        header.payload_len > kMaxShardPayloadSize)
    {
        return false;
    }

    const bool is_source = header.shard_index < kSourceShardsPerBlock;
    return is_source || (header.flags & DataFlagPartialSource) == 0;
}

bool encodeDataHeader(const DataHeader& header,
                      uint8_t* out,
                      std::size_t* inout_len)
{
    if (!out || !inout_len || !isValidDataHeader(header))
    {
        return false;
    }
    if (*inout_len < kDataHeaderSize)
    {
        *inout_len = kDataHeaderSize;
        return false;
    }

    out[0] = kDataMagic0;
    out[1] = kDataMagic1;
    out[2] = kVersion;
    out[3] = static_cast<uint8_t>(header.type);
    writeU64(header.session_id, out + 4);
    out[12] = header.block_index;
    out[13] = header.shard_index;
    out[14] = header.payload_len;
    out[15] = header.flags;
    *inout_len = kDataHeaderSize;
    return true;
}

bool decodeDataHeader(const uint8_t* data,
                      std::size_t len,
                      DataHeader* out_header)
{
    if (!data || !out_header || len != kDataHeaderSize ||
        data[0] != kDataMagic0 || data[1] != kDataMagic1 ||
        data[2] != kVersion)
    {
        return false;
    }

    DataHeader header{};
    header.type = static_cast<DataType>(data[3]);
    header.session_id = readU64(data + 4);
    header.block_index = data[12];
    header.shard_index = data[13];
    header.payload_len = data[14];
    header.flags = data[15];
    if (!isValidDataHeader(header))
    {
        return false;
    }

    *out_header = header;
    return true;
}

} // namespace chat::voice::vmp
