#pragma once

#include "chat/ports/i_mesh_peer_directory.h"

namespace chat
{

/**
 * Canonical owner for protocol peer facts and user contact facts.
 *
 * Protocol observations and user-owned contact facts share one durable owner.
 */
class IProtocolPeerRepository : public IMeshPeerDirectory
{
  public:
    ~IProtocolPeerRepository() override = default;
};

} // namespace chat
