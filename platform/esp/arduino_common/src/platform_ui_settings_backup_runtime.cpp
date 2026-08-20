#include "platform/ui/settings_backup_runtime.h"

#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
#if defined(ARDUINO)
#include <Arduino.h>
#include <Preferences.h>
#else
#include "esp_timer.h"
#include "nvs.h"
#include "platform/esp/idf_common/bsp_runtime.h"
#endif

#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>

#include "app/app_config.h"
#include "app/app_facade_access.h"
#include "app/tms_config_codec.h"
#if defined(ARDUINO)
#include "cJSON.h"
#else
#include "cJSON.h"
#endif
#include "chat/domain/chat_types.h"
#include "chat/infra/mesh_protocol_utils.h"
#include "platform/ui/device_runtime.h"
#include "platform/ui/reticulum_group_config_runtime.h"
#include "platform/ui/settings_store.h"

namespace platform::ui::settings_backup
{
namespace
{

namespace tms = ::app::tms;

constexpr const char* kBackupDir = "/trailmate";
constexpr const char* kBackupPath = "/trailmate/settings-backup.tms";
constexpr const char* kBackupTempPath = "/trailmate/settings-backup.tms.tmp";
constexpr const char* kLegacyBackupPath = "/trailmate/settings-backup.json";
constexpr const char* kBackupMagic = "trail-mate-settings-backup";
// Version 2 adds the complete Meshtastic channel presentation settings,
// Reticulum location-request policy, Reticulum group-storage owner, and
// explicit presence markers for settings_store preferences.
// Version 1 remains restore-compatible: fields absent from an older backup
// retain the current value instead of being reset.
constexpr int kBackupVersion = 2;
constexpr std::size_t kMaxBackupBytes = 24 * 1024;
constexpr std::size_t kMaxExtraBlobBytes = 128;
constexpr std::size_t kMaxExtraTextBytes = 128;

// New TMS backup I/O is deliberately fully bounded.  These are persistent
// scratch areas rather than task-stack arrays or whole-document buffers.
tms::LineScratch s_backup_line_scratch{};
char s_extra_text_scratch[kMaxExtraTextBytes]{};
uint8_t s_extra_blob_scratch[kMaxExtraBlobBytes]{};
char s_extra_key_scratch[96]{};

uint32_t uptime_ms()
{
#if defined(ARDUINO)
    return millis();
#else
    return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
#endif
}

bool storage_exists(const char* path)
{
    return ::platform::esp::arduino_common::storage::sd_exists(path);
}

bool storage_is_directory(const char* path)
{
    return ::platform::esp::arduino_common::storage::sd_is_directory(path);
}

bool storage_mkdir(const char* path)
{
    return ::platform::esp::arduino_common::storage::sd_mkdir(path);
}

bool storage_remove(const char* path)
{
    return ::platform::esp::arduino_common::storage::sd_remove(path);
}

bool storage_rename(const char* from, const char* to)
{
    return ::platform::esp::arduino_common::storage::sd_rename(from, to);
}

enum class ValueType : uint8_t
{
    Bool,
    Int,
    UInt,
    String,
    Blob,
};

struct ExtraKey
{
    const char* ns;
    const char* key;
    const char* storage_key;
    ValueType type;
};

constexpr ExtraKey kExtraKeys[] = {
    {"settings", "screen_timeout", "screen_timeout", ValueType::UInt},
    {"settings", "screen_brightness", "screen_bright", ValueType::Int},
    {"settings", "speaker_volume", "speaker_volume", ValueType::Int},
    {"settings", "vibration_enabled", "vibe_enabled", ValueType::Bool},
    {"settings", "wifi_enabled", "wifi_enabled", ValueType::Bool},
    {"settings", "wifi_ssid", "wifi_ssid", ValueType::String},
    {"settings", "wifi_password", "wifi_password", ValueType::String},
    {"settings", "wifi_profile_count", "wifi_prof_count", ValueType::Int},
    {"settings", "wifi_ssid_0", "wifi_ssid_0", ValueType::String},
    {"settings", "wifi_password_0", "wifi_password_0", ValueType::String},
    {"settings", "wifi_ssid_1", "wifi_ssid_1", ValueType::String},
    {"settings", "wifi_password_1", "wifi_password_1", ValueType::String},
    {"settings", "wifi_ssid_2", "wifi_ssid_2", ValueType::String},
    {"settings", "wifi_password_2", "wifi_password_2", ValueType::String},
    {"settings", "wifi_ssid_3", "wifi_ssid_3", ValueType::String},
    {"settings", "wifi_password_3", "wifi_password_3", ValueType::String},
    {"settings", "wifi_ssid_4", "wifi_ssid_4", ValueType::String},
    {"settings", "wifi_password_4", "wifi_password_4", ValueType::String},
    {"settings", "wifi_ssid_5", "wifi_ssid_5", ValueType::String},
    {"settings", "wifi_password_5", "wifi_password_5", ValueType::String},
    {"settings", "wifi_ssid_6", "wifi_ssid_6", ValueType::String},
    {"settings", "wifi_password_6", "wifi_password_6", ValueType::String},
    {"settings", "wifi_ssid_7", "wifi_ssid_7", ValueType::String},
    {"settings", "wifi_password_7", "wifi_password_7", ValueType::String},
    {"settings", "wifi_ssid_8", "wifi_ssid_8", ValueType::String},
    {"settings", "wifi_password_8", "wifi_password_8", ValueType::String},
    {"settings", "wifi_ssid_9", "wifi_ssid_9", ValueType::String},
    {"settings", "wifi_password_9", "wifi_password_9", ValueType::String},
    {"settings", "display_locale", "disp_locale", ValueType::String},
    {"settings", "enabled_imes", "enabled_imes", ValueType::String},
    {"settings", "timezone_offset", "timezone_offset", ValueType::Int},
    {"settings", "timezone_profile", "timezone_prof", ValueType::Int},
    {"settings", "timezone_tzdef", "timezone_tzdef", ValueType::Blob},
    {"settings", "chat_message_alerts", "chat_msg_alert", ValueType::Int},
    {"settings", "chat_contact_alerts", "chat_ct_alert", ValueType::Int},
    {"settings", "chat_auto_reply_enabled", "chat_auto_reply", ValueType::Bool},
    {"settings", "chat_auto_reply_text", "chat_auto_txt", ValueType::String},
    {"settings", "adv_debug", "adv_debug", ValueType::Bool},
    {"power", "gauge_design_mah", "gauge_dsgn", ValueType::UInt},
    {"power", "gauge_full_mah", "gauge_full_mah", ValueType::UInt},
};
constexpr std::size_t kExtraKeyCount = sizeof(kExtraKeys) / sizeof(kExtraKeys[0]);

void copy_bounded(char* out, std::size_t out_len, const char* text)
{
    if (!out || out_len == 0)
    {
        return;
    }
    std::snprintf(out, out_len, "%s", text ? text : "");
}

void set_status_message(Status& out, const char* message, const char* detail = nullptr)
{
    copy_bounded(out.message, sizeof(out.message), message);
    copy_bounded(out.detail, sizeof(out.detail), detail);
}

bool sd_available()
{
#if defined(ARDUINO)
    return ::platform::ui::device::card_ready() &&
           ::platform::esp::arduino_common::storage::sd_card_ready();
#else
    return ::platform::ui::device::card_ready() &&
           ::platform::esp::idf_common::bsp_runtime::sdcard_ready();
#endif
}

bool ensure_backup_dir()
{
    if (storage_exists(kBackupDir))
    {
        return storage_is_directory(kBackupDir);
    }
    return storage_mkdir(kBackupDir);
}

const char* value_type_name(ValueType type)
{
    switch (type)
    {
    case ValueType::Bool:
        return "bool";
    case ValueType::Int:
        return "int";
    case ValueType::UInt:
        return "uint";
    case ValueType::String:
        return "string";
    case ValueType::Blob:
        return "blob";
    }
    return "unknown";
}

bool bytes_to_hex(const uint8_t* data, std::size_t len, char* out, std::size_t out_len)
{
    if (!out || out_len == 0)
    {
        return false;
    }
    out[0] = '\0';
    if (!data && len > 0)
    {
        return false;
    }
    if (out_len < len * 2 + 1)
    {
        return false;
    }
    static constexpr char kHex[] = "0123456789ABCDEF";
    for (std::size_t index = 0; index < len; ++index)
    {
        out[index * 2] = kHex[(data[index] >> 4) & 0x0F];
        out[index * 2 + 1] = kHex[data[index] & 0x0F];
    }
    out[len * 2] = '\0';
    return true;
}

int hex_nibble(char ch)
{
    if (ch >= '0' && ch <= '9')
    {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f')
    {
        return 10 + ch - 'a';
    }
    if (ch >= 'A' && ch <= 'F')
    {
        return 10 + ch - 'A';
    }
    return -1;
}

bool hex_to_bytes_into(const char* text,
                       uint8_t* out,
                       std::size_t capacity,
                       std::size_t* out_len)
{
    if (out_len)
    {
        *out_len = 0;
    }
    if (!text || (!out && capacity != 0))
    {
        return false;
    }
    const std::size_t len = std::strlen(text);
    if ((len % 2) != 0)
    {
        return false;
    }
    const std::size_t byte_len = len / 2;
    if (byte_len > capacity)
    {
        return false;
    }
    for (std::size_t index = 0; index < byte_len; ++index)
    {
        const int high = hex_nibble(text[index * 2]);
        const int low = hex_nibble(text[index * 2 + 1]);
        if (high < 0 || low < 0)
        {
            return false;
        }
        if (out)
        {
            out[index] = static_cast<uint8_t>((high << 4) | low);
        }
    }
    if (out_len)
    {
        *out_len = byte_len;
    }
    return true;
}

cJSON* add_object(cJSON* parent, const char* key)
{
    cJSON* object = cJSON_AddObjectToObject(parent, key);
    return object;
}

void add_bool(cJSON* parent, const char* key, bool value)
{
    cJSON_AddBoolToObject(parent, key, value);
}

void add_int(cJSON* parent, const char* key, int value)
{
    cJSON_AddNumberToObject(parent, key, value);
}

void add_uint(cJSON* parent, const char* key, uint32_t value)
{
    cJSON_AddNumberToObject(parent, key, static_cast<double>(value));
}

void add_float(cJSON* parent, const char* key, float value)
{
    cJSON_AddNumberToObject(parent, key, static_cast<double>(value));
}

void add_string(cJSON* parent, const char* key, const char* value)
{
    cJSON_AddStringToObject(parent, key, value ? value : "");
}

void add_blob_hex(cJSON* parent, const char* key, const uint8_t* data, std::size_t len)
{
    char hex[chat::kMeshtasticChannelKeyMaxLen * 2 + 1] = {};
    if (bytes_to_hex(data, len, hex, sizeof(hex)))
    {
        add_string(parent, key, hex);
    }
}

bool json_bool(cJSON* object, const char* key, bool fallback)
{
    cJSON* item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (cJSON_IsBool(item))
    {
        return cJSON_IsTrue(item);
    }
    return fallback;
}

int json_int(cJSON* object, const char* key, int fallback)
{
    cJSON* item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsNumber(item))
    {
        return fallback;
    }
    return static_cast<int>(item->valuedouble);
}

uint32_t json_uint(cJSON* object, const char* key, uint32_t fallback)
{
    cJSON* item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsNumber(item) || item->valuedouble < 0.0)
    {
        return fallback;
    }
    return static_cast<uint32_t>(item->valuedouble);
}

float json_float(cJSON* object, const char* key, float fallback)
{
    cJSON* item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsNumber(item) || !std::isfinite(item->valuedouble))
    {
        return fallback;
    }
    return static_cast<float>(item->valuedouble);
}

