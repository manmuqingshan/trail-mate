#pragma once

#include "product_composition/target_profile.h"

namespace trailmate
{
namespace apps
{
namespace nrf52_node
{

struct Nrf52NodeAppShellConfig
{
    const char* target_id = "gat562_mesh_evb_pro";
    const char* target_family = "nrf52_node";
    const char* default_ux_pack_id = "tiny_node_status";
};

class Nrf52NodeAppShell
{
  public:
    const Nrf52NodeAppShellConfig& config() const;
    const char* targetId() const;
    const product_composition::TargetProfile* targetProfile() const;
    const char* activeUxPackId() const;
    bool validate() const;

  private:
    Nrf52NodeAppShellConfig config_{};
};

} // namespace nrf52_node
} // namespace apps
} // namespace trailmate
