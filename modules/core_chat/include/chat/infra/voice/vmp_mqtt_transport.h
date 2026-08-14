/**
 * @file vmp_mqtt_transport.h
 * @brief Bounded VMP object carrier for MQTT, isolated from MT/MC packets.
 *
 * This layer turns already-valid VMP control and media frames into small MQTT
 * application payloads. It deliberately has no radio API and never produces a
 * radio frame from an inbound MQTT payload. A platform bridge may publish the
 * envelopes when MQTT uplink is enabled, or pass subscribed payloads to the
 * receive transfer for local-only inbox storage.
 */

#pragma once

#include "chat/infra/voice/vmp_contact_secrets.h"
#include "chat/infra/voice/vmp_control_auth.h"
#include "chat/infra/voice/vmp_media_frames.h"
#include "chat/infra/voice/vmp_receive_block.h"

#include <cstddef>
#include <cstdint>

namespace chat::voice::vmp
{

inline constexpr std::size_t kMqttEnvelopePrefixSize = 4U;
inline constexpr std::size_t kMaxMqttEnvelopeSize =
    kMqttEnvelopePrefixSize + kPrivateShardFrameSize;

enum class MqttEnvelopeKind : uint8_t
{
    Control = 1U,
    Shard = 2U,
};

struct MqttEnvelopeView
{
    MqttEnvelopeKind kind = MqttEnvelopeKind::Control;
    const uint8_t* payload = nullptr;
    std::size_t payload_len = 0U;
};

/** @brief Wraps one exact VMP control or media frame for an MQTT topic. */
bool buildMqttEnvelope(MqttEnvelopeKind kind,
                       const uint8_t* payload,
                       std::size_t payload_len,
                       uint8_t* out,
                       std::size_t* inout_len);

/** @brief Parses only a bounded VMP MQTT envelope; no side effects. */
bool parseMqttEnvelope(const uint8_t* data,
                       std::size_t len,
                       MqttEnvelopeView* out_view);

enum class MqttTransferResult : uint8_t
{
    Rejected = 0U,
    Accepted = 1U,
    Duplicate = 2U,
    Complete = 3U,
};

/**
 * @brief Fixed one-object publish plan: control then exactly ten media shards.
 *
 * A platform MQTT client drains this plan at its ordinary traffic budget. It
 * has no retry/rebroadcast policy: a disconnected client merely leaves the
 * bounded local plan pending until replaced or explicitly cleared.
 */
class MqttTransmitTransfer final
{
  public:
    bool preparePrivate(const ControlFrame& control,
                        const uint8_t verified_contact_secret[kPrivateKeySize],
                        const uint8_t* encoded_media,
                        std::size_t encoded_media_len);

    bool prepareBroadcast(const ControlFrame& control,
                          const uint8_t* encoded_media,
                          std::size_t encoded_media_len);

    /** @brief Copies the current envelope without consuming it. */
    bool copyNextEnvelope(uint8_t* out, std::size_t* inout_len);
    /** @brief Commits the envelope previously copied by copyNextEnvelope(). */
    bool commitNextEnvelope();
    /** @brief Compatibility convenience: copy and immediately commit. */
    bool nextEnvelope(uint8_t* out, std::size_t* inout_len);
    bool hasNext() const { return prepared_ && next_index_ <= kTotalShardsPerBlock; }
    const ControlFrame& control() const { return control_; }
    void clear();

  private:
    bool prepareCommon(const ControlFrame& control,
                       const uint8_t* encoded_media,
                       std::size_t encoded_media_len);

    ControlFrame control_{};
    PrivateSessionKeys keys_{};
    TransmitBlock transmit_block_{};
    uint8_t control_wire_[kControlFrameSize] = {};
    uint8_t frame_wire_[kPrivateShardFrameSize] = {};
    uint8_t next_index_ = 0U;
    bool private_mode_ = false;
    bool prepared_ = false;
};

/**
 * @brief Single bounded inbound MQTT VMP object, terminating at local storage.
 *
 * One object is accepted at a time. Private control is authenticated using a
 * verified static contact secret and derives the MQTT-only data key schedule;
 * broadcast remains explicitly unverified. No method here sends, relays, or
 * exposes an accepted object to a radio stack.
 */
class MqttReceiveTransfer final
{
  public:
    MqttTransferResult acceptEnvelope(
        const uint8_t* envelope,
        std::size_t envelope_len,
        uint32_t self_node_id,
        const IVerifiedContactSecretProvider& contacts);

    bool recover(uint8_t* out_media,
                 std::size_t out_capacity,
                 std::size_t* out_media_len);
    const ControlFrame& control() const { return control_; }
    bool active() const { return active_; }
    bool complete() const { return complete_; }
    void clear();

  private:
    MqttTransferResult acceptControl(const uint8_t* control_wire,
                                     std::size_t control_len,
                                     uint32_t self_node_id,
                                     const IVerifiedContactSecretProvider& contacts);
    MqttTransferResult acceptShard(const uint8_t* frame, std::size_t frame_len);

    ControlFrame candidate_control_{};
    ControlFrame control_{};
    DataHeader data_header_{};
    PrivateSessionKeys keys_{};
    ReceiveBlock receive_block_{};
    uint8_t contact_secret_[kPrivateKeySize] = {};
    uint8_t plaintext_[kMaxShardPayloadSize] = {};
    bool private_mode_ = false;
    bool active_ = false;
    bool complete_ = false;
};

} // namespace chat::voice::vmp
