/**
 * @file vmp_media_frames.cpp
 * @brief Fixed VMP v1 ten-frame media preparation and wire protection.
 */

#include "chat/infra/voice/vmp_media_frames.h"

#include <cstring>

namespace chat::voice::vmp
{
namespace
{

uint32_t crc32c(const uint8_t* data, std::size_t len)
{
    if (!data && len != 0U)
    {
        return 0U;
    }
    uint32_t value = 0xFFFFFFFFU;
    for (std::size_t index = 0; index < len; ++index)
    {
        value ^= data[index];
        for (uint8_t bit = 0; bit < 8U; ++bit)
        {
            const uint32_t mask = static_cast<uint32_t>(
                -static_cast<int32_t>(value & 1U));
            value = (value >> 1U) ^ (0x82F63B78U & mask);
        }
    }
    return ~value;
}

void writeU32(uint32_t value, uint8_t out[4])
{
    out[0] = static_cast<uint8_t>(value >> 24U);
    out[1] = static_cast<uint8_t>(value >> 16U);
    out[2] = static_cast<uint8_t>(value >> 8U);
    out[3] = static_cast<uint8_t>(value);
}

uint32_t readU32(const uint8_t data[4])
{
    return (static_cast<uint32_t>(data[0]) << 24U) |
           (static_cast<uint32_t>(data[1]) << 16U) |
           (static_cast<uint32_t>(data[2]) << 8U) |
           static_cast<uint32_t>(data[3]);
}

} // namespace

bool buildPublicReadyFrame(const DataHeader& header,
                           uint8_t* out,
                           std::size_t* inout_len)
{
    if (!out || !inout_len ||
        (header.type != DataType::ReadyProbe && header.type != DataType::Ready) ||
        !isValidDataHeader(header))
    {
        return false;
    }
    if (*inout_len < kPublicReadyFrameSize)
    {
        *inout_len = kPublicReadyFrameSize;
        return false;
    }
    std::size_t header_len = kDataHeaderSize;
    if (!encodeDataHeader(header, out, &header_len))
    {
        return false;
    }
    writeU32(crc32c(out, header_len), out + header_len);
    *inout_len = kPublicReadyFrameSize;
    return true;
}

bool parsePublicReadyFrame(const uint8_t* data,
                           std::size_t len,
                           DataHeader* out_header)
{
    if (!data || !out_header || len != kPublicReadyFrameSize ||
        crc32c(data, kDataHeaderSize) != readU32(data + kDataHeaderSize))
    {
        return false;
    }
    DataHeader header{};
    if (!decodeDataHeader(data, kDataHeaderSize, &header) ||
        (header.type != DataType::ReadyProbe && header.type != DataType::Ready))
    {
        return false;
    }
    *out_header = header;
    return true;
}

bool parsePublicShardFrame(const uint8_t* data,
                           std::size_t len,
                           DataHeader* out_header,
                           const uint8_t** out_shard)
{
    if (!data || !out_header || !out_shard || len != kPublicShardFrameSize)
    {
        return false;
    }
    DataHeader header{};
    if (!decodeDataHeader(data, kDataHeaderSize, &header) ||
        header.type != DataType::Shard ||
        header.payload_len != kMaxShardPayloadSize)
    {
        return false;
    }
    const uint32_t expected_crc = crc32c(data, kDataHeaderSize + kMaxShardPayloadSize);
    if (expected_crc != readU32(data + kDataHeaderSize + kMaxShardPayloadSize))
    {
        return false;
    }
    *out_header = header;
    *out_shard = data + kDataHeaderSize;
    return true;
}

bool TransmitBlock::prepare(const uint8_t* encoded_media,
                            std::size_t encoded_media_len)
{
    clear();
    if (!encoded_media || encoded_media_len == 0U ||
        encoded_media_len > kMaxEncodedMediaSize)
    {
        return false;
    }
    MediaLayout planned{};
    if (!planMediaLayout(static_cast<uint16_t>(encoded_media_len), &planned))
    {
        return false;
    }

    std::memcpy(shards_, encoded_media, encoded_media_len);
    const uint8_t* source_shards[kSourceShardsPerBlock] = {};
    for (std::size_t index = 0; index < kSourceShardsPerBlock; ++index)
    {
        source_shards[index] = shards_[index];
    }
    if (!encodeRs10_8(source_shards,
                      kMaxShardPayloadSize,
                      shards_[kSourceShardsPerBlock],
                      shards_[kSourceShardsPerBlock + 1U]))
    {
        clear();
        return false;
    }

    layout_ = planned;
    prepared_ = true;
    return true;
}

void TransmitBlock::clear()
{
    layout_ = {};
    std::memset(shards_, 0, sizeof(shards_));
    prepared_ = false;
}

const uint8_t* TransmitBlock::shard(uint8_t shard_index) const
{
    return prepared_ && shard_index < kTotalShardsPerBlock
               ? shards_[shard_index]
               : nullptr;
}

bool TransmitBlock::buildHeader(uint64_t session_id,
                                uint8_t shard_index,
                                DataHeader* out_header) const
{
    if (!prepared_ || !out_header || session_id == 0U ||
        shard_index >= kTotalShardsPerBlock)
    {
        return false;
    }
    DataHeader header{};
    header.type = DataType::Shard;
    header.session_id = session_id;
    header.block_index = 0U;
    header.shard_index = shard_index;
    header.payload_len = kMaxShardPayloadSize;
    header.flags = DataFlagFinalBlock;
    if (shard_index < layout_.source_shard_count &&
        sourceShardPayloadSize(layout_, 0U, shard_index) < kMaxShardPayloadSize)
    {
        header.flags |= DataFlagPartialSource;
    }
    if (!isValidDataHeader(header))
    {
        return false;
    }
    *out_header = header;
    return true;
}

bool TransmitBlock::buildPublicShardFrame(uint64_t session_id,
                                          uint8_t shard_index,
                                          uint8_t* out,
                                          std::size_t* inout_len) const
{
    if (!out || !inout_len)
    {
        return false;
    }
    if (*inout_len < kPublicShardFrameSize)
    {
        *inout_len = kPublicShardFrameSize;
        return false;
    }
    DataHeader header{};
    std::size_t header_len = kDataHeaderSize;
    if (!buildHeader(session_id, shard_index, &header) ||
        !encodeDataHeader(header, out, &header_len))
    {
        return false;
    }
    std::memcpy(out + header_len, shards_[shard_index], kMaxShardPayloadSize);
    writeU32(crc32c(out, header_len + kMaxShardPayloadSize),
             out + header_len + kMaxShardPayloadSize);
    *inout_len = kPublicShardFrameSize;
    return true;
}

bool TransmitBlock::buildPrivateShardFrame(
    const PrivateSessionKeys& keys,
    const uint8_t session_nonce[kSessionNonceSize],
    uint64_t session_id,
    uint8_t shard_index,
    uint8_t* out,
    std::size_t* inout_len) const
{
    if (!session_nonce || !out || !inout_len)
    {
        return false;
    }
    if (*inout_len < kPrivateShardFrameSize)
    {
        *inout_len = kPrivateShardFrameSize;
        return false;
    }
    DataHeader header{};
    std::size_t header_len = kDataHeaderSize;
    if (!buildHeader(session_id, shard_index, &header) ||
        !encodeDataHeader(header, out, &header_len) ||
        !sealPrivateShard(keys,
                          session_nonce,
                          PrivateFrameDirection::SenderToReceiver,
                          header,
                          shards_[shard_index],
                          kMaxShardPayloadSize,
                          out + header_len,
                          out + header_len + kMaxShardPayloadSize))
    {
        return false;
    }
    *inout_len = kPrivateShardFrameSize;
    return true;
}

} // namespace chat::voice::vmp
