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

} // namespace platform::esp::arduino_common::storage
