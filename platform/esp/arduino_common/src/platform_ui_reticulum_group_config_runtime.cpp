#include "platform/ui/reticulum_group_config_runtime.h"

#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
#include "platform/ui/device_runtime.h"

#include <cstdio>
#include <cstring>

namespace platform::ui::reticulum_groups
{
namespace
{

using ::platform::esp::arduino_common::storage::SdRuntimeFile;

constexpr const char* kWorkingConfigPath = "/trailmate/config.tms";
constexpr const char* kLegacyConfigPath = "/trailmate/reticulum/groups.tsv";
constexpr const char* kLegacyTempPath = "/trailmate/reticulum/groups.tmp";
constexpr std::size_t kLegacyMaxFileBytes = 2048U;
constexpr std::size_t kLegacyLineBytes = 384U;

// Migration runs before application tasks begin. Keep its temporary state in
// static storage, rather than placing a group array or a line buffer on the
// ESP task stack.
char s_legacy_line[kLegacyLineBytes] = {};
chat::ReticulumGroupDestinationConfig
    s_imported_groups[chat::kReticulumGroupDestinationMaxCount] = {};

void copy_text(char* out, std::size_t out_len, const char* text)
{
    if (!out || out_len == 0U)
    {
        return;
    }
    std::snprintf(out, out_len, "%s", text ? text : "");
}

void set_status(Status& out, const char* message, const char* detail = kWorkingConfigPath)
{
    copy_text(out.message, sizeof(out.message), message);
    copy_text(out.detail, sizeof(out.detail), detail);
}

bool sd_available()
{
    return ::platform::ui::device::card_ready() &&
           ::platform::esp::arduino_common::storage::sd_card_ready();
}

char* trim_line(char* line)
{
    if (!line)
    {
        return nullptr;
    }
    std::size_t length = std::strlen(line);
    while (length > 0U &&
           (line[length - 1U] == '\r' || line[length - 1U] == ' ' ||
            line[length - 1U] == '\t'))
    {
        line[--length] = '\0';
    }
    while (*line == ' ' || *line == '\t')
    {
        ++line;
    }
    return line;
}

bool parse_enabled(const char* value, bool* enabled)
{
    if (!value || !enabled)
    {
        return false;
    }
    if (std::strcmp(value, "1") == 0 || std::strcmp(value, "true") == 0 ||
        std::strcmp(value, "yes") == 0 || std::strcmp(value, "enabled") == 0)
    {
        *enabled = true;
        return true;
    }
    if (std::strcmp(value, "0") == 0 || std::strcmp(value, "false") == 0 ||
        std::strcmp(value, "no") == 0 || std::strcmp(value, "disabled") == 0)
    {
        *enabled = false;
        return true;
    }
    return false;
}

bool parse_group_line(char* line, chat::ReticulumGroupDestinationConfig* out)
{
    if (!line || !out)
    {
        return false;
    }
    char* const first_tab = std::strchr(line, '\t');
    if (!first_tab)
    {
        return false;
    }
    *first_tab = '\0';
    char* const name = first_tab + 1U;
    char* const second_tab = std::strchr(name, '\t');
    if (!second_tab || std::strchr(second_tab + 1U, '\t'))
    {
        return false;
    }
    *second_tab = '\0';
    const char* const destination = second_tab + 1U;
    if (destination[0] == '\0' || std::strlen(name) >= sizeof(out->name))
    {
        return false;
    }

    chat::ReticulumGroupDestinationConfig parsed{};
    if (!parse_enabled(line, &parsed.enabled) ||
        !chat::parseReticulumDestinationHashText(destination,
                                                 &parsed.identity,
                                                 nullptr,
                                                 0U))
    {
        return false;
    }
    std::snprintf(parsed.name, sizeof(parsed.name), "%s", name);
    *out = parsed;
    return true;
}

bool consume_legacy_line(std::size_t* slot)
{
    if (!slot)
    {
        return false;
    }
    char* const line = trim_line(s_legacy_line);
    if (!line || line[0] == '\0' || line[0] == '#')
    {
        return true;
    }
    if (std::strcmp(line, "version\t1") == 0)
    {
        return true;
    }
    if (*slot >= chat::kReticulumGroupDestinationMaxCount ||
        !parse_group_line(line, &s_imported_groups[*slot]))
    {
        return false;
    }
    ++(*slot);
    return true;
}

Status direct_storage_disabled()
{
    Status out{};
    out.supported = false;
    out.sd_present = sd_available();
    set_status(out, "Reticulum groups are stored in config.tms");
    return out;
}

} // namespace

const char* config_path()
{
    return kWorkingConfigPath;
}

void clear(chat::ReticulumGroupDestinationConfig* groups, std::size_t group_count)
{
    if (!groups)
    {
        return;
    }
    for (std::size_t index = 0U; index < group_count; ++index)
    {
        groups[index] = chat::ReticulumGroupDestinationConfig{};
    }
}

Status load(chat::ReticulumGroupDestinationConfig*, std::size_t)
{
    return direct_storage_disabled();
}

Status submit(const chat::ReticulumGroupDestinationConfig*, std::size_t)
{
    return direct_storage_disabled();
}

Status flushPending()
{
    return direct_storage_disabled();
}

bool hasPending()
{
    return false;
}

Status save(const chat::ReticulumGroupDestinationConfig*, std::size_t)
{
    return direct_storage_disabled();
}

LegacyImportResult importLegacy(chat::ReticulumGroupDestinationConfig* groups,
                                std::size_t group_count)
{
    if (!groups || group_count != chat::kReticulumGroupDestinationMaxCount)
    {
        return LegacyImportResult::Invalid;
    }
    if (!sd_available())
    {
        return LegacyImportResult::Unavailable;
    }
    if (!::platform::esp::arduino_common::storage::sd_exists(kLegacyConfigPath))
    {
        return LegacyImportResult::NotPresent;
    }

    SdRuntimeFile file;
    if (!file.open(kLegacyConfigPath, "r"))
    {
        return LegacyImportResult::Invalid;
    }
    const uint64_t size = file.size();
    if (size == 0U || size > kLegacyMaxFileBytes)
    {
        file.close();
        return LegacyImportResult::Invalid;
    }

    clear(s_imported_groups, chat::kReticulumGroupDestinationMaxCount);
    std::size_t slot = 0U;
    std::size_t length = 0U;
    bool valid = true;
    for (uint64_t offset = 0U; valid && offset < size; ++offset)
    {
        const int raw = file.read_byte();
        if (raw < 0)
        {
            valid = false;
            break;
        }
        if (static_cast<char>(raw) == '\n')
        {
            s_legacy_line[length] = '\0';
            valid = consume_legacy_line(&slot);
            length = 0U;
            continue;
        }
        if (length + 1U >= sizeof(s_legacy_line))
        {
            valid = false;
            break;
        }
        s_legacy_line[length++] = static_cast<char>(raw);
    }
    if (valid && length > 0U)
    {
        s_legacy_line[length] = '\0';
        valid = consume_legacy_line(&slot);
    }
    file.close();
    if (!valid)
    {
        return LegacyImportResult::Invalid;
    }

    clear(groups, group_count);
    for (std::size_t index = 0U; index < slot; ++index)
    {
        groups[index] = s_imported_groups[index];
    }
    return LegacyImportResult::Imported;
}

bool discardLegacySource()
{
    if (!sd_available())
    {
        return false;
    }
    const bool removed_config =
        !::platform::esp::arduino_common::storage::sd_exists(kLegacyConfigPath) ||
        ::platform::esp::arduino_common::storage::sd_remove(kLegacyConfigPath);
    const bool removed_temp =
        !::platform::esp::arduino_common::storage::sd_exists(kLegacyTempPath) ||
        ::platform::esp::arduino_common::storage::sd_remove(kLegacyTempPath);
    return removed_config && removed_temp;
}

} // namespace platform::ui::reticulum_groups
