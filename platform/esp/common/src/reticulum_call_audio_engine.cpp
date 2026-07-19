#include "platform/esp/common/reticulum_call_audio_engine.h"

#include "chat/infra/reticulum/audio_call_wire.h"
#include "platform/esp/common/call_acoustic_echo_guard.h"
#include "platform/ui/reticulum_call_runtime.h"

#include <codec2.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <new>

namespace platform::esp::common::reticulum_call_audio
{
namespace
{

using ::chat::reticulum::audio_call::Codec2Mode;
using ::chat::reticulum::audio_call::DecodedPayload;

constexpr uint32_t kSampleRateHz = 8000;
constexpr uint8_t kMaxCodecFramesPerPacket = 13;
constexpr uint8_t kJitterStartFrames = 3;
constexpr uint8_t kJitterMaxFrames = 32;
constexpr uint8_t kInboundDrainLimit = 4;
constexpr uint8_t kIoFailureLimit = 25;
constexpr uint8_t kCallSpeakerVolumePercent = 100;
constexpr float kTxPcmGain = 1.0f;
constexpr float kRxPcmGain = 1.0f;
constexpr uint32_t kCaptureTaskStackBytes = 20 * 1024;
constexpr uint32_t kPlaybackTaskStackBytes = 20 * 1024;
constexpr UBaseType_t kTaskPriority = tskIDLE_PRIORITY + 2;
constexpr uint32_t kStopTimeoutMs = 750;

struct CodecSelection
{
    int codec2_mode = CODEC2_MODE_3200;
    Codec2Mode wire_mode = Codec2Mode::Mode3200;
    uint32_t target_packet_ms = 200;
};

struct MediaSession
{
    explicit MediaSession(const CodecSelection& selected)
        : selection(selected), echo_guard(kSampleRateHz)
    {
    }

