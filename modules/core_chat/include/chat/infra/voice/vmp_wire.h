/**
 * @file vmp_wire.h
 * @brief Trail Mate Voice Message Protocol (VMP) v1 binary framing.
 *
 * VMP is an application-owned, one-hop voice-message protocol.  Its frames
 * deliberately do not share Meshtastic, MeshCore, or Reticulum wire formats.
 * Authentication and encryption are applied by the VMP session layer; this
 * file only gives that layer strict, bounded binary framing.
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace chat::voice::vmp
{

inline constexpr uint8_t kVersion = 1;
inline constexpr std::size_t kControlFrameSize = 95;
inline constexpr std::size_t kControlIntegrityTagSize = 16;
inline constexpr std::size_t kSessionNonceSize = 12;
inline constexpr std::size_t kEphemeralPublicKeySize = 32;
inline constexpr std::size_t kDataHeaderSize = 16;
inline constexpr std::size_t kPrivateDataAuthTagSize = 16;
inline constexpr std::size_t kMaxShardPayloadSize = 160;
inline constexpr std::size_t kSourceShardsPerBlock = 8;
inline constexpr std::size_t kTotalShardsPerBlock = 10;
inline constexpr std::size_t kMaxBlocks = 1;
inline constexpr std::size_t kMaxEncodedMediaSize =
    kSourceShardsPerBlock * kMaxShardPayloadSize;
inline constexpr uint8_t kFecLayoutRs10_8 = 0xA8;
inline constexpr uint32_t kBroadcastTargetId = 0xFFFFFFFFU;

enum class ControlType : uint8_t
{
    Offer = 1,
    Accept = 2,
    Announce = 3,
    Cancel = 4,
};

enum class DeliveryMode : uint8_t
{
    Private = 1,
    Broadcast = 2,
};

enum class Codec : uint8_t
{
    Codec2_1300 = 1,
    Codec2_1200 = 2,
};

enum class DataType : uint8_t
{
    ReadyProbe = 1,
    Ready = 2,
    Shard = 3,
};

enum ControlFlag : uint8_t
{
    ControlFlagPrivate = 1U << 0U,
    ControlFlagBroadcast = 1U << 1U,
    ControlFlagPublicBroadcast = 1U << 2U,
    ControlFlagReticulumCarrierHint = 1U << 3U,
};

enum DataFlag : uint8_t
{
    DataFlagFinalBlock = 1U << 0U,
    DataFlagPartialSource = 1U << 1U,
};

struct ControlFrame
{
    ControlType type = ControlType::Offer;
    uint8_t flags = 0;
    /**
     * Logical local chat channel, independent of the 2.4 GHz RF channel.
     * It is authenticated for private control and CRC-covered for public
     * broadcast so a clip cannot be projected into a different chat thread.
     */
    uint8_t conversation_channel = 0;
    uint32_t sender_id = 0;
    uint32_t target_id = 0;
    uint64_t session_id = 0;
    uint8_t session_nonce[kSessionNonceSize] = {};
    uint8_t phy_profile_id = 0;
    uint8_t channel_index = 0;
    uint16_t encoded_media_len = 0;
    Codec codec = Codec::Codec2_1300;
    uint8_t fec_layout = kFecLayoutRs10_8;
    uint8_t total_blocks = 0;
    uint16_t data_start_delay_ms = 0;
    uint32_t object_fingerprint = 0;
    // Private OFFER/ACCEPT only: ephemeral X25519 public key. Public
    // broadcast must carry all zeroes here and performs no key exchange.
    uint8_t ephemeral_public_key[kEphemeralPublicKeySize] = {};
    // Private frames carry a truncated HMAC here; public broadcast frames use
    // an unkeyed corruption-detection value and provide no origin proof.
    uint8_t integrity_tag[kControlIntegrityTagSize] = {};
};

struct DataHeader
{
    DataType type = DataType::Shard;
    uint64_t session_id = 0;
    uint8_t block_index = 0;
    uint8_t shard_index = 0;
    uint8_t payload_len = 0;
    uint8_t flags = 0;
};

struct MediaLayout
{
    uint16_t encoded_media_len = 0;
    uint8_t source_shard_count = 0;
    uint8_t block_count = 0;
    uint8_t data_frame_count = 0;
};

/**
 * @brief Returns the delivery mode encoded in a valid VMP control frame.
 */
bool deliveryModeFor(const ControlFrame& frame, DeliveryMode* out_mode);

/**
 * @brief Validates VMP v1 semantic constraints before authentication work.
 */
bool isValidControlFrame(const ControlFrame& frame);

/**
 * @brief Encodes one fixed-size Sub-GHz control frame.
 *
 * Private OFFER/ACCEPT frames include an ephemeral X25519 public key so the
 * caller can establish a forward-secret media key without extra packets.
 */
bool encodeControlFrame(const ControlFrame& frame,
                        uint8_t* out,
                        std::size_t* inout_len);

/**
 * @brief Decodes and validates one fixed-size Sub-GHz control frame.
 */
bool decodeControlFrame(const uint8_t* data,
                        std::size_t len,
                        ControlFrame* out_frame);

/**
 * @brief Validates one unauthenticated 2.4 GHz data-frame header.
 */
bool isValidDataHeader(const DataHeader& header);

/**
 * @brief Encodes the fixed authenticated-data header for a VMP data frame.
 */
bool encodeDataHeader(const DataHeader& header,
                      uint8_t* out,
                      std::size_t* inout_len);

/**
 * @brief Decodes and validates the fixed authenticated-data header.
 */
bool decodeDataHeader(const uint8_t* data,
                      std::size_t len,
                      DataHeader* out_header);

/**
 * @brief Plans fixed V1 source/FEC blocks for one encoded Codec2 object.
 */
bool planMediaLayout(uint16_t encoded_media_len, MediaLayout* out_layout);

/**
 * @brief Returns the source-shard payload bytes before zero padding for one slot.
 *
 * A zero return means the slot is padding or is not a source-shard slot.
 */
std::size_t sourceShardPayloadSize(const MediaLayout& layout,
                                   uint8_t block_index,
                                   uint8_t shard_index);

} // namespace chat::voice::vmp
