#include "platform/ui/reticulum_group_config_runtime.h"

#include "platform/esp/idf_common/bsp_runtime.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>

namespace platform::ui::reticulum_groups
{
namespace
{

constexpr const char* kConfigDir = "/trailmate/reticulum";
constexpr const char* kConfigPath = "/trailmate/reticulum/groups.tsv";
constexpr const char* kConfigTempPath = "/trailmate/reticulum/groups.tmp";
constexpr std::size_t kMaxConfigBytes = 2048;

void copy_text(char* out, std::size_t out_len, const char* text)
{
    if (!out || out_len == 0)
    {
        return;
    }
    std::snprintf(out, out_len, "%s", text ? text : "");
}

void set_status(Status& out, const char* message, const char* detail = nullptr)
{
    copy_text(out.message, sizeof(out.message), message);
    copy_text(out.detail, sizeof(out.detail), detail);
}

std::string mount_path_for(const char* logical_path)
{
    return std::string(::platform::esp::idf_common::bsp_runtime::sdcard_mount_point()) +
           (logical_path ? logical_path : "");
}

bool path_exists(const char* logical_path)
{
    struct stat st
    {
    };
    return ::stat(mount_path_for(logical_path).c_str(), &st) == 0;
}

bool is_directory(const char* logical_path)
{
    struct stat st
    {
    };
    return ::stat(mount_path_for(logical_path).c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool ensure_config_dir()
{
    const std::string root = mount_path_for("/trailmate");
    const std::string reticulum = mount_path_for(kConfigDir);
    struct stat st
    {
    };
    if (::stat(root.c_str(), &st) != 0)
    {
        if (::mkdir(root.c_str(), 0775) != 0)
        {
            return false;
        }
    }
    if (::stat(reticulum.c_str(), &st) == 0 && S_ISDIR(st.st_mode))
    {
        return true;
    }
    return ::mkdir(reticulum.c_str(), 0775) == 0 || is_directory(kConfigDir);
}

bool enabled_text(const std::string& value)
{
    return value == "1" || value == "true" || value == "yes" || value == "enabled";
}

std::string trim_line(std::string line)
{
    while (!line.empty() &&
           (line.back() == '\r' || line.back() == '\n' ||
            line.back() == ' ' || line.back() == '\t'))
    {
        line.pop_back();
    }
    std::size_t start = 0;
    while (start < line.size() && (line[start] == ' ' || line[start] == '\t'))
    {
        ++start;
    }
    return start == 0 ? line : line.substr(start);
}

bool parse_group_line(const std::string& line,
                      chat::ReticulumGroupDestinationConfig& out,
                      char* error,
                      std::size_t error_len)
{
    const std::size_t first_tab = line.find('\t');
    const std::size_t second_tab =
        first_tab == std::string::npos ? std::string::npos : line.find('\t', first_tab + 1);
    if (first_tab == std::string::npos || second_tab == std::string::npos)
    {
        copy_text(error, error_len, "Invalid group config line");
        return false;
    }

    const std::string destination = line.substr(second_tab + 1);

    out = chat::ReticulumGroupDestinationConfig{};
    if (!chat::parseReticulumDestinationHashText(destination.c_str(),
                                                 &out.identity,
                                                 error,
                                                 error_len))
    {
        return false;
    }
    out.enabled = enabled_text(line.substr(0, first_tab));
    std::snprintf(out.name,
                  sizeof(out.name),
                  "%s",
                  line.substr(first_tab + 1, second_tab - first_tab - 1).c_str());
    return true;
}

bool read_config_text(std::string& out)
{
    out.clear();
    const std::string path = mount_path_for(kConfigPath);
    FILE* file = std::fopen(path.c_str(), "rb");
    if (!file)
    {
        return false;
    }
    if (std::fseek(file, 0, SEEK_END) != 0)
    {
        std::fclose(file);
        return false;
    }
    const long size = std::ftell(file);
    if (size <= 0 || static_cast<std::size_t>(size) > kMaxConfigBytes)
    {
        std::fclose(file);
        return false;
    }
    std::rewind(file);
    out.resize(static_cast<std::size_t>(size));
    const std::size_t read = std::fread(&out[0], 1, out.size(), file);
    std::fclose(file);
    if (read != out.size())
    {
        out.clear();
        return false;
    }
    return true;
}

bool write_text_atomic(const std::string& text)
{
    const std::string temp_path = mount_path_for(kConfigTempPath);
    const std::string final_path = mount_path_for(kConfigPath);
    std::remove(temp_path.c_str());

    FILE* file = std::fopen(temp_path.c_str(), "wb");
    if (!file)
    {
        return false;
    }
    const bool wrote = std::fwrite(text.data(), 1, text.size(), file) == text.size();
    std::fclose(file);
    if (!wrote)
    {
        std::remove(temp_path.c_str());
        return false;
    }
    std::remove(final_path.c_str());
    if (std::rename(temp_path.c_str(), final_path.c_str()) != 0)
    {
        std::remove(temp_path.c_str());
        return false;
    }
    return true;
}

} // namespace

const char* config_path()
{
    return kConfigPath;
}

void clear(chat::ReticulumGroupDestinationConfig* groups, std::size_t group_count)
{
    if (!groups)
    {
        return;
    }
    for (std::size_t index = 0; index < group_count; ++index)
    {
        groups[index] = chat::ReticulumGroupDestinationConfig{};
    }
}

Status load(chat::ReticulumGroupDestinationConfig* groups, std::size_t group_count)
{
    Status out{};
    out.supported = ::platform::esp::idf_common::bsp_runtime::sdcard_capable();
    out.sd_present = ::platform::esp::idf_common::bsp_runtime::ensure_sdcard_ready();
    if (!groups || group_count == 0)
    {
        set_status(out, "Group storage unavailable", kConfigPath);
        return out;
    }

    clear(groups, group_count);
    if (!out.supported || !out.sd_present)
    {
        set_status(out, "SD card required", kConfigPath);
        return out;
    }

    out.file_present = path_exists(kConfigPath);
    if (!out.file_present)
    {
        out.loaded = true;
        set_status(out, "No Reticulum groups", kConfigPath);
        return out;
    }

    std::string text;
    if (!read_config_text(text))
    {
        set_status(out, "Cannot read Reticulum groups", kConfigPath);
        return out;
    }

    std::size_t slot = 0;
    std::size_t line_start = 0;
    while (line_start <= text.size() && slot < group_count)
    {
        const std::size_t line_end = text.find('\n', line_start);
        std::string line = trim_line(text.substr(
            line_start,
            line_end == std::string::npos ? std::string::npos : line_end - line_start));
        if (!line.empty() && line[0] != '#' && line.rfind("version\t", 0) != 0)
        {
            char error[96] = {};
            chat::ReticulumGroupDestinationConfig parsed{};
            if (parse_group_line(line, parsed, error, sizeof(error)))
            {
                groups[slot++] = parsed;
            }
            else
            {
                std::printf("[RTGroupConfig] skip invalid line: %s\n", error);
            }
        }
        if (line_end == std::string::npos)
        {
            break;
        }
        line_start = line_end + 1;
    }

    out.loaded = true;
    set_status(out, slot == 0 ? "No Reticulum groups" : "Reticulum groups loaded", kConfigPath);
    return out;
}

Status save(const chat::ReticulumGroupDestinationConfig* groups, std::size_t group_count)
{
    Status out{};
    out.supported = ::platform::esp::idf_common::bsp_runtime::sdcard_capable();
    out.sd_present = ::platform::esp::idf_common::bsp_runtime::ensure_sdcard_ready();
    if (!groups || group_count == 0)
    {
        set_status(out, "Group storage unavailable", kConfigPath);
        return out;
    }
    if (!out.supported || !out.sd_present)
    {
        set_status(out, "SD card required", kConfigPath);
        return out;
    }
    if (!ensure_config_dir())
    {
        set_status(out, "Cannot create group directory", kConfigDir);
        return out;
    }

    std::string text;
    text.reserve(384);
    text += "# Trail Mate Reticulum groups\n";
    text += "version\t1\n";
    for (std::size_t index = 0; index < group_count; ++index)
    {
        const auto& group = groups[index];
        if (!chat::hasReticulumDestinationIdentity(group.identity))
        {
            continue;
        }
        char hash[chat::kReticulumPeerHashSize * 2 + 1] = {};
        chat::formatReticulumDestinationHashText(group.identity, hash, sizeof(hash));
        text += group.enabled ? "1\t" : "0\t";
        text += group.name[0] != '\0' ? group.name : "Group";
        text += "\t";
        text += hash;
        text += "\n";
    }

    if (!write_text_atomic(text))
    {
        set_status(out, "Cannot write Reticulum groups", kConfigPath);
        return out;
    }
    out.file_present = true;
    out.saved = true;
    set_status(out, "Reticulum groups saved", kConfigPath);
    return out;
}

} // namespace platform::ui::reticulum_groups
