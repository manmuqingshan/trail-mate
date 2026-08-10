/**
 * @file vmp_media_frames.h
 * @brief Fixed VMP v1 ten-frame media preparation and wire protection.
 *
 * A sender owns this bounded object for the lifetime of one voice send.  It
 * converts an already Codec2-encoded object into eight padded source shards
 * plus two RS parity shards.  It has no retry or forwarding capability.
 */

#pragma once

#include "chat/infra/voice/vmp_private_crypto.h"
#include "chat/infra/voice/vmp_rs_fec.h"

#include <cstddef>
#include <cstdint>

namespace chat::voice::vmp
{

inline constexpr std::size_t kPublicShardFrameSize =
    kDataHeaderSize + kMaxShardPayloadSize + 4U;
inline constexpr std::size_t kPublicReadyFrameSize = kDataHeaderSize + 4U;
inline constexpr std::size_t kPrivateShardFrameSize =
    kDataHeaderSize + kMaxShardPayloadSize + kPrivateDataAuthTagSize;

/**
 * @brief Builds public broadcast READY_PROBE/READY with CRC-32C protection.
 *
 * VMP broadcast sends only READY_PROBE; parsers recognize READY as well so a
 * received response can be explicitly ignored rather than mistaken for media.
 */
bool buildPublicReadyFrame(const DataHeader& header,
                           uint8_t* out,
                           std::size_t* inout_len);

/** @brief Parses a public zero-payload readiness frame and validates CRC-32C. */
bool parsePublicReadyFrame(const uint8_t* data,
                           std::size_t len,
                           DataHeader* out_header);

/** @brief Validates public broadcast data without claiming sender identity. */
bool parsePublicShardFrame(const uint8_t* data,
                           std::size_t len,
                           DataHeader* out_header,
                           const uint8_t** out_shard);

/**
 * @brief Owns one padded source/FEC block prepared for the fixed ten-frame TX.
 */
class TransmitBlock
{
  public:
    bool prepare(const uint8_t* encoded_media, std::size_t encoded_media_len);

    bool prepared() const { return prepared_; }
    const MediaLayout& layout() const { return layout_; }
    void clear();

    /**
     * @brief Builds a public broadcast frame: header + clear shard + CRC-32C.
     */
    bool buildPublicShardFrame(uint64_t session_id,
                               uint8_t shard_index,
                               uint8_t* out,
                               std::size_t* inout_len) const;

    /**
     * @brief Builds a private frame: header + ciphertext + full AEAD tag.
     */
    bool buildPrivateShardFrame(const PrivateSessionKeys& keys,
                                const uint8_t session_nonce[kSessionNonceSize],
                                uint64_t session_id,
                                uint8_t shard_index,
                                uint8_t* out,
                                std::size_t* inout_len) const;

    const uint8_t* shard(uint8_t shard_index) const;

  private:
    bool buildHeader(uint64_t session_id,
                     uint8_t shard_index,
                     DataHeader* out_header) const;

    MediaLayout layout_ = {};
    uint8_t shards_[kTotalShardsPerBlock][kMaxShardPayloadSize] = {};
    bool prepared_ = false;
};

} // namespace chat::voice::vmp
