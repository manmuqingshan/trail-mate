#pragma once

#include <cstdint>

namespace ui::widgets::foreground_operation
{

enum class Slot : std::uint8_t
{
    FirmwareUpdate = 0,
    PackageInstall,
    I18nFontLoad,
    RouteImage,
    SettingsAction,
    ReticulumPing,
    Count,
};

enum class Policy : std::uint8_t
{
    Hidden = 0,
    PageOnly,
    Overlay,
};

enum class Priority : std::uint8_t
{
    Background = 0,
    Foreground,
    Blocking,
    Critical,
};

struct Snapshot
{
    bool active = false;
    Slot slot = Slot::Count;
    Policy policy = Policy::Hidden;
    Priority priority = Priority::Background;
    int progress_percent = -1;
    std::uint32_t generation = 0;
    std::uint32_t updated_ms = 0;
    char title[48] = {};
    char detail[96] = {};
    char result[96] = {};
};

Snapshot make_snapshot(Slot slot,
                       Policy policy,
                       Priority priority,
                       const char* title,
                       const char* detail = nullptr,
                       int progress_percent = -1,
                       const char* result = nullptr,
                       std::uint32_t generation = 0);

void publish(const Snapshot& snapshot);
void clear(Slot slot, std::uint32_t generation = 0);
void sync_overlay();
bool slot_active(Slot slot);
void reset_all();

} // namespace ui::widgets::foreground_operation
