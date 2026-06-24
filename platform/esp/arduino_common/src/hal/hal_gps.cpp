#include "hal/hal_gps.h"

#include "board/GpsBoard.h"
#include "board/TLoRaPagerTypes.h"
#include "platform/esp/arduino_common/gps/GPS.h"

namespace hal
{

namespace
{
constexpr uint32_t kFreshFixMaxAgeMs = 15000;
}

void HalGps::begin(GpsBoard& board)
{
    board_ = &board;
}

bool HalGps::isReady() const
{
    return board_ != nullptr && board_->isGPSReady();
}

bool HalGps::init()
{
    if (board_ == nullptr)
    {
        return false;
    }
    return board_->initGPS();
}

void HalGps::powerOn()
{
    if (board_ == nullptr)
    {
        return;
    }
    board_->powerControl(POWER_GPS, true);
    delay(10);
}

void HalGps::powerOff()
{
    if (board_ == nullptr)
    {
        return;
    }
    board_->deinitGPS();
    board_->powerControl(POWER_GPS, false);
}

uint32_t HalGps::loop()
{
    if (board_ == nullptr)
    {
        return 0;
    }
    return board_->getGPS().loop();
}

uint32_t HalGps::lastLoopReadBytes() const
{
    if (board_ == nullptr)
    {
        return 0;
    }
    return board_->getGPS().lastLoopReadBytes();
}

bool HalGps::hasFix() const
{
    if (board_ == nullptr)
    {
        return false;
    }
    const auto& location = board_->getGPS().location;
    return location.isValid() && location.age() <= kFreshFixMaxAgeMs;
}

double HalGps::latitude() const
{
    return board_ != nullptr ? board_->getGPS().location.lat() : 0.0;
}

double HalGps::longitude() const
{
    return board_ != nullptr ? board_->getGPS().location.lng() : 0.0;
}

bool HalGps::hasAltitude() const
{
    return board_ != nullptr && board_->getGPS().altitude.isValid();
}

double HalGps::altitude() const
{
    return board_ != nullptr ? board_->getGPS().altitude.meters() : 0.0;
}

bool HalGps::hasSpeed() const
{
    return board_ != nullptr && board_->getGPS().speed.isValid();
}

double HalGps::speed() const
{
    return board_ != nullptr ? board_->getGPS().speed.mps() : 0.0;
}

bool HalGps::hasCourse() const
{
    return board_ != nullptr && board_->getGPS().course.isValid();
}

double HalGps::course() const
{
    return board_ != nullptr ? board_->getGPS().course.deg() : 0.0;
}

uint8_t HalGps::satellites() const
{
    return board_ != nullptr ? board_->getGPS().satellites.value() : 0;
}

size_t HalGps::getSatellites(gps::GnssSatInfo* out, size_t max) const
{
    return board_ != nullptr ? board_->getGPS().getSatellites(out, max) : 0;
}

gps::GnssStatus HalGps::getGnssStatus() const
{
    return board_ != nullptr ? board_->getGPS().getGnssStatus() : gps::GnssStatus{};
}

bool HalGps::syncTime(uint32_t gps_task_interval_ms)
{
    if (board_ == nullptr)
    {
        return false;
    }
    return board_->syncTimeFromGPS(gps_task_interval_ms);
}

bool HalGps::applyGnssConfig(uint8_t mode, uint8_t sat_mask, bool send_rxm, bool send_gnss)
{
    if (board_ == nullptr)
    {
        return false;
    }

    GPS& gps = board_->getGPS();
    bool ok_mode = !send_rxm || gps.setReceiverMode(mode, sat_mask);

#if defined(ARDUINO_T_DECK) || defined(ARDUINO_T_DECK_PRO)
    bool ok_gnss = !send_gnss || gps.configureGnss(sat_mask);
    Serial.printf("[GPS] gnss config tdeck mode=%u sat_mask=0x%02X rxm_send=%d rxm=%d cfg_gnss_send=%d cfg_gnss=%d\n",
                  static_cast<unsigned>(mode),
                  static_cast<unsigned>(sat_mask),
                  send_rxm ? 1 : 0,
                  ok_mode ? 1 : 0,
                  send_gnss ? 1 : 0,
                  ok_gnss ? 1 : 0);
    return ok_mode && ok_gnss;
#else
    bool ok_gnss = !send_gnss || gps.configureGnss(sat_mask);
    return ok_mode && ok_gnss;
#endif
}

bool HalGps::applyNmeaConfig(uint8_t output_hz, uint8_t sentence_mask)
{
    if (board_ == nullptr)
    {
        return false;
    }

    return board_->getGPS().configureNmeaOutput(output_hz, sentence_mask);
}

} // namespace hal
