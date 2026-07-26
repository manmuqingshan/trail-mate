#include "mesh/usecase/receive_packet_service.h"

namespace mesh
{

ReceivePacketService::ReceivePacketService(MeshProtocolStrategy& protocol,
                                           PeerIdentityService& identity,
                                           IMeshEventSink& events,
                                           IClock& clock,
                                           MeshDedupService* dedup)
    : protocol_(protocol),
      identity_(identity),
      events_(events),
      clock_(clock),
      dedup_(dedup)
{
}

void ReceivePacketService::onRadioPacket(const RadioRxPacket& packet)
{
    auto parsed = protocol_.parseRadioPacket(packet, event_scratch_);
    if (!parsed.ok)
    {
        events_.emit(MeshEvent{MeshEventKind::ProtocolError,
                               {},
                               static_cast<int>(parsed.failure)});
        return;
    }

    if (dedup_ && event_scratch_.packet_id != 0 &&
        !dedup_->accept(event_scratch_.peer,
                        event_scratch_.packet_id,
                        clock_.nowMs()))
    {
        return;
    }

    handleProtocolEvent(event_scratch_);
}

void ReceivePacketService::handleProtocolEvent(const MeshProtocolEvent& event)
{
    switch (event.kind)
    {
    case MeshProtocolEventKind::MessageReceived:
        events_.emit(MeshEvent{MeshEventKind::MessageReceived, event.peer, 0});
        break;
    case MeshProtocolEventKind::PeerKeyLearned:
        (void)identity_.rememberPeerKey(event.peer_key);
        events_.emit(MeshEvent{MeshEventKind::PeerKeyLearned, event.peer, 0});
        break;
    case MeshProtocolEventKind::PeerAdvertReceived:
        (void)identity_.rememberPeerKey(event.peer_key);
        events_.emit(MeshEvent{MeshEventKind::PeerKeyLearned, event.peer, 0});
        events_.emit(MeshEvent{MeshEventKind::PeerDiscovered, event.peer, 0});
        break;
    case MeshProtocolEventKind::PeerDiscovered:
        events_.emit(MeshEvent{MeshEventKind::PeerDiscovered, event.peer, 0});
        break;
    default:
        break;
    }
}

} // namespace mesh