const char* json_string(cJSON* object, const char* key)
{
    cJSON* item = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsString(item) && item->valuestring ? item->valuestring : nullptr;
}

void copy_json_string(cJSON* object, const char* key, char* out, std::size_t out_len)
{
    const char* value = json_string(object, key);
    if (value)
    {
        copy_bounded(out, out_len, value);
    }
}

void copy_json_blob(cJSON* object,
                    const char* key,
                    uint8_t* out,
                    std::size_t capacity,
                    uint8_t* out_len = nullptr)
{
    if (!out || capacity == 0)
    {
        return;
    }
    const char* hex = json_string(object, key);
    if (!hex)
    {
        return;
    }
    std::size_t bytes_len = 0;
    if (!hex_to_bytes_into(hex, out, capacity, &bytes_len))
    {
        return;
    }
    std::memset(out + bytes_len, 0, capacity - bytes_len);
    if (out_len)
    {
        *out_len = static_cast<uint8_t>(bytes_len);
    }
}

void add_chat_policy(cJSON* parent, const chat::ChatPolicy& policy)
{
    cJSON* object = add_object(parent, "chat_policy");
    if (!object)
    {
        return;
    }
    add_bool(object, "enable_relay", policy.enable_relay);
    add_int(object, "hop_limit_default", policy.hop_limit_default);
    add_bool(object, "ack_for_broadcast", policy.ack_for_broadcast);
    add_bool(object, "ack_for_squad", policy.ack_for_squad);
    add_int(object, "max_tx_retries", policy.max_tx_retries);
    add_int(object, "max_channels", policy.max_channels);
}

void restore_chat_policy(cJSON* object, chat::ChatPolicy& policy)
{
    if (!cJSON_IsObject(object))
    {
        return;
    }
    policy.enable_relay = json_bool(object, "enable_relay", policy.enable_relay);
    policy.hop_limit_default = static_cast<uint8_t>(
        json_int(object, "hop_limit_default", policy.hop_limit_default));
    policy.ack_for_broadcast = json_bool(object, "ack_for_broadcast", policy.ack_for_broadcast);
    policy.ack_for_squad = json_bool(object, "ack_for_squad", policy.ack_for_squad);
    policy.max_tx_retries = static_cast<uint8_t>(
        json_int(object, "max_tx_retries", policy.max_tx_retries));
    policy.max_channels = static_cast<uint8_t>(
        json_int(object, "max_channels", policy.max_channels));
}

void add_mqtt_client_config(cJSON* parent,
                            const char* key,
                            bool enabled,
                            bool uplink_enabled,
                            bool downlink_enabled,
                            const char* host,
                            uint16_t port,
                            const char* root,
                            const char* username,
                            const char* password)
{
    cJSON* object = add_object(parent, key);
    if (!object)
    {
        return;
    }
    add_bool(object, "enabled", enabled);
    add_bool(object, "uplink_enabled", uplink_enabled);
    add_bool(object, "downlink_enabled", downlink_enabled);
    add_string(object, "host", host);
    add_int(object, "port", port != 0 ? port : 1883);
    add_string(object, "root", root);
    add_string(object, "username", username);
    add_string(object, "password", password);
}

void restore_mqtt_client_config(cJSON* object,
                                bool& enabled,
                                bool& uplink_enabled,
                                bool& downlink_enabled,
                                char* host,
                                std::size_t host_len,
                                uint16_t& port,
                                char* root,
                                std::size_t root_len,
                                char* username,
                                std::size_t username_len,
                                char* password,
                                std::size_t password_len)
{
    if (!cJSON_IsObject(object))
    {
        return;
    }

    enabled = json_bool(object, "enabled", enabled);
    uplink_enabled = json_bool(object, "uplink_enabled", uplink_enabled);
    downlink_enabled = json_bool(object, "downlink_enabled", downlink_enabled);
    copy_json_string(object, "host", host, host_len);
    const int restored_port = json_int(object, "port", port != 0 ? port : 1883);
    if (restored_port > 0 && restored_port <= 65535)
    {
        port = static_cast<uint16_t>(restored_port);
    }
    copy_json_string(object, "root", root, root_len);
    copy_json_string(object, "username", username, username_len);
    copy_json_string(object, "password", password, password_len);
}

void add_meshcore_channel_config(cJSON* parent, const chat::MeshConfig& config)
{
    cJSON* channels = cJSON_AddArrayToObject(parent, "meshcore_channels");
    if (!channels)
    {
        return;
    }
    for (std::size_t index = 0; index < chat::kMeshCoreChannelMaxCount; ++index)
    {
        cJSON* item = cJSON_CreateObject();
        if (!item)
        {
            continue;
        }
        const chat::MeshCoreChannelConfig& channel =
            config.meshCoreChannel(static_cast<uint8_t>(index));
        add_int(item, "slot", static_cast<int>(index));
        add_bool(item, "enabled", index == 0 ? true : channel.enabled);
        add_string(item, "name", channel.name);
        add_blob_hex(item, "key", channel.key, sizeof(channel.key));
        cJSON_AddItemToArray(channels, item);
    }
}

void restore_meshcore_channel_config(cJSON* parent, chat::MeshConfig& config)
{
    cJSON* channels = cJSON_GetObjectItemCaseSensitive(parent, "meshcore_channels");
    if (cJSON_IsArray(channels))
    {
        const uint8_t active_slot =
            chat::normalizeMeshCoreChannelSlot(config.meshcore_channel_slot);
        config.resetMeshCoreChannels();
        config.meshcore_channel_slot = active_slot;
        cJSON* item = nullptr;
        cJSON_ArrayForEach(item, channels)
        {
            if (!cJSON_IsObject(item))
            {
                continue;
            }
            const int slot_raw = json_int(item, "slot", -1);
            if (slot_raw < 0 || slot_raw >= static_cast<int>(chat::kMeshCoreChannelMaxCount))
            {
                continue;
            }
            chat::MeshCoreChannelConfig& channel =
                config.meshCoreChannel(static_cast<uint8_t>(slot_raw));
            channel.enabled = json_bool(item, "enabled", channel.enabled);
            copy_json_string(item, "name", channel.name, sizeof(channel.name));
            copy_json_blob(item, "key", channel.key, sizeof(channel.key));
        }
    }
    else
    {
        config.importMeshCoreLegacyChannelMirror();
    }
    config.meshCoreChannel(0).enabled = true;
    config.syncMeshCoreLegacyChannelMirror();
}

void add_reticulum_group_config(cJSON* parent, const chat::MeshConfig& config)
{
    cJSON* groups = cJSON_AddArrayToObject(parent, "reticulum_groups");
    if (!groups)
    {
        return;
    }

    for (std::size_t index = 0; index < chat::kReticulumGroupDestinationMaxCount; ++index)
    {
        const chat::ReticulumGroupDestinationConfig& group = config.reticulum_groups[index];
        if (!chat::hasReticulumDestinationIdentity(group.identity))
        {
            continue;
        }

        char destination[chat::kReticulumPeerHashSize * 2 + 1] = {};
        chat::formatReticulumDestinationHashText(group.identity,
                                                 destination,
                                                 sizeof(destination));
        if (destination[0] == '\0')
        {
            continue;
        }

        cJSON* item = cJSON_CreateObject();
        if (!item)
        {
            continue;
        }
        add_int(item, "slot", static_cast<int>(index));
        add_bool(item, "enabled", group.enabled);
        add_string(item, "name", group.name);
        add_string(item, "destination", destination);
        cJSON_AddItemToArray(groups, item);
    }
}

bool restore_reticulum_group_config(cJSON* parent, chat::MeshConfig& config)
{
    cJSON* groups = cJSON_GetObjectItemCaseSensitive(parent, "reticulum_groups");
    if (!cJSON_IsArray(groups))
    {
        return false;
    }

    for (std::size_t index = 0; index < chat::kReticulumGroupDestinationMaxCount; ++index)
    {
        config.reticulum_groups[index] = chat::ReticulumGroupDestinationConfig{};
    }

    cJSON* item = nullptr;
    cJSON_ArrayForEach(item, groups)
    {
        if (!cJSON_IsObject(item))
        {
            continue;
        }
        const int slot = json_int(item, "slot", -1);
        if (slot < 0 || slot >= static_cast<int>(chat::kReticulumGroupDestinationMaxCount))
        {
            continue;
        }

        const char* destination = json_string(item, "destination");
        if (!destination)
        {
            continue;
        }

        chat::ReticulumGroupDestinationConfig restored{};
        char error[96] = {};
        if (!chat::parseReticulumDestinationHashText(destination,
                                                     &restored.identity,
                                                     error,
                                                     sizeof(error)))
        {
            std::printf("[SettingsBackup] skip invalid Reticulum group: %s\n", error);
            continue;
        }
        restored.enabled = json_bool(item, "enabled", restored.enabled);
        copy_json_string(item, "name", restored.name, sizeof(restored.name));
        config.reticulum_groups[static_cast<std::size_t>(slot)] = restored;
    }
    return true;
}

