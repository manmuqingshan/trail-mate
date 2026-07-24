#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace boards::tdeck
{

class TDeckAudioRuntime
{
  public:
    bool begin();
    void requestMessageTone(uint8_t volume_percent);
    bool isReady() const;

  private:
    static constexpr std::size_t kPcmSampleCount = 128U * 2U;

    static void taskEntry(void* context);
    void taskLoop();
    void playTone(uint8_t volume_percent);

    TaskHandle_t task_ = nullptr;
    std::atomic<bool> ready_{false};
    std::atomic<bool> faulted_{false};
    std::atomic<uint8_t> volume_percent_{45};
    std::array<int16_t, kPcmSampleCount> pcm_{};
};

} // namespace boards::tdeck
