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

    // The configuration document buffer is not safe as a pseudo-EXT_RAM
    // static: this target's linker places that section in internal BSS. It
    // must be created through a strict PSRAM allocation before every read or
    // serialization path can use it.
    assert(source.find("char* g_file_buffer = nullptr;") != std::string::npos);
    assert(source.find("bool ensure_file_buffer()") != std::string::npos);
    assert(source.find("MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT") != std::string::npos);
    assert(source.find("kConfigBufferBytes") != std::string::npos);
    assert(source.find("!out_len || !ensure_file_buffer()") != std::string::npos);
    assert(source.find("!sd_available() || !ensure_file_buffer()") != std::string::npos);

    // Parsing is a cold operation, unlike g_active which the Reticulum packet
    // paths read at runtime. Its 804-byte scratch configuration therefore
    // belongs in strict PSRAM and must fail cleanly before parsing begins.
    assert(source.find("ReticulumNetworkConfig* g_parse_scratch_storage = nullptr;") !=
           std::string::npos);
    assert(source.find("bool ensure_parse_scratch()") != std::string::npos);
    assert(source.find("[Reticulum][Config] parse_scratch allocation_failed") !=
           std::string::npos);
    assert(source.find("if (!ensure_parse_scratch())") != std::string::npos);
    assert(source.find("bool build_defaults(const chat::MeshConfig& legacy_config)") !=
           std::string::npos);
    assert(source.find("Reticulum config memory unavailable") != std::string::npos);
    return 0;
}
