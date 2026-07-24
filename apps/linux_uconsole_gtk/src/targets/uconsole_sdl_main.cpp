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

} // namespace

int main()
{
    trailmate::uconsole::desktop::SdlWindowPresenter window{
        {.width = 1280,
         .height = 720,
         .scale = 1,
         .fullscreen = envFlag("TRAIL_MATE_UCONSOLE_FULLSCREEN", false),
         .title = "Trail Mate uConsole"}};
    trailmate::uconsole::runUConsoleShell(
        window, {.width = 1280, .height = 720, .frame_time_ms = 16});
    return 0;
}
