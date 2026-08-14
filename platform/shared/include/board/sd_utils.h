#pragma once

#include <Arduino.h>
#include <SPI.h>
#if !defined(ARDUINO_ARCH_ESP32)
#error "sd_utils requires the ESP32 SdFat runtime; Arduino SD fallback is intentionally unsupported."
#endif
#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
#include "platform/esp/common/shared_spi_coordinator.h"
#include "sys/clock.h"

namespace sdutil
{

constexpr uint8_t kCardNone = 0;

inline void setCsHigh(int pin)
{
    if (pin < 0)
    {
        return;
    }
    pinMode(pin, OUTPUT);
    digitalWrite(pin, HIGH);
}

inline void releaseSdBusDevices(int sd_cs, const int* extra_cs, size_t extra_cs_count)
{
    for (size_t i = 0; i < extra_cs_count; ++i)
    {
        setCsHigh(extra_cs[i]);
    }
    setCsHigh(sd_cs);
}

inline void resetSharedSpiForSd(
    const ::platform::esp::arduino_common::storage::SdSpiBusConfig& spi_bus,
    int sd_cs,
    const int* extra_cs,
    size_t extra_cs_count)
{
    releaseSdBusDevices(sd_cs, extra_cs, extra_cs_count);
    if (spi_bus.miso >= 0)
    {
        pinMode(spi_bus.miso, INPUT_PULLUP);
    }
    // SPIClass::begin() is idempotent while the shared controller is active.
    // Do not call SPI.end() here: the display already owns the controller
    // configuration, and tearing it down between SD retries can invalidate
    // the next display transaction.
    spi_bus.spi.begin(spi_bus.sck, spi_bus.miso, spi_bus.mosi);
    releaseSdBusDevices(sd_cs, extra_cs, extra_cs_count);
    delay(2);
}

inline bool installSpiSd(
    int sd_cs,
    uint32_t spi_hz,
    const char* mount_point,
    const int* extra_cs,
    size_t extra_cs_count,
    uint8_t* out_card_type = nullptr,
    uint32_t* out_card_size_mb = nullptr,
    uint8_t max_files = 8,
    const ::platform::esp::arduino_common::storage::SdSpiBusConfig& spi_bus =
        {SPI, SCK, MISO, MOSI})
{
    if (sd_cs < 0 || spi_bus.sck < 0 || spi_bus.miso < 0 || spi_bus.mosi < 0)
    {
        Serial.println("[SD] shared SPI mount rejected: incomplete pin mapping");
        return false;
    }

    bool ok = false;
    uint8_t card_type = kCardNone;
    uint32_t card_size_mb = 0;
    size_t tried_count = 0;

    Serial.printf("[SD] SPI pins sck=%d miso=%d mosi=%d cs=%d hz=%lu\n",
                  spi_bus.sck, spi_bus.miso, spi_bus.mosi, sd_cs, (unsigned long)spi_hz);
    for (size_t i = 0; i < extra_cs_count; ++i)
    {
        Serial.printf("[SD] extra CS pin=%d level=%d\n", extra_cs[i], digitalRead(extra_cs[i]));
    }
    Serial.printf("[SD] sd CS pin=%d level=%d\n", sd_cs, digitalRead(sd_cs));

    sys::runtime::BusAcquireRequest request{};
    request.resource =
        ::platform::esp::common::SharedSpiCoordinator::kSharedBusResource;
    request.policy = sys::runtime::BusAccessPolicy::RecoveryExclusive;
    request.command_id = 0x53444D54U; // "SDMT"
    request.origin = request.command_id;
    request.deadline_ms = sys::millis_now() + 1000U;
    request.owner_label = "sd_mount";
    const auto result =
        ::platform::esp::common::shared_spi_coordinator().acquire(request);
    const sys::runtime::BusAccessToken bus_token = result.token;
    const bool locked = result.status == sys::runtime::BusAcquireStatus::Acquired &&
                        bus_token.valid;
    bool invalid_mounted_state = false;

    if (!locked)
    {
        Serial.println("[SD] shared SPI recovery token unavailable");
    }
    else
    {
        const uint32_t freqs[] = {spi_hz, 4000000U, 1000000U, 400000U};
        uint32_t tried_freqs[sizeof(freqs) / sizeof(freqs[0])] = {};
        for (size_t i = 0; i < (sizeof(freqs) / sizeof(freqs[0])); ++i)
        {
            const uint32_t hz_try = freqs[i];
            if (hz_try == 0)
            {
                continue;
            }
            bool already_tried = false;
            for (size_t j = 0; j < tried_count; ++j)
            {
                if (tried_freqs[j] == hz_try)
                {
                    already_tried = true;
                    break;
                }
            }
            if (already_tried)
            {
                continue;
            }
            tried_freqs[tried_count++] = hz_try;
            resetSharedSpiForSd(spi_bus, sd_cs, extra_cs, extra_cs_count);
            delay(10);
            Serial.printf("[SD] try hz=%lu\n", (unsigned long)hz_try);
            ok = ::platform::esp::arduino_common::storage::mount_sd_card(
                sd_cs, spi_bus, hz_try, mount_point, max_files);
            Serial.printf("[SD] mount -> %d\n", ok ? 1 : 0);
            if (ok)
            {
                break;
            }
            delay(25);
        }
    }

    if (bus_token.valid)
    {
        ::platform::esp::common::shared_spi_coordinator().release(bus_token);
    }

    // The recovery transaction ends with its final physical candidate. Read
    // the already-captured scalar mount snapshot only after releasing it; an
    // invalid result is cleaned up by a new, explicitly bounded recovery
    // transaction below.
    if (ok)
    {
        const auto info = ::platform::esp::arduino_common::storage::sd_card_info();
        card_type = info.card_type;
        if (card_type == kCardNone)
        {
            ok = false;
            invalid_mounted_state = true;
        }
    }

    if (invalid_mounted_state)
    {
        // Cleanup acquires its own short recovery token. It must happen after
        // the candidate fence is released so result publication never extends
        // the mount transaction's physical ownership interval.
        ::platform::esp::arduino_common::storage::unmount_sd_card();
    }

    if (ok)
    {
        ::platform::esp::arduino_common::storage::record_sd_card_mount_success(
            spi_hz, static_cast<uint8_t>(tried_count));
        const auto info = ::platform::esp::arduino_common::storage::sd_card_info();
        card_type = info.card_type;
        card_size_mb = static_cast<uint32_t>(info.card_size_bytes / (1024ULL * 1024ULL));
        Serial.printf("[SD] cardType=%u backend=%s fs=%s\n",
                      static_cast<unsigned>(card_type),
                      ::platform::esp::arduino_common::storage::sd_card_backend_name(),
                      ::platform::esp::arduino_common::storage::sd_card_filesystem_name());
        Serial.printf("[SD] card=%llu MB total=%llu MB sectors=%lu sector_size=%lu\n",
                      static_cast<unsigned long long>(info.card_size_bytes / (1024ULL * 1024ULL)),
                      static_cast<unsigned long long>(info.total_bytes / (1024ULL * 1024ULL)),
                      static_cast<unsigned long>(info.sector_count),
                      static_cast<unsigned long>(info.sector_size));
    }

    if (out_card_type)
    {
        *out_card_type = card_type;
    }
    if (out_card_size_mb)
    {
        *out_card_size_mb = card_size_mb;
    }
    return ok;
}

} // namespace sdutil