    CodecSelection selection{};
    CallAcousticEchoGuard echo_guard;
    SemaphoreHandle_t echo_mutex = nullptr;
    TaskHandle_t capture_task = nullptr;
    TaskHandle_t playback_task = nullptr;
    volatile bool ready = false;
    volatile bool stop_requested = false;
    bool failure_notified = false;
    uint8_t workers_remaining = 0;
};

Backend s_backend{};
bool s_backend_open = false;
MediaSession* s_session = nullptr;
portMUX_TYPE s_session_lock = portMUX_INITIALIZER_UNLOCKED;

CodecSelection codec_selection(
    const ::platform::ui::reticulum_call::Snapshot& snapshot)
{
    CodecSelection selection{};
    switch (snapshot.codec2_mode)
    {
    case ::platform::ui::reticulum_call::Codec2Mode::Mode700C:
        // The embedded Sideband profile is fixed to Codec2 3200. Keeping the
        // fallback here prevents the recursive 700C decoder from consuming an
        // unbounded ESP task stack if a stale session requests that mode.
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

MediaSession* active_session()
{
    portENTER_CRITICAL(&s_session_lock);
    MediaSession* session = s_session;
    portEXIT_CRITICAL(&s_session_lock);
    return session;
}

void close_backend()
{
    if (s_backend_open && s_backend.close)
    {
        s_backend.close();
    }
    s_backend_open = false;
}

bool session_should_run(const MediaSession* session)
{
    return session && !session->stop_requested &&
           ::platform::ui::reticulum_call::media_should_run();
}

bool wait_for_session_start(MediaSession* session)
{
    while (session && !session->ready && !session->stop_requested)
    {
        vTaskDelay(1);
    }
    return session_should_run(session);
}

void request_session_failure(MediaSession* session)
{
    bool notify = false;
    portENTER_CRITICAL(&s_session_lock);
    if (session && s_session == session && !session->failure_notified)
    {
        session->failure_notified = true;
        session->stop_requested = true;
        notify = true;
    }
    portEXIT_CRITICAL(&s_session_lock);

    if (notify)
    {
        ::platform::ui::reticulum_call::notify_media_failed();
    }
}

void worker_finished(MediaSession* session, bool capture_worker)
{
    bool last_worker = false;
    const TaskHandle_t current = xTaskGetCurrentTaskHandle();
    portENTER_CRITICAL(&s_session_lock);
    if (session)
    {
        if (capture_worker && session->capture_task == current)
        {
            session->capture_task = nullptr;
        }
        else if (!capture_worker && session->playback_task == current)
        {
            session->playback_task = nullptr;
        }
        if (session->workers_remaining > 0)
        {
            --session->workers_remaining;
        }
        last_worker = session->workers_remaining == 0;
        if (last_worker && s_session == session)
        {
            s_session = nullptr;
        }
    }
    portEXIT_CRITICAL(&s_session_lock);

    if (last_worker)
    {
        close_backend();
        if (session->echo_mutex)
        {
            vSemaphoreDelete(session->echo_mutex);
        }
        session->~MediaSession();
        audio_free(session);
    }
    vTaskDelete(nullptr);
}

bool suppress_capture(MediaSession* session,
                      int16_t* pcm,
                      std::size_t sample_count)
{
    if (!session || !session->echo_mutex ||
        xSemaphoreTake(session->echo_mutex, portMAX_DELAY) != pdTRUE)
    {
        return false;
    }
    const bool suppressed = session->echo_guard.suppressCapture(pcm, sample_count);
    xSemaphoreGive(session->echo_mutex);
    return suppressed;
}

void observe_render(MediaSession* session,
                    const int16_t* pcm,
                    std::size_t sample_count)
{
    if (!session || !session->echo_mutex ||
        xSemaphoreTake(session->echo_mutex, portMAX_DELAY) != pdTRUE)
    {
        return;
    }
    const bool changed = session->echo_guard.observeRender(pcm, sample_count);
    const bool suppressed = session->echo_guard.captureSuppressed();
    const uint16_t render_mean = session->echo_guard.lastRenderMeanAbs();
    xSemaphoreGive(session->echo_mutex);

    if (changed)
    {
        std::printf("[Reticulum][CallAudio] echo_guard suppressed=%u render_mean=%u\n",
                    suppressed ? 1U : 0U,
                    static_cast<unsigned>(render_mean));
    }
}

void capture_task(void* context)
{
    auto* session = static_cast<MediaSession*>(context);
    if (!wait_for_session_start(session))
    {
        worker_finished(session, true);
        return;
    }

    CODEC2* encoder = codec2_create(session->selection.codec2_mode);
    const int samples_per_frame = encoder ? codec2_samples_per_frame(encoder) : 0;
    const int bytes_per_frame = encoder ? codec2_bytes_per_frame(encoder) : 0;
    const uint32_t frame_interval_ms = samples_per_frame > 0
                                           ? static_cast<uint32_t>(
                                                 (samples_per_frame * 1000U) /
                                                 kSampleRateHz)
                                           : 0;
    const uint8_t frames_per_packet = frame_interval_ms > 0
                                          ? static_cast<uint8_t>(std::min<uint32_t>(
                                                kMaxCodecFramesPerPacket,
                                                std::max<uint32_t>(
                                                    1,
                                                    session->selection.target_packet_ms /
                                                        frame_interval_ms)))
                                          : 0;

    auto* pcm = samples_per_frame > 0
                    ? static_cast<int16_t*>(
                          audio_alloc(samples_per_frame * sizeof(int16_t)))
                    : nullptr;
    auto* codec_frame = bytes_per_frame > 0
                            ? static_cast<uint8_t*>(audio_alloc(bytes_per_frame))
                            : nullptr;
    auto* tx_frames = bytes_per_frame > 0
                          ? static_cast<uint8_t*>(audio_alloc(
                                bytes_per_frame * kMaxCodecFramesPerPacket))
                          : nullptr;
    auto* packet = static_cast<uint8_t*>(audio_alloc(
        ::platform::ui::reticulum_call::kAudioPacketMaxLen));

    if (!encoder || !pcm || !codec_frame || !tx_frames || !packet ||
        frames_per_packet == 0)
    {
        audio_free(pcm);
        audio_free(codec_frame);
        audio_free(tx_frames);
        audio_free(packet);
        if (encoder)
        {
            codec2_destroy(encoder);
        }
        request_session_failure(session);
        worker_finished(session, true);
        return;
    }

    uint8_t tx_frame_count = 0;
    uint8_t consecutive_read_failures = 0;
    bool stack_reported = false;
    while (session_should_run(session))
    {
        if (!s_backend.read_mono(pcm,
                                 static_cast<std::size_t>(samples_per_frame)))
        {
            if (++consecutive_read_failures >= kIoFailureLimit)
            {
                std::printf("[Reticulum][CallAudio] capture failed repeatedly\n");
                request_session_failure(session);
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(2));
            continue;
        }
        consecutive_read_failures = 0;

        if (!suppress_capture(session,
                              pcm,
                              static_cast<std::size_t>(samples_per_frame)))
        {
            for (int index = 0; index < samples_per_frame; ++index)
            {
                pcm[index] = clamp_sample(
                    static_cast<int32_t>(pcm[index] * kTxPcmGain));
            }
        }
        codec2_encode(encoder, codec_frame, pcm);
        if (!stack_reported)
        {
            stack_reported = true;
            std::printf("[Reticulum][CallAudio] stack_hwm phase=encode value=%u\n",
                        static_cast<unsigned>(
                            uxTaskGetStackHighWaterMark(nullptr)));
        }
        std::memcpy(tx_frames + (tx_frame_count * bytes_per_frame),
                    codec_frame,
                    bytes_per_frame);
        ++tx_frame_count;

        if (tx_frame_count >= frames_per_packet)
        {
            std::size_t packet_len =
                ::platform::ui::reticulum_call::kAudioPacketMaxLen;
            const auto snapshot = ::platform::ui::reticulum_call::snapshot();
            if (::chat::reticulum::audio_call::encodePayload(
                    session->selection.wire_mode,
                    tx_frames,
                    bytes_per_frame * tx_frame_count,
                    packet,
                    &packet_len))
            {
                (void)::platform::ui::reticulum_call::enqueue_outbound_audio(
                    snapshot.link_id,
                    packet,
                    packet_len);
            }
            tx_frame_count = 0;
        }
    }

    audio_free(pcm);
    audio_free(codec_frame);
    audio_free(tx_frames);
    audio_free(packet);
    codec2_destroy(encoder);
    worker_finished(session, true);
}

void playback_task(void* context)
{
    auto* session = static_cast<MediaSession*>(context);
    if (!wait_for_session_start(session))
    {
        worker_finished(session, false);
        return;
    }

    CODEC2* decoder = codec2_create(session->selection.codec2_mode);
    if (decoder)
    {
        codec2_set_lpc_post_filter(decoder, 1, 0, 0.8f, 0.2f);
    }
    const int samples_per_frame = decoder ? codec2_samples_per_frame(decoder) : 0;
    const int bytes_per_frame = decoder ? codec2_bytes_per_frame(decoder) : 0;
    const uint32_t frame_interval_ms = samples_per_frame > 0
                                           ? static_cast<uint32_t>(
                                                 (samples_per_frame * 1000U) /
                                                 kSampleRateHz)
                                           : 0;

    auto* pcm = samples_per_frame > 0
                    ? static_cast<int16_t*>(
                          audio_alloc(samples_per_frame * sizeof(int16_t)))
                    : nullptr;
    auto* silence = samples_per_frame > 0
                        ? static_cast<int16_t*>(
                              audio_alloc(samples_per_frame * sizeof(int16_t)))
                        : nullptr;
    auto* rx_frames = bytes_per_frame > 0
                          ? static_cast<uint8_t*>(audio_alloc(
                                bytes_per_frame * kJitterMaxFrames))
                          : nullptr;
    auto* inbound = static_cast<::platform::ui::reticulum_call::AudioPacket*>(
        audio_alloc(sizeof(::platform::ui::reticulum_call::AudioPacket)));

    if (!decoder || !pcm || !silence || !rx_frames || !inbound ||
        frame_interval_ms == 0)
    {
        audio_free(pcm);
        audio_free(silence);
        audio_free(rx_frames);
        audio_free(inbound);
        if (decoder)
        {
            codec2_destroy(decoder);
        }
        request_session_failure(session);
        worker_finished(session, false);
        return;
    }
    std::memset(silence, 0, samples_per_frame * sizeof(int16_t));

    uint8_t rx_frame_count = 0;
    uint8_t rx_head = 0;
    uint8_t rx_tail = 0;
    uint8_t consecutive_write_failures = 0;
    bool playback_primed = false;
    bool stack_reported = false;
    TickType_t last_wake = xTaskGetTickCount();
    const TickType_t frame_ticks =
        std::max<TickType_t>(1, pdMS_TO_TICKS(frame_interval_ms));

    while (session_should_run(session))
    {
        uint8_t drains = 0;
        while (drains < kInboundDrainLimit &&
               ::platform::ui::reticulum_call::dequeue_inbound_audio(inbound))
        {
            DecodedPayload decoded{};
            if (::chat::reticulum::audio_call::decodePayload(inbound->data,
                                                             inbound->len,
                                                             &decoded) &&
                decoded.mode == session->selection.wire_mode && decoded.encoded &&
                decoded.encoded_len >= static_cast<std::size_t>(bytes_per_frame) &&
                (decoded.encoded_len % static_cast<std::size_t>(bytes_per_frame)) ==
                    0)
            {
                const std::size_t frame_count =
                    decoded.encoded_len / static_cast<std::size_t>(bytes_per_frame);
                for (std::size_t index = 0; index < frame_count; ++index)
                {
                    if (rx_frame_count == kJitterMaxFrames)
                    {
                        rx_head = static_cast<uint8_t>(
                            (rx_head + 1U) % kJitterMaxFrames);
                        --rx_frame_count;
                    }
                    std::memcpy(rx_frames + (rx_tail * bytes_per_frame),
                                decoded.encoded + (index * bytes_per_frame),
                                bytes_per_frame);
                    rx_tail = static_cast<uint8_t>(
                        (rx_tail + 1U) % kJitterMaxFrames);
                    ++rx_frame_count;
                }
            }
            ++drains;
        }

        vTaskDelayUntil(&last_wake, frame_ticks);

        const int16_t* playback = silence;
        if (!playback_primed && rx_frame_count >= kJitterStartFrames)
        {
            playback_primed = true;
        }
        if (playback_primed && rx_frame_count > 0)
        {
            const uint8_t* frame = rx_frames + (rx_head * bytes_per_frame);
            codec2_decode(decoder, pcm, const_cast<uint8_t*>(frame));
            if (!stack_reported)
            {
                stack_reported = true;
                std::printf("[Reticulum][CallAudio] stack_hwm phase=decode value=%u\n",
                            static_cast<unsigned>(
                                uxTaskGetStackHighWaterMark(nullptr)));
            }
            rx_head = static_cast<uint8_t>((rx_head + 1U) % kJitterMaxFrames);
            --rx_frame_count;
            for (int index = 0; index < samples_per_frame; ++index)
            {
                pcm[index] = clamp_sample(
                    static_cast<int32_t>(pcm[index] * kRxPcmGain));
            }
            playback = pcm;
        }
        else if (playback_primed)
        {
            playback_primed = false;
        }

        observe_render(session,
                       playback,
                       static_cast<std::size_t>(samples_per_frame));
        if (!s_backend.write_mono(playback,
                                  static_cast<std::size_t>(samples_per_frame)))
        {
            if (++consecutive_write_failures >= kIoFailureLimit)
            {
                std::printf("[Reticulum][CallAudio] playback failed repeatedly\n");
                request_session_failure(session);
                break;
            }
        }
        else
        {
            consecutive_write_failures = 0;
        }
    }

    audio_free(pcm);
    audio_free(silence);
    audio_free(rx_frames);
    audio_free(inbound);
    codec2_destroy(decoder);
    worker_finished(session, false);
}

void destroy_unstarted_session(MediaSession* session)
{
    portENTER_CRITICAL(&s_session_lock);
    if (s_session == session)
    {
        s_session = nullptr;
    }
    portEXIT_CRITICAL(&s_session_lock);
    if (session)
    {
        if (session->echo_mutex)
        {
            vSemaphoreDelete(session->echo_mutex);
        }
        session->~MediaSession();
        audio_free(session);
    }
    close_backend();
}

bool media_start()
{
    if (MediaSession* existing = active_session())
    {
        return !existing->stop_requested;
    }
    if (!media_supported() || !s_backend.open(kSampleRateHz))
    {
        return false;
    }
    s_backend_open = true;
    set_speaker_volume(kCallSpeakerVolumePercent);

    const auto snapshot = ::platform::ui::reticulum_call::snapshot();
    const CodecSelection selection = codec_selection(snapshot);
    void* storage = heap_caps_malloc(sizeof(MediaSession),
                                     MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    auto* session = storage ? new (storage) MediaSession(selection) : nullptr;
    if (!session)
    {
        close_backend();
        return false;
    }
    session->echo_mutex = xSemaphoreCreateMutex();
    if (!session->echo_mutex)
    {
        destroy_unstarted_session(session);
        return false;
    }

    portENTER_CRITICAL(&s_session_lock);
    s_session = session;
    portEXIT_CRITICAL(&s_session_lock);

    if (xTaskCreate(capture_task,
                    "rt_call_capture",
                    kCaptureTaskStackBytes,
                    session,
                    kTaskPriority,
                    &session->capture_task) != pdPASS)
    {
        destroy_unstarted_session(session);
        return false;
    }

    if (xTaskCreate(playback_task,
                    "rt_call_play",
                    kPlaybackTaskStackBytes,
                    session,
                    kTaskPriority,
                    &session->playback_task) != pdPASS)
    {
        portENTER_CRITICAL(&s_session_lock);
        session->workers_remaining = 1;
        session->stop_requested = true;
        session->ready = true;
        portEXIT_CRITICAL(&s_session_lock);
        return false;
    }

    portENTER_CRITICAL(&s_session_lock);
    session->workers_remaining = 2;
    session->ready = true;
    portEXIT_CRITICAL(&s_session_lock);
    std::printf(
        "[Reticulum][CallAudio] media started codec2_mode=%d capture_stack=%u playback_stack=%u\n",
        selection.codec2_mode,
        static_cast<unsigned>(kCaptureTaskStackBytes),
        static_cast<unsigned>(kPlaybackTaskStackBytes));
    return true;
}

void media_stop()
{
    MediaSession* session = nullptr;
    TaskHandle_t capture = nullptr;
    TaskHandle_t playback = nullptr;
    portENTER_CRITICAL(&s_session_lock);
    session = s_session;
    if (session)
    {
        session->stop_requested = true;
        capture = session->capture_task;
        playback = session->playback_task;
    }
    portEXIT_CRITICAL(&s_session_lock);

    if (!session)
    {
        return;
    }
    const TaskHandle_t current = xTaskGetCurrentTaskHandle();
    if (current == capture || current == playback)
    {
        return;
    }

    const TickType_t started = xTaskGetTickCount();
    while (active_session() == session &&
           pdTICKS_TO_MS(xTaskGetTickCount() - started) < kStopTimeoutMs)
    {
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    if (active_session() == session)
    {
        std::printf("[Reticulum][CallAudio] stop timeout\n");
    }
}

} // namespace

bool install_backend(const Backend& backend)
{
    if (!backend_complete(backend) || active_session() || s_backend_open)
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
