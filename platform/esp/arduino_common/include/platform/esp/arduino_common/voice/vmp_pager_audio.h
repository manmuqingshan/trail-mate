/**
 * @file vmp_pager_audio.h
 * @brief Bounded Codec2 recording/playback adapter for Pager VMP.
 *
 * This adapter uses PSRAM-backed encoded media supplied by its enclosing VMP
 * media store, while it acquires its only DMA-capable PCM frame scratch for
 * the duration of recording or playback. Its five-second capture ceiling is
 * enforced by Codec2 frame count, not by a UI timer, so a blocked or slow UI
 * cannot cause over-recording.
 */

#pragma once

#include "chat/infra/voice/vmp_wire.h"

#include <cstddef>
#include <cstdint>

namespace platform::esp::arduino_common::voice::vmp_audio
{

inline constexpr uint32_t kSampleRateHz = 8000U;
inline constexpr uint8_t kBitsPerSample = 16U;
inline constexpr uint8_t kHardwareChannels = 2U;
inline constexpr std::size_t kCodec2FramesPerMessage = 125U;
inline constexpr std::size_t kCodec2SamplesPerFrame = 320U;
inline constexpr std::size_t kCodec2BytesPerFrame = 7U;
inline constexpr std::size_t kMaximumEncodedBytes =
    kCodec2FramesPerMessage * kCodec2BytesPerFrame;

enum class CaptureResult : uint8_t
{
    Complete,
    Cancelled,
    Unsupported,
    AudioBusy,
    AudioFailure,
    CodecFailure,
};

enum class PlaybackResult : uint8_t
{
    Complete,
    Unsupported,
    AudioBusy,
    InvalidMedia,
    AudioFailure,
    CodecFailure,
};

/**
 * @brief Pager microphone/speaker adapter for VMP's fixed Codec2-1300 format.
 *
 * Only Codec2-1300 is accepted.  That restriction keeps a five second clip
 * at 875 bytes so VMP v1 always fits in its one RS(10,8) radio block.
 */
class PagerCodec2Audio final
{
  public:
    PagerCodec2Audio() = default;
    PagerCodec2Audio(const PagerCodec2Audio&) = delete;
    PagerCodec2Audio& operator=(const PagerCodec2Audio&) = delete;

    [[nodiscard]] bool isSupported() const;
    [[nodiscard]] bool canCapture() const;

    /**
     * @brief Records at most 125 Codec2 frames (exactly five seconds).
     *
     * A caller may set `stop_requested` to terminate early; an early clip
     * remains a valid message if it contains at least one full Codec2 frame.
     */
    CaptureResult capture(const volatile bool* stop_requested = nullptr);

    [[nodiscard]] const uint8_t* encodedMedia() const;
    [[nodiscard]] std::size_t encodedMediaSize() const;
    [[nodiscard]] bool hasEncodedMedia() const;
    void clearEncodedMedia();

    /** @brief Decodes and plays one local VMP voice object. */
    PlaybackResult play(const uint8_t* encoded_media,
                        std::size_t encoded_media_len,
                        chat::voice::vmp::Codec codec,
                        uint8_t volume_percent = 100U);

  private:
    struct FrameScratch;

    bool acquireFrameScratch();
    void releaseFrameScratch();
    bool readCaptureFrame();
    bool writePlaybackFrame();
    void mixCaptureToMono();
    void duplicatePlaybackToStereo();

    // PCM needs internal DMA-capable RAM but only while the codec owns audio.
    // Encoded media can safely live in the Pager's PSRAM VMP media store.
    FrameScratch* frame_scratch_ = nullptr;
    uint8_t encoded_media_[kMaximumEncodedBytes] = {};
    std::size_t encoded_media_size_ = 0U;
};

} // namespace platform::esp::arduino_common::voice::vmp_audio
