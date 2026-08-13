#include <array>
#include <cassert>
#include <cstdint>

#include "boards/tdeck_pro/tdeck_pro_keyboard.h"

namespace
{
using boards::tdeck_pro::keyboard::Decoder;

void expect_event(Decoder& decoder, std::uint8_t event, char expected_key,
                  bool expected_pressed)
{
    char key = '\0';
    bool pressed = !expected_pressed;
    assert(decoder.decode(event, &key, &pressed));
    assert(key == expected_key);
    assert(pressed == expected_pressed);
}

void expect_ignored(Decoder& decoder, std::uint8_t event)
{
    char key = '\0';
    bool pressed = false;
    assert(!decoder.decode(event, &key, &pressed));
}
} // namespace

int main()
{
    Decoder decoder;

    // TCA8418 raw codes run in electrical order, so these are the exact
    // physical keys that drive the T-Deck Pro menu and saver contracts.
    expect_event(decoder, 0x80U | 9U, 'w', true);
    expect_event(decoder, 9U, 'w', false);
    expect_event(decoder, 0x80U | 19U, 's', true);
    expect_event(decoder, 19U, 's', false);
    expect_event(decoder, 0x80U | 15U, 'h', true);
    expect_event(decoder, 15U, 'h', false);
    expect_event(decoder, 0x80U | 21U, '\n', true);
    expect_event(decoder, 21U, '\n', false);
    expect_event(decoder, 0x80U | 33U, ' ', true);
    expect_event(decoder, 33U, ' ', false);
    expect_event(decoder, 0x80U | 11U, '\b', true);
    expect_event(decoder, 11U, '\b', false);

    constexpr std::array<char, Decoder::kKeyCount> kExpectedBase = {
        'p',
        'o',
        'i',
        'u',
        'y',
        't',
        'r',
        'e',
        'w',
        'q',
        '\b',
        'l',
        'k',
        'j',
        'h',
        'g',
        'f',
        'd',
        's',
        'a',
        '\n',
        '$',
        'm',
        'n',
        'b',
        'v',
        'c',
        'x',
        'z',
        '\0',
        '\0',
        '\0',
        ' ',
        '\0',
        '\0',
    };
    for (std::uint8_t raw = 1U; raw <= Decoder::kKeyCount; ++raw)
    {
        const char expected = kExpectedBase[raw - 1U];
        if (expected == '\0')
        {
            expect_ignored(decoder, static_cast<std::uint8_t>(0x80U | raw));
            expect_ignored(decoder, raw);
            continue;
        }
        expect_event(decoder, static_cast<std::uint8_t>(0x80U | raw), expected,
                     true);
        expect_event(decoder, raw, expected, false);
    }

    // Shift, Sym, and Alt stay board-local.  Their following key event is
    // still one standard character event for the shared input adapter.
    expect_ignored(decoder, 0x80U | 35U); // Left Shift
    expect_event(decoder, 0x80U | 15U, 'H', true);
    expect_event(decoder, 15U, 'H', false);

    expect_ignored(decoder, 0x80U | 32U); // Sym
    expect_event(decoder, 0x80U | 9U, '1', true);
    expect_event(decoder, 9U, '1', false);

    expect_ignored(decoder, 0x80U | 30U); // Alt
    expect_event(decoder, 0x80U | 15U, ':', true);
    expect_event(decoder, 15U, ':', false);

    expect_ignored(decoder, 0x80U | 32U); // Sym + Mic = 0
    expect_event(decoder, 0x80U | 34U, '0', true);
    expect_event(decoder, 34U, '0', false);

    expect_ignored(decoder, 0U);
    expect_ignored(decoder, 36U);
    expect_ignored(decoder, 1U); // Release without a recorded press.
    return 0;
}