void add_mesh_config(cJSON* parent,
                     const char* key,
                     const chat::MeshConfig& config,
                     bool include_meshcore_fields,
                     bool include_reticulum_fields)
{
    cJSON* object = add_object(parent, key);
    if (!object)
    {
        return;
    }
    add_int(object, "region", config.region);
    add_bool(object, "use_preset", config.use_preset);
    add_int(object, "modem_preset", config.modem_preset);
    add_float(object, "bandwidth_khz", config.bandwidth_khz);
    add_int(object, "spread_factor", config.spread_factor);
    add_int(object, "coding_rate", config.coding_rate);
    add_int(object, "tx_power", config.tx_power);
    add_int(object, "hop_limit", config.hop_limit);
    add_bool(object, "tx_enabled", config.tx_enabled);
    add_bool(object, "override_duty_cycle", config.override_duty_cycle);
    add_int(object, "channel_num", config.channel_num);
    add_float(object, "frequency_offset_mhz", config.frequency_offset_mhz);
    add_float(object, "override_frequency_mhz", config.override_frequency_mhz);
    add_bool(object, "enable_relay", config.enable_relay);
    add_bool(object, "ignore_mqtt", config.ignore_mqtt);
    add_bool(object, "config_ok_to_mqtt", config.config_ok_to_mqtt);
    add_string(object, "primary_channel_name", config.primary_channel_name);
    add_string(object, "secondary_channel_name", config.secondary_channel_name);
    add_uint(object, "primary_channel_id", config.primary_channel_id);
    add_uint(object, "secondary_channel_id", config.secondary_channel_id);
    add_int(object, "primary_key_len", config.primary_key_len);
    add_blob_hex(object, "primary_key", config.primary_key, config.primary_key_len);
    add_int(object, "secondary_key_len", config.secondary_key_len);
    add_blob_hex(object, "secondary_key", config.secondary_key, config.secondary_key_len);
    if (include_meshcore_fields)
    {
        add_int(object, "meshcore_region_preset", config.meshcore_region_preset);
        add_float(object, "meshcore_freq_mhz", config.meshcore_freq_mhz);
        add_float(object, "meshcore_bw_khz", config.meshcore_bw_khz);
        add_int(object, "meshcore_sf", config.meshcore_sf);
        add_int(object, "meshcore_cr", config.meshcore_cr);
        add_bool(object, "meshcore_client_repeat", config.meshcore_client_repeat);
        add_float(object, "meshcore_rx_delay_base", config.meshcore_rx_delay_base);
        add_float(object, "meshcore_airtime_factor", config.meshcore_airtime_factor);
        add_int(object, "meshcore_flood_max", config.meshcore_flood_max);
        add_bool(object, "meshcore_multi_acks", config.meshcore_multi_acks);
        add_int(object, "meshcore_send_profile",
                static_cast<int>(static_cast<uint8_t>(config.meshcore_send_profile)));
        add_int(object, "meshcore_forward_profile",
                static_cast<int>(static_cast<uint8_t>(config.meshcore_forward_profile)));
        add_int(object, "meshcore_channel_slot", config.meshcore_channel_slot);
        const chat::MeshCoreChannelConfig& active_channel =
            config.activeMeshCoreChannel();
        add_string(object, "meshcore_channel_name", active_channel.name);
        add_blob_hex(object, "meshcore_channel_key",
                     active_channel.key, sizeof(active_channel.key));
        add_meshcore_channel_config(object, config);
        add_mqtt_client_config(object,
                               "meshcore_mqtt",
                               config.meshcore_mqtt_enabled,
                               config.meshcore_mqtt_uplink_enabled,
                               config.meshcore_mqtt_downlink_enabled,
                               config.meshcore_mqtt_host,
                               config.meshcore_mqtt_port,
                               config.meshcore_mqtt_root,
                               config.meshcore_mqtt_username,
                               config.meshcore_mqtt_password);
    }
    if (include_reticulum_fields)
    {
        add_bool(object, "reticulum_lora_enabled", config.reticulum_lora_enabled);
        add_bool(object, "reticulum_wifi_gateway_enabled", config.reticulum_wifi_gateway_enabled);
        add_bool(object, "reticulum_wifi_auto_connect", config.reticulum_wifi_auto_connect);
        add_bool(object, "reticulum_anonymous_peer", config.reticulum_anonymous_peer);
        add_string(object, "reticulum_wifi_gateway_host", config.reticulum_wifi_gateway_host);
        add_int(object, "reticulum_wifi_gateway_port", config.reticulum_wifi_gateway_port);
        add_int(object,
                "reticulum_interface_policy",
                static_cast<int>(static_cast<uint8_t>(config.reticulum_interface_policy)));
        add_bool(object,
                 "reticulum_allow_location_requests",
                 config.reticulum_allow_location_requests);
        add_reticulum_group_config(object, config);
    }
}

bool restore_mesh_config(cJSON* object,
                         chat::MeshConfig& config,
                         bool include_meshcore_fields,
                         bool include_reticulum_fields)
{
    if (!cJSON_IsObject(object))
    {
        return false;
    }
    config.region = static_cast<uint8_t>(json_int(object, "region", config.region));
    config.use_preset = json_bool(object, "use_preset", config.use_preset);
    config.modem_preset = static_cast<uint8_t>(json_int(object, "modem_preset", config.modem_preset));
    config.bandwidth_khz = json_float(object, "bandwidth_khz", config.bandwidth_khz);
    config.spread_factor = static_cast<uint8_t>(json_int(object, "spread_factor", config.spread_factor));
    config.coding_rate = static_cast<uint8_t>(json_int(object, "coding_rate", config.coding_rate));
    config.tx_power = static_cast<int8_t>(json_int(object, "tx_power", config.tx_power));
    config.hop_limit = static_cast<uint8_t>(json_int(object, "hop_limit", config.hop_limit));
    config.tx_enabled = json_bool(object, "tx_enabled", config.tx_enabled);
    config.override_duty_cycle = json_bool(object, "override_duty_cycle", config.override_duty_cycle);
    config.channel_num = static_cast<uint16_t>(json_int(object, "channel_num", config.channel_num));
    config.frequency_offset_mhz = json_float(object, "frequency_offset_mhz", config.frequency_offset_mhz);
    config.override_frequency_mhz = json_float(object, "override_frequency_mhz", config.override_frequency_mhz);
    config.enable_relay = json_bool(object, "enable_relay", config.enable_relay);
    config.ignore_mqtt = json_bool(object, "ignore_mqtt", config.ignore_mqtt);
    config.config_ok_to_mqtt = json_bool(object, "config_ok_to_mqtt", config.config_ok_to_mqtt);
    copy_json_string(object, "primary_channel_name", config.primary_channel_name, sizeof(config.primary_channel_name));
    copy_json_string(object, "secondary_channel_name", config.secondary_channel_name, sizeof(config.secondary_channel_name));
    config.primary_channel_id = json_uint(object, "primary_channel_id", config.primary_channel_id);
    config.secondary_channel_id = json_uint(object, "secondary_channel_id", config.secondary_channel_id);
    uint8_t primary_key_len = static_cast<uint8_t>(
        json_int(object, "primary_key_len", config.primary_key_len));
    copy_json_blob(object, "primary_key", config.primary_key, sizeof(config.primary_key), &primary_key_len);
    config.primary_key_len = chat::normalizeMeshtasticChannelKeyLen(
        config.primary_key, sizeof(config.primary_key), primary_key_len);
    uint8_t secondary_key_len = static_cast<uint8_t>(
        json_int(object, "secondary_key_len", config.secondary_key_len));
    copy_json_blob(object, "secondary_key", config.secondary_key, sizeof(config.secondary_key), &secondary_key_len);
    config.secondary_key_len = chat::normalizeMeshtasticChannelKeyLen(
        config.secondary_key, sizeof(config.secondary_key), secondary_key_len);
    if (include_meshcore_fields)
    {
        config.meshcore_region_preset = static_cast<uint8_t>(
            json_int(object, "meshcore_region_preset", config.meshcore_region_preset));
        config.meshcore_freq_mhz = json_float(object, "meshcore_freq_mhz", config.meshcore_freq_mhz);
        config.meshcore_bw_khz = json_float(object, "meshcore_bw_khz", config.meshcore_bw_khz);
        config.meshcore_sf = static_cast<uint8_t>(json_int(object, "meshcore_sf", config.meshcore_sf));
        config.meshcore_cr = static_cast<uint8_t>(json_int(object, "meshcore_cr", config.meshcore_cr));
        config.meshcore_client_repeat = json_bool(object, "meshcore_client_repeat", config.meshcore_client_repeat);
        config.meshcore_rx_delay_base = json_float(object, "meshcore_rx_delay_base", config.meshcore_rx_delay_base);
        config.meshcore_airtime_factor = json_float(object, "meshcore_airtime_factor", config.meshcore_airtime_factor);
        config.meshcore_flood_max = static_cast<uint8_t>(json_int(object, "meshcore_flood_max", config.meshcore_flood_max));
        config.meshcore_multi_acks = json_bool(object, "meshcore_multi_acks", config.meshcore_multi_acks);
        config.meshcore_send_profile = static_cast<chat::MeshCorePayloadSendProfile>(
            json_int(object, "meshcore_send_profile",
                     static_cast<int>(static_cast<uint8_t>(config.meshcore_send_profile))));
        config.meshcore_forward_profile = static_cast<chat::MeshCoreForwardProfile>(
            json_int(object, "meshcore_forward_profile",
                     static_cast<int>(static_cast<uint8_t>(config.meshcore_forward_profile))));
        config.meshcore_channel_slot = static_cast<uint8_t>(
            json_int(object, "meshcore_channel_slot", config.meshcore_channel_slot));
        copy_json_string(object, "meshcore_channel_name", config.meshcore_channel_name, sizeof(config.meshcore_channel_name));
        copy_json_blob(object, "meshcore_channel_key", config.secondary_key, sizeof(config.secondary_key));
        restore_meshcore_channel_config(object, config);
        restore_mqtt_client_config(cJSON_GetObjectItemCaseSensitive(object, "meshcore_mqtt"),
                                   config.meshcore_mqtt_enabled,
                                   config.meshcore_mqtt_uplink_enabled,
                                   config.meshcore_mqtt_downlink_enabled,
                                   config.meshcore_mqtt_host,
                                   sizeof(config.meshcore_mqtt_host),
                                   config.meshcore_mqtt_port,
                                   config.meshcore_mqtt_root,
                                   sizeof(config.meshcore_mqtt_root),
                                   config.meshcore_mqtt_username,
                                   sizeof(config.meshcore_mqtt_username),
                                   config.meshcore_mqtt_password,
                                   sizeof(config.meshcore_mqtt_password));
    }
    if (include_reticulum_fields)
    {
        config.reticulum_lora_enabled =
            json_bool(object, "reticulum_lora_enabled", config.reticulum_lora_enabled);
        config.reticulum_wifi_gateway_enabled =
            json_bool(object, "reticulum_wifi_gateway_enabled", config.reticulum_wifi_gateway_enabled);
        config.reticulum_wifi_auto_connect =
            json_bool(object, "reticulum_wifi_auto_connect", config.reticulum_wifi_auto_connect);
        config.reticulum_anonymous_peer =
            json_bool(object, "reticulum_anonymous_peer", config.reticulum_anonymous_peer);
        copy_json_string(object,
                         "reticulum_wifi_gateway_host",
                         config.reticulum_wifi_gateway_host,
                         sizeof(config.reticulum_wifi_gateway_host));
        config.reticulum_wifi_gateway_port = static_cast<uint16_t>(
            json_int(object,
                     "reticulum_wifi_gateway_port",
                     config.reticulum_wifi_gateway_port != 0
                         ? config.reticulum_wifi_gateway_port
                         : 4242));
        const int policy = json_int(object,
                                    "reticulum_interface_policy",
                                    static_cast<int>(static_cast<uint8_t>(
                                        config.reticulum_interface_policy)));
        if (policy >= 0 &&
            policy <= static_cast<int>(static_cast<uint8_t>(
                          chat::ReticulumInterfacePolicy::WifiGatewayOnly)))
        {
            config.reticulum_interface_policy =
                static_cast<chat::ReticulumInterfacePolicy>(policy);
        }
        config.reticulum_allow_location_requests =
            json_bool(object,
                      "reticulum_allow_location_requests",
                      config.reticulum_allow_location_requests);
        return restore_reticulum_group_config(object, config);
    }
    return false;
}

