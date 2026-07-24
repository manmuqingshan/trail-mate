#pragma once

#include "chat/domain/chat_types.h"

namespace chat
{
class SdStore;
class SdProtocolPeerRepository;
} // namespace chat

namespace platform::esp::arduino_common::storage
{

// Starts the one-shot background recovery worker. The worker exits after
// hydration/maintenance, so storage recovery does not become another
// permanent always-on task.
void start_deferred_storage(chat::SdStore* store,
                            chat::SdProtocolPeerRepository* peer_directory,
                            chat::MeshProtocol active_protocol);

// Advances the retry/maintenance state machine from the foreground loop.
void tick_deferred_storage();

// Returns true once after the initial hydration has completed successfully.
// The foreground loop uses this edge to apply non-critical SD-backed state.
bool consume_hydration_ready();

} // namespace platform::esp::arduino_common::storage
