#include "platform/ui/reticulum_group_config_runtime.h"

#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
#include "platform/ui/device_runtime.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace platform::ui::reticulum_groups
{
namespace
{

using ::platform::esp::arduino_common::storage::SdRuntimeFile;

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

bool sd_available()
{
    return ::platform::ui::device::card_ready() &&
           ::platform::esp::arduino_common::storage::sd_card_ready();
}

bool ensure_config_dir()
{
    if (::platform::esp::arduino_common::storage::sd_exists(kConfigDir))
    {
        return ::platform::esp::arduino_common::storage::sd_is_directory(kConfigDir);
    }
    return ::platform::esp::arduino_common::storage::sd_mkdir(kConfigDir);
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

    const std::string enabled = line.substr(0, first_tab);
    const std::string name = line.substr(first_tab + 1, second_tab - first_tab - 1);
    const std::string destination = line.substr(second_tab + 1);

    out = chat::ReticulumGroupDestinationConfig{};
    if (!chat::parseReticulumDestinationHashText(destination.c_str(),
                                                 &out.identity,
                                                 error,
                                                 error_len))
    {
        return false;
    }
    out.enabled = enabled_text(enabled);
    std::snprintf(out.name, sizeof(out.name), "%s", name.c_str());
    return true;
}

bool read_config_text(std::string& out)
{
    out.clear();
    SdRuntimeFile file;
    if (!file.open(kConfigPath, "r"))
    {
        return false;
    }
    const std::size_t size = static_cast<std::size_t>(file.size());
    if (size == 0 || size > kMaxConfigBytes)
    {
        file.close();
        return false;
    }
    out.resize(size);
    const std::size_t read = file.read_bytes(&out[0], size);
    file.close();
    if (read != size)
    {
        out.clear();
        return false;
    }
    return true;
}

bool write_text_atomic(const std::string& text)
{
    if (::platform::esp::arduino_common::storage::sd_exists(kConfigTempPath))
    {
        ::platform::esp::arduino_common::storage::sd_remove(kConfigTempPath);
    }

    SdRuntimeFile file;
    if (!file.open(kConfigTempPath, "w"))
    {
        return false;
    }
    const bool wrote = file.write(text.data(), text.size()) == text.size();
    file.close();
    if (!wrote)
    {
        ::platform::esp::arduino_common::storage::sd_remove(kConfigTempPath);
        return false;
    }

    if (::platform::esp::arduino_common::storage::sd_exists(kConfigPath))
    {
        ::platform::esp::arduino_common::storage::sd_remove(kConfigPath);
    }
    if (!::platform::esp::arduino_common::storage::sd_rename(kConfigTempPath, kConfigPath))
    {
        ::platform::esp::arduino_common::storage::sd_remove(kConfigTempPath);
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
    out.supported = true;
    out.sd_present = sd_available();
    if (!groups || group_count == 0)
    {
        set_status(out, "Group storage unavailable", kConfigPath);
        return out;
    }

    clear(groups, group_count);
    if (!out.sd_present)
    {
        set_status(out, "SD card required", kConfigPath);
        return out;
    }

    out.file_present = ::platform::esp::arduino_common::storage::sd_exists(kConfigPath);
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
        std::string line = text.substr(
            line_start,
            line_end == std::string::npos ? std::string::npos : line_end - line_start);
        line = trim_line(line);
        if (!line.empty() && line[0] != '#')
        {
            if (line.rfind("version\t", 0) != 0)
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
    out.supported = true;
    out.sd_present = sd_available();
    if (!groups || group_count == 0)
    {
        set_status(out, "Group storage unavailable", kConfigPath);
        return out;
    }
    if (!out.sd_present)
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
