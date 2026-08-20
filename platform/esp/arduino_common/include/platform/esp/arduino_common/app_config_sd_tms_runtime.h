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

// Binds the live AppConfig after startup.  Independent settings-store owners
// then mark a coalesced SD projection dirty without reaching back into UI code.
void bindWorkingConfig(const AppConfig& config);

// Flushes a pending settings-store change from the normal application loop.
// It deliberately does no SD I/O from Wi-Fi timers or other write callers.
void serviceWorkingConfig();

// Records that NVS now contains a newer configuration before the SD projection
// is replaced.  This prevents an old SD file from overwriting fresh NVS after
// a card removal or interrupted write.
bool markNvsCommitted();

// Streams the supplied configuration to /trailmate/config.tms and writes the
// matching small binary commit record.  A missing card is intentionally not a
// failure of the primary NVS configuration save.
bool syncWorkingConfig(const AppConfig& config);

// Suppresses settings-store change notifications across the NVS half of a
// factory reset.  This prevents a partially reset runtime from recreating the
// just-removed SD authority before the restart.
void beginWorkingConfigReset();
void endWorkingConfigReset();

// Factory reset removes the SD authority as well as its tiny commit metadata.
// A missing SD card is already-reset from this runtime's perspective.
bool resetWorkingConfig();

const char* loadResultName(LoadResult result);

} // namespace app::sd_tms
