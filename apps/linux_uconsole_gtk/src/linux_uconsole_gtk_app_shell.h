#pragma once

#include "product_composition/target_profile.h"

namespace trailmate
{
namespace apps
{
namespace linux_uconsole_gtk
{

struct LinuxUConsoleGtkAppShellConfig
{
    const char* target_id = "uconsole";
    const char* ux_pack_id = "uconsole_desktop";
};

class LinuxUConsoleGtkAppShell
{
  public:
    LinuxUConsoleGtkAppShell() = default;
    explicit LinuxUConsoleGtkAppShell(LinuxUConsoleGtkAppShellConfig config);

    const LinuxUConsoleGtkAppShellConfig& config() const;
    const char* targetId() const;
    const product_composition::TargetProfile* targetProfile() const;
    const char* activeUxPackId() const;
    bool validate() const;

  private:
    LinuxUConsoleGtkAppShellConfig config_{};
};

} // namespace linux_uconsole_gtk
} // namespace apps
} // namespace trailmate
