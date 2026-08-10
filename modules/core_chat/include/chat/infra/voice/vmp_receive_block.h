/**
 * @file vmp_receive_block.h
 * @brief Caller-owned bounded VMP v1 media receive block.
 *
 * This object is intentionally a member/static-storage candidate, not a task
 * stack local.  It retains one fixed `(10,8)` FEC block and exposes no radio
 * transmission operation, which makes an inbound voice message terminate in
 * local storage rather than becoming a relay candidate.
 */

#pragma once

#include "chat/infra/voice/vmp_rs_fec.h"

#include <cstddef>
#include <cstdint>

namespace chat::voice::vmp
{

enum class ReceiveBlockResult : uint8_t
{
    Accepted = 1,
    Duplicate = 2,
    Invalid = 3,
    Complete = 4,
};

class ReceiveBlock
{
  public:
    /** @brief Clears prior state and binds this slot to one validated layout. */
    bool begin(const MediaLayout& layout);

    /**
     * @brief Stores one already-authenticated 160-byte source or parity shard.
     *
     * Authentication/decryption belongs to the radio/session layer and MUST
     * happen before this method.  The block rejects duplicate indices and
     * foreign/variable-size shards without changing its existing contents.
     */
    ReceiveBlockResult accept(const DataHeader& header,
                              const uint8_t* shard,
                              std::size_t shard_len);

    /**
     * @brief Attempts RS recovery after at least eight unique shards arrive.
     *
     * The reconstructed encoded media is copied into caller-owned output.  It
     * is exactly the length announced by the validated control frame.
     */
    bool recover(uint8_t* out_media,
                 std::size_t out_capacity,
                 std::size_t* out_media_len);

    void clear();

    bool active() const { return active_; }
    bool recovered() const { return recovered_; }
    std::size_t receivedShardCount() const { return received_shard_count_; }
    const MediaLayout& layout() const { return layout_; }

  private:
    MediaLayout layout_ = {};
    uint8_t shards_[kTotalShardsPerBlock][kMaxShardPayloadSize] = {};
    bool present_[kTotalShardsPerBlock] = {};
    std::size_t received_shard_count_ = 0;
    bool active_ = false;
    bool recovered_ = false;
};

} // namespace chat::voice::vmp
