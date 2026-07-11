#pragma once

#include "chat/domain/chat_types.h"
#include "ui/screens/settings/settings_state.h"
#include <cstddef>
#include <cstdint>

namespace settings::ui::spec
{

enum class DynamicOptionKind : std::uint8_t
{
    None,
    ChatRegion,
    MeshCoreRegionPreset,
    Locale,
    TimeZone,
    WifiNetwork,
    TxPower,
};

struct VisibilityContext
{
    chat::MeshProtocol protocol = chat::MeshProtocol::Meshtastic;
    bool wifi_supported = false;
    bool has_wifi_networks = false;
    bool firmware_update_supported = false;
    bool settings_backup_supported = false;
    bool wireless_companion_supported = false;
    bool mt_secondary_enabled = false;
    bool mt_use_preset = false;
    bool reticulum_wifi_visible = false;
    bool reticulum_lora_visible = false;
    bool screen_brightness_supported = false;
    bool screen_timeout_supported = false;
    bool gps_baud_supported = false;
    bool gps_init_policy_supported = false;
    bool gps_gnss_supported = false;
    bool gps_interval_supported = false;
    bool gps_alt_ref_supported = false;
    bool gps_coord_format_supported = false;
    bool external_nmea_supported = false;
    bool battery_gauge_supported = false;
};

SettingId id_for_key(const char* pref_key);
const char* key_for_id(SettingId id);

void bind_item(SettingItem& item);
void bind_items(SettingItem* items, std::size_t count);

DynamicOptionKind dynamic_option_kind(SettingId id);
bool option_labels_are_translated(SettingId id);
bool option_labels_use_content_font(SettingId id);
bool is_settings_store_owned_enum(SettingId id);
bool is_settings_store_owned_toggle(SettingId id);
bool should_show(SettingId id, const VisibilityContext& context);

} // namespace settings::ui::spec
