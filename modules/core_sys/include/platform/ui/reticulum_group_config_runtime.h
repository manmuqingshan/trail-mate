#pragma once

#include <cstddef>
#include <cstdint>

#include "chat/domain/chat_types.h"

namespace platform::ui::reticulum_groups
{

struct Status
{
    bool supported = false;
    bool sd_present = false;
    bool file_present = false;
    bool loaded = false;
    bool saved = false;
    bool queued = false;
    char message[96] = {};
    char detail[128] = {};
};

// The Arduino implementation reads the pre-TMS groups.tsv format only while
// migrating an old installation. It never participates in normal runtime
// configuration ownership or writes the legacy file.
enum class LegacyImportResult : uint8_t
{
    NotPresent,
    Imported,
    Unavailable,
    Invalid,
};

const char* config_path();
void clear(chat::ReticulumGroupDestinationConfig* groups, std::size_t group_count);
Status load(chat::ReticulumGroupDestinationConfig* groups, std::size_t group_count);
Status submit(const chat::ReticulumGroupDestinationConfig* groups,
              std::size_t group_count);
Status flushPending();
bool hasPending();
Status save(const chat::ReticulumGroupDestinationConfig* groups, std::size_t group_count);

LegacyImportResult importLegacy(
    chat::ReticulumGroupDestinationConfig* groups,
    std::size_t group_count);
bool discardLegacySource();

} // namespace platform::ui::reticulum_groups
