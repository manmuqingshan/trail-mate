#include "ui/mono/screens/screensaver_layout.h"

#ifndef TRAIL_MATE_NRF_MONO_SCREEN_128X64_ONLY
#define TRAIL_MATE_NRF_MONO_SCREEN_128X64_ONLY 0
#endif

#ifndef TRAIL_MATE_NRF_MONO_SCREEN_192X176_ONLY
#define TRAIL_MATE_NRF_MONO_SCREEN_192X176_ONLY 0
#endif

#if TRAIL_MATE_NRF_MONO_SCREEN_128X64_ONLY && TRAIL_MATE_NRF_MONO_SCREEN_192X176_ONLY
#error "Only one fixed mono screensaver layout target can be enabled."
#endif

#if !TRAIL_MATE_NRF_MONO_SCREEN_192X176_ONLY
#include "ui/mono/screens/screen_128x64/screensaver_layout.h"
#endif

#if !TRAIL_MATE_NRF_MONO_SCREEN_128X64_ONLY
#include "ui/mono/screens/screen_192x176/screensaver_layout.h"
#endif

namespace ui::mono
{

bool usesLargeScreensaverLayout(const MonoDisplay& display)
{
#if TRAIL_MATE_NRF_MONO_SCREEN_192X176_ONLY
    (void)display;
    return true;
#elif TRAIL_MATE_NRF_MONO_SCREEN_128X64_ONLY
    (void)display;
    return false;
#else
    return screen_192x176::matches(display);
#endif
}

void renderScreensaverLayout(MonoDisplay& display, const TextRenderer& text_renderer,
                             const TextRenderer& accent_text_renderer,
                             const ScreensaverLayoutModel& model)
{
#if TRAIL_MATE_NRF_MONO_SCREEN_192X176_ONLY
    screen_192x176::renderScreensaverLayout(display, text_renderer, accent_text_renderer, model);
#elif TRAIL_MATE_NRF_MONO_SCREEN_128X64_ONLY
    (void)accent_text_renderer;
    screen_128x64::renderScreensaverLayout(display, text_renderer, model);
#else
    if (screen_192x176::matches(display))
    {
        screen_192x176::renderScreensaverLayout(display, text_renderer, accent_text_renderer, model);
        return;
    }

    screen_128x64::renderScreensaverLayout(display, text_renderer, model);
#endif
}

} // namespace ui::mono