void add_aprs_config(cJSON* parent, const app::AprsConfig& config)
{
    cJSON* object = add_object(parent, "aprs");
    if (!object)
    {
        return;
    }
    add_bool(object, "enabled", config.enabled);
    add_string(object, "igate_callsign", config.igate_callsign);
    add_int(object, "igate_ssid", config.igate_ssid);
    add_string(object, "tocall", config.tocall);
    add_string(object, "path", config.path);
    add_int(object, "tx_min_interval_s", config.tx_min_interval_s);
    add_int(object, "dedupe_window_s", config.dedupe_window_s);
    char symbol_table[2] = {config.symbol_table, '\0'};
    char symbol_code[2] = {config.symbol_code, '\0'};
    add_string(object, "symbol_table", symbol_table);
    add_string(object, "symbol_code", symbol_code);
    add_int(object, "position_interval_s", config.position_interval_s);
    add_bool(object, "self_enable", config.self_enable);
    add_string(object, "self_callsign", config.self_callsign);
}

void restore_aprs_config(cJSON* object, app::AprsConfig& config)
{
    if (!cJSON_IsObject(object))
    {
        return;
    }
    config.enabled = json_bool(object, "enabled", config.enabled);
    copy_json_string(object, "igate_callsign", config.igate_callsign, sizeof(config.igate_callsign));
    config.igate_ssid = static_cast<uint8_t>(json_int(object, "igate_ssid", config.igate_ssid));
    copy_json_string(object, "tocall", config.tocall, sizeof(config.tocall));
    copy_json_string(object, "path", config.path, sizeof(config.path));
    config.tx_min_interval_s = static_cast<uint16_t>(
        json_int(object, "tx_min_interval_s", config.tx_min_interval_s));
    config.dedupe_window_s = static_cast<uint16_t>(
        json_int(object, "dedupe_window_s", config.dedupe_window_s));
    const char* symbol_table = json_string(object, "symbol_table");
    if (symbol_table && symbol_table[0] != '\0')
    {
        config.symbol_table = symbol_table[0];
    }
    const char* symbol_code = json_string(object, "symbol_code");
    if (symbol_code && symbol_code[0] != '\0')
    {
        config.symbol_code = symbol_code[0];
    }
    config.position_interval_s = static_cast<uint16_t>(
        json_int(object, "position_interval_s", config.position_interval_s));
    config.self_enable = json_bool(object, "self_enable", config.self_enable);
    copy_json_string(object, "self_callsign", config.self_callsign, sizeof(config.self_callsign));
}

cJSON* create_app_config_json(const app::AppConfig& config)
{
    cJSON* object = cJSON_CreateObject();
    if (!object)
    {
        return nullptr;
    }
    add_chat_policy(object, config.chat_policy);
    add_mesh_config(object, "meshtastic", config.meshtastic_config, false, false);
    add_mesh_config(object, "meshcore", config.meshcore_config, true, false);
    add_mesh_config(object, "reticulum", config.reticulumConfig(), false, true);
    add_mqtt_client_config(object,
                           "meshtastic_mqtt",
                           config.meshtastic_mqtt_enabled,
                           config.meshtastic_mqtt_uplink_enabled,
                           config.meshtastic_mqtt_downlink_enabled,
                           config.meshtastic_mqtt_host,
                           config.meshtastic_mqtt_port,
                           config.meshtastic_mqtt_root,
                           config.meshtastic_mqtt_username,
                           config.meshtastic_mqtt_password);
    add_int(object, "mesh_protocol", static_cast<int>(config.mesh_protocol));
    add_string(object, "node_name", config.node_name);
    add_string(object, "short_name", config.short_name);
    add_bool(object, "primary_enabled", config.primary_enabled);
    add_bool(object, "secondary_enabled", config.secondary_enabled);
    add_bool(object, "primary_uplink_enabled", config.primary_uplink_enabled);
    add_bool(object, "primary_downlink_enabled", config.primary_downlink_enabled);
    add_bool(object, "secondary_uplink_enabled", config.secondary_uplink_enabled);
    add_bool(object, "secondary_downlink_enabled", config.secondary_downlink_enabled);
    add_bool(object,
             "primary_channel_has_module_settings",
             config.primary_channel_has_module_settings);
    add_uint(object,
             "primary_channel_position_precision",
             config.primary_channel_position_precision);
    add_bool(object, "primary_channel_is_muted", config.primary_channel_is_muted);
    add_bool(object,
             "secondary_channel_has_module_settings",
             config.secondary_channel_has_module_settings);
    add_uint(object,
             "secondary_channel_position_precision",
             config.secondary_channel_position_precision);
    add_bool(object, "secondary_channel_is_muted", config.secondary_channel_is_muted);
    add_bool(object, "gps_enabled", config.gps_enabled);
    add_uint(object, "gps_init_baud", config.gps_init_baud);
    add_uint(object, "gps_init_probe_ms", config.gps_init_probe_ms);
    add_int(object, "gps_init_profile", config.gps_init_profile);
    add_int(object, "gps_init_rxm_policy", config.gps_init_rxm_policy);
    add_int(object, "gps_init_gnss_policy", config.gps_init_gnss_policy);
    add_int(object, "gps_init_nmea_policy", config.gps_init_nmea_policy);
    add_uint(object, "gps_interval_ms", config.gps_interval_ms);
    add_int(object, "gps_mode", config.gps_mode);
    add_int(object, "gps_sat_mask", config.gps_sat_mask);
    add_int(object, "gps_strategy", config.gps_strategy);
    add_int(object, "gps_alt_ref", config.gps_alt_ref);
    add_int(object, "gps_coord_format", config.gps_coord_format);
    add_uint(object, "motion_idle_ms", config.motion_config.idle_timeout_ms);
    add_int(object, "motion_sensor_id", config.motion_config.sensor_id);
    add_int(object, "external_nmea_output_hz", config.external_nmea_output_hz);
    add_int(object, "external_nmea_sentence_mask", config.external_nmea_sentence_mask);
    add_int(object, "map_coord_system", config.map_coord_system);
    add_int(object, "map_source", config.map_source);
    add_bool(object, "map_contour_enabled", config.map_contour_enabled);
    add_bool(object, "map_track_enabled", config.map_track_enabled);
    add_int(object, "map_track_interval", config.map_track_interval);
    add_int(object, "map_track_format", config.map_track_format);
    add_int(object, "chat_channel", config.chat_channel);
    add_bool(object, "net_duty_cycle", config.net_duty_cycle);
    add_int(object, "net_channel_util", config.net_channel_util);
    add_int(object, "privacy_encrypt_mode", config.privacy_encrypt_mode);
    add_bool(object, "route_enabled", config.route_enabled);
    add_string(object, "route_path", config.route_path);
    add_aprs_config(object, config.aprs);
    return object;
}

