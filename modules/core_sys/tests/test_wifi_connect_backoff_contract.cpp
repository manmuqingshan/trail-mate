#include <cassert>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace
{

std::string readFile(const std::filesystem::path& path)
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

} // namespace

int main(int argc, char** argv)
{
    assert(argc == 2);
    const std::filesystem::path repo_root = argv[1];
    const std::string contract = readFile(
        repo_root /
        "modules/core_sys/include/platform/ui/wifi_access_runtime.h");
    const std::string access_runtime = readFile(
        repo_root /
        "platform/esp/arduino_common/src/platform_ui_wifi_access_runtime.cpp");
    const std::string mqtt_runtime = readFile(
        repo_root /
        "platform/esp/arduino_common/src/chat/infra/mesh_mqtt_client_runtime.cpp");

    assert(contains(contract, "struct ConnectResult"));
    assert(contains(contract, "std::uint32_t retry_after_ms = 0;"));
    assert(contains(access_runtime, "struct ConnectAttemptWindow"));
    assert(contains(access_runtime,
                    "window.retry_after_ms = kConnectBackoffMs - age_ms;"));
    assert(contains(access_runtime,
                    "if (decision != Decision::Granted && log_denial)"));

    assert(contains(mqtt_runtime, "wifi_retry_not_before_ms_"));
    assert(contains(
        mqtt_runtime,
        "if (deadlinePending(now_ms, wifi_retry_not_before_ms_))"));
    assert(contains(
        mqtt_runtime,
        "now_ms + connect_result.retry_after_ms;"));

    return 0;
}
