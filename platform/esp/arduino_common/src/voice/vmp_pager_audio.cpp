/**
 * @file vmp_pager_audio.cpp
 * @brief Bounded Codec2 recording/playback adapter for Pager VMP.
 */

#include "platform/esp/arduino_common/voice/vmp_pager_audio.h"

#include <cmath>
#include <cstring>
#include <new>

#if defined(ARDUINO_T_LORA_PAGER)

#include <Arduino.h>
#include <codec2.h>
#include <esp_heap_caps.h>

#include "boards/tlora_pager/tlora_pager_board.h"
#include "platform/esp/boards/board_runtime.h"

namespace platform::esp::arduino_common::voice::vmp_audio
{
namespace
{

using ::boards::tlora_pager::PagerAudioOwner;
using ::boards::tlora_pager::TLoRaPagerBoard;

constexpr PagerAudioOwner kOwner = PagerAudioOwner::VoiceMessage;
constexpr float kCaptureGainDb = 24.0F;
constexpr std::size_t kAudioLevelLogIntervalFrames = 25U;

TLoRaPagerBoard* pagerBoard()
{
    ::platform::esp::boards::AppContextInitHandles handles;
    if (!::platform::esp::boards::tryResolveAppContextInitHandles(&handles) ||
        !handles.board)
    {
        return nullptr;
    }
    return static_cast<TLoRaPagerBoard*>(handles.board);
}

void secureClear(uint8_t* data, std::size_t size)
{
    volatile uint8_t* cursor = data;
    while (cursor && size-- != 0U)
    {
        *cursor++ = 0U;
    }
}

int16_t clampToInt16(int32_t value)
{
    if (value > 32767)
    {
        return 32767;
    }
    if (value < -32768)
    {
        return -32768;
    }
    return static_cast<int16_t>(value);
}

bool beginCaptureAudio(TLoRaPagerBoard* board)
{
    if (!board ||
        board->openAudioSession(kOwner,
                                kBitsPerSample,
                                kHardwareChannels,
                                kSampleRateHz,
                                false) != 0)
    {
        return false;
    }
    if (!board->audioSetGain(kOwner, kCaptureGainDb) ||
        !board->audioSetMute(kOwner, false))
    {
        board->closeAudioSession(kOwner);
        return false;
    }
    return true;
}

bool beginPlaybackAudio(TLoRaPagerBoard* board)
{
    if (!board ||
        board->openAudioSession(kOwner,
                                kBitsPerSample,
                                kHardwareChannels,
                                kSampleRateHz,
                                true) != 0)
    {
        return false;
    }
    if (!board->audioSetOutMute(kOwner, false))
    {
        board->closeAudioSession(kOwner);
        return false;
    }
    return true;
}

struct PcmLevel
{
    uint16_t peak = 0U;
    uint16_t rms = 0U;
};

struct CapturePcmLevel
{
    PcmLevel left{};
    PcmLevel right{};
    PcmLevel mono{};
};

PcmLevel measurePcmLevel(const int16_t* samples,
                         std::size_t sample_count,
                         std::size_t stride = 1U,
                         std::size_t offset = 0U)
{
    uint32_t peak = 0U;
    uint64_t sum_of_squares = 0U;
    for (std::size_t index = 0U; index < sample_count; ++index)
    {
        const int32_t sample = samples[index * stride + offset];
        const uint32_t magnitude = sample < 0 ? static_cast<uint32_t>(-sample)
                                              : static_cast<uint32_t>(sample);
        if (magnitude > peak)
        {
            peak = magnitude;
        }
        sum_of_squares += static_cast<uint64_t>(sample * sample);
    }
    const float mean_square =
        static_cast<float>(sum_of_squares) / static_cast<float>(sample_count);
    return {static_cast<uint16_t>(peak),
            static_cast<uint16_t>(sqrtf(mean_square) + 0.5F)};
}

CapturePcmLevel measureCapturePcmLevel(const int16_t* stereo, const int16_t* mono)
{
    return {measurePcmLevel(stereo, kCodec2SamplesPerFrame, kHardwareChannels, 0U),
            measurePcmLevel(stereo, kCodec2SamplesPerFrame, kHardwareChannels, 1U),
            measurePcmLevel(mono, kCodec2SamplesPerFrame)};
}

bool shouldLogPcmLevel(std::size_t frame_index, std::size_t frame_count)
{
    const std::size_t completed_frames = frame_index + 1U;
    return frame_index == 0U ||
           (completed_frames % kAudioLevelLogIntervalFrames) == 0U ||
           completed_frames == frame_count;
}

void logCapturePcmLevel(std::size_t frame_index, const CapturePcmLevel& level)
{
    Serial.printf("[VMP][AUDIO] level capture frame=%u left_peak=%u left_rms=%u "
                  "right_peak=%u right_rms=%u mono_peak=%u mono_rms=%u\n",
                  static_cast<unsigned>(frame_index + 1U),
                  static_cast<unsigned>(level.left.peak),
                  static_cast<unsigned>(level.left.rms),
                  static_cast<unsigned>(level.right.peak),
                  static_cast<unsigned>(level.right.rms),
                  static_cast<unsigned>(level.mono.peak),
                  static_cast<unsigned>(level.mono.rms));
}

void logPlaybackPcmLevel(std::size_t frame_index,
                         const PcmLevel& level,
                         uint8_t output_volume)
{
    Serial.printf("[VMP][AUDIO] level playback frame=%u mono_peak=%u mono_rms=%u "
                  "output_volume=%u\n",
                  static_cast<unsigned>(frame_index + 1U),
                  static_cast<unsigned>(level.peak),
                  static_cast<unsigned>(level.rms),
                  static_cast<unsigned>(output_volume));
}

} // namespace

struct PagerCodec2Audio::FrameScratch
{
    int16_t stereo[kCodec2SamplesPerFrame * kHardwareChannels] = {};
    int16_t mono[kCodec2SamplesPerFrame] = {};
};

bool PagerCodec2Audio::isSupported() const
{
    return pagerBoard() != nullptr;
}

bool PagerCodec2Audio::canCapture() const
{
    return pagerBoard() != nullptr;
}

CaptureResult PagerCodec2Audio::capture(const volatile bool* stop_requested)
{
    clearEncodedMedia();
    const uint32_t started_ms = millis();
    Serial.printf("[VMP][AUDIO] capture begin max_ms=5000 stop_hook=%u\n",
                  stop_requested ? 1U : 0U);
    TLoRaPagerBoard* const board = pagerBoard();
    if (!board || !acquireFrameScratch())
    {
        Serial.printf("[VMP][AUDIO] capture rejected reason=unsupported\n");
        return CaptureResult::Unsupported;
    }
    if (!beginCaptureAudio(board))
    {
        releaseFrameScratch();
        Serial.printf("[VMP][AUDIO] capture rejected reason=audio_busy\n");
        return CaptureResult::AudioBusy;
    }

    CODEC2* const encoder = codec2_create(CODEC2_MODE_1300);
    const int sample_count = encoder ? codec2_samples_per_frame(encoder) : 0;
    const int byte_count = encoder ? codec2_bytes_per_frame(encoder) : 0;
    if (!encoder || sample_count != static_cast<int>(kCodec2SamplesPerFrame) ||
        byte_count != static_cast<int>(kCodec2BytesPerFrame))
    {
        if (encoder)
        {
            codec2_destroy(encoder);
        }
        board->closeAudioSession(kOwner);
        releaseFrameScratch();
        Serial.printf("[VMP][AUDIO] capture rejected reason=codec_failure\n");
        return CaptureResult::CodecFailure;
    }

    CaptureResult result = CaptureResult::Complete;
    CapturePcmLevel last_capture_level{};
    bool has_capture_level = false;
    bool stopped_by_release = false;
    for (std::size_t frame = 0; frame < kCodec2FramesPerMessage; ++frame)
    {
        if (stop_requested && *stop_requested)
        {
            stopped_by_release = true;
            result = encoded_media_size_ == 0U ? CaptureResult::Cancelled
                                               : CaptureResult::Complete;
            Serial.printf("[VMP][AUDIO] capture stop_requested frames=%u bytes=%u\n",
                          static_cast<unsigned>(frame),
                          static_cast<unsigned>(encoded_media_size_));
            break;
        }
        if (!readCaptureFrame())
        {
            result = CaptureResult::AudioFailure;
            break;
        }
        mixCaptureToMono();
        last_capture_level = measureCapturePcmLevel(frame_scratch_->stereo,
                                                    frame_scratch_->mono);
        has_capture_level = true;
        if (shouldLogPcmLevel(frame, kCodec2FramesPerMessage))
        {
            logCapturePcmLevel(frame, last_capture_level);
        }
        codec2_encode(encoder,
                      encoded_media_ + encoded_media_size_,
                      frame_scratch_->mono);
        encoded_media_size_ += kCodec2BytesPerFrame;
        if ((frame + 1U) % 25U == 0U)
        {
            Serial.printf("[VMP][AUDIO] capture progress elapsed_ms=%lu frames=%u bytes=%u\n",
                          static_cast<unsigned long>(millis() - started_ms),
                          static_cast<unsigned>(frame + 1U),
                          static_cast<unsigned>(encoded_media_size_));
        }
    }

    codec2_destroy(encoder);
    board->closeAudioSession(kOwner);
    const std::size_t captured_frame_count = encoded_media_size_ / kCodec2BytesPerFrame;
    if (has_capture_level && captured_frame_count % kAudioLevelLogIntervalFrames != 0U)
    {
        logCapturePcmLevel(captured_frame_count - 1U, last_capture_level);
    }
    releaseFrameScratch();
    const std::size_t encoded_size = encoded_media_size_;
    if (result != CaptureResult::Complete)
    {
        clearEncodedMedia();
    }
    Serial.printf("[VMP][AUDIO] capture end result=%u reason=%s elapsed_ms=%lu frames=%u bytes=%u\n",
                  static_cast<unsigned>(result),
                  stopped_by_release ? "release" : "limit_or_error",
                  static_cast<unsigned long>(millis() - started_ms),
                  static_cast<unsigned>(encoded_size / kCodec2BytesPerFrame),
                  static_cast<unsigned>(encoded_size));
    return result;
}

const uint8_t* PagerCodec2Audio::encodedMedia() const
{
    return encoded_media_size_ != 0U ? encoded_media_ : nullptr;
}

std::size_t PagerCodec2Audio::encodedMediaSize() const
{
    return encoded_media_size_;
}

bool PagerCodec2Audio::hasEncodedMedia() const
{
    return encoded_media_size_ != 0U;
}

void PagerCodec2Audio::clearEncodedMedia()
{
    secureClear(encoded_media_, sizeof(encoded_media_));
    encoded_media_size_ = 0U;
}

PlaybackResult PagerCodec2Audio::play(const uint8_t* encoded_media,
                                      std::size_t encoded_media_len,
                                      chat::voice::vmp::Codec codec,
                                      uint8_t volume_percent)
{
    if (!encoded_media || encoded_media_len == 0U ||
        encoded_media_len > kMaximumEncodedBytes ||
        encoded_media_len % kCodec2BytesPerFrame != 0U ||
        codec != chat::voice::vmp::Codec::Codec2_1300)
    {
        return PlaybackResult::InvalidMedia;
    }

    TLoRaPagerBoard* const board = pagerBoard();
    if (!board || !acquireFrameScratch())
    {
        return PlaybackResult::Unsupported;
    }
    if (!beginPlaybackAudio(board))
    {
        releaseFrameScratch();
        return PlaybackResult::AudioBusy;
    }
    const uint8_t output_volume = volume_percent > 100U ? 100U : volume_percent;
    (void)board->audioSetVolume(kOwner, output_volume);

    CODEC2* const decoder = codec2_create(CODEC2_MODE_1300);
    const int sample_count = decoder ? codec2_samples_per_frame(decoder) : 0;
    const int byte_count = decoder ? codec2_bytes_per_frame(decoder) : 0;
    if (!decoder || sample_count != static_cast<int>(kCodec2SamplesPerFrame) ||
        byte_count != static_cast<int>(kCodec2BytesPerFrame))
    {
        if (decoder)
        {
            codec2_destroy(decoder);
        }
        board->closeAudioSession(kOwner);
        releaseFrameScratch();
        return PlaybackResult::CodecFailure;
    }
    codec2_set_lpc_post_filter(decoder, 1, 0, 0.8F, 0.2F);

    PlaybackResult result = PlaybackResult::Complete;
    const std::size_t frame_count = encoded_media_len / kCodec2BytesPerFrame;
    for (std::size_t frame_index = 0U, offset = 0U; offset < encoded_media_len;
         ++frame_index, offset += kCodec2BytesPerFrame)
    {
        codec2_decode(decoder,
                      frame_scratch_->mono,
                      const_cast<uint8_t*>(encoded_media + offset));
        const PcmLevel level =
            measurePcmLevel(frame_scratch_->mono, kCodec2SamplesPerFrame);
        if (shouldLogPcmLevel(frame_index, frame_count))
        {
            logPlaybackPcmLevel(frame_index, level, output_volume);
        }
        duplicatePlaybackToStereo();
        if (!writePlaybackFrame())
        {
            result = PlaybackResult::AudioFailure;
            break;
        }
    }

    codec2_destroy(decoder);
    board->closeAudioSession(kOwner);
    releaseFrameScratch();
    return result;
}

bool PagerCodec2Audio::acquireFrameScratch()
{
    if (frame_scratch_)
    {
        return true;
    }
    void* const storage = heap_caps_malloc_prefer(
        sizeof(FrameScratch),
        2,
        MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT,
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!storage)
    {
        return false;
    }
    frame_scratch_ = new (storage) FrameScratch{};
    return true;
}

void PagerCodec2Audio::releaseFrameScratch()
{
    if (!frame_scratch_)
    {
        return;
    }
    secureClear(reinterpret_cast<uint8_t*>(frame_scratch_), sizeof(FrameScratch));
    heap_caps_free(frame_scratch_);
    frame_scratch_ = nullptr;
}

bool PagerCodec2Audio::readCaptureFrame()
{
    TLoRaPagerBoard* const board = pagerBoard();
    return board && frame_scratch_ &&
           board->audioRead(kOwner,
                            reinterpret_cast<uint8_t*>(frame_scratch_->stereo),
                            sizeof(frame_scratch_->stereo)) == 0;
}

bool PagerCodec2Audio::writePlaybackFrame()
{
    TLoRaPagerBoard* const board = pagerBoard();
    return board && frame_scratch_ &&
           board->audioWrite(kOwner,
                             reinterpret_cast<const uint8_t*>(frame_scratch_->stereo),
                             sizeof(frame_scratch_->stereo)) == 0;
}

void PagerCodec2Audio::mixCaptureToMono()
{
    for (std::size_t index = 0U; index < kCodec2SamplesPerFrame; ++index)
    {
        const int32_t left = frame_scratch_->stereo[index * kHardwareChannels];
        const int32_t right = frame_scratch_->stereo[index * kHardwareChannels + 1U];
        frame_scratch_->mono[index] = clampToInt16((left + right) / 2);
    }
}

void PagerCodec2Audio::duplicatePlaybackToStereo()
{
    for (std::size_t index = 0U; index < kCodec2SamplesPerFrame; ++index)
    {
        frame_scratch_->stereo[index * kHardwareChannels] = frame_scratch_->mono[index];
        frame_scratch_->stereo[index * kHardwareChannels + 1U] = frame_scratch_->mono[index];
    }
}

} // namespace platform::esp::arduino_common::voice::vmp_audio

