#pragma once

#include "sys/shared_spi_access.h"

namespace platform::esp::arduino_common::storage
{

// Used only by the board-enabled SdFat SPI driver. Business and filesystem
// callers must not acquire the physical bus through this interface.
bool sd_spi_bus_acquire(sys::runtime::BusAccessToken& token);
void sd_spi_bus_release(const sys::runtime::BusAccessToken& token);

} // namespace platform::esp::arduino_common::storage
