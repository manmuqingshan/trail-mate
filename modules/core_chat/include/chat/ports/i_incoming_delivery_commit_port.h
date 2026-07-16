/**
 * @file i_incoming_delivery_commit_port.h
 * @brief Optional two-phase commit port for durable incoming deliveries.
 */

#pragma once

#include "../domain/chat_types.h"

namespace chat
{

class IIncomingDeliveryCommitPort
{
  public:
    virtual ~IIncomingDeliveryCommitPort() = default;

    /**
     * Complete a previously polled incoming text delivery.
     *
     * Adapters that need application-level acceptance, such as an LXMF
     * propagation client, use this callback to defer remote acknowledgement
     * until the message and its durable identity have been committed.
     */
    virtual void commitIncomingText(const MeshIncomingText& message,
                                    bool durably_accepted) = 0;
};

} // namespace chat
