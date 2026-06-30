#pragma once

#include "../../../../../../boards/t_echo_lite/include/boards/t_echo_lite/assets/pager_notification_adpcm.h"

#include <cstddef>
#include <cstdint>

namespace platform::ui::audio::pager_notification
{

namespace asset = ::boards::t_echo_lite::assets;

constexpr uint32_t kSourceSampleRateHz = asset::kPagerNotificationSampleRateHz;
constexpr uint32_t kPlaybackSampleRateHz = 44100U;
constexpr uint8_t kChannels = 2U;
static_assert(kPlaybackSampleRateHz % kSourceSampleRateHz == 0,
              "Pager notification asset sample rate must divide the playback sample rate");
constexpr uint8_t kSampleRepeat =
    static_cast<uint8_t>(kPlaybackSampleRateHz / kSourceSampleRateHz);

constexpr int8_t kImaAdpcmIndexTable[16] = {
    -1,
    -1,
    -1,
    -1,
    2,
    4,
    6,
    8,
    -1,
    -1,
    -1,
    -1,
    2,
    4,
    6,
    8,
};

constexpr int16_t kImaAdpcmStepTable[89] = {
    7,
    8,
    9,
    10,
    11,
    12,
    13,
    14,
    16,
    17,
    19,
    21,
    23,
    25,
    28,
    31,
    34,
    37,
    41,
    45,
    50,
    55,
    60,
    66,
    73,
    80,
    88,
    97,
    107,
    118,
    130,
    143,
    157,
    173,
    190,
    209,
    230,
    253,
    279,
    307,
    337,
    371,
    408,
    449,
    494,
    544,
    598,
    658,
    724,
    796,
    876,
    963,
    1060,
    1166,
    1282,
    1411,
    1552,
    1707,
    1878,
    2066,
    2272,
    2499,
    2749,
    3024,
    3327,
    3660,
    4026,
    4428,
    4871,
    5358,
    5894,
    6484,
    7132,
    7845,
    8630,
    9493,
    10442,
    11487,
    12635,
    13899,
    15289,
    16818,
    18500,
    20350,
    22385,
    24623,
    27086,
    29794,
    32767,
};

struct AdpcmPlaybackState
{
    int16_t predictor = asset::kPagerNotificationInitialSample;
    int16_t current_sample = asset::kPagerNotificationInitialSample;
    uint8_t step_index = 0U;
    uint8_t repeat_remaining = 0U;
    uint32_t sample_index = 0U;
};

inline bool hasMore(const AdpcmPlaybackState& state)
{
    return state.sample_index < asset::kPagerNotificationSampleCount ||
           state.repeat_remaining > 0U;
}

inline int16_t clampPcmSample(int32_t sample)
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

inline int16_t nextSourceSample(AdpcmPlaybackState& state)
{
    if (state.sample_index == 0U)
    {
        state.sample_index = 1U;
        return state.current_sample;
    }

    const uint32_t nibble_index = state.sample_index - 1U;
    const uint8_t encoded = asset::kPagerNotificationAdpcmData[nibble_index / 2U];
    const uint8_t code = (nibble_index & 1U) == 0U ? (encoded & 0x0FU) : (encoded >> 4);
    const int16_t step = kImaAdpcmStepTable[state.step_index];

    int32_t diff = step >> 3;
    if ((code & 0x01U) != 0U)
    {
        diff += step >> 2;
    }
    if ((code & 0x02U) != 0U)
    {
        diff += step >> 1;
    }
    if ((code & 0x04U) != 0U)
    {
        diff += step;
    }

    const int32_t predictor = (code & 0x08U) != 0U
                                  ? static_cast<int32_t>(state.predictor) - diff
                                  : static_cast<int32_t>(state.predictor) + diff;
    state.predictor = clampPcmSample(predictor);
    state.current_sample = state.predictor;

    const int16_t next_index =
        static_cast<int16_t>(state.step_index) + kImaAdpcmIndexTable[code & 0x0FU];
    if (next_index < 0)
    {
        state.step_index = 0U;
    }
    else if (next_index > 88)
    {
        state.step_index = 88U;
    }
    else
    {
        state.step_index = static_cast<uint8_t>(next_index);
    }

    ++state.sample_index;
    return state.current_sample;
}

inline bool nextPlaybackSample(AdpcmPlaybackState& state, int16_t& sample)
{
    if (!hasMore(state))
    {
        return false;
    }

    if (state.repeat_remaining == 0U)
    {
        if (state.sample_index >= asset::kPagerNotificationSampleCount)
        {
            return false;
        }
        state.current_sample = nextSourceSample(state);
        state.repeat_remaining = kSampleRepeat;
    }

    sample = state.current_sample;
    --state.repeat_remaining;
    return true;
}

inline uint16_t fillStereoInterleaved(AdpcmPlaybackState& state,
                                      int16_t* pcm,
                                      uint16_t max_frames)
{
    uint16_t frames = 0U;
    while (frames < max_frames)
    {
        int16_t sample = 0;
        if (!nextPlaybackSample(state, sample))
        {
            break;
        }

        pcm[frames * kChannels] = sample;
        pcm[(frames * kChannels) + 1U] = sample;
        ++frames;
    }
    return frames;
}

} // namespace platform::ui::audio::pager_notification
