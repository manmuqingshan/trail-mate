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

std::size_t occurrences(const std::string& haystack, const char* needle)
{
    const std::string target = needle ? needle : "";
    if (target.empty())
    {
        return 0;
    }

    std::size_t count = 0;
    std::size_t offset = 0;
    while ((offset = haystack.find(target, offset)) != std::string::npos)
    {
        ++count;
        offset += target.size();
    }
    return count;
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
    const std::string wifi_runtime = readFile(
        repo_root /
        "platform/esp/common/include/platform/esp/common/wifi_runtime_impl.h");
    const std::string c6_wifi_runtime =
        readFile(repo_root / "firmware/c6_companion/components/tm_wifi/tm_wifi.c");
    const std::string team_pairing_transport = readFile(
        repo_root /
        "platform/esp/arduino_common/src/team/pairing/team_pairing_transport_espnow.cpp");
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

    assert(contains(wifi_runtime,
                    "esp_netif_get_handle_from_ifkey(\"WIFI_STA_DEF\")"));
    assert(contains(wifi_runtime, "profile_retry_timer"));
    assert(contains(wifi_runtime, "void schedule_profile_retry()"));
    assert(contains(wifi_runtime, "void profile_retry_timer_cb(void*)"));
    assert(contains(wifi_runtime, "schedule_profile_retry();"));
    assert(contains(wifi_runtime, "(void)connect(nullptr);"));
    assert(!contains(wifi_runtime, "network_time_sync_attempted"));
    assert(contains(wifi_runtime, "void finish_network_time_sync()"));
    assert(contains(wifi_runtime, "GOT_IP triggers one-shot SNTP"));
    assert(contains(wifi_runtime, "duplicate GOT_IP ignored while SNTP is in progress"));
    assert(occurrences(wifi_runtime, "finish_network_time_sync();") >= 5);

    assert(!contains(c6_wifi_runtime, "s_time_sync_attempted"));
    assert(contains(c6_wifi_runtime, "static void finish_network_time_sync(void)"));
    assert(contains(c6_wifi_runtime, "GOT_IP triggers one-shot SNTP"));
    assert(contains(c6_wifi_runtime, "Ignoring duplicate GOT_IP while SNTP is in progress"));
    assert(occurrences(c6_wifi_runtime, "finish_network_time_sync();") >= 5);
    assert(contains(team_pairing_transport,
                    "set_non_preemptible_activity(true, \"team_pairing\")"));
    assert(contains(team_pairing_transport, "set_non_preemptible_activity(false)"));

    assert(contains(mqtt_runtime, "wifi_retry_not_before_ms_"));
    assert(contains(
        mqtt_runtime,
        "if (deadlinePending(now_ms, wifi_retry_not_before_ms_))"));
    assert(contains(
        mqtt_runtime,
        "now_ms + connect_result.retry_after_ms;"));

    return 0;
}
