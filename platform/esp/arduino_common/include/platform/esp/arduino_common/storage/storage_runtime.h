#pragma once

#include "chat/domain/chat_types.h"

namespace chat
{
class SdStore;
class SdProtocolPeerRepository;
} // namespace chat

namespace platform::esp::arduino_common::storage
{

// Arms the stable storage maintenance owner. The task remains blocked on its
// event queue after maintenance completes.
void start_deferred_storage(chat::SdStore* store,
                            chat::SdProtocolPeerRepository* peer_directory,
                            chat::MeshProtocol active_protocol);

// Advances the retry/maintenance state machine from the foreground loop.
void tick_deferred_storage();

// Requests cancellation at the next operation boundary and stops accepting
// foreground maintenance ticks.
void stop_deferred_storage();

// Returns true while the initial SD-backed state is being hydrated. The
// foreground lifecycle must not enter store/repository code during this
// exclusive hydration phase.
bool hydration_active();

// Optional interactive SD reads, such as map tiles, must defer while the
// shared-display-SPI startup hydration barrier is active. The query is false
// for independent storage topologies.
bool interactive_storage_reads_deferred();

// Returns true once after the initial hydration has completed successfully.
// The foreground loop uses this edge to apply non-critical SD-backed state.
bool consume_hydration_ready();

} // namespace platform::esp::arduino_common::storage
