/**
 * @file vmp_receive_block.cpp
 * @brief Caller-owned bounded VMP v1 media receive block.
 */

#include "chat/infra/voice/vmp_receive_block.h"

#include <cstring>

namespace chat::voice::vmp
{

bool ReceiveBlock::begin(const MediaLayout& layout)
{
    if (layout.encoded_media_len == 0U ||
        layout.encoded_media_len > kMaxEncodedMediaSize ||
        layout.block_count != kMaxBlocks ||
        layout.data_frame_count != kTotalShardsPerBlock ||
        layout.source_shard_count == 0U ||
        layout.source_shard_count > kSourceShardsPerBlock)
    {
        return false;
    }
    MediaLayout verified = {};
    if (!planMediaLayout(layout.encoded_media_len, &verified) ||
        verified.source_shard_count != layout.source_shard_count ||
        verified.block_count != layout.block_count ||
        verified.data_frame_count != layout.data_frame_count)
    {
        return false;
    }

    clear();
    layout_ = layout;
    active_ = true;
    return true;
}

ReceiveBlockResult ReceiveBlock::accept(const DataHeader& header,
                                        const uint8_t* shard,
                                        std::size_t shard_len)
{
    if (!active_ || recovered_ || !shard || header.type != DataType::Shard ||
        !isValidDataHeader(header) || header.block_index != 0U ||
        header.shard_index >= kTotalShardsPerBlock ||
        shard_len != kMaxShardPayloadSize ||
        header.payload_len != kMaxShardPayloadSize)
    {
        return ReceiveBlockResult::Invalid;
    }
    if (present_[header.shard_index])
    {
        return ReceiveBlockResult::Duplicate;
    }

    std::memcpy(shards_[header.shard_index], shard, kMaxShardPayloadSize);
    present_[header.shard_index] = true;
    ++received_shard_count_;
    return received_shard_count_ == kSourceShardsPerBlock
               ? ReceiveBlockResult::Complete
               : ReceiveBlockResult::Accepted;
}

bool ReceiveBlock::recover(uint8_t* out_media,
                           std::size_t out_capacity,
                           std::size_t* out_media_len)
{
    if (!active_ || !out_media || !out_media_len ||
        out_capacity < layout_.encoded_media_len ||
        received_shard_count_ < kSourceShardsPerBlock)
    {
        return false;
    }

    if (!recovered_)
    {
        uint8_t* slots[kTotalShardsPerBlock] = {};
        for (std::size_t index = 0; index < kTotalShardsPerBlock; ++index)
        {
            slots[index] = shards_[index];
        }
        if (!recoverRs10_8(slots, present_, kMaxShardPayloadSize))
        {
            return false;
        }
        recovered_ = true;
    }

    std::size_t copied = 0;
    for (uint8_t source = 0; source < layout_.source_shard_count; ++source)
    {
        const std::size_t source_len = sourceShardPayloadSize(layout_, 0U, source);
        if (source_len == 0U || copied + source_len > layout_.encoded_media_len)
        {
            return false;
        }
        std::memcpy(out_media + copied, shards_[source], source_len);
        copied += source_len;
    }
    if (copied != layout_.encoded_media_len)
    {
        return false;
    }
    *out_media_len = copied;
    return true;
}

void ReceiveBlock::clear()
{
    layout_ = {};
    std::memset(shards_, 0, sizeof(shards_));
    std::memset(present_, 0, sizeof(present_));
    received_shard_count_ = 0;
    active_ = false;
    recovered_ = false;
}

} // namespace chat::voice::vmp
