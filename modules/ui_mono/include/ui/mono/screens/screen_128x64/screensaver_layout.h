#pragma once

#include "ui/mono/screens/screensaver_layout.h"

namespace ui::mono::screen_128x64
{

void renderScreensaverLayout(MonoDisplay& display, const TextRenderer& text_renderer,
                             const ScreensaverLayoutModel& model);

} // namespace ui::mono::screen_128x64
