#include "platform/esp/common/call_acoustic_echo_guard.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>

using platform::esp::common::reticulum_call_audio::CallAcousticEchoGuard;

int main()
{
    constexpr std::size_t kFrameSamples = 160;
    std::array<int16_t, kFrameSamples> quiet{};
    std::array<int16_t, kFrameSamples> remote_voice{};
    std::array<int16_t, kFrameSamples> capture{};
    remote_voice.fill(800);
    capture.fill(1200);

    CallAcousticEchoGuard guard(8000, 400, 60);
    assert(!guard.observeRender(quiet.data(), quiet.size()));
    assert(!guard.captureSuppressed());

    assert(guard.observeRender(remote_voice.data(), remote_voice.size()));
    assert(guard.captureSuppressed());
    assert(guard.lastRenderMeanAbs() == 800);
    assert(guard.suppressCapture(capture.data(), capture.size()));
    assert(std::all_of(capture.begin(), capture.end(), [](int16_t sample)
                       { return sample == 0; }));

    assert(!guard.observeRender(quiet.data(), quiet.size()));
    assert(!guard.observeRender(quiet.data(), quiet.size()));
    assert(guard.captureSuppressed());
    assert(guard.observeRender(quiet.data(), quiet.size()));
    assert(!guard.captureSuppressed());

    capture.fill(1200);
    assert(!guard.suppressCapture(capture.data(), capture.size()));
    assert(std::all_of(capture.begin(), capture.end(), [](int16_t sample)
                       { return sample == 1200; }));

    return 0;
}