bool restore_app_config_json(cJSON* object, app::AppConfig& config)
{
    if (!cJSON_IsObject(object))
    {
        return false;
    }
    restore_chat_policy(cJSON_GetObjectItemCaseSensitive(object, "chat_policy"), config.chat_policy);
    restore_mesh_config(cJSON_GetObjectItemCaseSensitive(object, "meshtastic"), config.meshtastic_config, false, false);
    restore_mesh_config(cJSON_GetObjectItemCaseSensitive(object, "meshcore"), config.meshcore_config, true, false);
    bool restore_reticulum_groups = false;
    cJSON* reticulum_object = cJSON_GetObjectItemCaseSensitive(object, "reticulum");
    if (cJSON_IsObject(reticulum_object))
    {
        restore_reticulum_groups =
            restore_mesh_config(reticulum_object, config.reticulumConfig(), false, true);
    }
    else
    {
        restore_reticulum_groups =
            restore_mesh_config(cJSON_GetObjectItemCaseSensitive(object, "rnode"),
                                config.reticulumConfig(),
                                false,
                                true);
    }
    const int protocol = json_int(object, "mesh_protocol", static_cast<int>(config.mesh_protocol));
    if (protocol >= 0 && protocol <= 0xFF &&
        chat::infra::isValidMeshProtocolValue(static_cast<uint8_t>(protocol)))
    {
        config.mesh_protocol = chat::infra::meshProtocolFromRaw(static_cast<uint8_t>(protocol));
    }
    restore_mqtt_client_config(cJSON_GetObjectItemCaseSensitive(object, "meshtastic_mqtt"),
                               config.meshtastic_mqtt_enabled,
                               config.meshtastic_mqtt_uplink_enabled,
                               config.meshtastic_mqtt_downlink_enabled,
                               config.meshtastic_mqtt_host,
                               sizeof(config.meshtastic_mqtt_host),
                               config.meshtastic_mqtt_port,
                               config.meshtastic_mqtt_root,
                               sizeof(config.meshtastic_mqtt_root),
                               config.meshtastic_mqtt_username,
                               sizeof(config.meshtastic_mqtt_username),
                               config.meshtastic_mqtt_password,
                               sizeof(config.meshtastic_mqtt_password));
    copy_json_string(object, "node_name", config.node_name, sizeof(config.node_name));
    copy_json_string(object, "short_name", config.short_name, sizeof(config.short_name));
    config.ble_enabled = false;
    config.primary_enabled = json_bool(object, "primary_enabled", config.primary_enabled);
    config.secondary_enabled = json_bool(object, "secondary_enabled", config.secondary_enabled);
    config.primary_uplink_enabled = json_bool(object, "primary_uplink_enabled", config.primary_uplink_enabled);
    config.primary_downlink_enabled = json_bool(object, "primary_downlink_enabled", config.primary_downlink_enabled);
    config.secondary_uplink_enabled = json_bool(object, "secondary_uplink_enabled", config.secondary_uplink_enabled);
    config.secondary_downlink_enabled = json_bool(object, "secondary_downlink_enabled", config.secondary_downlink_enabled);
    config.primary_channel_has_module_settings = json_bool(
        object,
        "primary_channel_has_module_settings",
        config.primary_channel_has_module_settings);
    config.primary_channel_position_precision = json_uint(
        object,
        "primary_channel_position_precision",
        config.primary_channel_position_precision);
    config.primary_channel_is_muted =
        json_bool(object, "primary_channel_is_muted", config.primary_channel_is_muted);
    config.secondary_channel_has_module_settings = json_bool(
        object,
        "secondary_channel_has_module_settings",
        config.secondary_channel_has_module_settings);
    config.secondary_channel_position_precision = json_uint(
        object,
        "secondary_channel_position_precision",
        config.secondary_channel_position_precision);
    config.secondary_channel_is_muted =
        json_bool(object, "secondary_channel_is_muted", config.secondary_channel_is_muted);
    config.gps_enabled = json_bool(object, "gps_enabled", config.gps_enabled);
    config.gps_init_baud = json_uint(object, "gps_init_baud", config.gps_init_baud);
    config.gps_init_probe_ms = json_uint(object, "gps_init_probe_ms", config.gps_init_probe_ms);
    config.gps_init_profile = static_cast<uint8_t>(json_int(object, "gps_init_profile", config.gps_init_profile));
    config.gps_init_rxm_policy = static_cast<uint8_t>(json_int(object, "gps_init_rxm_policy", config.gps_init_rxm_policy));
    config.gps_init_gnss_policy = static_cast<uint8_t>(json_int(object, "gps_init_gnss_policy", config.gps_init_gnss_policy));
    config.gps_init_nmea_policy = static_cast<uint8_t>(json_int(object, "gps_init_nmea_policy", config.gps_init_nmea_policy));
    config.gps_interval_ms = json_uint(object, "gps_interval_ms", config.gps_interval_ms);
    config.gps_mode = static_cast<uint8_t>(json_int(object, "gps_mode", config.gps_mode));
    config.gps_sat_mask = static_cast<uint8_t>(json_int(object, "gps_sat_mask", config.gps_sat_mask));
    config.gps_strategy = static_cast<uint8_t>(json_int(object, "gps_strategy", config.gps_strategy));
    config.gps_alt_ref = static_cast<uint8_t>(json_int(object, "gps_alt_ref", config.gps_alt_ref));
    config.gps_coord_format = static_cast<uint8_t>(json_int(object, "gps_coord_format", config.gps_coord_format));
    config.motion_config.idle_timeout_ms = json_uint(object, "motion_idle_ms", config.motion_config.idle_timeout_ms);
    config.motion_config.sensor_id = static_cast<uint8_t>(
        json_int(object, "motion_sensor_id", config.motion_config.sensor_id));
    config.external_nmea_output_hz = static_cast<uint8_t>(
        json_int(object, "external_nmea_output_hz", config.external_nmea_output_hz));
    config.external_nmea_sentence_mask = static_cast<uint8_t>(
        json_int(object, "external_nmea_sentence_mask", config.external_nmea_sentence_mask));
    config.map_coord_system = static_cast<uint8_t>(json_int(object, "map_coord_system", config.map_coord_system));
    config.map_source = static_cast<uint8_t>(json_int(object, "map_source", config.map_source));
    config.map_contour_enabled = json_bool(object, "map_contour_enabled", config.map_contour_enabled);
    config.map_track_enabled = json_bool(object, "map_track_enabled", config.map_track_enabled);
    config.map_track_interval = static_cast<uint8_t>(json_int(object, "map_track_interval", config.map_track_interval));
    config.map_track_format = static_cast<uint8_t>(json_int(object, "map_track_format", config.map_track_format));
    config.chat_channel = static_cast<uint8_t>(json_int(object, "chat_channel", config.chat_channel));
    config.net_duty_cycle = json_bool(object, "net_duty_cycle", config.net_duty_cycle);
    config.net_channel_util = static_cast<uint8_t>(json_int(object, "net_channel_util", config.net_channel_util));
    config.privacy_encrypt_mode = static_cast<uint8_t>(
        json_int(object, "privacy_encrypt_mode", config.privacy_encrypt_mode));
    config.route_enabled = json_bool(object, "route_enabled", config.route_enabled);
    copy_json_string(object, "route_path", config.route_path, sizeof(config.route_path));
    restore_aprs_config(cJSON_GetObjectItemCaseSensitive(object, "aprs"), config.aprs);
    return restore_reticulum_groups;
}

bool extra_preference_exists(const ExtraKey& key)
{
#if defined(ARDUINO)
    Preferences prefs;
    if (!prefs.begin(key.ns, true))
    {
        return false;
    }
    const PreferenceType type = prefs.getType(key.storage_key ? key.storage_key : key.key);
    prefs.end();
    return type != PT_INVALID;
#else
    nvs_handle_t handle = 0;
    if (nvs_open(key.ns, NVS_READONLY, &handle) != ESP_OK)
    {
        return false;
    }

    const char* storage_key = key.storage_key ? key.storage_key : key.key;
    esp_err_t err = ESP_ERR_NVS_NOT_FOUND;
    switch (key.type)
    {
    case ValueType::Bool:
    {
        uint8_t value = 0;
        err = nvs_get_u8(handle, storage_key, &value);
        break;
    }
    case ValueType::Int:
    {
        int32_t value = 0;
        err = nvs_get_i32(handle, storage_key, &value);
        break;
    }
    case ValueType::UInt:
    {
        uint32_t value = 0;
        err = nvs_get_u32(handle, storage_key, &value);
        break;
    }
    case ValueType::String:
    {
        std::size_t size = 0;
        err = nvs_get_str(handle, storage_key, nullptr, &size);
        break;
    }
    case ValueType::Blob:
    {
        std::size_t size = 0;
        err = nvs_get_blob(handle, storage_key, nullptr, &size);
        break;
    }
    }
    nvs_close(handle);
    return err == ESP_OK;
#endif
}

void add_extra_value(cJSON* parent, const ExtraKey& key)
{
    cJSON* ns_object = cJSON_GetObjectItemCaseSensitive(parent, key.ns);
    if (!cJSON_IsObject(ns_object))
    {
        ns_object = cJSON_AddObjectToObject(parent, key.ns);
    }
    if (!ns_object)
    {
        return;
    }

    cJSON* value_object = cJSON_AddObjectToObject(ns_object, key.key);
    if (!value_object)
    {
        return;
    }
    add_string(value_object, "type", value_type_name(key.type));

    // A portable snapshot must preserve both an explicit value and the
    // absence of a value. The latter means that this device is using the
    // setting's code-defined default; restore must clear a stale target-side
    // override instead of silently retaining it.
    const bool present = extra_preference_exists(key);
    add_bool(value_object, "present", present);
    if (!present)
    {
        return;
    }

    switch (key.type)
    {
    case ValueType::Bool:
        add_bool(value_object,
                 "value",
                 ::platform::ui::settings_store::get_bool(key.ns, key.key, false));
        break;
    case ValueType::Int:
        add_int(value_object,
                "value",
                ::platform::ui::settings_store::get_int(key.ns, key.key, 0));
        break;
    case ValueType::UInt:
        add_uint(value_object,
                 "value",
                 ::platform::ui::settings_store::get_uint(key.ns, key.key, 0));
        break;
    case ValueType::String:
    {
        std::string value;
        if (::platform::ui::settings_store::get_string(key.ns, key.key, value))
        {
            add_string(value_object, "value", value.c_str());
        }
        break;
    }
    case ValueType::Blob:
    {
        uint8_t value[kMaxExtraBlobBytes] = {};
        std::size_t value_len = 0;
        if (::platform::ui::settings_store::get_blob_into(key.ns,
                                                          key.key,
                                                          value,
                                                          sizeof(value),
                                                          &value_len))
        {
            char hex[kMaxExtraBlobBytes * 2 + 1] = {};
            if (bytes_to_hex(value, value_len, hex, sizeof(hex)))
            {
                add_string(value_object, "value", hex);
            }
        }
        break;
    }
    }
}

void restore_extra_value(const ExtraKey& key, cJSON* value_object)
{
    if (!cJSON_IsObject(value_object))
    {
        return;
    }

    cJSON* present = cJSON_GetObjectItemCaseSensitive(value_object, "present");
    if (cJSON_IsFalse(present))
    {
        // `remove_keys` accepts logical names and applies the physical NVS
        // alias internally. Do not erase `storage_key` directly here.
        const char* keys[] = {key.key};
        ::platform::ui::settings_store::remove_keys(key.ns, keys, 1);
        return;
    }

    // Version 1 documents do not contain `present`; their existing values
    // must still import and omitted entries must leave the target unchanged.
    cJSON* value = cJSON_GetObjectItemCaseSensitive(value_object, "value");
    switch (key.type)
    {
    case ValueType::Bool:
        if (cJSON_IsBool(value))
        {
            ::platform::ui::settings_store::put_bool(key.ns, key.key, cJSON_IsTrue(value));
        }
        break;
    case ValueType::Int:
        if (cJSON_IsNumber(value))
        {
            ::platform::ui::settings_store::put_int(key.ns, key.key, static_cast<int>(value->valuedouble));
        }
        break;
    case ValueType::UInt:
        if (cJSON_IsNumber(value) && value->valuedouble >= 0.0)
        {
            ::platform::ui::settings_store::put_uint(key.ns, key.key, static_cast<uint32_t>(value->valuedouble));
        }
        break;
    case ValueType::String:
        if (cJSON_IsString(value) && value->valuestring)
        {
            (void)::platform::ui::settings_store::put_string(key.ns, key.key, value->valuestring);
        }
        break;
    case ValueType::Blob:
        if (cJSON_IsString(value) && value->valuestring)
        {
            std::size_t bytes_len = 0;
            if (hex_to_bytes_into(value->valuestring,
                                  s_extra_blob_scratch,
                                  sizeof(s_extra_blob_scratch),
                                  &bytes_len))
            {
                (void)::platform::ui::settings_store::put_blob(
                    key.ns,
                    key.key,
                    bytes_len == 0 ? nullptr : s_extra_blob_scratch,
                    bytes_len);
            }
        }
        break;
    }
}

