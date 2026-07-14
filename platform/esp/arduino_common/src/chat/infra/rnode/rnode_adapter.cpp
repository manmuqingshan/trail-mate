/**
 * @file rnode_adapter.cpp
 * @brief Minimal RNode raw-payload mesh adapter
 */

#include "platform/esp/arduino_common/chat/infra/rnode/rnode_adapter.h"

#include "chat/time_utils.h"
#if defined(ARDUINO)
#include "platform/esp/arduino_common/app_tasks.h"
#endif
#include "platform/esp/common/reticulum_runtime_compat.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <esp_heap_caps.h>
#include <new>

namespace chat
{
namespace rnode
{

namespace
{
constexpr float kDefaultFrequencyMHz = 869.525f;
constexpr float kDefaultBandwidthKHz = 125.0f;
constexpr uint8_t kDefaultSpreadingFactor = 9;
constexpr uint8_t kDefaultCodingRate = 5;
constexpr int8_t kDefaultTxPowerDbm = 17;
constexpr int kRadioOk = 0;
constexpr int kRadioUnsupported = -1;

template <typename T>
T clampValue(T value, T min_value, T max_value)
{
    if (value < min_value)
    {
        return min_value;
    }
    if (value > max_value)
    {
        return max_value;
    }
    return value;
}

} // namespace

RNodeAdapter::RNodeAdapter(LoraBoard& board)
    : board_(board)
{
}

void* RNodeAdapter::operator new(std::size_t size)
{
    void* ptr = heap_caps_malloc_prefer(size,
                                        2,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    return ptr != nullptr ? ptr : ::operator new(size);
}

void RNodeAdapter::operator delete(void* ptr) noexcept
{
    heap_caps_free(ptr);
}

void RNodeAdapter::operator delete(void* ptr, std::size_t) noexcept
{
    operator delete(ptr);
}

MeshCapabilities RNodeAdapter::getCapabilities() const
{
    return MeshCapabilities{};
}

bool RNodeAdapter::sendText(ChannelId channel, const std::string& text,
                            MessageId* out_msg_id, NodeId peer)
{
    (void)channel;
    (void)text;
    (void)peer;
    if (out_msg_id)
    {
        *out_msg_id = 0;
    }
    return false;
}

bool RNodeAdapter::pollIncomingText(MeshIncomingText* out)
{
    (void)out;
    return false;
}

bool RNodeAdapter::sendAppData(ChannelId channel, uint32_t portnum,
                               const uint8_t* payload, size_t len,
                               NodeId dest, bool want_ack,
                               MessageId packet_id,
                               bool want_response)
{
    (void)channel;
    (void)dest;
    (void)want_ack;
    (void)want_response;

    // RNode air payloads are raw Reticulum/TNC bytes. We reserve port 0
    // for pass-through raw payload transmission and reject higher-level
    // app-data semantics until a Reticulum-compatible upper layer exists.
    if (!payload || len == 0 || portnum != 0 || !ready_ || !board_.isRadioOnline())
    {
        return false;
    }

    tx_air_packets_scratch_ = EncodedAirPacketSet{};
    EncodedAirPacketSet& air_packets = tx_air_packets_scratch_;
    const uint8_t sequence = static_cast<uint8_t>(((packet_id != 0 ? packet_id : next_sequence_) & 0x0FU));
    next_sequence_ = static_cast<uint8_t>((sequence + 1U) & 0x0FU);
    if (!encodeAirPacketSet(payload, len, sequence, &air_packets))
    {
        return false;
    }

    Serial.printf("[RNode][TX] raw_len=%u seq=%u air_count=%u first_len=%u second_len=%u\n",
                  static_cast<unsigned>(len),
                  static_cast<unsigned>(sequence),
                  static_cast<unsigned>(air_packets.count),
                  static_cast<unsigned>(air_packets.first_len),
                  static_cast<unsigned>(air_packets.second_len));

    int first_state = kRadioUnsupported;
    int second_state = kRadioOk;
    {
#if defined(ARDUINO)
        app::AppTasks::ScopedRadioTransmitActivity tx_activity;
#endif
        first_state = board_.transmitRadio(air_packets.first, air_packets.first_len);
        if (first_state == kRadioOk && air_packets.count > 1U)
        {
            second_state = board_.transmitRadio(air_packets.second, air_packets.second_len);
        }
    }
    if (first_state != kRadioOk)
    {
        Serial.printf("[RNode][TX] result ok=0 first=%d second=%d\n",
                      first_state,
                      second_state);
        startRadioReceive();
        return false;
    }

    if (second_state != kRadioOk)
    {
        Serial.printf("[RNode][TX] result ok=0 first=%d second=%d\n",
                      first_state,
                      second_state);
        startRadioReceive();
        return false;
    }

    Serial.printf("[RNode][TX] result ok=1 first=%d second=%d raw_len=%u\n",
                  first_state,
                  second_state,
                  static_cast<unsigned>(len));
    startRadioReceive();
    return true;
}

bool RNodeAdapter::pollIncomingData(MeshIncomingData* out)
{
    return app_receive_queue_.pop(out);
}

void RNodeAdapter::applyConfig(const MeshConfig& config)
{
    config_ = config;

    const float freq_mhz =
        (config_.override_frequency_mhz > 0.0f) ? config_.override_frequency_mhz : kDefaultFrequencyMHz;
    const float bw_khz =
        (config_.bandwidth_khz > 0.0f) ? config_.bandwidth_khz : kDefaultBandwidthKHz;
    const uint8_t sf =
        clampValue<uint8_t>(config_.spread_factor != 0 ? config_.spread_factor : kDefaultSpreadingFactor, 5U, 12U);
    const uint8_t cr =
        clampValue<uint8_t>(config_.coding_rate != 0 ? config_.coding_rate : kDefaultCodingRate, 5U, 8U);
    const int8_t tx_power =
        clampValue<int8_t>(config_.tx_power != 0 ? config_.tx_power : kDefaultTxPowerDbm, -9, 22);

    radio_freq_hz_ = static_cast<uint32_t>(std::lround(freq_mhz * 1000000.0f));
    radio_bw_hz_ = static_cast<uint32_t>(std::lround(bw_khz * 1000.0f));
    radio_sf_ = sf;
    radio_cr_ = cr;

    const uint16_t preamble =
        chat::rnode::recommendPreambleSymbols(radio_bw_hz_, radio_sf_, radio_cr_);

    board_.configureLoraRadio(freq_mhz, bw_khz, sf, cr, tx_power, preamble, kSyncWord, kCrcLen);
    ready_ = true;
    startRadioReceive();
}

void RNodeAdapter::setLastRxStats(float rssi, float snr)
{
    last_rx_rssi_ = rssi;
    last_rx_snr_ = snr;
}

bool RNodeAdapter::isReady() const
{
    return ready_ && board_.isRadioOnline();
}

bool RNodeAdapter::pollIncomingRawPacket(uint8_t* out_data, size_t& out_len, size_t max_len)
{
    if (!has_pending_raw_packet_ || !out_data || max_len == 0)
    {
        return false;
    }

    const size_t copy_len = std::min(last_raw_packet_.len, max_len);
    memcpy(out_data, last_raw_packet_.data, copy_len);
    out_len = copy_len;
    has_pending_raw_packet_ = false;
    return true;
}

void RNodeAdapter::handleRawPacket(const uint8_t* data, size_t size)
{
    if (!data || size == 0)
    {
        return;
    }

    size_t payload_len = sizeof(rx_payload_scratch_);
    bool complete = false;
    if (!feedAirPacket(&reassembly_, data, size, rx_payload_scratch_, &payload_len, &complete) || !complete)
    {
        Serial.printf("[RNode][RX] air_len=%u complete=%u accepted=0\n",
                      static_cast<unsigned>(size),
                      complete ? 1U : 0U);
        return;
    }

    memcpy(last_raw_packet_.data, rx_payload_scratch_, payload_len);
    last_raw_packet_.len = payload_len;
    has_pending_raw_packet_ = true;
    Serial.printf("[RNode][RX] air_len=%u raw_len=%u complete=1\n",
                  static_cast<unsigned>(size),
                  static_cast<unsigned>(payload_len));
    enqueueIncomingData(rx_payload_scratch_, payload_len);
}

void RNodeAdapter::startRadioReceive()
{
    if (!board_.isRadioOnline())
    {
        return;
    }
    (void)board_.startRadioReceive();
}

void RNodeAdapter::enqueueIncomingData(const uint8_t* payload, size_t len)
{
    if (!payload || len == 0)
    {
        return;
    }

    MeshIncomingData incoming;
    incoming.portnum = 0;
    incoming.from = 0;
    incoming.to = 0;
    incoming.packet_id = now_message_timestamp();
    incoming.request_id = 0;
    incoming.channel = ChannelId::PRIMARY;
    incoming.channel_hash = 0xFF;
    incoming.hop_limit = 0xFF;
    incoming.want_response = false;

    incoming.rx_meta.rx_timestamp_ms = millis();
    const uint32_t epoch_s = now_epoch_seconds();
    if (is_valid_epoch(epoch_s))
    {
        incoming.rx_meta.rx_timestamp_s = epoch_s;
        incoming.rx_meta.time_source = RxTimeSource::DeviceUtc;
    }
    else
    {
        incoming.rx_meta.rx_timestamp_s = incoming.rx_meta.rx_timestamp_ms / 1000U;
        incoming.rx_meta.time_source = RxTimeSource::Uptime;
    }
    incoming.rx_meta.origin = RxOrigin::Mesh;
    incoming.rx_meta.direct = true;
    incoming.rx_meta.from_is = false;
    incoming.rx_meta.rssi_dbm_x10 = static_cast<int16_t>(std::lround(last_rx_rssi_ * 10.0f));
    incoming.rx_meta.snr_db_x10 = static_cast<int16_t>(std::lround(last_rx_snr_ * 10.0f));
    incoming.rx_meta.freq_hz = radio_freq_hz_;
    incoming.rx_meta.bw_hz = radio_bw_hz_;
    incoming.rx_meta.sf = radio_sf_;
    incoming.rx_meta.cr = radio_cr_;

    ::chat::infra::IncomingQueuePushReport report{};
    if (app_receive_queue_.push(incoming,
                                payload,
                                len,
                                ::chat::infra::IncomingQueuePriority::P1User,
                                &report))
    {
        if (report.dropped_existing)
        {
            Serial.printf("[RNode] RX data queue pressure evicted_prio=%u depth=%u\n",
                          static_cast<unsigned>(report.dropped_priority),
                          static_cast<unsigned>(app_receive_queue_.size()));
        }
        return;
    }

    Serial.printf("[RNode] RX data queue drop len=%u depth=%u\n",
                  static_cast<unsigned>(len),
                  static_cast<unsigned>(app_receive_queue_.size()));
}

} // namespace rnode
} // namespace chat
