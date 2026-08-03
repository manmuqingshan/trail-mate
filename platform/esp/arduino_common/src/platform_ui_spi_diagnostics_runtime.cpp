#include "platform/ui/spi_diagnostics_runtime.h"

#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
#include "platform/esp/common/shared_spi_coordinator.h"
#include "sys/clock.h"

namespace platform::ui::spi_diagnostics
{

void read(Snapshot& out)
{
    out = {};
    out.available = true;

    // SdCardInfo is a fixed scalar record (not a file or sector buffer). The
    // storage runtime publishes the successful mount rate only after SdFat
    // initialization completed.
    const ::platform::esp::arduino_common::storage::SdCardInfo card =
        ::platform::esp::arduino_common::storage::sd_card_info();
    out.sd_ready = card.backend !=
                       ::platform::esp::arduino_common::storage::SdCardBackend::None &&
                   card.card_type != 0U && card.sector_size != 0U;
    out.sd_card_type = card.card_type;
    out.sd_filesystem_type = card.fat_type;
    out.sd_initialized_hz = card.initialized_spi_hz;
    out.sd_configured_hz = card.configured_spi_hz;
    out.sd_initialization_attempts = card.initialization_attempts;
    out.sd_capacity_mib =
        static_cast<uint32_t>(card.card_size_bytes / (1024ULL * 1024ULL));

    ::platform::esp::common::SharedSpiCoordinator::RuntimeSnapshot bus{};
    ::platform::esp::common::shared_spi_coordinator().runtimeSnapshot(
        bus, ::sys::millis_now());
    out.bus_active = bus.owner_active;
    out.bus_policy = static_cast<uint8_t>(bus.owner_policy);
    out.health_status = static_cast<uint8_t>(bus.health_status);
    out.health_last_error = bus.health_last_error;
    out.bus_held_ms = bus.owner_held_ms;
    out.display_requests = bus.display_frame_requests;
    out.display_completions = bus.display_frame_completions;
    out.display_busy_retries = bus.display_frame_busy_retries;
    out.display_failures = bus.display_frame_failures;
    out.display_deferrals = bus.display_frame_deferrals;
    out.maximum_display_wait_ms = bus.maximum_display_frame_wait_ms;
    out.release_mismatches = bus.release_mismatches;
    out.maximum_hold_ms = bus.maximum_hold_ms;
}

} // namespace platform::ui::spi_diagnostics
