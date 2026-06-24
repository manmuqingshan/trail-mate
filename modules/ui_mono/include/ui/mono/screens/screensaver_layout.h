#pragma once

#include "ui/mono/runtime.h"
#include "ui/mono/text_renderer.h"

namespace ui::mono
{

struct ScreensaverLayoutModel
{
    const char* protocol = "";
    const char* frequency = "";
    const char* time_main = "";
    const char* seconds = "";
    const char* status = "";
    const char* battery = "";
    const char* satellites = "";
    const char* unread = "";
    const char* node = "";
    const char* timezone = "";
    const char* gps = "";
    const char* ble = "";
    const char* ram = "";
    bool battery_low = false;
};

bool usesLargeScreensaverLayout(const MonoDisplay& display);
void renderScreensaverLayout(MonoDisplay& display, const TextRenderer& text_renderer,
                             const TextRenderer& accent_text_renderer,
                             const ScreensaverLayoutModel& model);

} // namespace ui::mono
