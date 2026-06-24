#include "ui/mono/screens/screen_192x176/screensaver_layout.h"

#ifndef TRAIL_MATE_NRF_MONO_SCREEN_128X64_ONLY
#define TRAIL_MATE_NRF_MONO_SCREEN_128X64_ONLY 0
#endif

#if !TRAIL_MATE_NRF_MONO_SCREEN_128X64_ONLY

#include "../screen_layout_primitives.h"

#include <algorithm>
#include <cstdio>

namespace ui::mono::screen_192x176
{
namespace
{
using namespace screens::detail;
}

bool matches(const MonoDisplay& display)
{
    return display.width() >= 160 && display.height() >= 120;
}

void renderScreensaverLayout(MonoDisplay& display, const TextRenderer&,
                             const TextRenderer& accent_text_renderer,
                             const ScreensaverLayoutModel& model)
{
    const int w = display.width();
    const int h = display.height();
    constexpr int kMargin = 8;

    char msg_buf[16] = {};
    char node_buf[24] = {};
    std::snprintf(msg_buf, sizeof(msg_buf), "MSG %s", nonEmpty(model.unread, "0"));
    std::snprintf(node_buf, sizeof(node_buf), "ID %s", nonEmpty(model.node, "--"));

    drawTextClipped(display, accent_text_renderer, kMargin, 5, 74, nonEmpty(model.protocol, "--"));
    drawAlignedTextClipped(display, accent_text_renderer, w / 2, 5, (w / 2) - kMargin,
                           nonEmpty(model.frequency, "--"), true);
    display.drawHLine(kMargin, 20, w - (kMargin * 2));

    constexpr int kClockScale = 4;
    const int clock_w = measureScaledClockText(model.time_main, kClockScale);
    const int clock_x = std::max(0, (w - clock_w) / 2);
    const int clock_y = std::max(28, (h / 2) - 54);
    drawScaledClockText(display, clock_x, clock_y, model.time_main, kClockScale);

    const int status_y = clock_y + 66;
    drawCenteredTextClipped(display, accent_text_renderer, status_y, w, nonEmpty(model.status, "--"));

    const int grid_top = std::min(h - 55, status_y + accent_text_renderer.lineHeight() + 8);
    display.drawHLine(kMargin, grid_top, w - (kMargin * 2));

    const int row1_y = grid_top + 8;
    const int row_gap = accent_text_renderer.lineHeight() + 4;
    const int row2_y = row1_y + row_gap;
    const int row3_y = row2_y + row_gap;
    const int col_w = (w - (kMargin * 2)) / 3;

    drawTextClipped(display, accent_text_renderer, kMargin, row1_y, col_w, nonEmpty(model.battery, "BAT:--"));
    drawCenteredTextClipped(display, accent_text_renderer, row1_y, w, nonEmpty(model.satellites, "SAT --"));
    drawAlignedTextClipped(display, accent_text_renderer, w - kMargin - col_w, row1_y, col_w, msg_buf, true);

    drawTextClipped(display, accent_text_renderer, kMargin, row2_y, (w / 2) - kMargin, node_buf);
    drawAlignedTextClipped(display, accent_text_renderer, w / 2, row2_y, (w / 2) - kMargin,
                           nonEmpty(model.gps, "GPS --"), true);

    drawTextClipped(display, accent_text_renderer, kMargin, row3_y, (w / 2) - kMargin, nonEmpty(model.ram, "RAM --"));
    drawAlignedTextClipped(display, accent_text_renderer, w / 2, row3_y, (w / 2) - kMargin,
                           nonEmpty(model.timezone, "UTC+0"), true);

    if (model.battery_low)
    {
        display.fillRect(w - kMargin - 18, h - 8, 18, 3, true);
    }
}

} // namespace ui::mono::screen_192x176

#endif
