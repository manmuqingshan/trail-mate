/**
 * @file lxmf_packet_router.cpp
 * @brief Single routing decision point for Reticulum packets entering LXMF.
 */

#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_packet_router.h"

namespace chat::lxmf::runtime
{

PacketRoute ReticulumPacketRouter::route(const reticulum::ParsedPacket& packet) const
{
    switch (packet.packet_type)
    {
    case reticulum::PacketType::Announce:
        return PacketRoute::Announce;
    case reticulum::PacketType::Proof:
        return PacketRoute::Proof;
    case reticulum::PacketType::LinkRequest:
        return PacketRoute::LinkRequest;
    case reticulum::PacketType::Data:
        return PacketRoute::Data;
    default:
        return PacketRoute::LinkOrTransport;
    }
}

} // namespace chat::lxmf::runtime
