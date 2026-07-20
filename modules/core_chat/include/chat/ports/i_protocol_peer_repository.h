#pragma once

#include "chat/ports/i_contact_store.h"
#include "chat/ports/i_mesh_peer_directory.h"
#include "chat/ports/i_node_store.h"

namespace chat
{

/**
 * Canonical owner for protocol peer facts and user contact facts.
 *
 * The three inherited ports are separate read/write views of one repository;
 * they must never be backed by independent stores in the product runtime.
 */
class IProtocolPeerRepository : public IMeshPeerDirectory
{
  public:
    ~IProtocolPeerRepository() override = default;

    virtual contacts::INodeStore& nodeStoreView() = 0;
    virtual contacts::IContactStore& contactStoreView() = 0;
};

} // namespace chat
