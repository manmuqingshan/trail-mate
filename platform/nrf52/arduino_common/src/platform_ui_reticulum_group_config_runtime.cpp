#include "platform/ui/reticulum_group_config_runtime.h"

#include <cstdio>
#include <cstring>

namespace platform::ui::reticulum_groups
{
namespace
{

constexpr const char* kConfigPath = "/trailmate/reticulum/groups.tsv";

void copy_text(char* out, std::size_t out_len, const char* text)
{
    if (!out || out_len == 0)
    {
        return;
    }
    std::snprintf(out, out_len, "%s", text ? text : "");
}

void set_status(Status& out, const char* message)
{
    copy_text(out.message, sizeof(out.message), message);
    copy_text(out.detail, sizeof(out.detail), kConfigPath);
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
    clear(groups, group_count);
    Status out{};
    out.supported = false;
    set_status(out, "Reticulum group SD config unsupported");
    return out;
}

Status save(const chat::ReticulumGroupDestinationConfig*, std::size_t)
{
    Status out{};
    out.supported = false;
    set_status(out, "Reticulum group SD config unsupported");
    return out;
}

} // namespace platform::ui::reticulum_groups
