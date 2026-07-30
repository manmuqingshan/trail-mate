#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

#include "platform/desktop/sdl_window_presenter.h"
#include "uconsole/uconsole_desktop_shell.h"

namespace
{

struct CaptureTarget
{
    const char* id = nullptr;
    const char* filename = nullptr;
    int nav_steps = 0;
    char shortcut = '\0';
};

constexpr CaptureTarget kTargets[] = {
    {"overview", "overview.png", 0, '\0'},
    {"chat", "chat.png", 1, '\0'},
    {"map", "map.png", 2, '\0'},
    {"gps", "gps-sky-plot.png", 4, '\0'},
    {"radio-tools", "radio-tools.png", 7, '\0'},
    {"extensions", "extensions.png", 10, '\0'},
    {"sidebar-collapsed", "sidebar-collapsed.png", 2, '\\'},
    {"shortcut-help", "shortcut-help.png", 0, 'h'},
};

void setEnvironment(const char* name, const std::string& value)
{
#if defined(_WIN32)
    if (_putenv_s(name, value.c_str()) != 0)
    {
        throw std::runtime_error(std::string("failed to set ") + name);
    }
#else
    if (setenv(name, value.c_str(), 1) != 0)
    {
        throw std::runtime_error(std::string("failed to set ") + name);
    }
#endif
}

void configureCaptureRuntime()
{
    const std::filesystem::path runtime_root =
        std::filesystem::current_path() / ".codex-build" /
        "uconsole-sdl-runtime";
    setEnvironment("TRAIL_MATE_RUNTIME_MODE", "demo");
    setEnvironment("TRAIL_MATE_SETTINGS_ROOT",
                   (runtime_root / "settings").string());
    setEnvironment("TRAIL_MATE_SD_ROOT", (runtime_root / "sd").string());
    setEnvironment("TRAIL_MATE_CACHE_ROOT", (runtime_root / "cache").string());
}

void captureTarget(const CaptureTarget& target,
                   const std::filesystem::path& output_directory)
{
    const std::filesystem::path screenshot =
        output_directory / target.filename;
    trailmate::uconsole::desktop::SdlWindowPresenter presenter{
        {.width = 1280,
         .height = 720,
         .scale = 1,
         .fullscreen = false,
         .title = std::string("Trail Mate uConsole SDL - ") + target.id,
         .screenshot_path = screenshot.string(),
         .screenshot_after_frames = 45,
         .initial_nav_steps = target.nav_steps,
         .initial_shortcut = target.shortcut,
         .hidden = true}};
    trailmate::uconsole::runUConsoleShell(
        presenter, {.width = 1280, .height = 720, .frame_time_ms = 16});
    if (!std::filesystem::exists(screenshot))
    {
        throw std::runtime_error("SDL capture did not create " +
                                 screenshot.string());
    }
    std::cout << target.id << ": " << screenshot.string() << '\n';
}

} // namespace

int main(int argc, char** argv)
{
    try
    {
        const std::filesystem::path output_directory =
            argc > 1 ? std::filesystem::path(argv[1])
                     : std::filesystem::path("docs/specs/images/uconsole-sdl");
        std::filesystem::create_directories(output_directory);
        configureCaptureRuntime();
        for (const auto& target : kTargets)
        {
            captureTarget(target, output_directory);
        }
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "uConsole SDL capture failed: " << error.what() << '\n';
        return 1;
    }
}
