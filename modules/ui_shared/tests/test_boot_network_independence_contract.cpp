#include <cassert>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace
{
std::string read_file(const std::filesystem::path& path)
{
    std::ifstream stream(path);
    assert(stream.good());
    std::ostringstream contents;
    contents << stream.rdbuf();
    return contents.str();
}
} // namespace

int main(int argc, char** argv)
{
    assert(argc == 2);
    const std::filesystem::path root(argv[1]);
    const std::string boot = read_file(
        root / "modules/ui_shared/src/ui/ui_boot.cpp");
    const std::string startup = read_file(
        root / "apps/esp32_lvgl/src/esp32_lvgl_arduino_startup_runtime.cpp");

    // ESP startup must not hold the shell behind an arbitrary multi-second
    // splash gate. A board-specific override remains available above.
    assert(boot.find("constexpr uint32_t kMinShowMs = 900;") != std::string::npos);
    assert(boot.find("constexpr uint32_t kMinShowMs = 3000;") == std::string::npos);

    // The menu is constructed and finalized without a synchronous Wi-Fi call.
    const std::size_t menu_pos = startup.find("initializeShell();");
    const std::size_t finish_pos = startup.find("finishStartup(waking_from_sleep);");
    assert(menu_pos != std::string::npos);
    assert(finish_pos != std::string::npos);
    assert(menu_pos < finish_pos);
    assert(startup.find("wifi_access::ensure_connected") == std::string::npos);
    assert(startup.find("wifi::connect") == std::string::npos);

    return 0;
}
