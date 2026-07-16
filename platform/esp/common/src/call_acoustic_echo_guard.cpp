#include "platform/esp/common/call_acoustic_echo_guard.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace platform::esp::common::reticulum_call_audio
{

CallAcousticEchoGuard::CallAcousticEchoGuard(uint32_t sample_rate_hz,
                                             uint16_t render_active_mean_abs,
                                             uint16_t release_hold_ms)
    : render_active_mean_abs_(render_active_mean_abs)
{
    const uint64_t hold_samples =
        (static_cast<uint64_t>(sample_rate_hz) * release_hold_ms) / 1000U;
    release_hold_samples_ = static_cast<uint32_t>(
        std::min<uint64_t>(hold_samples, std::numeric_limits<uint32_t>::max()));
}

bool CallAcousticEchoGuard::observeRender(const int16_t* pcm,
                                          std::size_t sample_count)
{
    if (!pcm || sample_count == 0)
    {
        return false;
    }

    uint64_t magnitude_sum = 0;
    for (std::size_t index = 0; index < sample_count; ++index)
    {
        const int32_t sample = pcm[index];
        magnitude_sum += static_cast<uint32_t>(sample < 0 ? -sample : sample);
    }
    last_render_mean_abs_ = static_cast<uint16_t>(
        std::min<uint64_t>(magnitude_sum / sample_count,
                           std::numeric_limits<uint16_t>::max()));

    const bool was_suppressed = captureSuppressed();
    if (last_render_mean_abs_ >= render_active_mean_abs_)
    {
        remaining_hold_samples_ = release_hold_samples_;
    }
    else if (remaining_hold_samples_ > sample_count)
    {
        remaining_hold_samples_ -= static_cast<uint32_t>(sample_count);
    }
    else
    {
        remaining_hold_samples_ = 0;
    }
    return was_suppressed != captureSuppressed();
}

bool CallAcousticEchoGuard::suppressCapture(int16_t* pcm,
                                            std::size_t sample_count) const
{
    if (!captureSuppressed() || !pcm || sample_count == 0)
    {
        return false;
    }
    std::memset(pcm, 0, sample_count * sizeof(int16_t));
    return true;
}

bool CallAcousticEchoGuard::captureSuppressed() const
{
    return remaining_hold_samples_ != 0;
}

uint16_t CallAcousticEchoGuard::lastRenderMeanAbs() const
{
    return last_render_mean_abs_;
}

} // namespace platform::esp::common::reticulum_call_audio
