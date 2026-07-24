#include <cstdlib>
#include <string>

#include "platform/desktop/sdl_window_presenter.h"
#include "uconsole/uconsole_desktop_shell.h"

namespace
{

bool envFlag(const char* name, bool fallback)
{
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0')
    {
        return fallback;
    }
    const std::string text(value);
    return text == "1" || text == "true" || text == "TRUE" ||
           text == "on" || text == "ON";
}

int envInt(const char* name, int fallback)
{
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0')
    {
        return fallback;
    }
    try
    {
        return std::stoi(value);
    }
    catch (...)
    {
        return fallback;
    }
}

} // namespace

int main()
{
    const bool fullscreen =
        envFlag("TRAIL_MATE_UCONSOLE_FULLSCREEN", false);
    const int width = envInt("TRAIL_MATE_UCONSOLE_WIDTH", 1280);
    // The Wayland desktop reserves the panel and native titlebar outside the
    // SDL client area. Fit the logical canvas to the remaining 658px so the
    // shell footer remains visible on the 1280x720 uConsole output.
    const int height = envInt("TRAIL_MATE_UCONSOLE_HEIGHT",
                              fullscreen ? 720 : 658);
    trailmate::uconsole::desktop::SdlWindowPresenter window{
        {.width = width,
         .height = height,
         .scale = 1,
         .fullscreen = fullscreen,
         .title = "Trail Mate uConsole"}};
    trailmate::uconsole::runUConsoleShell(
        window, {.width = width, .height = height, .frame_time_ms = 16});
    return 0;
}
