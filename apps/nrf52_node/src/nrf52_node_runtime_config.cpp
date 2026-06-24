#include "nrf52_node_runtime_config.h"

namespace trailmate::apps::nrf52_node
{
namespace
{

#ifndef TRAILMATE_TARGET_ID
#define TRAILMATE_TARGET_ID "gat562_mesh_evb_pro"
#endif

#if defined(TRAIL_MATE_LORA_TX_POWER_MAX_DBM)
constexpr std::int8_t kTargetLoraTxPowerMaxDbm =
    TRAIL_MATE_LORA_TX_POWER_MAX_DBM;
#else
constexpr std::int8_t kTargetLoraTxPowerMaxDbm = 0;
#endif

const Nrf52NodeRuntimeConfig kRuntimeConfig{
    TRAILMATE_TARGET_ID,
    product_composition::findTargetProfile(TRAILMATE_TARGET_ID),
    kTargetLoraTxPowerMaxDbm,
    true,
};

} // namespace

const Nrf52NodeRuntimeConfig& runtimeConfig()
{
    return kRuntimeConfig;
}

} // namespace trailmate::apps::nrf52_node
