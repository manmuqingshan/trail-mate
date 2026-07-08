/**
 * @file rnode_adapter.h
 * @brief Minimal RNode raw-payload mesh adapter
 */

#pragma once

#include "board/LoraBoard.h"
#include "chat/infra/mesh_incoming_queue.h"
#include "chat/infra/rnode/rnode_packet_wire.h"
#include "chat/ports/i_mesh_adapter.h"
#include <cstddef>

namespace chat
{
namespace rnode
{

class RNodeAdapter : public IMeshAdapter
{
  public:
    explicit RNodeAdapter(LoraBoard& board);

    static void* operator new(std::size_t size);
    static void operator delete(void* ptr) noexcept;
    static void operator delete(void* ptr, std::size_t size) noexcept;

    MeshCapabilities getCapabilities() const override;
    bool sendText(ChannelId channel, const std::string& text,
                  MessageId* out_msg_id, NodeId peer = 0) override;
    bool pollIncomingText(MeshIncomingText* out) override;
    bool sendAppData(ChannelId channel, uint32_t portnum,
                     const uint8_t* payload, size_t len,
                     NodeId dest = 0, bool want_ack = false,
                     MessageId packet_id = 0,
                     bool want_response = false) override;
    bool pollIncomingData(MeshIncomingData* out) override;
    void applyConfig(const MeshConfig& config) override;
    void setLastRxStats(float rssi, float snr) override;
    bool isReady() const override;
    bool pollIncomingRawPacket(uint8_t* out_data, size_t& out_len, size_t max_len) override;
    void handleRawPacket(const uint8_t* data, size_t size) override;
    float lastRxRssi() const { return last_rx_rssi_; }
    float lastRxSnr() const { return last_rx_snr_; }

  private:
    static constexpr uint8_t kSyncWord = 0x12;
    static constexpr uint8_t kCrcLen = 2;

    struct PendingRawPacket
    {
        uint8_t data[chat::rnode::kRNodeMaxPayloadSize] = {};
        size_t len = 0;
    };

    LoraBoard& board_;
    MeshConfig config_;
    bool ready_ = false;
    float last_rx_rssi_ = 0.0f;
    float last_rx_snr_ = 0.0f;
    uint32_t radio_freq_hz_ = 0;
    uint32_t radio_bw_hz_ = 0;
    uint8_t radio_sf_ = 0;
    uint8_t radio_cr_ = 0;
    uint8_t next_sequence_ = 0;
    chat::rnode::ReassemblyState reassembly_;
    PendingRawPacket last_raw_packet_;
    chat::rnode::EncodedAirPacketSet tx_air_packets_scratch_{};
    uint8_t rx_payload_scratch_[chat::rnode::kRNodeMaxPayloadSize] = {};
    bool has_pending_raw_packet_ = false;
    static constexpr std::size_t kIncomingQueueDepth = 4;
    ::chat::infra::IncomingDataQueue<kIncomingQueueDepth, chat::rnode::kRNodeMaxPayloadSize> app_receive_queue_;

    void startRadioReceive();
    void enqueueIncomingData(const uint8_t* payload, size_t len);
};

} // namespace rnode
} // namespace chat
