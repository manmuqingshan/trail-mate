#pragma once

namespace chat
{
class SdStore;
}

namespace platform::esp::idf_common::storage
{

void start_deferred_storage(chat::SdStore* store);
void tick_deferred_storage();
bool consume_hydration_ready();

} // namespace platform::esp::idf_common::storage
