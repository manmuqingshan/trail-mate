#pragma once

#include <cstddef>
#include <cstdint>

#include "driver/sdmmc_host.h"
#include "platform/esp/idf_common/sdmmc_host_runtime.h"
#include "sdmmc_cmd.h"

namespace platform::esp::idf_common::sd_card_runtime
{

bool mount_sdmmc(sdmmc_host_runtime::SlotOwner owner,
                 const sdmmc_host_t& host,
                 const sdmmc_slot_config_t& slot_config,
                 const char* mount_point,
                 uint8_t max_files);

void unmount_sdmmc(sdmmc_host_runtime::SlotOwner owner);

sdmmc_card_t* mounted_card();

} // namespace platform::esp::idf_common::sd_card_runtime
