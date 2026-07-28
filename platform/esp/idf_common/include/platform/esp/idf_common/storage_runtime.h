#pragma once

namespace chat
{
class SdStore;
class MeshPeerDirectoryCore;
} // namespace chat

namespace platform::esp::idf_common::storage
{

void start_deferred_storage(chat::SdStore* store,
                            chat::MeshPeerDirectoryCore* peer_directory);
void tick_deferred_storage();
void stop_deferred_storage();
bool hydration_active();
bool consume_hydration_ready();

} // namespace platform::esp::idf_common::storage
