#pragma once

#include "ui/mono/screens/screensaver_layout.h"

namespace ui::mono::screen_192x176
{

bool matches(const MonoDisplay& display);
void renderScreensaverLayout(MonoDisplay& display, const TextRenderer& text_renderer,
                             const TextRenderer& accent_text_renderer,
                             const ScreensaverLayoutModel& model);

} // namespace ui::mono::screen_192x176
