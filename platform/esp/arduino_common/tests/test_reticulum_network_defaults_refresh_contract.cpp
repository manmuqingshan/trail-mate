#include <cassert>
#include <fstream>
#include <iterator>
#include <string>

int main(int argc, char** argv)
{
    assert(argc == 2);

    const std::string path =
        std::string(argv[1]) +
        "/platform/esp/arduino_common/src/platform_ui_reticulum_network_config_runtime.cpp";
    std::ifstream file(path, std::ios::binary);
    assert(file.is_open());
    const std::string source((std::istreambuf_iterator<char>(file)),
                             std::istreambuf_iterator<char>());

    // The Settings-derived default topology is authoritative whenever no
    // user-authored SD configuration exists. A cached SD configuration is
    // only a boot-time fallback and must be replaced once that absence is
    // confirmed, then refreshed after Settings changes.
    assert(source.find("void refresh_default_projection(") != std::string::npos);
    assert(source.find("if (!g_status.file_present)") != std::string::npos);
    assert(source.find("refresh_default_projection(legacy_config, true);") !=
           std::string::npos);
    assert(source.find("refresh_default_projection(legacy_config, false);") !=
           std::string::npos);
    return 0;
}
