#pragma once

#include <Arduino.h>
#include <SPI.h>
#if !defined(ARDUINO_ARCH_ESP32)
#error "sd_utils requires the ESP32 SdFat runtime; Arduino SD fallback is intentionally unsupported."
#endif
#include "platform/esp/arduino_common/storage/sd_card_runtime.h"

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

inline void resetSharedSpiForSd(int sd_cs, const int* extra_cs, size_t extra_cs_count)
{
    releaseSdBusDevices(sd_cs, extra_cs, extra_cs_count);
    pinMode(MISO, INPUT_PULLUP);
    SPI.end();
    delay(2);
    SPI.begin(SCK, MISO, MOSI);
    releaseSdBusDevices(sd_cs, extra_cs, extra_cs_count);
    delay(2);
}

template <typename Lockable>
inline bool installSpiSd(Lockable& bus, int sd_cs, uint32_t spi_hz, const char* mount_point,
                         const int* extra_cs, size_t extra_cs_count,
                         uint8_t* out_card_type = nullptr, uint32_t* out_card_size_mb = nullptr,
                         bool use_lock = true, uint8_t max_files = 8)
{
    if (sd_cs < 0)
    {
        return false;
    }

    resetSharedSpiForSd(sd_cs, extra_cs, extra_cs_count);
    SPIClass& sd_bus = SPI;
    Serial.printf("[SD] SPI pins sck=%d miso=%d mosi=%d cs=%d hz=%lu\n",
                  SCK, MISO, MOSI, sd_cs, (unsigned long)spi_hz);
    for (size_t i = 0; i < extra_cs_count; ++i)
    {
        Serial.printf("[SD] extra CS pin=%d level=%d\n", extra_cs[i], digitalRead(extra_cs[i]));
    }
    Serial.printf("[SD] sd CS pin=%d level=%d\n", sd_cs, digitalRead(sd_cs));

    bool ok = false;
    uint8_t card_type = kCardNone;
    uint32_t card_size_mb = 0;

    bool locked = true;
    if (use_lock)
    {
        locked = bus.lock(portMAX_DELAY);
    }

    if (locked)
    {
        const uint32_t freqs[] = {spi_hz, 4000000U, 1000000U, 400000U};
        uint32_t tried_freqs[sizeof(freqs) / sizeof(freqs[0])] = {};
        size_t tried_count = 0;
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
            resetSharedSpiForSd(sd_cs, extra_cs, extra_cs_count);
            delay(10);
            Serial.printf("[SD] try hz=%lu\n", (unsigned long)hz_try);
            ok = ::platform::esp::arduino_common::storage::mount_sd_card(
                sd_cs, sd_bus, hz_try, mount_point, max_files);
            Serial.printf("[SD] mount -> %d\n", ok ? 1 : 0);
            if (ok)
            {
                break;
            }
            delay(25);
        }
        if (ok)
        {
            const auto info = ::platform::esp::arduino_common::storage::sd_card_info();
            card_type = info.card_type;
            Serial.printf("[SD] cardType=%u backend=%s fs=%s\n",
                          (unsigned)card_type,
                          ::platform::esp::arduino_common::storage::sd_card_backend_name(),
                          ::platform::esp::arduino_common::storage::sd_card_filesystem_name());
            if (card_type != kCardNone)
            {
                card_size_mb = static_cast<uint32_t>(info.card_size_bytes / (1024ULL * 1024ULL));
                Serial.printf("[SD] card=%llu MB total=%llu MB sectors=%lu sector_size=%lu\n",
                              static_cast<unsigned long long>(info.card_size_bytes / (1024ULL * 1024ULL)),
                              static_cast<unsigned long long>(info.total_bytes / (1024ULL * 1024ULL)),
                              static_cast<unsigned long>(info.sector_count),
                              static_cast<unsigned long>(info.sector_size));
            }
            else
            {
                ok = false;
                ::platform::esp::arduino_common::storage::unmount_sd_card();
            }
        }
        if (use_lock)
        {
            bus.unlock();
        }
    }
    else
    {
        Serial.println("[SD] shared SPI lock failed");
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