#elif defined(ARDUINO_T_DECK)

#include <Arduino.h>

#include "boards/tdeck/tdeck_board.h"
#include "platform/esp/boards/board_runtime.h"

namespace platform::esp::arduino_common::voice::vmp_audio
{
namespace
{

using ::boards::tdeck::TDeckBoard;

TDeckBoard* tdeckBoard()
{
    ::platform::esp::boards::AppContextInitHandles handles;
    if (!::platform::esp::boards::tryResolveAppContextInitHandles(&handles) ||
        !handles.board)
    {
        return nullptr;
    }
    return static_cast<TDeckBoard*>(handles.board);
}

} // namespace

bool PagerCodec2Audio::isSupported() const
{
    TDeckBoard* const board = tdeckBoard();
    return board && board->isVoicePlaybackReady();
}

bool PagerCodec2Audio::canCapture() const
{
    // T-Deck v1 has a bounded speaker path here; recording and outbound VMP
    // remain unavailable until a separate microphone path is implemented.
    return false;
}

CaptureResult PagerCodec2Audio::capture(const volatile bool*)
{
    clearEncodedMedia();
    return CaptureResult::Unsupported;
}

const uint8_t* PagerCodec2Audio::encodedMedia() const
{
    return nullptr;
}

std::size_t PagerCodec2Audio::encodedMediaSize() const
{
    return 0U;
}

bool PagerCodec2Audio::hasEncodedMedia() const
{
    return false;
}

void PagerCodec2Audio::clearEncodedMedia()
{
    std::memset(encoded_media_, 0, sizeof(encoded_media_));
    encoded_media_size_ = 0U;
}

PlaybackResult PagerCodec2Audio::play(const uint8_t* encoded_media,
                                      std::size_t encoded_media_len,
                                      chat::voice::vmp::Codec codec,
                                      uint8_t volume_percent)
{
    if (!encoded_media || encoded_media_len == 0U ||
        encoded_media_len > kMaximumEncodedBytes ||
        encoded_media_len % kCodec2BytesPerFrame != 0U ||
        codec != chat::voice::vmp::Codec::Codec2_1300)
    {
        return PlaybackResult::InvalidMedia;
    }

    TDeckBoard* const board = tdeckBoard();
    if (!board || !board->isVoicePlaybackReady())
    {
        return PlaybackResult::Unsupported;
    }
    return board->playCodec2Voice(encoded_media, encoded_media_len, volume_percent)
               ? PlaybackResult::Complete
               : PlaybackResult::AudioBusy;
}

} // namespace platform::esp::arduino_common::voice::vmp_audio

