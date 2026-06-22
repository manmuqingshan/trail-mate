#include <cassert>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace
{

std::string read_file(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    assert(stream.is_open());
    std::ostringstream out;
    out << stream.rdbuf();
    return out.str();
}

bool contains(const std::string& haystack, const char* needle)
{
    return haystack.find(needle) != std::string::npos;
}

std::size_t position_of(const std::string& haystack, const char* needle)
{
    const auto pos = haystack.find(needle);
    assert(pos != std::string::npos);
    return pos;
}

} // namespace

int main(int argc, char** argv)
{
    assert(argc == 2);
    const std::filesystem::path repo_root = argv[1];

    const std::string arduino_startup = read_file(
        repo_root / "apps/esp32_lvgl/src/esp32_lvgl_arduino_startup_runtime.cpp");
    const std::size_t board_init = position_of(
        arduino_startup,
        "startup_support::initializeBoard");
    const std::size_t begin_log = position_of(
        arduino_startup,
        "debug::begin_sd_debug_log");
    const std::size_t display_init = position_of(
        arduino_startup,
        "display_runtime::initialize");
    const std::size_t begin_boot = position_of(
        arduino_startup,
        "startup_shell::beginBootUi");
    const std::size_t storage_init = position_of(
        arduino_startup,
        "boards::initializeStorage");
    const std::size_t export_core = position_of(
        arduino_startup,
        "debug::export_previous_coredump_to_sd");
    assert(board_init < display_init);
    assert(display_init < begin_boot);
    assert(begin_boot < storage_init);
    assert(storage_init < begin_log);
    assert(begin_boot < begin_log);
    assert(begin_log < export_core);

    const std::string header = read_file(
        repo_root /
        "platform/esp/arduino_common/include/platform/esp/arduino_common/debug/sd_debug_log.h");
    assert(contains(header, "begin_sd_debug_log"));
    assert(contains(header, "export_previous_coredump_to_sd"));

    const std::string implementation = read_file(
        repo_root / "platform/esp/arduino_common/src/debug/sd_debug_log.cpp");
    assert(contains(implementation, "/trailmate/debug/debug.log"));
    assert(contains(implementation, "/trailmate/debug/debug.prev.log"));
    assert(contains(implementation, "/trailmate/coredumps"));
    assert(contains(implementation, "kMaxDebugLogBytes = 256ULL * 1024ULL"));
    assert(contains(implementation, "esp_core_dump_image_get"));
    assert(contains(implementation, "esp_core_dump_image_check"));
    assert(contains(implementation, "ESP_PARTITION_SUBTYPE_DATA_COREDUMP"));
    assert(contains(implementation, "flash_addr - partition->address"));
    assert(contains(implementation, "esp_partition_read"));
    assert(contains(implementation, "esp_core_dump_image_erase"));
    assert(contains(implementation, "keeping flash copy"));

    const std::string platformio = read_file(repo_root / "platformio.ini");
    assert(contains(platformio, "board_build.partitions = partitions.csv"));

    const std::string partitions = read_file(repo_root / "partitions.csv");
    assert(contains(partitions, "coredump, data, coredump"));
    assert(contains(partitions, "0xFF0000"));
    assert(contains(partitions, "0x10000"));

    const std::size_t write_payload = position_of(
        implementation,
        "export_coredump_payload(partition, path, partition_offset, size)");
    const std::size_t erase = position_of(
        implementation,
        "esp_core_dump_image_erase()");
    assert(write_payload < erase);

    const std::string idf_startup = read_file(
        repo_root / "apps/esp32_lvgl/src/esp32_lvgl_startup_runtime.cpp");
    const std::size_t idf_display = position_of(
        idf_startup,
        "boards::initializeDisplay");
    const std::size_t idf_begin_boot = position_of(
        idf_startup,
        "showBootUi(config");
    const std::size_t idf_sd_ready = position_of(
        idf_startup,
        "bsp_runtime::ensure_sdcard_ready");
    const std::size_t idf_export_core = position_of(
        idf_startup,
        "idf_common::debug::export_previous_coredump_to_sd");
    assert(idf_display < idf_begin_boot);
    assert(idf_begin_boot < idf_sd_ready);
    assert(idf_sd_ready < idf_export_core);

    const std::string idf_header = read_file(
        repo_root /
        "platform/esp/idf_common/include/platform/esp/idf_common/debug/sd_coredump_export.h");
    assert(contains(idf_header, "SdCoredumpExportStatus"));
    assert(contains(idf_header, "export_previous_coredump_to_sd"));

    const std::string idf_implementation = read_file(
        repo_root / "platform/esp/idf_common/src/debug/sd_coredump_export.cpp");
    assert(contains(idf_implementation, "bsp_runtime::sdcard_mount_point"));
    assert(contains(idf_implementation, "trailmate"));
    assert(contains(idf_implementation, "coredumps"));
    assert(contains(idf_implementation, "esp_core_dump_image_get"));
    assert(contains(idf_implementation, "esp_core_dump_image_check"));
    assert(contains(idf_implementation, "ESP_PARTITION_SUBTYPE_DATA_COREDUMP"));
    assert(contains(idf_implementation, "flash_addr - partition->address"));
    assert(contains(idf_implementation, "esp_partition_read"));
    assert(contains(idf_implementation, "esp_core_dump_image_erase"));
    assert(contains(idf_implementation, "keeping flash copy"));

    const std::string idf_sources = read_file(
        repo_root / "builds/esp_idf/ESP_IDF_COMPONENT_SOURCES.cmake");
    assert(contains(idf_sources, "platform/esp/idf_common/src/debug/sd_coredump_export.cpp"));

    const char* idf_targets[] = {
        "tab5",
        "tdeck",
        "t_display_p4_amoled",
        "t_display_p4_tft",
        "tlora_pager",
        "twatch",
    };
    for (const char* target : idf_targets)
    {
        const std::string defaults = read_file(
            repo_root / "builds/esp_idf/targets" / target / "sdkconfig.defaults");
        assert(contains(defaults, "CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=y"));
        assert(contains(defaults, "CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF=y"));
    }

    const std::string doc = read_file(
        repo_root / "docs/devices/esp32-sd-debug-coredump.md");
    assert(contains(doc, "ESP32 shared diagnostics"));
    assert(contains(doc, "SD-capable ESP32 product paths"));
    assert(contains(doc, "Pager"));
    assert(contains(doc, "T-Deck"));
    assert(contains(doc, "Tab5"));
    assert(contains(doc, "T-Display-P4"));
    assert(contains(doc, "next normal boot"));
    assert(contains(doc, "does not write FAT/exFAT files from the panic handler"));
    assert(contains(doc, "CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH"));
    assert(contains(doc, "/trailmate/coredumps/core-*.elf"));
    return 0;
}
