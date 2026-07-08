/**
 * @file reticulum_adapter.h
 * @brief Product-level Reticulum adapter boundary for ESP Arduino targets.
 *
 * The current device-side Reticulum implementation uses the LXMF service layer
 * over the existing RNode-compatible raw carrier. Keep this header as the
 * product protocol entry point so factory code does not expose LXMF or RNode as
 * user-selectable protocols.
 */

#pragma once

#include "chat/ports/i_mesh_adapter.h"

#include <memory>

class LoraBoard;

namespace chat::lxmf
{
class LxmfAdapter;
}

namespace chat::reticulum
{

namespace lxmf = ::chat::lxmf;

class ReticulumAdapter final : public IMeshAdapter
{
  public:
    explicit ReticulumAdapter(LoraBoard& board);
    ~ReticulumAdapter() override;

    ReticulumAdapter(const ReticulumAdapter&) = delete;
    ReticulumAdapter& operator=(const ReticulumAdapter&) = delete;

    MeshCapabilities getCapabilities() const override;
    bool sendText(ChannelId channel, const std::string& text,
                  MessageId* out_msg_id, NodeId peer = 0) override;
    MeshSendResult sendTextDetailed(ChannelId channel, const std::string& text,
                                    MessageId forced_msg_id = 0,
                                    NodeId peer = 0) override;
    MeshSendResult sendTextToReticulumDestination(
        ChannelId channel,
        const std::string& text,
        MessageId forced_msg_id,
        const ReticulumPeerIdentity& destination) override;
    bool pollIncomingText(MeshIncomingText* out) override;
    bool sendAppData(ChannelId channel, uint32_t portnum,
                     const uint8_t* payload, size_t len,
                     NodeId dest = 0, bool want_ack = false,
                     MessageId packet_id = 0,
                     bool want_response = false) override;
    bool pollIncomingData(MeshIncomingData* out) override;
    bool requestNodeInfo(NodeId dest, bool want_response) override;
    bool broadcastSelfIdentity() override;
    NodeId getNodeId() const override;
    bool getReticulumLocalIdentityInfo(ReticulumLocalIdentityInfo* out) const override;
    void applyConfig(const MeshConfig& config) override;
    void setUserInfo(const char* long_name, const char* short_name) override;
    bool isReady() const override;
    bool pollIncomingRawPacket(uint8_t* out_data, size_t& out_len, size_t max_len) override;
    void handleRawPacket(const uint8_t* data, size_t size) override;
    void setLastRxStats(float rssi, float snr) override;

  private:
    std::unique_ptr<lxmf::LxmfAdapter> service_;
};

} // namespace chat::reticulum
