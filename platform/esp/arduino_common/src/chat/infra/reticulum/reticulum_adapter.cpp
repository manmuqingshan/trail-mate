/**
 * @file reticulum_adapter.cpp
 * @brief Product-level Reticulum adapter boundary for ESP Arduino targets.
 */

#include "platform/esp/arduino_common/chat/infra/reticulum/reticulum_adapter.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_adapter.h"

namespace chat::reticulum
{

ReticulumAdapter::ReticulumAdapter(LoraBoard& board)
    : service_(new lxmf::LxmfAdapter(board))
{
}

ReticulumAdapter::~ReticulumAdapter() = default;

MeshCapabilities ReticulumAdapter::getCapabilities() const
{
    return service_->getCapabilities();
}

bool ReticulumAdapter::sendText(ChannelId channel, const std::string& text,
                                MessageId* out_msg_id, NodeId peer)
{
    return service_->sendText(channel, text, out_msg_id, peer);
}

MeshSendResult ReticulumAdapter::sendTextDetailed(ChannelId channel, const std::string& text,
                                                  MessageId forced_msg_id,
                                                  NodeId peer)
{
    return service_->sendTextDetailed(channel, text, forced_msg_id, peer);
}

MeshSendResult ReticulumAdapter::sendTextToReticulumDestination(
    ChannelId channel,
    const std::string& text,
    MessageId forced_msg_id,
    const ReticulumPeerIdentity& destination)
{
    return service_->sendTextToReticulumDestination(channel, text, forced_msg_id, destination);
}

bool ReticulumAdapter::pollIncomingText(MeshIncomingText* out)
{
    return service_->pollIncomingText(out);
}

bool ReticulumAdapter::sendAppData(ChannelId channel, uint32_t portnum,
                                   const uint8_t* payload, size_t len,
                                   NodeId dest, bool want_ack,
                                   MessageId packet_id, bool want_response)
{
    return service_->sendAppData(channel,
                                 portnum,
                                 payload,
                                 len,
                                 dest,
                                 want_ack,
                                 packet_id,
                                 want_response);
}

bool ReticulumAdapter::pollIncomingData(MeshIncomingData* out)
{
    return service_->pollIncomingData(out);
}

bool ReticulumAdapter::requestNodeInfo(NodeId dest, bool want_response)
{
    return service_->requestNodeInfo(dest, want_response);
}

bool ReticulumAdapter::broadcastSelfIdentity()
{
    return service_->broadcastSelfIdentity();
}

NodeId ReticulumAdapter::getNodeId() const
{
    return service_->getNodeId();
}

bool ReticulumAdapter::getReticulumLocalIdentityInfo(ReticulumLocalIdentityInfo* out) const
{
    return service_->getReticulumLocalIdentityInfo(out);
}

void ReticulumAdapter::applyConfig(const MeshConfig& config)
{
    service_->applyConfig(config);
}

void ReticulumAdapter::setUserInfo(const char* long_name, const char* short_name)
{
    service_->setUserInfo(long_name, short_name);
}

bool ReticulumAdapter::isReady() const
{
    return service_->isReady();
}

bool ReticulumAdapter::pollIncomingRawPacket(uint8_t* out_data, size_t& out_len, size_t max_len)
{
    return service_->pollIncomingRawPacket(out_data, out_len, max_len);
}

void ReticulumAdapter::handleRawPacket(const uint8_t* data, size_t size)
{
    service_->handleRawPacket(data, size);
}

void ReticulumAdapter::setLastRxStats(float rssi, float snr)
{
    service_->setLastRxStats(rssi, snr);
}

void ReticulumAdapter::processSendQueue()
{
    service_->processSendQueue();
}

} // namespace chat::reticulum
