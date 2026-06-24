#pragma once

#include "ui/mono/runtime.h"
#include "ui/mono/text_renderer.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace ui::mono::screens::detail
{

inline const char* nonEmpty(const char* text, const char* fallback = "")
{
    return (text && text[0] != '\0') ? text : fallback;
}

inline void copyText(char* dst, size_t dst_len, const char* src)
{
    if (!dst || dst_len == 0)
    {
        return;
    }
    if (!src)
    {
        dst[0] = '\0';
        return;
    }
    std::snprintf(dst, dst_len, "%s", src);
}

inline void drawTextClipped(MonoDisplay& display, const TextRenderer& text_renderer,
                            int x, int y, int w, const char* text, bool inverse = false)
{
    if (!text || w <= 0)
    {
        return;
    }

    char clipped[64] = {};
    if (text_renderer.measureTextWidth(text) <= w)
    {
        copyText(clipped, sizeof(clipped), text);
    }
    else if (w > text_renderer.ellipsisWidth())
    {
        const size_t keep_bytes = text_renderer.clipTextToWidth(text, w - text_renderer.ellipsisWidth());
        const size_t copy_len = std::min(keep_bytes, sizeof(clipped) - 4);
        std::memcpy(clipped, text, copy_len);
        clipped[copy_len] = '\0';
        std::strcat(clipped, "...");
    }
    else
    {
        const size_t keep_bytes = text_renderer.clipTextToWidth(text, w);
        const size_t copy_len = std::min(keep_bytes, sizeof(clipped) - 1);
        std::memcpy(clipped, text, copy_len);
        clipped[copy_len] = '\0';
    }
    text_renderer.drawText(display, x, y, clipped, inverse);
}

inline void drawAlignedTextClipped(MonoDisplay& display, const TextRenderer& text_renderer,
                                   int x, int y, int w, const char* text, bool align_right)
{
    if (!text || w <= 0)
    {
        return;
    }

    const int full_width = text_renderer.measureTextWidth(text);
    if (full_width <= w)
    {
        const int draw_x = align_right ? (x + std::max(0, w - full_width)) : x;
        text_renderer.drawText(display, draw_x, y, text);
        return;
    }

    char clipped[64] = {};
    if (w > text_renderer.ellipsisWidth())
    {
        const size_t keep_bytes = text_renderer.clipTextToWidth(text, w - text_renderer.ellipsisWidth());
        const size_t copy_len = std::min(keep_bytes, sizeof(clipped) - 4);
        std::memcpy(clipped, text, copy_len);
        clipped[copy_len] = '\0';
        std::strcat(clipped, "...");
    }
    else
    {
        const size_t keep_bytes = text_renderer.clipTextToWidth(text, w);
        const size_t copy_len = std::min(keep_bytes, sizeof(clipped) - 1);
        std::memcpy(clipped, text, copy_len);
        clipped[copy_len] = '\0';
    }
    const int clipped_width = text_renderer.measureTextWidth(clipped);
    const int draw_x = align_right ? (x + std::max(0, w - clipped_width)) : x;
    text_renderer.drawText(display, draw_x, y, clipped);
}

inline void drawCenteredTextClipped(MonoDisplay& display, const TextRenderer& text_renderer,
                                    int y, int w, const char* text)
{
    const char* value = nonEmpty(text, "--");
    const int text_w = text_renderer.measureTextWidth(value);
    if (text_w <= w)
    {
        text_renderer.drawText(display, std::max(0, (w - text_w) / 2), y, value);
        return;
    }
    drawTextClipped(display, text_renderer, 0, y, w, value);
}

inline void drawClockSegmentH(MonoDisplay& display, int x, int y, int w)
{
    if (w < 6)
    {
        display.fillRect(x, y, w, 2, true);
        return;
    }
    display.fillRect(x + 1, y, w - 2, 1, true);
    display.fillRect(x, y + 1, w, 1, true);
    display.drawPixel(x + 1, y + 2, true);
    display.drawPixel(x + w - 2, y + 2, true);
}

inline void drawClockSegmentV(MonoDisplay& display, int x, int y, int h)
{
    if (h < 6)
    {
        display.fillRect(x, y, 2, h, true);
        return;
    }
    display.drawPixel(x + 1, y, true);
    display.fillRect(x, y + 1, 2, h - 2, true);
    display.drawPixel(x, y + h - 1, true);
}

inline void drawClockDigit(MonoDisplay& display, int x, int y, char ch)
{
    if (ch == ':')
    {
        display.fillRect(x + 1, y + 5, 2, 2, true);
        display.fillRect(x + 1, y + 13, 2, 2, true);
        return;
    }
    if (ch < '0' || ch > '9')
    {
        return;
    }

    static constexpr uint8_t kDigitSegments[] = {
        0b1111110,
        0b0110000,
        0b1101101,
        0b1111001,
        0b0110011,
        0b1011011,
        0b1011111,
        0b1110000,
        0b1111111,
        0b1111011,
    };
    const uint8_t seg = kDigitSegments[ch - '0'];
    constexpr int kDigitW = 8;
    constexpr int kDigitH = 15;
    constexpr int kMidY = 6;
    constexpr int kBottomY = kDigitH - 3;
    constexpr int kLeftX = 0;
    constexpr int kRightX = kDigitW - 2;
    constexpr int kSegW = 6;
    constexpr int kSegH = 4;

    if (seg & 0b1000000)
    {
        drawClockSegmentH(display, x + 1, y, kSegW);
    }
    if (seg & 0b0100000)
    {
        drawClockSegmentV(display, x + kRightX, y + 1, kSegH);
    }
    if (seg & 0b0010000)
    {
        drawClockSegmentV(display, x + kRightX, y + 8, kSegH);
    }
    if (seg & 0b0001000)
    {
        drawClockSegmentH(display, x + 1, y + kBottomY, kSegW);
    }
    if (seg & 0b0000100)
    {
        drawClockSegmentV(display, x + kLeftX, y + 8, kSegH);
    }
    if (seg & 0b0000010)
    {
        drawClockSegmentV(display, x + kLeftX, y + 1, kSegH);
    }
    if (seg & 0b0000001)
    {
        drawClockSegmentH(display, x + 1, y + kMidY, kSegW);
    }
}

inline int clockGlyphWidth(char ch)
{
    return ch == ':' ? 2 : 8;
}

inline int measureClockText(const char* text)
{
    if (!text || text[0] == '\0')
    {
        return 0;
    }

    int width = 0;
    for (const char* p = text; *p != '\0'; ++p)
    {
        if (p != text)
        {
            width += 2;
        }
        width += clockGlyphWidth(*p);
    }
    return width;
}

inline void drawClockText(MonoDisplay& display, int x, int y, const char* text)
{
    if (!text)
    {
        return;
    }

    int cursor_x = x;
    for (const char* p = text; *p != '\0'; ++p)
    {
        drawClockDigit(display, cursor_x, y, *p);
        cursor_x += clockGlyphWidth(*p) + 2;
    }
}

inline void drawScaledSegment(MonoDisplay& display, int x, int y, int rel_x, int rel_y, int w, int h, int scale)
{
    display.fillRect(x + rel_x * scale, y + rel_y * scale, w * scale, h * scale, true);
}

inline void drawScaledClockDigit(MonoDisplay& display, int x, int y, char ch, int scale)
{
    if (ch == ':')
    {
        display.fillRect(x + scale, y + 5 * scale, 2 * scale, 2 * scale, true);
        display.fillRect(x + scale, y + 13 * scale, 2 * scale, 2 * scale, true);
        return;
    }
    if (ch < '0' || ch > '9')
    {
        return;
    }

    static constexpr uint8_t kDigitSegments[] = {
        0b1111110,
        0b0110000,
        0b1101101,
        0b1111001,
        0b0110011,
        0b1011011,
        0b1011111,
        0b1110000,
        0b1111111,
        0b1111011,
    };
    const uint8_t seg = kDigitSegments[ch - '0'];
    if (seg & 0b1000000)
    {
        drawScaledSegment(display, x, y, 1, 0, 6, 2, scale);
    }
    if (seg & 0b0100000)
    {
        drawScaledSegment(display, x, y, 6, 1, 2, 5, scale);
    }
    if (seg & 0b0010000)
    {
        drawScaledSegment(display, x, y, 6, 8, 2, 5, scale);
    }
    if (seg & 0b0001000)
    {
        drawScaledSegment(display, x, y, 1, 12, 6, 2, scale);
    }
    if (seg & 0b0000100)
    {
        drawScaledSegment(display, x, y, 0, 8, 2, 5, scale);
    }
    if (seg & 0b0000010)
    {
        drawScaledSegment(display, x, y, 0, 1, 2, 5, scale);
    }
    if (seg & 0b0000001)
    {
        drawScaledSegment(display, x, y, 1, 6, 6, 2, scale);
    }
}

inline int scaledClockGlyphWidth(char ch, int scale)
{
    return (ch == ':' ? 2 : 8) * scale;
}

inline int measureScaledClockText(const char* text, int scale)
{
    if (!text || text[0] == '\0')
    {
        return 0;
    }

    int width = 0;
    for (const char* p = text; *p != '\0'; ++p)
    {
        if (p != text)
        {
            width += 2 * scale;
        }
        width += scaledClockGlyphWidth(*p, scale);
    }
    return width;
}

inline void drawScaledClockText(MonoDisplay& display, int x, int y, const char* text, int scale)
{
    if (!text)
    {
        return;
    }

    int cursor_x = x;
    for (const char* p = text; *p != '\0'; ++p)
    {
        drawScaledClockDigit(display, cursor_x, y, *p, scale);
        cursor_x += scaledClockGlyphWidth(*p, scale) + 2 * scale;
    }
}

} // namespace ui::mono::screens::detail
