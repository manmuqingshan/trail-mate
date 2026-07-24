#if defined(ARDUINO_T_DECK)

#include "boards/tdeck/tdeck_audio_runtime.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>

#include "pins_arduino.h"
#include "platform/esp/common/memory_budget.h"
#include "platform/ui/audio/pager_notification_tone.h"

#include <driver/i2s.h>
#include <esp_err.h>

namespace boards::tdeck
{
namespace
{

constexpr i2s_port_t kSpeakerI2sPort = static_cast<i2s_port_t>(DAC_I2S_PORT);
constexpr size_t kFramesPerChunk = 128;
constexpr size_t kDmaBufferCount = 2;
constexpr size_t kDmaBufferLength = 128;
constexpr size_t kAudioInternalReservation = 4096;
constexpr size_t kAudioDmaReservation = 8 * 1024;
constexpr size_t kAudioInternalFloor = 48 * 1024;
constexpr size_t kAudioDmaFloor = 16 * 1024;
constexpr uint32_t kAudioTaskStackBytes = 2048;
constexpr UBaseType_t kAudioTaskPriority = 2;

} // namespace

bool TDeckAudioRuntime::begin()
{
    if (ready_.load(std::memory_order_acquire))
    {
        return true;
    }
    if (faulted_.load(std::memory_order_acquire))
    {
        return false;
    }

    ::platform::esp::common::memory::logSnapshot("audio", "before_i2s");
    if (!::platform::esp::common::memory::admit("audio",
                                                kAudioInternalReservation,
                                                kAudioDmaReservation,
                                                0,
                                                kAudioInternalFloor,
                                                kAudioDmaFloor))
    {
        std::printf("[TDeck][Audio] unavailable before I2S install\n");
        faulted_.store(true, std::memory_order_release);
        return false;
    }

    const i2s_config_t config = {
        .mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = ::platform::ui::audio::pager_notification::kPlaybackSampleRateHz,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = kDmaBufferCount,
        .dma_buf_len = kDmaBufferLength,
        .use_apll = false,
        .tx_desc_auto_clear = true,
        .fixed_mclk = 0,
#if defined(I2S_MCLK_MULTIPLE_256)
        .mclk_multiple = I2S_MCLK_MULTIPLE_256,
        .bits_per_chan = I2S_BITS_PER_CHAN_DEFAULT,
#endif
    };

    esp_err_t err = i2s_driver_install(kSpeakerI2sPort, &config, 0, nullptr);
    if (err != ESP_OK)
    {
        std::printf("[TDeck][Audio] unavailable I2S install err=%s\n",
                    esp_err_to_name(err));
        faulted_.store(true, std::memory_order_release);
        return false;
    }

    const i2s_pin_config_t pins = {
        .mck_io_num = I2S_PIN_NO_CHANGE,
        .bck_io_num = DAC_I2S_BCK,
        .ws_io_num = DAC_I2S_WS,
        .data_out_num = DAC_I2S_DOUT,
        .data_in_num = I2S_PIN_NO_CHANGE,
    };
    err = i2s_set_pin(kSpeakerI2sPort, &pins);
    if (err != ESP_OK)
    {
        std::printf("[TDeck][Audio] unavailable I2S pins err=%s\n", esp_err_to_name(err));
        i2s_driver_uninstall(kSpeakerI2sPort);
        faulted_.store(true, std::memory_order_release);
        return false;
    }

    err = i2s_start(kSpeakerI2sPort);
    if (err != ESP_OK)
    {
        std::printf("[TDeck][Audio] unavailable I2S start err=%s\n", esp_err_to_name(err));
        i2s_driver_uninstall(kSpeakerI2sPort);
        faulted_.store(true, std::memory_order_release);
        return false;
    }
    i2s_zero_dma_buffer(kSpeakerI2sPort);

    const BaseType_t task_result =
        xTaskCreatePinnedToCore(&TDeckAudioRuntime::taskEntry,
                                "tdeck_audio",
                                kAudioTaskStackBytes,
                                this,
                                kAudioTaskPriority,
                                &task_,
                                1);
    if (task_result != pdPASS)
    {
        std::printf("[TDeck][Audio] unavailable audio task create failed\n");
        i2s_stop(kSpeakerI2sPort);
        i2s_driver_uninstall(kSpeakerI2sPort);
        task_ = nullptr;
        faulted_.store(true, std::memory_order_release);
        return false;
    }

    ready_.store(true, std::memory_order_release);
    std::printf("[TDeck][Audio] runtime ready port=%u dma=%u/%u task_stack=%u\n",
                static_cast<unsigned>(kSpeakerI2sPort),
                static_cast<unsigned>(kDmaBufferCount),
                static_cast<unsigned>(kDmaBufferLength),
                static_cast<unsigned>(kAudioTaskStackBytes));
    ::platform::esp::common::memory::logSnapshot("audio", "after_i2s");
    return true;
}

void TDeckAudioRuntime::requestMessageTone(uint8_t volume_percent)
{
    if (volume_percent == 0U)
    {
        return;
    }
    volume_percent = std::min<uint8_t>(volume_percent, 100U);
    volume_percent_.store(volume_percent, std::memory_order_release);

    if (!ready_.load(std::memory_order_acquire) || task_ == nullptr)
    {
        std::printf("[TDeck][Audio] tone dropped runtime_unavailable volume=%u\n",
                    static_cast<unsigned>(volume_percent));
        return;
    }

    xTaskNotifyGive(task_);
}

bool TDeckAudioRuntime::isReady() const
{
    return ready_.load(std::memory_order_acquire);
}

void TDeckAudioRuntime::taskEntry(void* context)
{
    auto* runtime = static_cast<TDeckAudioRuntime*>(context);
    if (runtime != nullptr)
    {
        runtime->taskLoop();
    }
    vTaskDelete(nullptr);
}

void TDeckAudioRuntime::taskLoop()
{
    for (;;)
    {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (!ready_.load(std::memory_order_acquire))
        {
            continue;
        }
        playTone(volume_percent_.load(std::memory_order_acquire));
        const unsigned long stack_free =
            static_cast<unsigned long>(uxTaskGetStackHighWaterMark(nullptr)) *
            sizeof(StackType_t);
        std::printf("[TDeck][Audio][Mem] task_stack_free=%lu\n", stack_free);
    }
}

void TDeckAudioRuntime::playTone(uint8_t volume_percent)
{
    namespace pager_tone = ::platform::ui::audio::pager_notification;
    const float gain = static_cast<float>(volume_percent) / 100.0f;
    pager_tone::AdpcmPlaybackState tone_state{};
    size_t total_frames = 0;
    size_t total_bytes = 0;

    while (pager_tone::hasMore(tone_state))
    {
        const uint16_t frames = pager_tone::fillStereoInterleaved(
            tone_state,
            pcm_.data(),
            static_cast<uint16_t>(kFramesPerChunk));
        if (frames == 0U)
        {
            break;
        }

        if (gain < 0.999f)
        {
            for (size_t index = 0;
                 index < static_cast<size_t>(frames) * pager_tone::kChannels;
                 ++index)
            {
                pcm_[index] =
                    static_cast<int16_t>(static_cast<float>(pcm_[index]) * gain);
            }
        }

        const size_t bytes =
            static_cast<size_t>(frames) * pager_tone::kChannels * sizeof(int16_t);
        size_t bytes_written = 0;
        const esp_err_t err = i2s_write(kSpeakerI2sPort,
                                        pcm_.data(),
                                        bytes,
                                        &bytes_written,
                                        pdMS_TO_TICKS(100));
        total_frames += bytes_written / (pager_tone::kChannels * sizeof(int16_t));
        total_bytes += bytes_written;
        if (err != ESP_OK || bytes_written != bytes)
        {
            std::printf("[TDeck][Audio] tone write failed err=%s requested=%u written=%u\n",
                        esp_err_to_name(err),
                        static_cast<unsigned>(bytes),
                        static_cast<unsigned>(bytes_written));
            return;
        }
    }

    std::printf("[TDeck][Audio] tone complete frames=%u bytes=%u\n",
                static_cast<unsigned>(total_frames),
                static_cast<unsigned>(total_bytes));
}

} // namespace boards::tdeck

#endif // defined(ARDUINO_T_DECK)
