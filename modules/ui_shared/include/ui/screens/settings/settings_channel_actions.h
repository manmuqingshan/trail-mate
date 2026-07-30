#pragma once

#include "app/app_config.h"
#include "ui/screens/settings/settings_state.h"
#include <cstddef>
#include <cstdint>

namespace settings::ui::channel
{

bool is_zero_key(const std::uint8_t* key, std::size_t len);
void bytes_to_hex(const std::uint8_t* data, std::size_t len, char* out, std::size_t out_len);
bool parse_psk(const char* text,
               std::uint8_t* out,
               std::size_t out_len,
               std::size_t* parsed_len = nullptr);
void meshtastic_key_to_text(const std::uint8_t* key,
                            std::size_t key_capacity,
                            std::uint8_t stored_len,
                            char* out,
                            std::size_t out_len);
bool parse_meshtastic_key_text(const char* text,
                               std::uint8_t* key,
                               std::size_t key_capacity,
                               std::uint8_t* out_key_len);

void sync_meshtastic_channel_fields(const app::AppConfig& config, SettingsData& settings);
void sync_meshcore_channel_fields(const app::AppConfig& config, SettingsData& settings);
bool generate_meshtastic_channel_key(app::AppConfig& config,
                                     SettingsData& settings,
                                     bool primary);
bool generate_meshcore_channel_key(app::AppConfig& config, SettingsData& settings);

} // namespace settings::ui::channel
