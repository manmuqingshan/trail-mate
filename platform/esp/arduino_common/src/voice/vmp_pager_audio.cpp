/**
 * @file vmp_pager_audio.cpp
 * @brief Bounded Codec2 recording/playback adapter for Pager VMP.
 */

#include "platform/esp/arduino_common/voice/vmp_pager_audio.h"

#include <cstring>
#include <new>

#if defined(ARDUINO_T_LORA_PAGER)

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

bool beginAudio(TLoRaPagerBoard* board, bool speaker_enabled)
{
    if (!board ||
        board->openAudioSession(kOwner,
                                kBitsPerSample,
                                kHardwareChannels,
                                kSampleRateHz,
                                speaker_enabled) != 0)
    {
        return false;
    }
    if (!board->audioSetGain(kOwner, kCaptureGainDb) ||
        !board->audioSetMute(kOwner, false))
    {
        board->closeAudioSession(kOwner);
        return false;
    }
    if (speaker_enabled && !board->audioSetOutMute(kOwner, false))
    {
        board->closeAudioSession(kOwner);
        return false;
    }
    return true;
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

CaptureResult PagerCodec2Audio::capture(const volatile bool* stop_requested)
{
    clearEncodedMedia();
    TLoRaPagerBoard* const board = pagerBoard();
    if (!board || !acquireFrameScratch())
    {
        return CaptureResult::Unsupported;
    }
    if (!beginAudio(board, false))
    {
        releaseFrameScratch();
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
        return CaptureResult::CodecFailure;
    }

    CaptureResult result = CaptureResult::Complete;
    for (std::size_t frame = 0; frame < kCodec2FramesPerMessage; ++frame)
    {
        if (stop_requested && *stop_requested)
        {
            result = encoded_media_size_ == 0U ? CaptureResult::Cancelled
                                               : CaptureResult::Complete;
            break;
        }
        if (!readCaptureFrame())
        {
            result = CaptureResult::AudioFailure;
            break;
        }
        mixCaptureToMono();
        codec2_encode(encoder,
                      encoded_media_ + encoded_media_size_,
                      frame_scratch_->mono);
        encoded_media_size_ += kCodec2BytesPerFrame;
    }

    codec2_destroy(encoder);
    board->closeAudioSession(kOwner);
    releaseFrameScratch();
    if (result != CaptureResult::Complete)
    {
        clearEncodedMedia();
    }
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
    if (!beginAudio(board, true))
    {
        releaseFrameScratch();
        return PlaybackResult::AudioBusy;
    }
    (void)board->audioSetVolume(kOwner, volume_percent > 100U ? 100U
                                                              : volume_percent);

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
    for (std::size_t offset = 0U; offset < encoded_media_len;
         offset += kCodec2BytesPerFrame)
    {
        codec2_decode(decoder,
                      frame_scratch_->mono,
                      const_cast<uint8_t*>(encoded_media + offset));
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

#else

namespace platform::esp::arduino_common::voice::vmp_audio
{

bool PagerCodec2Audio::isSupported() const
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
