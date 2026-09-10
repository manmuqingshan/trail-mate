#if defined(TRAIL_MATE_SDFAT_SDMMC)
#include "platform/esp/arduino_common/storage/sdmmc_block_device.h"

#include <cstring>
#include <driver/sdmmc_host.h>
#include <esp_attr.h>
#include <esp_log.h>

namespace platform::esp::arduino_common::storage
{
namespace
{
DMA_ATTR uint8_t s_sector_scratch[512];
}

bool ArduinoSdmmcBlockDevice::begin(int clock, int command, int data0)
{
    if (ready_) return true;
    if (clock < 0 || command < 0 || data0 < 0) return false;
    end();
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.flags = SDMMC_HOST_FLAG_1BIT;
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 1;
    slot.clk = static_cast<gpio_num_t>(clock);
    slot.cmd = static_cast<gpio_num_t>(command);
    slot.d0 = static_cast<gpio_num_t>(data0);
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    esp_err_t result = sdmmc_host_init();
    if (result == ESP_OK)
    {
        host_ready_ = true;
        result = sdmmc_host_init_slot(host.slot, &slot);
    }
    if (result == ESP_OK) result = sdmmc_card_init(&host, &card_);
    if (result != ESP_OK)
    {
        ESP_LOGW("sdmmc", "card initialization failed: %s", esp_err_to_name(result));
        end();
        return false;
    }
    ready_ = card_.csd.sector_size == sizeof(s_sector_scratch) && card_.csd.capacity > 0;
    if (!ready_) end();
    return ready_;
}

void ArduinoSdmmcBlockDevice::end()
{
    ready_ = false;
    if (host_ready_)
    {
        sdmmc_host_deinit();
        host_ready_ = false;
    }
}

bool ArduinoSdmmcBlockDevice::readSector(Sector_t sector, uint8_t* destination)
{
    if (!ready_ || !destination || sector >= sectorCount()) return false;
    if (sdmmc_read_sectors(&card_, s_sector_scratch, sector, 1) != ESP_OK) return false;
    std::memcpy(destination, s_sector_scratch, sizeof(s_sector_scratch));
    return true;
}

bool ArduinoSdmmcBlockDevice::readSectors(Sector_t sector, uint8_t* destination, size_t count)
{
    if (!ready_ || (!destination && count) || sector > sectorCount() || count > sectorCount() - sector) return false;
    for (size_t index = 0; index < count; ++index)
    {
        if (!readSector(sector + index, destination + index * sizeof(s_sector_scratch))) return false;
    }
    return true;
}

bool ArduinoSdmmcBlockDevice::writeSector(Sector_t sector, const uint8_t* source)
{
    if (!ready_ || !source || sector >= sectorCount()) return false;
    std::memcpy(s_sector_scratch, source, sizeof(s_sector_scratch));
    return sdmmc_write_sectors(&card_, s_sector_scratch, sector, 1) == ESP_OK;
}

bool ArduinoSdmmcBlockDevice::writeSectors(Sector_t sector, const uint8_t* source, size_t count)
{
    if (!ready_ || (!source && count) || sector > sectorCount() || count > sectorCount() - sector) return false;
    for (size_t index = 0; index < count; ++index)
    {
        if (!writeSector(sector + index, source + index * sizeof(s_sector_scratch))) return false;
    }
    return true;
}
} // namespace platform::esp::arduino_common::storage
#endif
