/**
 * @file app_config_sd_tms_runtime.h
 * @brief SD-card working projection for the ESP AppConfig Preferences store.
 */

#pragma once

#include "app/app_config.h"

#include <cstdint>

namespace app::sd_tms
{

enum class LoadResult : uint8_t
{
    Unavailable,
    Missing,
    Invalid,
    DeferredToNvs,
    Applied,
};

// Validates the on-card document first and applies it in a second pass only
// when it is the current SD authority.  `config` is otherwise untouched.
LoadResult loadWorkingConfig(AppConfig& config);

// Records that NVS now contains a newer configuration before the SD projection
// is replaced.  This prevents an old SD file from overwriting fresh NVS after
// a card removal or interrupted write.
bool markNvsCommitted();

// Streams the supplied configuration to /trailmate/config.tms and writes the
// matching small binary commit record.  A missing card is intentionally not a
// failure of the primary NVS configuration save.
bool syncWorkingConfig(const AppConfig& config);

const char* loadResultName(LoadResult result);

} // namespace app::sd_tms
