/**
 * @file reticulum_call_audio_runtime.cpp
 * @brief Pager ES8311/Codec2 media hooks for MeshChat-compatible Reticulum calls.
 */

#include "platform/esp/arduino_common/reticulum_call_audio_runtime.h"

#include "platform/ui/reticulum_call_runtime.h"

#if defined(ARDUINO_T_LORA_PAGER) && \
    (defined(ARDUINO_LILYGO_LORA_SX1262) || defined(ARDUINO_LILYGO_LORA_LR1121))

#include "boards/tlora_pager/tlora_pager_board.h"
#include "chat/infra/reticulum/audio_call_wire.h"
#include "platform/esp/boards/board_runtime.h"

#include <codec2.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace platform::esp::arduino_common
{
namespace
{

using ::chat::reticulum::audio_call::Codec2Mode;
using ::chat::reticulum::audio_call::DecodedPayload;

constexpr uint32_t kSampleRate = 8000;
constexpr uint8_t kBitsPerSample = 16;
constexpr uint8_t kI2sChannels = 2;
constexpr uint8_t kCodecFramesPerPacket = 2;
constexpr uint8_t kJitterMinFrames = 3;
constexpr uint8_t kJitterMaxFrames = 16;
constexpr uint8_t kDefaultVolume = 78;
constexpr float kDefaultGainDb = 36.0f;
constexpr float kTxPcmGain = 1.5f;
constexpr float kRxPcmGain = 2.0f;
constexpr uint32_t kTaskStackBytes = 18 * 1024;
constexpr UBaseType_t kTaskPriority = tskIDLE_PRIORITY + 2;

TaskHandle_t s_task = nullptr;
volatile bool s_stop_requested = false;
bool s_registered = false;
::boards::tlora_pager::TLoRaPagerBoard* s_board = nullptr;

uint32_t now_ms()
{
    return static_cast<uint32_t>(pdTICKS_TO_MS(xTaskGetTickCount()));
}

void* audio_alloc(std::size_t bytes)
{
    return heap_caps_malloc_prefer(bytes,
                                   2,
                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                   MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

void audio_free(void* ptr)
{
    if (ptr)
    {
        heap_caps_free(ptr);
    }
}

::boards::tlora_pager::TLoRaPagerBoard* resolve_board()
{
    ::platform::esp::boards::AppContextInitHandles handles;
    if (!::platform::esp::boards::tryResolveAppContextInitHandles(&handles) ||
        !handles.board)
    {
        return nullptr;
    }
    return static_cast<::boards::tlora_pager::TLoRaPagerBoard*>(handles.board);
}

bool media_supported()
{
    auto* board = resolve_board();
    return board && ((board->getDevicesProbe() & HW_CODEC_ONLINE) != 0);
}

int16_t clamp_sample(int32_t sample)
{
    if (sample > 32767)
    {
        return 32767;
    }
    if (sample < -32768)
    {
        return -32768;
    }
    return static_cast<int16_t>(sample);
}

void free_buffers(int16_t* pcm_in_i2s,
                  int16_t* pcm_out_i2s,
                  int16_t* pcm_in,
                  int16_t* pcm_out,
                  int16_t* silence_i2s,
                  uint8_t* codec_frame,
                  uint8_t* tx_frames,
                  uint8_t* rx_frames,
                  uint8_t* packet_buf)
{
    audio_free(pcm_in_i2s);
    audio_free(pcm_out_i2s);
    audio_free(pcm_in);
    audio_free(pcm_out);
    audio_free(silence_i2s);
    audio_free(codec_frame);
    audio_free(tx_frames);
    audio_free(rx_frames);
    audio_free(packet_buf);
}

void close_codec()
{
    if (s_board)
    {
        s_board->codec.setMute(true);
        s_board->codec.close();
    }
    s_board = nullptr;
}

void call_audio_task(void*)
{
    CODEC2* codec2_state = codec2_create(CODEC2_MODE_1200);
    if (!codec2_state)
    {
        close_codec();
        s_task = nullptr;
        vTaskDelete(nullptr);
        return;
    }
    codec2_set_lpc_post_filter(codec2_state, 1, 0, 0.8f, 0.2f);

    const int samples_per_frame = codec2_samples_per_frame(codec2_state);
    const int bytes_per_frame = codec2_bytes_per_frame(codec2_state);
    const int i2s_samples_per_frame = samples_per_frame * kI2sChannels;
    const uint32_t frame_interval_ms =
        static_cast<uint32_t>((samples_per_frame * 1000U) / kSampleRate);

    auto* pcm_in_i2s =
        static_cast<int16_t*>(audio_alloc(i2s_samples_per_frame * sizeof(int16_t)));
    auto* pcm_out_i2s =
        static_cast<int16_t*>(audio_alloc(i2s_samples_per_frame * sizeof(int16_t)));
    auto* pcm_in = static_cast<int16_t*>(audio_alloc(samples_per_frame * sizeof(int16_t)));
    auto* pcm_out = static_cast<int16_t*>(audio_alloc(samples_per_frame * sizeof(int16_t)));
    auto* silence_i2s =
        static_cast<int16_t*>(audio_alloc(i2s_samples_per_frame * sizeof(int16_t)));
    auto* codec_frame = static_cast<uint8_t*>(audio_alloc(bytes_per_frame));
    auto* tx_frames =
        static_cast<uint8_t*>(audio_alloc(bytes_per_frame * kCodecFramesPerPacket));
    auto* rx_frames =
        static_cast<uint8_t*>(audio_alloc(bytes_per_frame * kJitterMaxFrames));
    auto* packet_buf =
        static_cast<uint8_t*>(audio_alloc(::platform::ui::reticulum_call::kAudioPacketMaxLen));

    if (!pcm_in_i2s || !pcm_out_i2s || !pcm_in || !pcm_out || !silence_i2s ||
        !codec_frame || !tx_frames || !rx_frames || !packet_buf)
    {
        free_buffers(pcm_in_i2s,
                     pcm_out_i2s,
                     pcm_in,
                     pcm_out,
                     silence_i2s,
                     codec_frame,
                     tx_frames,
                     rx_frames,
                     packet_buf);
        codec2_destroy(codec2_state);
        close_codec();
        s_task = nullptr;
        vTaskDelete(nullptr);
        return;
    }
    std::memset(silence_i2s, 0, i2s_samples_per_frame * sizeof(int16_t));

    uint8_t tx_frame_count = 0;
    uint8_t rx_frame_count = 0;
    uint8_t rx_head = 0;
    uint8_t rx_tail = 0;
    uint32_t last_play_ms = now_ms();

    while (!s_stop_requested &&
           ::platform::ui::reticulum_call::media_should_run())
    {
        const auto snapshot = ::platform::ui::reticulum_call::snapshot();

        ::platform::ui::reticulum_call::AudioPacket inbound{};
        uint8_t inbound_drains = 0;
        while (inbound_drains < 2 &&
               ::platform::ui::reticulum_call::dequeue_inbound_audio(&inbound))
        {
            DecodedPayload decoded{};
            if (::chat::reticulum::audio_call::decodePayload(
                    inbound.data,
                    inbound.len,
                    &decoded) &&
                decoded.mode == Codec2Mode::Mode1200 &&
                decoded.encoded &&
                decoded.encoded_len >= static_cast<std::size_t>(bytes_per_frame))
            {
                const std::size_t frames =
                    decoded.encoded_len / static_cast<std::size_t>(bytes_per_frame);
                for (std::size_t index = 0; index < frames; ++index)
                {
                    if (rx_frame_count >= kJitterMaxFrames)
                    {
                        break;
                    }
                    uint8_t* dst = rx_frames + (rx_tail * bytes_per_frame);
                    std::memcpy(dst,
                                decoded.encoded + (index * bytes_per_frame),
                                bytes_per_frame);
                    rx_tail = static_cast<uint8_t>((rx_tail + 1U) % kJitterMaxFrames);
                    ++rx_frame_count;
                }
            }
            ++inbound_drains;
        }

        if (s_board &&
            s_board->codec.read(reinterpret_cast<uint8_t*>(pcm_in_i2s),
                                i2s_samples_per_frame * sizeof(int16_t)) == 0)
        {
            for (int index = 0; index < samples_per_frame; ++index)
            {
                const int32_t left = pcm_in_i2s[index * 2];
                const int32_t right = pcm_in_i2s[(index * 2) + 1];
                const int32_t mixed = static_cast<int32_t>(((left + right) / 2) * kTxPcmGain);
                pcm_in[index] = clamp_sample(mixed);
            }

            codec2_encode(codec2_state, codec_frame, pcm_in);
            std::memcpy(tx_frames + (tx_frame_count * bytes_per_frame),
                        codec_frame,
                        bytes_per_frame);
            ++tx_frame_count;

            if (tx_frame_count >= kCodecFramesPerPacket)
            {
                std::size_t packet_len =
                    ::platform::ui::reticulum_call::kAudioPacketMaxLen;
                if (::chat::reticulum::audio_call::encodePayload(
                        Codec2Mode::Mode1200,
                        tx_frames,
                        bytes_per_frame * tx_frame_count,
                        packet_buf,
                        &packet_len))
                {
                    (void)::platform::ui::reticulum_call::enqueue_outbound_audio(
                        snapshot.link_id,
                        packet_buf,
                        packet_len);
                }
                tx_frame_count = 0;
            }
        }

        const uint32_t current_ms = now_ms();
        if (current_ms - last_play_ms >= frame_interval_ms)
        {
            last_play_ms = current_ms;
            if (rx_frame_count >= kJitterMinFrames)
            {
                const uint8_t* frame = rx_frames + (rx_head * bytes_per_frame);
                codec2_decode(codec2_state, pcm_out, const_cast<uint8_t*>(frame));
                rx_head = static_cast<uint8_t>((rx_head + 1U) % kJitterMaxFrames);
                --rx_frame_count;

                for (int index = 0; index < samples_per_frame; ++index)
                {
                    const int32_t sample =
                        static_cast<int32_t>(pcm_out[index] * kRxPcmGain);
                    const int16_t out = clamp_sample(sample);
                    pcm_out_i2s[index * 2] = out;
                    pcm_out_i2s[(index * 2) + 1] = out;
                }
                if (s_board)
                {
                    (void)s_board->codec.write(reinterpret_cast<uint8_t*>(pcm_out_i2s),
                                               i2s_samples_per_frame * sizeof(int16_t));
                }
            }
            else if (s_board)
            {
                (void)s_board->codec.write(reinterpret_cast<uint8_t*>(silence_i2s),
                                           i2s_samples_per_frame * sizeof(int16_t));
            }
        }

        vTaskDelay(pdMS_TO_TICKS(2));
    }

    free_buffers(pcm_in_i2s,
                 pcm_out_i2s,
                 pcm_in,
                 pcm_out,
                 silence_i2s,
                 codec_frame,
                 tx_frames,
                 rx_frames,
                 packet_buf);
    codec2_destroy(codec2_state);
    close_codec();
    s_stop_requested = false;
    s_task = nullptr;
    vTaskDelete(nullptr);
}

bool media_start()
{
    if (s_task)
    {
        return !s_stop_requested;
    }
    if (!media_supported())
    {
        return false;
    }

    s_board = resolve_board();
    if (!s_board)
    {
        return false;
    }
    if (s_board->codec.open(kBitsPerSample, kI2sChannels, kSampleRate) != 0)
    {
        s_board = nullptr;
        return false;
    }
    s_board->codec.setVolume(kDefaultVolume);
    s_board->codec.setGain(kDefaultGainDb);
    s_board->codec.setMute(false);

    s_stop_requested = false;
    const BaseType_t rc = xTaskCreate(call_audio_task,
                                      "rt_call_audio",
                                      kTaskStackBytes,
                                      nullptr,
                                      kTaskPriority,
                                      &s_task);
    if (rc != pdPASS)
    {
        close_codec();
        s_task = nullptr;
        return false;
    }
    std::printf("[Reticulum][CallAudio] media started codec2=1200\n");
    return true;
}

void media_stop()
{
    s_stop_requested = true;
}

} // namespace

void ensureReticulumCallAudioRuntimeRegistered()
{
    if (s_registered)
    {
        return;
    }
    ::platform::ui::reticulum_call::MediaHooks hooks{};
    hooks.is_supported = media_supported;
    hooks.start = media_start;
    hooks.stop = media_stop;
    ::platform::ui::reticulum_call::set_media_hooks(hooks);
    s_registered = true;
}

} // namespace platform::esp::arduino_common

#else

namespace platform::esp::arduino_common
{
namespace
{
bool s_registered = false;

bool media_supported()
{
    return false;
}

bool media_start()
{
    return false;
}

void media_stop()
{
}

} // namespace

void ensureReticulumCallAudioRuntimeRegistered()
{
    if (s_registered)
    {
        return;
    }
    ::platform::ui::reticulum_call::MediaHooks hooks{};
    hooks.is_supported = media_supported;
    hooks.start = media_start;
    hooks.stop = media_stop;
    ::platform::ui::reticulum_call::set_media_hooks(hooks);
    s_registered = true;
}

} // namespace platform::esp::arduino_common

#endif
