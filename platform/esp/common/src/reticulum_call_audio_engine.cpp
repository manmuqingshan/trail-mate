#include "platform/esp/common/reticulum_call_audio_engine.h"

#include "chat/infra/reticulum/audio_call_wire.h"
#include "platform/ui/reticulum_call_runtime.h"

#include <codec2.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace platform::esp::common::reticulum_call_audio
{
namespace
{

using ::chat::reticulum::audio_call::Codec2Mode;
using ::chat::reticulum::audio_call::DecodedPayload;

constexpr uint32_t kSampleRateHz = 8000;
constexpr uint8_t kMaxCodecFramesPerPacket = 13;
constexpr uint8_t kJitterMinFrames = 3;
constexpr uint8_t kJitterMaxFrames = 16;
constexpr float kTxPcmGain = 1.5f;
constexpr float kRxPcmGain = 1.0f;
constexpr uint32_t kTaskStackBytes = 18 * 1024;
constexpr UBaseType_t kTaskPriority = tskIDLE_PRIORITY + 2;
constexpr uint32_t kStopTimeoutMs = 750;

Backend s_backend{};
TaskHandle_t s_task = nullptr;
volatile bool s_stop_requested = false;
bool s_backend_open = false;

struct CodecSelection
{
    int codec2_mode = CODEC2_MODE_3200;
    Codec2Mode wire_mode = Codec2Mode::Mode3200;
    uint32_t target_packet_ms = 200;
};

CodecSelection codec_selection(
    const ::platform::ui::reticulum_call::Snapshot& snapshot)
{
    CodecSelection selection{};
    switch (snapshot.codec2_mode)
    {
    case ::platform::ui::reticulum_call::Codec2Mode::Mode700C:
        selection.codec2_mode = CODEC2_MODE_700C;
        selection.wire_mode = Codec2Mode::Mode700C;
        selection.target_packet_ms = 400;
        break;
    case ::platform::ui::reticulum_call::Codec2Mode::Mode1200:
        selection.codec2_mode = CODEC2_MODE_1200;
        selection.wire_mode = Codec2Mode::Mode1200;
        selection.target_packet_ms = 520;
        break;
    case ::platform::ui::reticulum_call::Codec2Mode::Mode1600:
        selection.codec2_mode = CODEC2_MODE_1600;
        selection.wire_mode = Codec2Mode::Mode1600;
        selection.target_packet_ms = 320;
        break;
    case ::platform::ui::reticulum_call::Codec2Mode::Mode3200:
        break;
    }
    return selection;
}

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

bool backend_complete(const Backend& backend)
{
    return backend.is_supported && backend.open && backend.close &&
           backend.read_mono && backend.write_mono && backend.speaker_volume &&
           backend.set_speaker_volume;
}

bool media_supported()
{
    return backend_complete(s_backend) && s_backend.is_supported();
}

uint8_t speaker_volume()
{
    return backend_complete(s_backend) ? s_backend.speaker_volume() : 0;
}

void set_speaker_volume(uint8_t volume_percent)
{
    if (backend_complete(s_backend))
    {
        s_backend.set_speaker_volume(volume_percent > 100U ? 100U : volume_percent);
    }
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

void close_backend()
{
    if (s_backend_open && s_backend.close)
    {
        s_backend.close();
    }
    s_backend_open = false;
}

void free_buffers(int16_t* pcm_in,
                  int16_t* pcm_out,
                  int16_t* silence,
                  uint8_t* codec_frame,
                  uint8_t* tx_frames,
                  uint8_t* rx_frames,
                  uint8_t* packet_buf)
{
    audio_free(pcm_in);
    audio_free(pcm_out);
    audio_free(silence);
    audio_free(codec_frame);
    audio_free(tx_frames);
    audio_free(rx_frames);
    audio_free(packet_buf);
}

void fail_media_start(CODEC2* codec2_state,
                      int16_t* pcm_in,
                      int16_t* pcm_out,
                      int16_t* silence,
                      uint8_t* codec_frame,
                      uint8_t* tx_frames,
                      uint8_t* rx_frames,
                      uint8_t* packet_buf)
{
    free_buffers(pcm_in,
                 pcm_out,
                 silence,
                 codec_frame,
                 tx_frames,
                 rx_frames,
                 packet_buf);
    if (codec2_state)
    {
        codec2_destroy(codec2_state);
    }
    close_backend();
    s_task = nullptr;
    ::platform::ui::reticulum_call::notify_media_failed();
    vTaskDelete(nullptr);
}

void call_audio_task(void*)
{
    const auto initial_snapshot = ::platform::ui::reticulum_call::snapshot();
    const CodecSelection selection = codec_selection(initial_snapshot);
    CODEC2* codec2_state = codec2_create(selection.codec2_mode);
    if (!codec2_state)
    {
        fail_media_start(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
        return;
    }
    codec2_set_lpc_post_filter(codec2_state, 1, 0, 0.8f, 0.2f);

    const int samples_per_frame = codec2_samples_per_frame(codec2_state);
    const int bytes_per_frame = codec2_bytes_per_frame(codec2_state);
    const uint32_t frame_interval_ms =
        static_cast<uint32_t>((samples_per_frame * 1000U) / kSampleRateHz);
    const uint8_t codec_frames_per_packet = static_cast<uint8_t>(
        std::min<uint32_t>(kMaxCodecFramesPerPacket,
                           std::max<uint32_t>(1,
                                              selection.target_packet_ms /
                                                  frame_interval_ms)));

    auto* pcm_in = static_cast<int16_t*>(audio_alloc(samples_per_frame * sizeof(int16_t)));
    auto* pcm_out = static_cast<int16_t*>(audio_alloc(samples_per_frame * sizeof(int16_t)));
    auto* silence = static_cast<int16_t*>(audio_alloc(samples_per_frame * sizeof(int16_t)));
    auto* codec_frame = static_cast<uint8_t*>(audio_alloc(bytes_per_frame));
    auto* tx_frames =
        static_cast<uint8_t*>(audio_alloc(bytes_per_frame * kMaxCodecFramesPerPacket));
    auto* rx_frames =
        static_cast<uint8_t*>(audio_alloc(bytes_per_frame * kJitterMaxFrames));
    auto* packet_buf =
        static_cast<uint8_t*>(audio_alloc(::platform::ui::reticulum_call::kAudioPacketMaxLen));

    if (!pcm_in || !pcm_out || !silence || !codec_frame || !tx_frames || !rx_frames ||
        !packet_buf)
    {
        fail_media_start(codec2_state,
                         pcm_in,
                         pcm_out,
                         silence,
                         codec_frame,
                         tx_frames,
                         rx_frames,
                         packet_buf);
        return;
    }
    std::memset(silence, 0, samples_per_frame * sizeof(int16_t));

    uint8_t tx_frame_count = 0;
    uint8_t rx_frame_count = 0;
    uint8_t rx_head = 0;
    uint8_t rx_tail = 0;
    uint32_t last_play_ms = now_ms();

    while (!s_stop_requested && ::platform::ui::reticulum_call::media_should_run())
    {
        const auto snapshot = ::platform::ui::reticulum_call::snapshot();

        ::platform::ui::reticulum_call::AudioPacket inbound{};
        uint8_t inbound_drains = 0;
        while (inbound_drains < 2 &&
               ::platform::ui::reticulum_call::dequeue_inbound_audio(&inbound))
        {
            DecodedPayload decoded{};
            if (::chat::reticulum::audio_call::decodePayload(inbound.data,
                                                             inbound.len,
                                                             &decoded) &&
                decoded.mode == selection.wire_mode && decoded.encoded &&
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

        if (s_backend.read_mono(pcm_in, static_cast<std::size_t>(samples_per_frame)))
        {
            for (int index = 0; index < samples_per_frame; ++index)
            {
                pcm_in[index] =
                    clamp_sample(static_cast<int32_t>(pcm_in[index] * kTxPcmGain));
            }
            codec2_encode(codec2_state, codec_frame, pcm_in);
            std::memcpy(tx_frames + (tx_frame_count * bytes_per_frame),
                        codec_frame,
                        bytes_per_frame);
            ++tx_frame_count;

            if (tx_frame_count >= codec_frames_per_packet)
            {
                std::size_t packet_len =
                    ::platform::ui::reticulum_call::kAudioPacketMaxLen;
                if (::chat::reticulum::audio_call::encodePayload(
                        selection.wire_mode,
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
            const int16_t* playback = silence;
            if (rx_frame_count >= kJitterMinFrames)
            {
                const uint8_t* frame = rx_frames + (rx_head * bytes_per_frame);
                codec2_decode(codec2_state, pcm_out, const_cast<uint8_t*>(frame));
                rx_head = static_cast<uint8_t>((rx_head + 1U) % kJitterMaxFrames);
                --rx_frame_count;
                for (int index = 0; index < samples_per_frame; ++index)
                {
                    pcm_out[index] =
                        clamp_sample(static_cast<int32_t>(pcm_out[index] * kRxPcmGain));
                }
                playback = pcm_out;
            }
            (void)s_backend.write_mono(playback,
                                       static_cast<std::size_t>(samples_per_frame));
        }

        vTaskDelay(pdMS_TO_TICKS(2));
    }

    free_buffers(pcm_in,
                 pcm_out,
                 silence,
                 codec_frame,
                 tx_frames,
                 rx_frames,
                 packet_buf);
    codec2_destroy(codec2_state);
    close_backend();
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
    if (!media_supported() || !s_backend.open(kSampleRateHz))
    {
        return false;
    }

    s_backend_open = true;
    s_stop_requested = false;
    const BaseType_t rc = xTaskCreate(call_audio_task,
                                      "rt_call_audio",
                                      kTaskStackBytes,
                                      nullptr,
                                      kTaskPriority,
                                      &s_task);
    if (rc != pdPASS)
    {
        close_backend();
        s_task = nullptr;
        return false;
    }
    const auto snapshot = ::platform::ui::reticulum_call::snapshot();
    const CodecSelection selection = codec_selection(snapshot);
    std::printf("[Reticulum][CallAudio] media started codec2_mode=%d wire=%u\n",
                selection.codec2_mode,
                static_cast<unsigned>(snapshot.wire_profile));
    return true;
}

void media_stop()
{
    s_stop_requested = true;
    const TaskHandle_t task = s_task;
    if (!task || xTaskGetCurrentTaskHandle() == task)
    {
        return;
    }

    const TickType_t started = xTaskGetTickCount();
    while (s_task && pdTICKS_TO_MS(xTaskGetTickCount() - started) < kStopTimeoutMs)
    {
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    if (s_task)
    {
        std::printf("[Reticulum][CallAudio] stop timeout\n");
    }
}

} // namespace

bool install_backend(const Backend& backend)
{
    if (!backend_complete(backend) || s_task || s_backend_open)
    {
        return false;
    }

    s_backend = backend;
    ::platform::ui::reticulum_call::MediaHooks hooks{};
    hooks.is_supported = media_supported;
    hooks.start = media_start;
    hooks.stop = media_stop;
    hooks.speaker_volume = speaker_volume;
    hooks.set_speaker_volume = set_speaker_volume;
    ::platform::ui::reticulum_call::set_media_hooks(hooks);
    return true;
}

} // namespace platform::esp::common::reticulum_call_audio