void add_extra_settings(cJSON* root)
{
    cJSON* extra = cJSON_AddObjectToObject(root, "extra_settings");
    if (!extra)
    {
        return;
    }
    for (const ExtraKey& key : kExtraKeys)
    {
        add_extra_value(extra, key);
    }
}

void restore_extra_settings(cJSON* root)
{
    cJSON* extra = cJSON_GetObjectItemCaseSensitive(root, "extra_settings");
    if (!cJSON_IsObject(extra))
    {
        return;
    }
    for (const ExtraKey& key : kExtraKeys)
    {
        cJSON* ns_object = cJSON_GetObjectItemCaseSensitive(extra, key.ns);
        cJSON* value_object = cJSON_IsObject(ns_object)
                                  ? cJSON_GetObjectItemCaseSensitive(ns_object, key.key)
                                  : nullptr;
        restore_extra_value(key, value_object);
    }
}

uint32_t tms_crc32_update(uint32_t crc, const uint8_t* data, std::size_t length)
{
    for (std::size_t index = 0U; index < length; ++index)
    {
        crc ^= data[index];
        for (uint8_t bit = 0U; bit < 8U; ++bit)
        {
            crc = (crc & 1U) != 0U ? (crc >> 1U) ^ 0xEDB88320UL : crc >> 1U;
        }
    }
    return crc;
}

struct TmsBackupOutput
{
    ::platform::esp::arduino_common::storage::SdRuntimeFile* file = nullptr;
    const app::AppConfig* config = nullptr;
    uint32_t running_crc = 0xFFFFFFFFUL;
};

bool write_tms_output(void* context, const char* data, std::size_t length)
{
    auto* output = static_cast<TmsBackupOutput*>(context);
    if (!output || !output->file || !data || length == 0U ||
        output->file->write(data, length) != length)
    {
        return false;
    }
    output->running_crc = tms_crc32_update(
        output->running_crc, reinterpret_cast<const uint8_t*>(data), length);
    return true;
}

bool format_extra_record_key(const ExtraKey& key, const char* suffix)
{
    const int written = std::snprintf(s_extra_key_scratch,
                                      sizeof(s_extra_key_scratch),
                                      "extra.%s.%s.%s",
                                      key.ns,
                                      key.key,
                                      suffix);
    return written > 0 && static_cast<std::size_t>(written) < sizeof(s_extra_key_scratch);
}

bool write_extra_records(void* context, tms::RecordWriter& writer)
{
    auto* output = static_cast<TmsBackupOutput*>(context);
    if (!output)
    {
        return false;
    }
    for (const ExtraKey& key : kExtraKeys)
    {
        const bool present = extra_preference_exists(key);
        if (!format_extra_record_key(key, "present") ||
            !writer.boolean(s_extra_key_scratch, present))
        {
            return false;
        }
        if (!present)
        {
            continue;
        }
        if (!format_extra_record_key(key, "value"))
        {
            return false;
        }
        switch (key.type)
        {
        case ValueType::Bool:
            if (!writer.boolean(s_extra_key_scratch,
                                ::platform::ui::settings_store::get_bool(key.ns, key.key, false)))
            {
                return false;
            }
            break;
        case ValueType::Int:
            if (!writer.i32(s_extra_key_scratch,
                            static_cast<int32_t>(::platform::ui::settings_store::get_int(
                                key.ns, key.key, 0))))
            {
                return false;
            }
            break;
        case ValueType::UInt:
            if (!writer.u32(s_extra_key_scratch,
                            ::platform::ui::settings_store::get_uint(key.ns, key.key, 0U)))
            {
                return false;
            }
            break;
        case ValueType::String:
        {
            std::size_t length = 0U;
            if (!::platform::ui::settings_store::get_string_into(key.ns,
                                                                  key.key,
                                                                  s_extra_text_scratch,
                                                                  sizeof(s_extra_text_scratch),
                                                                  &length) ||
                !writer.text(s_extra_key_scratch, s_extra_text_scratch))
            {
                return false;
            }
            break;
        }
        case ValueType::Blob:
        {
            std::size_t length = 0U;
            if (!::platform::ui::settings_store::get_blob_into(key.ns,
                                                                key.key,
                                                                s_extra_blob_scratch,
                                                                sizeof(s_extra_blob_scratch),
                                                                &length) ||
                !writer.blob(s_extra_key_scratch, s_extra_blob_scratch, length))
            {
                return false;
            }
            break;
        }
        }
    }
    if (!output->config)
    {
        return false;
    }
    const chat::MeshConfig& reticulum = output->config->reticulumConfig();
    for (std::size_t slot = 0U; slot < chat::kReticulumGroupDestinationMaxCount; ++slot)
    {
        const chat::ReticulumGroupDestinationConfig& group = reticulum.reticulum_groups[slot];
        const bool present = chat::hasReticulumDestinationIdentity(group.identity);
        const int present_len = std::snprintf(s_extra_key_scratch,
                                             sizeof(s_extra_key_scratch),
                                             "reticulum_group.%u.present",
                                             static_cast<unsigned>(slot));
        if (present_len <= 0 || static_cast<std::size_t>(present_len) >= sizeof(s_extra_key_scratch) ||
            !writer.boolean(s_extra_key_scratch, present))
        {
            return false;
        }
        if (!present)
        {
            continue;
        }
        if (std::snprintf(s_extra_key_scratch,
                          sizeof(s_extra_key_scratch),
                          "reticulum_group.%u.enabled",
                          static_cast<unsigned>(slot)) <= 0 ||
            !writer.boolean(s_extra_key_scratch, group.enabled) ||
            std::snprintf(s_extra_key_scratch,
                          sizeof(s_extra_key_scratch),
                          "reticulum_group.%u.name",
                          static_cast<unsigned>(slot)) <= 0 ||
            !writer.text(s_extra_key_scratch, group.name) ||
            std::snprintf(s_extra_key_scratch,
                          sizeof(s_extra_key_scratch),
                          "reticulum_group.%u.destination",
                          static_cast<unsigned>(slot)) <= 0 ||
            !writer.blob(s_extra_key_scratch,
                         group.identity.destination_hash,
                         chat::kReticulumPeerHashSize))
        {
            return false;
        }
    }
    // The checksum covers each previous physical line, including its newline.
    // It is intentionally emitted immediately before END so a portable backup
    // can be verified without an allocation-proportional side structure.
    return writer.u32("checksum.crc32", output->running_crc ^ 0xFFFFFFFFUL);
}

bool replace_tms_backup(const char* temporary_path, const char* path)
{
    if (storage_exists(path) && !storage_remove(path))
    {
        return false;
    }
    if (storage_rename(temporary_path, path))
    {
        return true;
    }
    (void)storage_remove(temporary_path);
    return false;
}

bool write_tms_backup()
{
    if (!ensure_backup_dir())
    {
        return false;
    }
    if (storage_exists(kBackupTempPath))
    {
        (void)storage_remove(kBackupTempPath);
    }
    ::platform::esp::arduino_common::storage::SdRuntimeFile file;
    if (!file.open(kBackupTempPath, "w"))
    {
        return false;
    }
    TmsBackupOutput output{&file, &app::appFacade().readConfig()};
    tms::DocumentInfo info{};
    const bool wrote = tms::writeDocument(*output.config,
                                          tms::DocumentKind::Backup,
                                          {&output, write_tms_output},
                                          s_backup_line_scratch,
                                          &info,
                                          write_extra_records,
                                          &output);
    const bool flushed = file.flush();
    file.close();
    if (!wrote || !flushed)
    {
        (void)storage_remove(kBackupTempPath);
        return false;
    }
    return replace_tms_backup(kBackupTempPath, kBackupPath);
}

struct ExtraRestoreState
{
    uint64_t seen_present = 0U;
    uint64_t present_values = 0U;
    uint64_t seen_values = 0U;
    bool saw_checksum = false;
};

struct ReticulumGroupRestoreState
{
    uint8_t seen_present = 0U;
    uint8_t present_values = 0U;
    uint8_t seen_enabled = 0U;
    uint8_t seen_name = 0U;
    uint8_t seen_destination = 0U;
};

static_assert(kExtraKeyCount < 64U,
              "TMS backup extra-field validation uses one bounded bitset");

int base64_value(char value)
{
    if (value >= 'A' && value <= 'Z') return value - 'A';
    if (value >= 'a' && value <= 'z') return value - 'a' + 26;
    if (value >= '0' && value <= '9') return value - '0' + 52;
    if (value == '+') return 62;
    if (value == '/') return 63;
    return -1;
}

bool decode_tms_text(const char* value, char* out, std::size_t capacity)
{
    if (!value || !out || capacity == 0U)
    {
        return false;
    }
    std::size_t written = 0U;
    for (const char* cursor = value; *cursor != '\0'; ++cursor)
    {
        unsigned char decoded = static_cast<unsigned char>(*cursor);
        if (*cursor == '%')
        {
            const int high = hex_nibble(cursor[1]);
            const int low = hex_nibble(cursor[2]);
            if (high < 0 || low < 0 || (high == 0 && low == 0))
            {
                return false;
            }
            decoded = static_cast<unsigned char>((high << 4) | low);
            cursor += 2;
        }
        if (written + 1U >= capacity)
        {
            return false;
        }
        out[written++] = static_cast<char>(decoded);
    }
    out[written] = '\0';
    return true;
}

