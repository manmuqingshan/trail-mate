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
    const std::string source = read_file(
        root / "platform/esp/arduino_common/src/screen_sleep.cpp");

    // State transitions must be claimed independently from slow board I/O.
    assert(source.find("struct ScreenHardwareAction") != std::string::npos);
    assert(source.find("apply_screen_hardware_action(action);") != std::string::npos);

    // A wake event cannot be dropped because the sleep task briefly owns the
    // state mutex while another task is polling input.
    const std::size_t wake_start = source.find("void wakeScreenSaver()");
    const std::size_t wake_end = source.find("void enterFromScreenSaver()", wake_start);
    assert(wake_start != std::string::npos);
    assert(wake_end != std::string::npos);
    const std::string wake_source = source.substr(wake_start, wake_end - wake_start);
    assert(wake_source.find("xSemaphoreTake(s_activity_mutex, portMAX_DELAY)") !=
           std::string::npos);
    assert(wake_source.find("xSemaphoreTake(s_activity_mutex, pdMS_TO_TICKS(10))") ==
           std::string::npos);

    return 0;
}
