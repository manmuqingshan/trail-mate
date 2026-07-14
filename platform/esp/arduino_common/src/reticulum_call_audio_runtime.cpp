/**
 * @file reticulum_call_audio_runtime.cpp
 * @brief Arduino board backend for the shared Reticulum Codec2 call engine.
 */

#include "platform/esp/arduino_common/reticulum_call_audio_runtime.h"

#include "platform/esp/common/reticulum_call_audio_engine.h"

#if defined(ARDUINO_T_LORA_PAGER) && \
    (defined(ARDUINO_LILYGO_LORA_SX1262) || defined(ARDUINO_LILYGO_LORA_LR1121))

#include "boards/tlora_pager/tlora_pager_board.h"
#include "platform/esp/boards/board_runtime.h"

namespace platform::esp::arduino_common
{
namespace
{

constexpr uint8_t kBitsPerSample = 16;
constexpr uint8_t kCodecChannels = 2;
constexpr std::size_t kMaxCodec2SamplesPerFrame = 320;
constexpr uint8_t kDefaultVolume = 78;
constexpr float kDefaultGainDb = 36.0f;

bool s_registered = false;
::boards::tlora_pager::TLoRaPagerBoard* s_board = nullptr;
int16_t s_codec_pcm[kMaxCodec2SamplesPerFrame * kCodecChannels] = {};

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

bool open_audio(uint32_t sample_rate_hz)
{
    s_board = resolve_board();
    if (!s_board ||
        s_board->codec.open(kBitsPerSample, kCodecChannels, sample_rate_hz) != 0)
    {
        s_board = nullptr;
        return false;
    }

    s_board->codec.setVolume(s_board->getMessageToneVolume());
    s_board->codec.setGain(kDefaultGainDb);
    s_board->codec.setMute(false);
    s_board->codec.setOutMute(false);
    return true;
}

void close_audio()
{
    if (s_board)
    {
        s_board->codec.setMute(true);
        s_board->codec.setOutMute(true);
        s_board->codec.close();
    }
    s_board = nullptr;
}

bool read_mono(int16_t* pcm, std::size_t sample_count)
{
    if (!s_board || !pcm || sample_count == 0 ||
        sample_count > kMaxCodec2SamplesPerFrame ||
        s_board->codec.read(reinterpret_cast<uint8_t*>(s_codec_pcm),
                            sample_count * kCodecChannels * sizeof(int16_t)) != 0)
    {
        return false;
    }

    for (std::size_t index = 0; index < sample_count; ++index)
    {
        const int32_t left = s_codec_pcm[index * kCodecChannels];
        const int32_t right = s_codec_pcm[(index * kCodecChannels) + 1U];
        pcm[index] = static_cast<int16_t>((left + right) / 2);
    }
    return true;
}

bool write_mono(const int16_t* pcm, std::size_t sample_count)
{
    if (!s_board || !pcm || sample_count == 0 ||
        sample_count > kMaxCodec2SamplesPerFrame)
    {
        return false;
    }

    for (std::size_t index = 0; index < sample_count; ++index)
    {
        s_codec_pcm[index * kCodecChannels] = pcm[index];
        s_codec_pcm[(index * kCodecChannels) + 1U] = pcm[index];
    }
    return s_board->codec.write(reinterpret_cast<uint8_t*>(s_codec_pcm),
                                sample_count * kCodecChannels *
                                    sizeof(int16_t)) == 0;
}

uint8_t speaker_volume()
{
    auto* board = resolve_board();
    return board ? board->getMessageToneVolume() : kDefaultVolume;
}

void set_speaker_volume(uint8_t volume_percent)
{
    const uint8_t volume = volume_percent > 100U ? 100U : volume_percent;
    auto* board = resolve_board();
    if (board)
    {
        board->setMessageToneVolume(volume);
    }
    if (s_board)
    {
        s_board->codec.setVolume(volume);
    }
}

} // namespace

void ensureReticulumCallAudioRuntimeRegistered()
{
    if (s_registered)
    {
        return;
    }

    ::platform::esp::common::reticulum_call_audio::Backend backend{};
    backend.is_supported = media_supported;
    backend.open = open_audio;
    backend.close = close_audio;
    backend.read_mono = read_mono;
    backend.write_mono = write_mono;
    backend.speaker_volume = speaker_volume;
    backend.set_speaker_volume = set_speaker_volume;
    s_registered =
        ::platform::esp::common::reticulum_call_audio::install_backend(backend);
}

} // namespace platform::esp::arduino_common

#else

namespace platform::esp::arduino_common
{
namespace
{

bool s_registered = false;

bool unsupported()
{
    return false;
}

bool open_unsupported(uint32_t)
{
    return false;
}

void close_unsupported()
{
}

bool read_unsupported(int16_t*, std::size_t)
{
    return false;
}

bool write_unsupported(const int16_t*, std::size_t)
{
    return false;
}

uint8_t volume_unsupported()
{
    return 0;
}

void set_volume_unsupported(uint8_t)
{
}

} // namespace

void ensureReticulumCallAudioRuntimeRegistered()
{
    if (s_registered)
    {
        return;
    }

    ::platform::esp::common::reticulum_call_audio::Backend backend{};
    backend.is_supported = unsupported;
    backend.open = open_unsupported;
    backend.close = close_unsupported;
    backend.read_mono = read_unsupported;
    backend.write_mono = write_unsupported;
    backend.speaker_volume = volume_unsupported;
    backend.set_speaker_volume = set_volume_unsupported;
    s_registered =
        ::platform::esp::common::reticulum_call_audio::install_backend(backend);
}

} // namespace platform::esp::arduino_common

#endif