bool decode_tms_base64(const char* value,
                       uint8_t* out,
                       std::size_t capacity,
                       std::size_t* out_len)
{
    if (out_len)
    {
        *out_len = 0U;
    }
    if (!value || !out)
    {
        return false;
    }
    const std::size_t length = std::strlen(value);
    if (length == 0U)
    {
        return true;
    }
    if ((length % 4U) != 0U)
    {
        return false;
    }
    std::size_t padding = value[length - 1U] == '=' ? 1U : 0U;
    if (padding == 1U && value[length - 2U] == '=')
    {
        ++padding;
    }
    if (padding > 2U || (padding == 1U && value[length - 2U] == '=') ||
        (padding == 2U && value[length - 3U] == '='))
    {
        return false;
    }
    const std::size_t decoded_length = length / 4U * 3U - padding;
    if (decoded_length > capacity)
    {
        return false;
    }
    std::size_t written = 0U;
    for (std::size_t index = 0U; index < length; index += 4U)
    {
        const bool last = index + 4U == length;
        const int a = base64_value(value[index]);
        const int b = base64_value(value[index + 1U]);
        const int c = value[index + 2U] == '=' ? -2 : base64_value(value[index + 2U]);
        const int d = value[index + 3U] == '=' ? -2 : base64_value(value[index + 3U]);
        if (a < 0 || b < 0 || c == -1 || d == -1 || (!last && (c < 0 || d < 0)) ||
            (c == -2 && d != -2))
        {
            return false;
        }
        const uint32_t bits = (static_cast<uint32_t>(a) << 18U) |
                              (static_cast<uint32_t>(b) << 12U) |
                              (static_cast<uint32_t>(c < 0 ? 0 : c) << 6U) |
                              static_cast<uint32_t>(d < 0 ? 0 : d);
        out[written++] = static_cast<uint8_t>((bits >> 16U) & 0xFFU);
        if (c != -2) out[written++] = static_cast<uint8_t>((bits >> 8U) & 0xFFU);
        if (d != -2) out[written++] = static_cast<uint8_t>(bits & 0xFFU);
    }
    if (out_len)
    {
        *out_len = decoded_length;
    }
    return written == decoded_length;
}

bool parse_tms_bool(const char* type, const char* value, bool* out)
{
    if (!type || !value || std::strcmp(type, "bool") != 0 ||
        (std::strcmp(value, "0") != 0 && std::strcmp(value, "1") != 0))
    {
        return false;
    }
    if (out) *out = value[0] == '1';
    return true;
}

bool parse_tms_u32(const char* type, const char* value, uint32_t* out)
{
    if (!type || !value || std::strcmp(type, "u32") != 0 || value[0] == '\0')
    {
        return false;
    }
    uint32_t parsed = 0U;
    for (const char* cursor = value; *cursor != '\0'; ++cursor)
    {
        if (*cursor < '0' || *cursor > '9')
        {
            return false;
        }
        const uint32_t digit = static_cast<uint32_t>(*cursor - '0');
        if (parsed > (UINT32_MAX - digit) / 10U)
        {
            return false;
        }
        parsed = parsed * 10U + digit;
    }
    if (out) *out = parsed;
    return true;
}

bool parse_tms_i32(const char* type, const char* value, int* out)
{
    if (!type || !value || std::strcmp(type, "i32") != 0 || value[0] == '\0')
    {
        return false;
    }
    char* end = nullptr;
    const long parsed = std::strtol(value, &end, 10);
    if (end == value || *end != '\0' || parsed < INT32_MIN || parsed > INT32_MAX)
    {
        return false;
    }
    if (out) *out = static_cast<int>(parsed);
    return true;
}

int find_extra_key(const char* ns, const char* key)
{
    for (std::size_t index = 0U; index < kExtraKeyCount; ++index)
    {
        if (std::strcmp(kExtraKeys[index].ns, ns) == 0 &&
            std::strcmp(kExtraKeys[index].key, key) == 0)
        {
            return static_cast<int>(index);
        }
    }
    return -1;
}

bool consume_extra_record(char* line, bool apply, ExtraRestoreState* state)
{
    if (!line || !state || std::strncmp(line, "extra.", 6U) != 0)
    {
        return false;
    }
    char* equal = std::strchr(line, '=');
    if (!equal || equal == line)
    {
        return false;
    }
    *equal = '\0';
    char* type = equal + 1;
    char* colon = std::strchr(type, ':');
    if (!colon || colon == type)
    {
        return false;
    }
    *colon = '\0';
    char* value = colon + 1;
    char* ns = line + 6U;
    char* dot = std::strchr(ns, '.');
    if (!dot)
    {
        return false;
    }
    *dot = '\0';
    char* setting = dot + 1;
    dot = std::strrchr(setting, '.');
    if (!dot)
    {
        return false;
    }
    *dot = '\0';
    const char* suffix = dot + 1;
    const int index = find_extra_key(ns, setting);
    if (index < 0)
    {
        return true; // forward-compatible extra setting
    }
    const uint64_t bit = 1ULL << static_cast<uint8_t>(index);
    const ExtraKey& descriptor = kExtraKeys[index];
    if (std::strcmp(suffix, "present") == 0)
    {
        bool present = false;
        if (!parse_tms_bool(type, value, &present))
        {
            return false;
        }
        state->seen_present |= bit;
        if (present)
        {
            state->present_values |= bit;
        }
        else if (apply)
        {
            const char* keys[] = {descriptor.key};
            ::platform::ui::settings_store::remove_keys(descriptor.ns, keys, 1U);
        }
        return true;
    }
    if (std::strcmp(suffix, "value") != 0 || (state->seen_present & bit) == 0U ||
        (state->present_values & bit) == 0U)
    {
        return false;
    }
    state->seen_values |= bit;
    switch (descriptor.type)
    {
    case ValueType::Bool:
    {
        bool parsed = false;
        if (!parse_tms_bool(type, value, &parsed)) return false;
        if (apply) ::platform::ui::settings_store::put_bool(descriptor.ns, descriptor.key, parsed);
        return true;
    }
    case ValueType::Int:
    {
        int parsed = 0;
        if (!parse_tms_i32(type, value, &parsed)) return false;
        if (apply) ::platform::ui::settings_store::put_int(descriptor.ns, descriptor.key, parsed);
        return true;
    }
    case ValueType::UInt:
    {
        uint32_t parsed = 0U;
        if (!parse_tms_u32(type, value, &parsed)) return false;
        if (apply) ::platform::ui::settings_store::put_uint(descriptor.ns, descriptor.key, parsed);
        return true;
    }
    case ValueType::String:
        if (!decode_tms_text(value, s_extra_text_scratch, sizeof(s_extra_text_scratch)) ||
            std::strcmp(type, "str") != 0)
            return false;
        return !apply || ::platform::ui::settings_store::put_string(
                             descriptor.ns, descriptor.key, s_extra_text_scratch);
    case ValueType::Blob:
    {
        std::size_t length = 0U;
        if (std::strcmp(type, "b64") != 0 ||
            !decode_tms_base64(value,
                               s_extra_blob_scratch,
                               sizeof(s_extra_blob_scratch),
                               &length))
            return false;
        return !apply || ::platform::ui::settings_store::put_blob(
                             descriptor.ns,
                             descriptor.key,
                             length == 0U ? nullptr : s_extra_blob_scratch,
                             length);
    }
    default:
        return false;
    }

}

bool consume_reticulum_group_record(char* line,
                                    app::AppConfig* target,
                                    ReticulumGroupRestoreState* state)
{
    static constexpr const char* kPrefix = "reticulum_group.";
    if (!line || !state || std::strncmp(line, kPrefix, std::strlen(kPrefix)) != 0)
    {
        return false;
    }
    char* equal = std::strchr(line, '=');
    if (!equal || equal == line)
    {
        return false;
    }
    *equal = '\0';
    char* type = equal + 1;
    char* colon = std::strchr(type, ':');
    if (!colon || colon == type)
    {
        return false;
    }
    *colon = '\0';
    char* value = colon + 1;
    const char* slot_text = line + std::strlen(kPrefix);
    if (slot_text[0] < '0' ||
        slot_text[0] >= '0' + static_cast<char>(chat::kReticulumGroupDestinationMaxCount) ||
        slot_text[1] != '.')
    {
        return false;
    }
    const uint8_t slot = static_cast<uint8_t>(slot_text[0] - '0');
    const uint8_t bit = static_cast<uint8_t>(1U << slot);
    const char* suffix = slot_text + 2;
    chat::ReticulumGroupDestinationConfig* group =
        target ? &target->reticulumConfig().reticulum_groups[slot] : nullptr;
    if (std::strcmp(suffix, "present") == 0)
    {
        bool present = false;
        if (!parse_tms_bool(type, value, &present))
        {
            return false;
        }
        state->seen_present |= bit;
        if (present)
        {
            state->present_values |= bit;
        }
        else if (group)
        {
            *group = chat::ReticulumGroupDestinationConfig{};
        }
        return true;
    }
    if ((state->seen_present & bit) == 0U || (state->present_values & bit) == 0U)
    {
        return false;
    }
    if (std::strcmp(suffix, "enabled") == 0)
    {
        bool enabled = false;
        if (!parse_tms_bool(type, value, &enabled)) return false;
        if (group) group->enabled = enabled;
        state->seen_enabled |= bit;
        return true;
    }
    if (std::strcmp(suffix, "name") == 0)
    {
        if (std::strcmp(type, "str") != 0 ||
            !decode_tms_text(value, s_extra_text_scratch, sizeof(s_extra_text_scratch)) ||
            std::strlen(s_extra_text_scratch) >= chat::kReticulumGroupNameMaxLen)
            return false;
        if (group)
        {
            std::snprintf(group->name, sizeof(group->name), "%s", s_extra_text_scratch);
        }
        state->seen_name |= bit;
        return true;
    }
    if (std::strcmp(suffix, "destination") == 0)
    {
        std::size_t length = 0U;
        if (std::strcmp(type, "b64") != 0 ||
            !decode_tms_base64(value,
                               s_extra_blob_scratch,
                               sizeof(s_extra_blob_scratch),
                               &length) ||
            length != chat::kReticulumPeerHashSize)
            return false;
        if (group)
        {
            group->identity = chat::makeReticulumDestinationIdentity(s_extra_blob_scratch);
        }
        state->seen_destination |= bit;
        return true;
    }
    return false;
}

bool consume_tms_checksum(char* line, uint32_t crc_before_line, ExtraRestoreState* state)
{
    if (!line || !state || std::strncmp(line, "checksum.crc32=", 15U) != 0)
    {
        return false;
    }
    char* equal = std::strchr(line, '=');
    char* colon = equal ? std::strchr(equal + 1, ':') : nullptr;
    if (!equal || !colon)
    {
        return false;
    }
    *colon = '\0';
    uint32_t expected = 0U;
    if (!parse_tms_u32(equal + 1, colon + 1, &expected) || state->saw_checksum ||
        expected != (crc_before_line ^ 0xFFFFFFFFUL))
    {
        return false;
    }
    state->saw_checksum = true;
    return true;
}

