/**
 * @file lxmf_packet_router.h
 * @brief Single routing decision point for Reticulum packets entering LXMF.
 */

#pragma once

#include "chat/infra/lxmf/lxmf_wire.h"

namespace chat::lxmf::runtime
{

enum class PacketRoute
{
    Announce,
    Proof,
    LinkRequest,
    Data,
    LinkOrTransport,
};

class ReticulumPacketRouter
{
  public:
    PacketRoute route(const reticulum::ParsedPacket& packet) const;
};

} // namespace chat::lxmf::runtime
