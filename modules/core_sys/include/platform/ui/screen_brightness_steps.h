#pragma once

#include <cstddef>
#include <cstdint>

namespace platform::ui::screen_brightness_steps
{

inline constexpr std::size_t kStepCount = 10;
inline constexpr uint8_t kPercentPerStep = 10;
inline constexpr const char* kStepLabels[kStepCount] = {
    "10%",
    "20%",
    "30%",
    "40%",
    "50%",
    "60%",
    "70%",
    "80%",
    "90%",
    "100%",
};

constexpr uint8_t levelForPercent(uint8_t percent, uint8_t max_level)
{
    if (max_level == 0)
    {
        return 0;
    }

    const uint8_t bounded_percent = percent < kPercentPerStep
                                        ? kPercentPerStep
                                        : (percent > 100U ? 100U : percent);
    return static_cast<uint8_t>(
        (static_cast<uint32_t>(bounded_percent) * max_level + 50U) / 100U);
}

constexpr uint8_t levelForStep(std::size_t step_index, uint8_t max_level)
{
    const std::size_t bounded_step = step_index < kStepCount ? step_index : (kStepCount - 1U);
    return levelForPercent(
        static_cast<uint8_t>((bounded_step + 1U) * kPercentPerStep),
        max_level);
}

constexpr uint8_t minimumLevel(uint8_t max_level)
{
    return levelForStep(0, max_level);
}

constexpr uint8_t clampLevel(int level, uint8_t max_level)
{
    if (max_level == 0)
    {
        return 0;
    }

    const uint8_t minimum = minimumLevel(max_level);
    if (level < static_cast<int>(minimum))
    {
        return minimum;
    }
    if (level > static_cast<int>(max_level))
    {
        return max_level;
    }
    return static_cast<uint8_t>(level);
}

constexpr uint8_t nextLevel(uint8_t current_level, uint8_t max_level)
{
    if (max_level == 0)
    {
        return 0;
    }

    const uint8_t current_step = static_cast<uint8_t>(
        (static_cast<uint32_t>(current_level) * kStepCount + (max_level / 2U)) / max_level);
    const uint8_t next_step = current_step >= kStepCount
                                  ? 1U
                                  : static_cast<uint8_t>(current_step + 1U);
    return levelForPercent(static_cast<uint8_t>(next_step * kPercentPerStep), max_level);
}

} // namespace platform::ui::screen_brightness_steps
