#pragma once

#include <cstddef>
#include <cstdint>

namespace platform::esp::common::reticulum_call_audio
{

struct Backend
{
    bool (*is_supported)() = nullptr;
    bool (*open)(uint32_t sample_rate_hz) = nullptr;
    void (*close)() = nullptr;
    bool (*read_mono)(int16_t* pcm, std::size_t sample_count) = nullptr;
    bool (*write_mono)(const int16_t* pcm, std::size_t sample_count) = nullptr;
    uint8_t (*speaker_volume)() = nullptr;
    void (*set_speaker_volume)(uint8_t volume_percent) = nullptr;
};

bool install_backend(const Backend& backend);

} // namespace platform::esp::common::reticulum_call_audio