bool finish_extra_restore(const ExtraRestoreState& state)
{
    return state.saw_checksum && state.seen_present == ((1ULL << kExtraKeyCount) - 1ULL) &&
           state.seen_values == state.present_values;
}

bool finish_reticulum_group_restore(const ReticulumGroupRestoreState& state)
{
    const uint8_t all_slots = static_cast<uint8_t>(
        (1U << chat::kReticulumGroupDestinationMaxCount) - 1U);
    return state.seen_present == all_slots &&
           state.seen_enabled == state.present_values &&
           state.seen_name == state.present_values &&
           state.seen_destination == state.present_values;
}

bool read_tms_backup(app::AppConfig* target, bool apply_extra)
{
    if (!storage_exists(kBackupPath))
    {
        return false;
    }
    ::platform::esp::arduino_common::storage::SdRuntimeFile file;
    if (!file.open(kBackupPath, "r") || file.size() == 0U ||
        file.size() > tms::kMaxDocumentBytes)
    {
        file.close();
        return false;
    }
    tms::Decoder decoder(target, tms::DocumentKind::Backup);
    ExtraRestoreState extra{};
    ReticulumGroupRestoreState groups{};
    uint32_t running_crc = 0xFFFFFFFFUL;
    uint32_t line_crc_start = running_crc;
    std::size_t line_length = 0U;
    const uint64_t size = file.size();
    bool ok = true;
    for (uint64_t index = 0U; index < size; ++index)
    {
        if (line_length == 0U)
        {
            line_crc_start = running_crc;
        }
        const int raw = file.read_byte();
        if (raw < 0)
        {
            ok = false;
            break;
        }
        const uint8_t byte = static_cast<uint8_t>(raw);
        running_crc = tms_crc32_update(running_crc, &byte, 1U);
        if (byte != '\n')
        {
            if (line_length + 1U >= sizeof(s_backup_line_scratch.bytes))
            {
                ok = false;
                break;
            }
            s_backup_line_scratch.bytes[line_length++] = static_cast<char>(byte);
            continue;
        }
        if (line_length > 0U && s_backup_line_scratch.bytes[line_length - 1U] == '\r')
        {
            --line_length;
        }
        s_backup_line_scratch.bytes[line_length] = '\0';
        if (extra.saw_checksum && std::strcmp(s_backup_line_scratch.bytes, "END") != 0)
        {
            ok = false;
            break;
        }
        if (std::strncmp(s_backup_line_scratch.bytes, "extra.", 6U) == 0)
        {
            ok = consume_extra_record(s_backup_line_scratch.bytes, apply_extra, &extra);
        }
        else if (std::strncmp(s_backup_line_scratch.bytes, "reticulum_group.", 16U) == 0)
        {
            ok = consume_reticulum_group_record(s_backup_line_scratch.bytes, target, &groups);
        }
        else if (std::strncmp(s_backup_line_scratch.bytes, "checksum.crc32=", 15U) == 0)
        {
            ok = consume_tms_checksum(s_backup_line_scratch.bytes, line_crc_start, &extra);
        }
        else
        {
            ok = decoder.consumeLine(s_backup_line_scratch.bytes);
        }
        if (!ok) break;
        line_length = 0U;
    }
    file.close();
    return ok && line_length == 0U && decoder.finish() && finish_extra_restore(extra) &&
           finish_reticulum_group_restore(groups);
}

bool restore_tms_backup()
{
    if (!read_tms_backup(nullptr, false))
    {
        return false;
    }
    app::IAppFacade& facade = app::appFacade();
    auto edit = facade.beginConfigEdit();
    if (!edit || !read_tms_backup(&edit.config(), true))
    {
        return false;
    }
    const ::platform::ui::reticulum_groups::Status group_submit =
        ::platform::ui::reticulum_groups::submit(
            edit.config().reticulumConfig().reticulum_groups,
            chat::kReticulumGroupDestinationMaxCount);
    if (!group_submit.queued)
    {
        return false;
    }
    const ::platform::ui::reticulum_groups::Status group_save =
        ::platform::ui::reticulum_groups::flushPending();
    if (!group_save.saved)
    {
        return false;
    }
    edit.commit(app::AppConfigChangeSet::allPersisted());
    return true;
}

cJSON* create_backup_document()
{
    app::IAppFacade& facade = app::appFacade();
    cJSON* root = cJSON_CreateObject();
    if (!root)
    {
        return nullptr;
    }
    add_string(root, "magic", kBackupMagic);
    add_int(root, "version", kBackupVersion);
    add_string(root, "firmware", ::platform::ui::device::firmware_version());
    add_uint(root, "created_ms", uptime_ms());

    cJSON* app_config = create_app_config_json(facade.readConfig());
    if (!app_config)
    {
        cJSON_Delete(root);
        return nullptr;
    }
    cJSON_AddItemToObject(root, "app_config", app_config);
    add_extra_settings(root);
    return root;
}

bool write_text_atomic(const char* path, const char* temp_path, const char* text, std::size_t len)
{
    if (!path || !temp_path || !text)
    {
        return false;
    }
    if (storage_exists(temp_path))
    {
        storage_remove(temp_path);
    }
    ::platform::esp::arduino_common::storage::SdRuntimeFile file;
    if (!file.open(temp_path, "w"))
    {
        return false;
    }
    const bool wrote = file.write(reinterpret_cast<const uint8_t*>(text), len) == len;
    const bool flushed = file.flush();
    file.close();
    if (!wrote || !flushed)
    {
        storage_remove(temp_path);
        return false;
    }
    if (storage_exists(path))
    {
        storage_remove(path);
    }
    if (!storage_rename(temp_path, path))
    {
        storage_remove(temp_path);
        return false;
    }
    return true;
}

bool read_file_text(const char* path, std::string& out)
{
    out.clear();
    ::platform::esp::arduino_common::storage::SdRuntimeFile file;
    if (!file.open(path, "r"))
    {
        return false;
    }
    const std::size_t size = file.size();
    if (size == 0 || size > kMaxBackupBytes)
    {
        file.close();
        return false;
    }
    out.resize(size);
    const std::size_t read = file.read_bytes(&out[0], size);
    file.close();
    if (read != size)
    {
        out.clear();
        return false;
    }
    return true;
}

bool validate_document(cJSON* root, char* error, std::size_t error_len)
{
    if (!cJSON_IsObject(root))
    {
        copy_bounded(error, error_len, "Invalid backup document");
        return false;
    }
    const char* magic = json_string(root, "magic");
    if (!magic || std::strcmp(magic, kBackupMagic) != 0)
    {
        copy_bounded(error, error_len, "Not a Trail-Mate settings backup");
        return false;
    }
    const int version = json_int(root, "version", 0);
    if (version <= 0 || version > kBackupVersion)
    {
        copy_bounded(error, error_len, "Unsupported backup version");
        return false;
    }
    if (!cJSON_IsObject(cJSON_GetObjectItemCaseSensitive(root, "app_config")))
    {
        copy_bounded(error, error_len, "Backup missing app_config");
        return false;
    }
    return true;
}

} // namespace

bool is_supported()
{
    return true;
}

Status status()
{
    Status out{};
    out.supported = true;
    out.sd_present = sd_available();
    out.has_backup = out.sd_present && storage_exists(kBackupPath);
    out.busy = false;
    if (!out.sd_present)
    {
        set_status_message(out, "SD not detected", kBackupPath);
    }
    else if (out.has_backup)
    {
        set_status_message(out, "Backup found", kBackupPath);
    }
    else
    {
        set_status_message(out, "No backup found", kBackupPath);
    }
    return out;
}

bool backup()
{
    if (!sd_available())
    {
        return false;
    }
    return write_tms_backup();
}

bool restore()
{
    if (!sd_available())
    {
        return false;
    }
    if (storage_exists(kBackupPath))
    {
        return restore_tms_backup();
    }
    // JSON is retained only as an explicit one-release compatibility import.
    // The normal backup path never creates it and never allocates a JSON tree.
    if (!storage_exists(kLegacyBackupPath))
    {
        return false;
    }
    std::string text;
    if (!read_file_text(kLegacyBackupPath, text))
    {
        return false;
    }
    cJSON* root = cJSON_ParseWithLength(text.c_str(), text.size());
    char error[96] = {};
    if (!validate_document(root, error, sizeof(error)))
    {
        if (root)
        {
            cJSON_Delete(root);
        }
        std::printf("[SettingsBackup] restore validation failed: %s\n", error);
        return false;
    }

    app::IAppFacade& facade = app::appFacade();
    auto edit = facade.beginConfigEdit();
    if (!edit)
    {
        cJSON_Delete(root);
        return false;
    }
    const bool restore_reticulum_groups =
        restore_app_config_json(cJSON_GetObjectItemCaseSensitive(root, "app_config"),
                                edit.config());

    if (restore_reticulum_groups)
    {
        const ::platform::ui::reticulum_groups::Status group_submit =
            ::platform::ui::reticulum_groups::submit(
                edit.config().reticulumConfig().reticulum_groups,
                chat::kReticulumGroupDestinationMaxCount);
        if (!group_submit.queued)
        {
            std::printf("[SettingsBackup] Reticulum group restore was not queued: %s\n",
                        group_submit.message);
            cJSON_Delete(root);
            return false;
        }
    }

    if (restore_reticulum_groups)
    {
        const ::platform::ui::reticulum_groups::Status group_save =
            ::platform::ui::reticulum_groups::flushPending();
        if (!group_save.saved)
        {
            std::printf("[SettingsBackup] Reticulum group restore failed: %s\n",
                        group_save.message);
            cJSON_Delete(root);
            return false;
        }
    }

    // The SD-backed owner has succeeded, so no NVS state has been changed on
    // an SD write failure. NVS still does not provide a transaction spanning
    // its independent AppConfig and settings_store owners.
    restore_extra_settings(root);
    cJSON_Delete(root);
    edit.commit(app::AppConfigChangeSet::allPersisted());
    return true;
}

bool remove()
{
    if (!sd_available())
    {
        return false;
    }
    if (storage_exists(kBackupTempPath))
    {
        storage_remove(kBackupTempPath);
    }
    return !storage_exists(kBackupPath) || storage_remove(kBackupPath);
}

} // namespace platform::ui::settings_backup
