#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>
#include <string_view>

#include "platform/gtk/gtk_canvas_presenter.h"
#include "uconsole/uconsole_desktop_shell.h"

namespace
{

std::string envString(const char* name, const char* fallback)
{
    if (const char* value = std::getenv(name))
    {
        if (*value != '\0') return value;
    }
    return fallback;
}

int envInt(const char* name, int fallback)
{
    if (const char* value = std::getenv(name))
    {
        if (*value != '\0')
        {
            try
            {
                return std::stoi(value);
            }
            catch (...)
            {
                return fallback;
            }
        }
    }
    return fallback;
}

struct LaunchOptions
{
    int width = 1280;
    int height = 720;
    bool fullscreen = true;
};

LaunchOptions parseOptions(int argc, char** argv)
{
    LaunchOptions options{};
    options.width = envInt("TRAIL_MATE_UCONSOLE_WIDTH", 1280);
    options.height = envInt("TRAIL_MATE_UCONSOLE_HEIGHT", 720);
    for (int index = 1; index < argc; ++index)
    {
        const std::string_view current{argv[index]};
        if (current == "--width" && (index + 1) < argc)
        {
            options.width = std::stoi(argv[++index]);
        }
        else if (current == "--height" && (index + 1) < argc)
        {
            options.height = std::stoi(argv[++index]);
        }
        else if (current == "--fullscreen")
        {
            options.fullscreen = true;
        }
        else if (current == "--windowed")
        {
            options.fullscreen = false;
        }
    }

    return options;
}

} // namespace

int main(int argc, char** argv)
{
    try
    {
        const LaunchOptions options = parseOptions(argc, argv);
        trailmate::uconsole::gtk::GtkCanvasPresenter window{
            {.width = options.width,
             .height = options.height,
             .fullscreen = options.fullscreen,
             .title = "Trail Mate uConsole",
             .screenshot_path =
                 envString("TRAIL_MATE_UCONSOLE_SCREENSHOT", ""),
             .screenshot_after_frames =
                 envInt("TRAIL_MATE_UCONSOLE_SCREENSHOT_AFTER_FRAMES", 45),
             .initial_nav_steps =
                 envInt("TRAIL_MATE_UCONSOLE_INITIAL_NAV_STEPS", 0),
             .initial_shortcut =
                 envString("TRAIL_MATE_UCONSOLE_INITIAL_SHORTCUT", "")
                         .empty()
                     ? '\0'
                     : envString("TRAIL_MATE_UCONSOLE_INITIAL_SHORTCUT", "")
                           .front()}};
        trailmate::uconsole::UConsoleShellOptions shell{};
        shell.width = options.width;
        shell.height = options.height;
        trailmate::uconsole::runUConsoleShell(window, shell);
        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "uConsole startup failed: " << ex.what() << '\n';
        return 1;
    }
}
