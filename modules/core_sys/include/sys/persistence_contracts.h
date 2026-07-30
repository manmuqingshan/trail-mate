#pragma once

#include <cstdint>

namespace sys
{

using PersistenceGeneration = uint32_t;

enum class PersistenceResultKind : uint8_t
{
    Completed,
    InProgress,
    StateBusy,
    DeviceUnavailable,
    RetryLater,
    IoError,
    Cancelled,
    StaleGeneration,
};

} // namespace sys
