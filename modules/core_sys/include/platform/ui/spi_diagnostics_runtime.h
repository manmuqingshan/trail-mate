#pragma once

#include <cstdint>

namespace platform::ui::spi_diagnostics
{

// Small, caller-owned runtime view for Settings. It carries scalar facts only:
// no SPI handles, no coordinator tokens, and no display/storage payloads.
struct Snapshot
{
    bool available = false;
    bool sd_ready = false;
    uint8_t sd_card_type = 0;
    uint8_t sd_filesystem_type = 0;
    uint32_t sd_initialized_hz = 0;
    uint32_t sd_configured_hz = 0;
    uint8_t sd_initialization_attempts = 0;
    uint32_t sd_capacity_mib = 0;
    bool bus_active = false;
    uint8_t bus_policy = 0;
    uint8_t health_status = 0;
    int32_t health_last_error = 0;
    uint32_t bus_held_ms = 0;
    uint32_t display_requests = 0;
    uint32_t display_completions = 0;
    uint32_t display_busy_retries = 0;
    uint32_t display_failures = 0;
    uint32_t display_deferrals = 0;
    uint32_t maximum_display_wait_ms = 0;
    uint32_t release_mismatches = 0;
    uint32_t maximum_hold_ms = 0;
};

static_assert(sizeof(Snapshot) <= 64U,
              "SPI diagnostics must remain a small scalar-only snapshot.");

// Fills caller-provided storage without I/O, heap allocation, or access to a
// pixel buffer. Currently implemented for Arduino ESP32 shared-SPI targets.
void read(Snapshot& out);

} // namespace platform::ui::spi_diagnostics
