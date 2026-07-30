/**
 * @file reticulum_network_config_runtime.h
 * @brief Active Reticulum network configuration snapshot
 */

#pragma once

#include "chat/domain/chat_types.h"
#include "chat/domain/reticulum_network_config.h"

#include <cstddef>
#include <cstdint>

namespace platform::ui::reticulum_network_config
{

enum class Source : uint8_t
{
    Defaults = 0,
    LastKnownGood = 1,
    SdCard = 2,
};

struct Status
{
    bool supported = false;
    bool sd_present = false;
    bool file_present = false;
    bool valid = false;
    bool reload_deferred = false;
    Source source = Source::Defaults;
    uint32_t generation = 0;
    uint8_t configured_interfaces = 0;
    char message[64] = {};
    char detail[96] = {};
};

void initialize(const chat::MeshConfig& legacy_config);
void poll(const chat::MeshConfig& legacy_config);
const chat::reticulum::ReticulumNetworkConfig& active();
Status status();
bool reload(const chat::MeshConfig& legacy_config);
bool export_template(const chat::MeshConfig& legacy_config);
const char* config_path();
const char* source_name(Source source);

} // namespace platform::ui::reticulum_network_config
