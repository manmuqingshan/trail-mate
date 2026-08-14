#include <cassert>
#include <fstream>
#include <iterator>
#include <string>

namespace
{

std::string read_file(const char* root, const char* relative_path)
{
    std::string path(root);
    path += "/";
    path += relative_path;

    std::ifstream file(path, std::ios::binary);
    assert(file.is_open());
    return std::string((std::istreambuf_iterator<char>(file)),
                       std::istreambuf_iterator<char>());
}

std::size_t occurrence_count(const std::string& text, const char* needle)
{
    std::size_t count = 0;
    std::size_t offset = 0;
    while ((offset = text.find(needle, offset)) != std::string::npos)
    {
        ++count;
        offset += std::string(needle).size();
    }
    return count;
}

void assert_round_tripped(const std::string& source, const char* key)
{
    assert(occurrence_count(source, key) >= 2U);
}

} // namespace

int main(int argc, char** argv)
{
    assert(argc == 2);

    const std::string source = read_file(
        argv[1],
        "platform/esp/arduino_common/src/platform_ui_settings_backup_runtime.cpp");

    // Version 2 is the first backup schema that explicitly covers all of the
    // persistent AppConfig channel presentation and Reticulum policy fields.
    assert(source.find("constexpr int kBackupVersion = 2;") != std::string::npos);

    const char* const kChannelPresentationKeys[] = {
        "primary_channel_has_module_settings",
        "primary_channel_position_precision",
        "primary_channel_is_muted",
        "secondary_channel_has_module_settings",
        "secondary_channel_position_precision",
        "secondary_channel_is_muted",
    };
    for (const char* key : kChannelPresentationKeys)
    {
        assert_round_tripped(source, key);
    }

    assert_round_tripped(source, "reticulum_allow_location_requests");
    assert_round_tripped(source, "reticulum_groups");
    assert(source.find("{\"settings\", \"chat_auto_reply_text\", \"chat_auto_txt\", ValueType::String}") !=
           std::string::npos);

    // An absent source-side NVS value represents its code-defined default.
    // Schema v2 records that state and clears a stale destination override;
    // otherwise a restore would not produce the same effective settings.
    assert(source.find("add_bool(value_object, \"present\", present);") != std::string::npos);
    assert(source.find("if (cJSON_IsFalse(present))") != std::string::npos);
    assert(source.find("::platform::ui::settings_store::remove_keys(key.ns, keys, 1);") !=
           std::string::npos);

    // Reticulum groups are owned by their own SD storage service. Restore
    // must use that owner and wait for its physical write before reporting
    // success, rather than only updating AppConfig's runtime mirror.
    assert(source.find("::platform::ui::reticulum_groups::submit(") !=
           std::string::npos);
    assert(source.find("::platform::ui::reticulum_groups::flushPending()") !=
           std::string::npos);
    assert(source.find("restore_extra_settings(root);\n    cJSON_Delete(root);\n    edit.commit") !=
           std::string::npos);
    return 0;
}
