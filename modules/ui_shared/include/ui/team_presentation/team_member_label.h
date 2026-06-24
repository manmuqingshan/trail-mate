#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

namespace ui
{
namespace team_presentation
{

inline std::string shortTeamMemberLabel(uint32_t node_id)
{
    char label[5]{};
    std::snprintf(label,
                  sizeof(label),
                  "%04lX",
                  static_cast<unsigned long>(node_id & 0xFFFFU));
    return std::string(label);
}

} // namespace team_presentation
} // namespace ui
