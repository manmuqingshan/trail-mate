#pragma once

#include <cstddef>

namespace platform::esp::idf_common
{

void setLvglTaskOwnedUiDispatch(bool enabled);
void tickBoundLifecycle(std::size_t max_events = 32);
void tickLvglTaskOwnedUiLifecycle(std::size_t max_events = 32);

} // namespace platform::esp::idf_common
