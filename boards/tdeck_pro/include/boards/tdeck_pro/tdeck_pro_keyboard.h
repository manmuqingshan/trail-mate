#pragma once

#include <array>
#include <cstdint>

namespace boards::tdeck_pro::keyboard
{

// Converts the T-Deck Pro's TCA8418 FIFO events into the character contract
// shared by the Arduino/LVGL input adapter.  The controller's raw key order
// follows the electrical matrix, not the printing order on the keycaps.
class Decoder
{
  public:
    static constexpr std::uint8_t kKeyCount = 35;

    bool decode(std::uint8_t event, char* key, bool* pressed)
    {
        if (key == nullptr || pressed == nullptr)
        {
            return false;
        }

        *pressed = (event & 0x80U) != 0U;
        const std::uint8_t key_code = static_cast<std::uint8_t>(event & 0x7FU);
        if (key_code == 0U || key_code > kKeyCount)
        {
            return false;
        }

        const std::uint8_t index = static_cast<std::uint8_t>(key_code - 1U);
        if (is_modifier(index))
        {
            if (*pressed)
            {
                activate_modifier(index);
            }
            return false;
        }

        if (*pressed)
        {
            const char translated = translate_press(index);
            pressed_keys_[index] = translated;
            if (translated == '\0')
            {
                return false;
            }
            *key = translated;
            return true;
        }

        const char translated = pressed_keys_[index];
        pressed_keys_[index] = '\0';
        if (translated == '\0')
        {
            return false;
        }
        *key = translated;
        return true;
    }

  private:
    static constexpr std::uint8_t kRightShiftIndex = 30;
    static constexpr std::uint8_t kSymbolIndex = 31;
    static constexpr std::uint8_t kAltIndex = 29;
    static constexpr std::uint8_t kLeftShiftIndex = 34;

    // Key codes 1..35, in the raw order specified by TCA8418 Table 1.  This
    // is the v1.0 T-Deck Pro matrix used by LilyGo's factory firmware and the
    // A7682E board profile.  A zero is a modifier or the unmodified Mic key.
    static constexpr std::array<char, kKeyCount> kBaseMap = {
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

    // Alt and Sym share the symbol layer printed on the keycaps.  Only the
    // Mic key has a symbol-layer character (0); modifiers remain non-text.
    static constexpr std::array<char, kKeyCount> kSymbolMap = {
        '@',
        '+',
        '-',
        '_',
        ')',
        '(',
        '3',
        '2',
        '1',
        '#',
        '\b',
        '"',
        '\'',
        ';',
        ':',
        '/',
        '6',
        '5',
        '4',
        '*',
        '\n',
        '$',
        '.',
        ',',
        '!',
        '?',
        '9',
        '8',
        '7',
        '\0',
        '\0',
        '\0',
        ' ',
        '0',
        '\0',
    };

    static constexpr bool is_modifier(std::uint8_t index)
    {
        return index == kRightShiftIndex || index == kSymbolIndex ||
               index == kAltIndex || index == kLeftShiftIndex;
    }

    void activate_modifier(std::uint8_t index)
    {
        if (index == kRightShiftIndex || index == kLeftShiftIndex)
        {
            shift_pending_ = true;
        }
        else if (index == kSymbolIndex)
        {
            symbol_pending_ = true;
        }
        else if (index == kAltIndex)
        {
            alt_pending_ = true;
        }
    }

    char translate_press(std::uint8_t index)
    {
        const bool symbol_layer = symbol_pending_ || alt_pending_;
        char translated = symbol_layer ? kSymbolMap[index] : kBaseMap[index];

        if (shift_pending_ && translated >= 'a' && translated <= 'z')
        {
            translated = static_cast<char>(translated - 'a' + 'A');
        }

        // Modifiers are one-shot, matching the physical keyboard's existing
        // text-entry behavior while keeping the shared LVGL adapter unaware
        // of TCA8418-specific state.
        shift_pending_ = false;
        symbol_pending_ = false;
        alt_pending_ = false;
        return translated;
    }

    std::array<char, kKeyCount> pressed_keys_{};
    bool shift_pending_ = false;
    bool symbol_pending_ = false;
    bool alt_pending_ = false;
};

} // namespace boards::tdeck_pro::keyboard
