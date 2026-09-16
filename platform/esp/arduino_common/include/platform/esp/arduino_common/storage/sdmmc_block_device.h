#pragma once

#if defined(TRAIL_MATE_SDFAT_SDMMC)
#include <common/FsBlockDeviceInterface.h>
#include <sdmmc_cmd.h>

namespace platform::esp::arduino_common::storage
{
// The enclosing SD runtime mutex owns all calls, including scratch-buffer use.
class ArduinoSdmmcBlockDevice final : public FsBlockDeviceInterface
{
  public:
    bool begin(int clock, int command, int data0);
    void end() override;
    bool isBusy() override { return false; }
    bool readSector(Sector_t sector, uint8_t* destination) override;
    bool readSectors(Sector_t sector, uint8_t* destination, size_t count) override;
    bool writeSector(Sector_t sector, const uint8_t* source) override;
    bool writeSectors(Sector_t sector, const uint8_t* source, size_t count) override;
    Sector_t sectorCount() override { return ready_ ? card_.csd.capacity : 0; }
    bool syncDevice() override { return ready_; }
    bool highCapacity() const { return (card_.ocr & (1UL << 30)) != 0; }

  private:
    sdmmc_card_t card_{};
    bool host_ready_ = false;
    bool ready_ = false;
};
} // namespace platform::esp::arduino_common::storage
#endif
