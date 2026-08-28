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

std::string function_body(const std::string& source, const char* signature)
{
    const std::size_t begin = source.find(signature);
    assert(begin != std::string::npos);
    const std::size_t end = source.find("\n}\n", begin);
    assert(end != std::string::npos);
    return source.substr(begin, end - begin + 3U);
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

    const std::string header = read_file(
        repo_root /
        "platform/esp/arduino_common/include/platform/esp/arduino_common/app_config_sd_tms_runtime.h");
    assert(contains(header, "enum class WorkingConfigSyncResult"));
    assert(contains(header, "syncPendingWorkingConfig()"));

    const std::string sd_runtime = read_file(
        repo_root / "platform/esp/arduino_common/src/app_config_sd_tms_runtime.cpp");
    const std::string sync_pending =
        function_body(sd_runtime, "WorkingConfigSyncResult syncPendingWorkingConfig()");
    assert(contains(sync_pending, "if (!s_sync_pending)"));
    assert(contains(sync_pending, "if (!sd_available())"));
    assert(contains(sync_pending, "syncWorkingConfig(*s_working_config)"));
    assert(contains(sync_pending, "s_sync_pending = false"));
    const std::string service = function_body(sd_runtime, "void serviceWorkingConfig()");
    assert(contains(service, "syncPendingWorkingConfig()"));

    const std::string time_runtime = read_file(
        repo_root / "modules/core_sys/include/platform/ui/time_runtime.h");
    assert(contains(time_runtime, "enum class TimezoneProfilePersistenceResult"));
    assert(contains(time_runtime, "set_timezone_profile_id_and_persist(int profile_id)"));

    const std::string esp_time_runtime = read_file(
        repo_root / "platform/esp/arduino_common/src/platform_ui_time_runtime.cpp");
    assert(contains(esp_time_runtime, "app_config_sd_tms_runtime.h"));
    const std::string persist_timezone = function_body(
        esp_time_runtime,
        "TimezoneProfilePersistenceResult set_timezone_profile_id_and_persist(int profile_id)");
    assert(contains(persist_timezone, "requestWorkingConfigSync()"));
    assert(contains(persist_timezone, "syncPendingWorkingConfig()"));
    assert(contains(persist_timezone, "TimezoneProfilePersistenceResult::Deferred"));
    assert(contains(persist_timezone, "set_timezone_profile_id(previous_profile_id)"));

    const std::string settings = read_file(
        repo_root / "modules/ui_shared/src/ui/screens/settings/settings_page_components.cpp");
    assert(!contains(settings, "app_config_sd_tms_runtime.h"));
    const std::string timezone_branch = settings.substr(
        settings.find("if (id == settings::ui::SettingId::TimezoneProfile)"),
        settings.find("modal_close();", settings.find("if (id == settings::ui::SettingId::TimezoneProfile)")) -
            settings.find("if (id == settings::ui::SettingId::TimezoneProfile)"));
    assert(contains(timezone_branch, "set_timezone_profile_id_and_persist(payload->value)"));
    assert(contains(timezone_branch, "TimezoneProfilePersistenceResult::Failed"));
    assert(!contains(timezone_branch, "requestWorkingConfigSync()"));
    assert(!contains(timezone_branch, "syncPendingWorkingConfig()"));
    assert(contains(timezone_branch, "rebuild_active_app = true"));
    assert(!contains(timezone_branch, "restart_now = true"));
    assert(!contains(timezone_branch, "platform_restart()"));
    return 0;
}
