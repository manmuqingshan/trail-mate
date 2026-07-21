#include "platform/ui/screen_brightness_steps.h"

#include <cassert>
#include <cstddef>
#include <cstdint>

int main()
{
    namespace brightness = platform::ui::screen_brightness_steps;

    constexpr uint8_t kMaxLevel = 16;
    constexpr uint8_t kExpectedLevels[brightness::kStepCount] = {
        2,
        3,
        5,
        6,
        8,
        10,
        11,
        13,
        14,
        16,
    };

    for (std::size_t index = 0; index < brightness::kStepCount; ++index)
    {
        assert(brightness::levelForStep(index, kMaxLevel) == kExpectedLevels[index]);
        const uint8_t next = brightness::nextLevel(kExpectedLevels[index], kMaxLevel);
        const uint8_t expected_next =
            kExpectedLevels[(index + 1U) % brightness::kStepCount];
        assert(next == expected_next);
    }

    assert(brightness::minimumLevel(kMaxLevel) == 2);
    assert(brightness::clampLevel(-1, kMaxLevel) == 2);
    assert(brightness::clampLevel(0, kMaxLevel) == 2);
    assert(brightness::clampLevel(7, kMaxLevel) == 7);
    assert(brightness::clampLevel(17, kMaxLevel) == 16);
    assert(brightness::nextLevel(0, kMaxLevel) == 2);

    assert(brightness::minimumLevel(0) == 0);
    assert(brightness::clampLevel(10, 0) == 0);
    assert(brightness::nextLevel(10, 0) == 0);
    return 0;
}
