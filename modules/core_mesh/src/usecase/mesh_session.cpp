#include "mesh/usecase/mesh_session.h"

namespace mesh
{

MeshSession::MeshSession(IPacketRadio& radio,
                         MeshProtocolStrategy& protocol,
                         DirectMessageService& direct,
                         ReceivePacketService& receive,
                         IClock& clock)
    : radio_(radio),
      protocol_(protocol),
      direct_(direct),
      receive_(receive),
      clock_(clock)
{
}

bool MeshSession::start(const MeshRuntimeConfig& config)
{
    state_ = MeshSessionState::Starting;
    auto radio_config = protocol_.deriveRadioConfig(config);
    auto configured = radio_.configure(radio_config);
    if (!configured.ok)
    {
        state_ = MeshSessionState::Error;
        return false;
    }

    state_ = MeshSessionState::Ready;
    return true;
}

void MeshSession::stop()
{
    state_ = MeshSessionState::Stopped;
}

void MeshSession::tick()
{
    (void)clock_;
    if (state_ != MeshSessionState::Ready)
    {
        return;
    }

    while (radio_.poll(rx_packet_scratch_))
    {
        receive_.onRadioPacket(rx_packet_scratch_);
        rx_packet_scratch_ = RadioRxPacket{};
    }
}

SendResult MeshSession::sendDirect(const DirectMessageCommand& command)
{
    if (state_ != MeshSessionState::Ready)
    {
        return SendResult::fail(SendFailure::NotReady);
    }
    return direct_.sendDirect(command);
}

MeshSessionState MeshSession::state() const
{
    return state_;
}

} // namespace mesh
