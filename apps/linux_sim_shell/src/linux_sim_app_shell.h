#pragma once

#include "product_composition/target_profile.h"

namespace trailmate
{
namespace apps
{
namespace linux_sim_shell
{

struct LinuxSimAppShellConfig
{
    const char* target_id = "linux_sim";
    const char* ux_pack_id = "simulator_full";
};

class LinuxSimAppShell
{
  public:
    LinuxSimAppShell() = default;
    explicit LinuxSimAppShell(LinuxSimAppShellConfig config);

    const LinuxSimAppShellConfig& config() const;
    const char* targetId() const;
    const product_composition::TargetProfile* targetProfile() const;
    const char* activeUxPackId() const;
    bool validate() const;

  private:
    LinuxSimAppShellConfig config_{};
};

} // namespace linux_sim_shell
} // namespace apps
} // namespace trailmate
