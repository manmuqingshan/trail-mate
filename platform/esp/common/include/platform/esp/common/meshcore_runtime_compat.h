#pragma once

#include "esp_random.h"
#include "mbedtls/sha256.h"
#include "platform/esp/common/mbedtls_sha256_compat.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#if defined(ARDUINO)
#include <Arduino.h>
#else
#include "esp_mac.h"
#endif

namespace platform::esp::common::meshcore_runtime
{

class Sha256Digest final
{
  public:
    Sha256Digest()
    {
        mbedtls_sha256_init(&context_);
        active_ = (crypto::sha256_starts(&context_, 0) == 0);
    }

    ~Sha256Digest()
    {
        mbedtls_sha256_free(&context_);
    }

    Sha256Digest(const Sha256Digest&) = delete;
    Sha256Digest& operator=(const Sha256Digest&) = delete;

    void update(const uint8_t* data, std::size_t len)
    {
        if (active_ && data && len > 0)
        {
            active_ = (crypto::sha256_update(&context_, data, len) == 0);
        }
    }

    bool finalize(uint8_t* out, std::size_t len)
    {
        if (!active_ || !out || len == 0)
        {
            return false;
        }

        std::array<uint8_t, 32> digest{};
        active_ = false;
        if (crypto::sha256_finish(&context_, digest.data()) != 0)
        {
            return false;
        }
        std::memcpy(out, digest.data(), std::min(len, digest.size()));
        return true;
    }

  private:
    mbedtls_sha256_context context_{};
    bool active_ = false;
};

inline uint32_t device_node_id()
{
    uint8_t mac[6] = {};
#if defined(ARDUINO)
    const uint64_t raw = ESP.getEfuseMac();
    std::memcpy(mac, &raw, sizeof(mac));
#else
    if (esp_efuse_mac_get_default(mac) != ESP_OK)
    {
        return 0;
    }
#endif
    return (static_cast<uint32_t>(mac[2]) << 24) |
           (static_cast<uint32_t>(mac[3]) << 16) |
           (static_cast<uint32_t>(mac[4]) << 8) |
           static_cast<uint32_t>(mac[5]);
}

inline uint32_t random_between(uint32_t min_inclusive, uint32_t max_exclusive)
{
    if (max_exclusive <= min_inclusive)
    {
        return min_inclusive;
    }
    return min_inclusive +
           (static_cast<uint32_t>(esp_random()) % (max_exclusive - min_inclusive));
}

} // namespace platform::esp::common::meshcore_runtime
