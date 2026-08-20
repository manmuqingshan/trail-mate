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

} // namespace

int main(int argc, char** argv)
{
    assert(argc == 2);

    const std::string source = read_file(
        argv[1],
        "platform/esp/arduino_common/src/platform_ui_settings_backup_runtime.cpp");

    // The default backup is a bounded line-oriented TMS document.  JSON only
    // remains as an explicit compatibility restore input; it must not be the
    // writer or normal restore path.
    assert(source.find("/trailmate/settings-backup.tms") != std::string::npos);
    assert(source.find("/trailmate/settings-backup.json") != std::string::npos);
    assert(source.find("tms::writeDocument(") != std::string::npos);
    assert(source.find("tms::DocumentKind::Backup") != std::string::npos);
    assert(source.find("write_extra_records") != std::string::npos);
    assert(source.find("checksum.crc32") != std::string::npos);
    assert(source.find("read_tms_backup") != std::string::npos);
    assert(source.find("restore_tms_backup") != std::string::npos);
    assert(source.find("if (storage_exists(kBackupPath))") != std::string::npos);
    assert(source.find("read_file_text(kLegacyBackupPath, text)") != std::string::npos);

    // The new ESP path must keep text/blob retrieval bounded and retain the
    // existing present-marker semantics so a backup clears stale destination
    // overrides rather than merely omitting them.
    assert(source.find("get_string_into") != std::string::npos);
    assert(source.find("s_extra_text_scratch") != std::string::npos);
    assert(source.find("s_extra_blob_scratch") != std::string::npos);
    assert(source.find("state->seen_present") != std::string::npos);
    assert(source.find("settings_store::remove_keys") != std::string::npos);

    // Keep the historic JSON codec isolated from the new default writer.
    const std::size_t backup_begin = source.find("bool backup()\n{");
    const std::size_t restore_begin = source.find("bool restore()\n{");
    assert(backup_begin != std::string::npos && restore_begin != std::string::npos);
    const std::string backup_body = source.substr(backup_begin, restore_begin - backup_begin);
    assert(backup_body.find("cJSON_") == std::string::npos);
    assert(backup_body.find("std::string") == std::string::npos);
    return 0;
}