#else

namespace platform::esp::arduino_common::voice::vmp_audio
{

bool PagerCodec2Audio::isSupported() const
{
    return false;
}

bool PagerCodec2Audio::canCapture() const
{
    return false;
}

CaptureResult PagerCodec2Audio::capture(const volatile bool*)
{
    clearEncodedMedia();
    return CaptureResult::Unsupported;
}

const uint8_t* PagerCodec2Audio::encodedMedia() const
{
    return nullptr;
}

std::size_t PagerCodec2Audio::encodedMediaSize() const
{
    return 0U;
}

bool PagerCodec2Audio::hasEncodedMedia() const
{
    return false;
}

void PagerCodec2Audio::clearEncodedMedia()
{
    std::memset(encoded_media_, 0, sizeof(encoded_media_));
    encoded_media_size_ = 0U;
}

PlaybackResult PagerCodec2Audio::play(const uint8_t*,
                                      std::size_t,
                                      chat::voice::vmp::Codec,
                                      uint8_t)
{
    return PlaybackResult::Unsupported;
}

bool PagerCodec2Audio::readCaptureFrame()
{
    return false;
}

bool PagerCodec2Audio::writePlaybackFrame()
{
    return false;
}

void PagerCodec2Audio::mixCaptureToMono()
{
}

void PagerCodec2Audio::duplicatePlaybackToStereo()
{
}

} // namespace platform::esp::arduino_common::voice::vmp_audio

#endif
