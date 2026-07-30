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

void require_shared_state_machine(const std::string& source)
{
    assert(source.find("screen_power_state_machine.h") != std::string::npos);
    assert(source.find("StateMachine") != std::string::npos);
    assert(source.find("Event::Input") != std::string::npos);
    assert(source.find("s_screen_sleeping") == std::string::npos);
    assert(source.find("s_screen_saver_active") == std::string::npos);
    assert(source.find("wakeScreenSaver") == std::string::npos);
    assert(source.find("enterFromScreenSaver") == std::string::npos);
    assert(source.find("updateUserActivity") == std::string::npos);
}
} // namespace

int main(int argc, char** argv)
{
    assert(argc == 2);
    const std::filesystem::path root(argv[1]);
    require_shared_state_machine(read_file(
        root / "platform/esp/arduino_common/src/screen_sleep.cpp"));
    require_shared_state_machine(read_file(
        root / "platform/esp/idf_common/src/screen_sleep.cpp"));

    const std::string input = read_file(
        root / "platform/esp/arduino_common/src/LV_Helper_v9.cpp");
    assert(input.find("platform::ui::screen::handle_input();") !=
           std::string::npos);
    assert(input.find("wakeScreenSaver") == std::string::npos);
    assert(input.find("enterFromScreenSaver") == std::string::npos);

    return 0;
}
