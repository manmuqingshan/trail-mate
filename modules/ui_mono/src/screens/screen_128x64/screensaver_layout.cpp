#include "ui/mono/screens/screen_128x64/screensaver_layout.h"

#ifndef TRAIL_MATE_NRF_MONO_SCREEN_192X176_ONLY
#define TRAIL_MATE_NRF_MONO_SCREEN_192X176_ONLY 0
#endif

#if !TRAIL_MATE_NRF_MONO_SCREEN_192X176_ONLY

#include "../screen_layout_primitives.h"

#include <algorithm>
#include <cstdio>

namespace ui::mono::screen_128x64
{
namespace
{
using namespace screens::detail;
}

void renderScreensaverLayout(MonoDisplay& display, const TextRenderer& text_renderer,
                             const ScreensaverLayoutModel& model)
{
    char top_left_buf[32] = {};
    char top_detail_right[16] = {};
    char footer_left[24] = {};

    std::snprintf(top_left_buf, sizeof(top_left_buf), "%s %s",
                  nonEmpty(model.protocol, "--"),
                  nonEmpty(model.battery, "BAT:--"));
    std::snprintf(top_detail_right, sizeof(top_detail_right), "MSG %s", nonEmpty(model.unread, "0"));
    std::snprintf(footer_left, sizeof(footer_left), "ID %s", nonEmpty(model.node, "--"));

    constexpr int kTopY = 1;
    constexpr int kTopDetailY = 9;
    constexpr int kTimeY = 24;
    constexpr int kSidePrimaryY = 27;
    constexpr int kSideSecondaryY = 35;
    constexpr int kSecY = 30;
    constexpr int kDateY = 44;
    constexpr int kFooterY = 55;

    drawTextClipped(display, text_renderer, 2, kTopY, 60, top_left_buf);
    drawAlignedTextClipped(display, text_renderer, 66, kTopY, display.width() - 68,
                           nonEmpty(model.frequency, "--"), true);
    drawTextClipped(display, text_renderer, 2, kTopDetailY, 42, nonEmpty(model.satellites, "SAT --"));
    drawAlignedTextClipped(display, text_renderer, 70, kTopDetailY, display.width() - 72, top_detail_right, true);

    const int time_w = measureClockText(model.time_main);
    const int time_x = std::max(0, (display.width() - time_w) / 2);
    drawClockText(display, time_x, kTimeY, model.time_main);
    if (model.seconds && model.seconds[0] != '\0')
    {
        const int sec_x = std::min(display.width() - 12, time_x + time_w + 4);
        text_renderer.drawText(display, sec_x, kSecY, model.seconds);
    }

    drawTextClipped(display, text_renderer, 0, kSidePrimaryY, 42, model.gps);
    if (model.ble && model.ble[0] != '\0')
    {
        drawTextClipped(display, text_renderer, 0, kSideSecondaryY, 42, model.ble);
    }
    drawCenteredTextClipped(display, text_renderer, kDateY, display.width(), model.status);

    const int ram_w = text_renderer.measureTextWidth(nonEmpty(model.ram, "RAM --"));
    const int ram_x = std::max(0, (display.width() - ram_w) / 2);
    constexpr int kFooterGap = 4;
    const int footer_left_w = std::max(0, ram_x - kFooterGap);
    const int footer_right_x = std::min(display.width(), ram_x + ram_w + kFooterGap);
    const int footer_right_w = std::max(0, display.width() - footer_right_x);
    if (footer_left_w > 0)
    {
        drawAlignedTextClipped(display, text_renderer, 0, kFooterY, footer_left_w, footer_left, false);
    }
    text_renderer.drawText(display, ram_x, kFooterY, nonEmpty(model.ram, "RAM --"));
    if (footer_right_w > 0)
    {
        drawAlignedTextClipped(display, text_renderer, footer_right_x, kFooterY, footer_right_w,
                               nonEmpty(model.timezone, "UTC+0"), true);
    }

    if (model.battery_low)
    {
        display.fillRect(display.width() - 10, kFooterY + text_renderer.lineHeight() - 2, 7, 2, true);
    }
}

} // namespace ui::mono::screen_128x64

#endif
