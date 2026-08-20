#include "app/tms_config_codec.h"

#include "chat/infra/mesh_protocol_utils.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace app::tms
{
namespace
{

constexpr const char* kMagic = "TMSET2";
constexpr const char* kWorkingKind = "working";
constexpr const char* kBackupKind = "backup";
// The full AppConfig projection currently emits just over two hundred short
// records (MeshCore has eight independent channel slots).  This remains a
// parser-only scalar limit; it never reserves one entry per record.
constexpr uint16_t kMaxRecords = 384U;

bool isTextSafe(unsigned char value)
{
    return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
           (value >= '0' && value <= '9') || value == '-' || value == '_' ||
           value == '.' || value == '/' || value == ':';
}

int hexNibble(char value)
{
    if (value >= '0' && value <= '9')
    {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f')
    {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F')
    {
        return value - 'A' + 10;
    }
    return -1;
}

int base64Value(char value)
{
    if (value >= 'A' && value <= 'Z')
    {
        return value - 'A';
    }
    if (value >= 'a' && value <= 'z')
    {
        return value - 'a' + 26;
    }
    if (value >= '0' && value <= '9')
    {
        return value - '0' + 52;
    }
    if (value == '+')
    {
        return 62;
    }
    if (value == '/')
    {
        return 63;
    }
    return -1;
}

bool parseUnsigned(const char* value, uint32_t* out)
{
    if (!value || value[0] == '\0')
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
    if (out)
    {
        *out = parsed;
    }
    return true;
}

bool parseSigned(const char* value, int32_t* out)
{
    if (!value || value[0] == '\0')
    {
        return false;
    }
    bool negative = false;
    if (*value == '-')
    {
        negative = true;
        ++value;
    }
    if (*value == '\0')
    {
        return false;
    }
    uint32_t magnitude = 0U;
    if (!parseUnsigned(value, &magnitude))
    {
        return false;
    }
    if (negative)
    {
        if (magnitude > 2147483648UL)
        {
            return false;
        }
        if (out)
        {
            *out = magnitude == 2147483648UL
                       ? INT32_MIN
                       : -static_cast<int32_t>(magnitude);
        }
        return true;
    }
    if (magnitude > 2147483647UL)
    {
        return false;
    }
    if (out)
    {
        *out = static_cast<int32_t>(magnitude);
    }
    return true;
}

bool parseFloat(const char* value, float* out)
{
    if (!value || value[0] == '\0')
    {
        return false;
    }
    char* end = nullptr;
    const float parsed = std::strtof(value, &end);
    if (end == value || *end != '\0' || !std::isfinite(parsed))
    {
        return false;
    }
    if (out)
    {
        *out = parsed;
    }
    return true;
}

bool typeIs(const char* type, const char* expected)
{
    return type && expected && std::strcmp(type, expected) == 0;
}

bool assignBool(const char* type, const char* value, bool* target)
{
    if (!typeIs(type, "bool") || !value ||
        (std::strcmp(value, "0") != 0 && std::strcmp(value, "1") != 0))
    {
        return false;
    }
    if (target)
    {
        *target = value[0] == '1';
    }
    return true;
}

bool assignU8(const char* type,
              const char* value,
              uint8_t* target,
              uint8_t maximum = UINT8_MAX)
{
    uint32_t parsed = 0U;
    if (!typeIs(type, "u8") || !parseUnsigned(value, &parsed) || parsed > maximum)
    {
        return false;
    }
    if (target)
    {
        *target = static_cast<uint8_t>(parsed);
    }
    return true;
}

bool assignU16(const char* type,
               const char* value,
               uint16_t* target,
               uint16_t maximum = UINT16_MAX)
{
    uint32_t parsed = 0U;
    if (!typeIs(type, "u16") || !parseUnsigned(value, &parsed) || parsed > maximum)
    {
        return false;
    }
    if (target)
    {
        *target = static_cast<uint16_t>(parsed);
    }
    return true;
}

bool assignU32(const char* type, const char* value, uint32_t* target)
{
    uint32_t parsed = 0U;
    if (!typeIs(type, "u32") || !parseUnsigned(value, &parsed))
    {
        return false;
    }
    if (target)
    {
        *target = parsed;
    }
    return true;
}

bool assignI8(const char* type,
              const char* value,
              int8_t* target,
              int8_t minimum = INT8_MIN,
              int8_t maximum = INT8_MAX)
{
    int32_t parsed = 0;
    if (!typeIs(type, "i8") || !parseSigned(value, &parsed) || parsed < minimum ||
        parsed > maximum)
    {
        return false;
    }
    if (target)
    {
        *target = static_cast<int8_t>(parsed);
    }
    return true;
}

bool assignFloat(const char* type, const char* value, float* target)
{
    float parsed = 0.0F;
    if (!typeIs(type, "f32") || !parseFloat(value, &parsed))
    {
        return false;
    }
    if (target)
    {
        *target = parsed;
    }
    return true;
}

bool assignText(const char* type,
                const char* value,
                char* target,
                std::size_t capacity)
{
    if (!typeIs(type, "str") || !value || capacity == 0U)
    {
        return false;
    }

    std::size_t output_length = 0U;
    for (const char* cursor = value; *cursor != '\0'; ++cursor)
    {
        unsigned char decoded = static_cast<unsigned char>(*cursor);
        if (*cursor == '%')
        {
            const int high = hexNibble(cursor[1]);
            const int low = hexNibble(cursor[2]);
            if (high < 0 || low < 0)
            {
                return false;
            }
            decoded = static_cast<unsigned char>((high << 4) | low);
            if (decoded == 0U)
            {
                return false;
            }
            cursor += 2;
        }
        if (output_length + 1U >= capacity)
        {
            return false;
        }
        if (target)
        {
            target[output_length] = static_cast<char>(decoded);
        }
        ++output_length;
    }
    if (target)
    {
        target[output_length] = '\0';
    }
    return true;
}

bool decodeBase64(const char* value,
                  uint8_t* target,
                  std::size_t capacity,
                  std::size_t* decoded_length)
{
    if (!value)
    {
        return false;
    }
    const std::size_t length = std::strlen(value);
    if (length == 0U)
    {
        if (decoded_length)
        {
            *decoded_length = 0U;
        }
        return true;
    }
    if ((length % 4U) != 0U)
    {
        return false;
    }
    std::size_t padding = 0U;
    if (value[length - 1U] == '=')
    {
        ++padding;
        if (value[length - 2U] == '=')
        {
            ++padding;
        }
    }
    if (padding > 2U || length < padding ||
        ((padding == 1U && value[length - 2U] == '=') ||
         (padding == 2U && value[length - 3U] == '=')))
    {
        return false;
    }
    const std::size_t output_length = (length / 4U) * 3U - padding;
    if (output_length > capacity)
    {
        return false;
    }

    std::size_t output_index = 0U;
    for (std::size_t index = 0U; index < length; index += 4U)
    {
        const bool last = index + 4U == length;
        const int first = base64Value(value[index]);
        const int second = base64Value(value[index + 1U]);
        const int third = value[index + 2U] == '=' ? -2 : base64Value(value[index + 2U]);
        const int fourth = value[index + 3U] == '=' ? -2 : base64Value(value[index + 3U]);
        if (first < 0 || second < 0 || third == -1 || fourth == -1 ||
            (!last && (third < 0 || fourth < 0)) ||
            (third == -2 && fourth != -2) || (!last && (third == -2 || fourth == -2)))
        {
            return false;
        }
        if ((third == -2 || fourth == -2) && !last)
        {
            return false;
        }
        const uint32_t bits = (static_cast<uint32_t>(first) << 18U) |
                              (static_cast<uint32_t>(second) << 12U) |
                              (static_cast<uint32_t>(third < 0 ? 0 : third) << 6U) |
                              static_cast<uint32_t>(fourth < 0 ? 0 : fourth);
        if (target)
        {
            target[output_index] = static_cast<uint8_t>((bits >> 16U) & 0xFFU);
        }
        ++output_index;
        if (third != -2)
        {
            if (target)
            {
                target[output_index] = static_cast<uint8_t>((bits >> 8U) & 0xFFU);
            }
            ++output_index;
        }
        if (fourth != -2)
        {
            if (target)
            {
                target[output_index] = static_cast<uint8_t>(bits & 0xFFU);
            }
            ++output_index;
        }
    }
    if (decoded_length)
    {
        *decoded_length = output_length;
    }
    return output_index == output_length;
}

bool assignBlob(const char* type,
                const char* value,
                uint8_t* target,
                std::size_t capacity,
                std::size_t* output_length,
                std::size_t exact_length = SIZE_MAX)
{
    if (!typeIs(type, "b64"))
    {
        return false;
    }
    std::size_t decoded = 0U;
    if (!decodeBase64(value, target, capacity, &decoded) ||
        (exact_length != SIZE_MAX && decoded != exact_length))
    {
        return false;
    }
    if (output_length)
    {
        *output_length = decoded;
    }
    return true;
}

class Encoder
{
  public:
    Encoder(Output output, LineScratch& scratch, DocumentInfo* info)
        : output_(output), scratch_(scratch), info_(info)
    {
    }

    bool raw(const char* line)
    {
        if (!line)
        {
            return false;
        }
        const std::size_t length = std::strlen(line);
        if (length >= sizeof(scratch_.bytes))
        {
            return false;
        }
        std::memcpy(scratch_.bytes, line, length);
        return emit(scratch_.bytes, length);
    }

    bool boolean(const char* key, bool value)
    {
        return formatted("%s=bool:%u", key, value ? 1U : 0U);
    }

    bool u8(const char* key, uint8_t value)
    {
        return formatted("%s=u8:%u", key, static_cast<unsigned>(value));
    }

    bool i8(const char* key, int8_t value)
    {
        return formatted("%s=i8:%d", key, static_cast<int>(value));
    }

    bool i32(const char* key, int32_t value)
    {
        return formatted("%s=i32:%ld", key, static_cast<long>(value));
    }

    bool u16(const char* key, uint16_t value)
    {
        return formatted("%s=u16:%u", key, static_cast<unsigned>(value));
    }

    bool u32(const char* key, uint32_t value)
    {
        return formatted("%s=u32:%lu", key, static_cast<unsigned long>(value));
    }

    bool f32(const char* key, float value)
    {
        return formatted("%s=f32:%.9g", key, static_cast<double>(value));
    }

    bool enumeration(const char* key, const char* value)
    {
        return formatted("%s=enum:%s", key, value ? value : "");
    }

    bool text(const char* key, const char* value)
    {
        if (!key || !value)
        {
            return false;
        }
        const int prefix = std::snprintf(scratch_.bytes,
                                         sizeof(scratch_.bytes),
                                         "%s=str:",
                                         key);
        if (prefix < 0 || static_cast<std::size_t>(prefix) >= sizeof(scratch_.bytes))
        {
            return false;
        }
        std::size_t length = static_cast<std::size_t>(prefix);
        static constexpr char kHex[] = "0123456789ABCDEF";
        for (const unsigned char* cursor =
                 reinterpret_cast<const unsigned char*>(value);
             *cursor != 0U;
             ++cursor)
        {
            if (isTextSafe(*cursor))
            {
                if (length + 1U >= sizeof(scratch_.bytes))
                {
                    return false;
                }
                scratch_.bytes[length++] = static_cast<char>(*cursor);
            }
            else
            {
                if (length + 3U >= sizeof(scratch_.bytes))
                {
                    return false;
                }
                scratch_.bytes[length++] = '%';
                scratch_.bytes[length++] = kHex[(*cursor >> 4U) & 0x0FU];
                scratch_.bytes[length++] = kHex[*cursor & 0x0FU];
            }
        }
        scratch_.bytes[length] = '\0';
        return emit(scratch_.bytes, length);
    }

    bool blob(const char* key, const uint8_t* value, std::size_t length)
    {
        if (!key || (!value && length != 0U))
        {
            return false;
        }
        const int prefix = std::snprintf(scratch_.bytes,
                                         sizeof(scratch_.bytes),
                                         "%s=b64:",
                                         key);
        if (prefix < 0 || static_cast<std::size_t>(prefix) >= sizeof(scratch_.bytes))
        {
            return false;
        }
        std::size_t written = static_cast<std::size_t>(prefix);
        static constexpr char kBase64[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (std::size_t index = 0U; index < length; index += 3U)
        {
            const std::size_t remaining = length - index;
            if (written + 4U >= sizeof(scratch_.bytes))
            {
                return false;
            }
            const uint32_t triple = static_cast<uint32_t>(value[index]) << 16U |
                                    (remaining > 1U
                                         ? static_cast<uint32_t>(value[index + 1U]) << 8U
                                         : 0U) |
                                    (remaining > 2U ? static_cast<uint32_t>(value[index + 2U])
                                                    : 0U);
            scratch_.bytes[written++] = kBase64[(triple >> 18U) & 0x3FU];
            scratch_.bytes[written++] = kBase64[(triple >> 12U) & 0x3FU];
            scratch_.bytes[written++] =
                remaining > 1U ? kBase64[(triple >> 6U) & 0x3FU] : '=';
            scratch_.bytes[written++] = remaining > 2U ? kBase64[triple & 0x3FU] : '=';
        }
        scratch_.bytes[written] = '\0';
        return emit(scratch_.bytes, written);
    }

  private:
    template <typename... Args>
    bool formatted(const char* format, Args... args)
    {
        const int written = std::snprintf(
            scratch_.bytes, sizeof(scratch_.bytes), format, args...);
        return written >= 0 && static_cast<std::size_t>(written) < sizeof(scratch_.bytes) &&
               emit(scratch_.bytes, static_cast<std::size_t>(written));
    }

    bool emit(const char* line, std::size_t length)
    {
        if (!output_.write || !line || length + 1U > sizeof(scratch_.bytes))
        {
            return false;
        }
        scratch_.bytes[length] = '\n';
        const bool ok = output_.write(output_.context, scratch_.bytes, length + 1U);
        if (ok && info_)
        {
            ++info_->records;
        }
        return ok;
    }

    Output output_{};
    LineScratch& scratch_;
    DocumentInfo* info_ = nullptr;
};

bool writeMeshBase(Encoder& encoder, const char* prefix, const chat::MeshConfig& config)
{
    char key[48] = {};
#define TMS_MESH_VALUE(method, suffix, value)                                              \
    do                                                                                       \
    {                                                                                        \
        std::snprintf(key, sizeof(key), "%s%s", prefix, suffix);                          \
        if (!encoder.method(key, value))                                                     \
        {                                                                                    \
            return false;                                                                    \
        }                                                                                    \
    } while (false)
    TMS_MESH_VALUE(u8, "region", config.region);
    TMS_MESH_VALUE(boolean, "use_preset", config.use_preset);
    TMS_MESH_VALUE(u8, "modem_preset", config.modem_preset);
    TMS_MESH_VALUE(f32, "bandwidth_khz", config.bandwidth_khz);
    TMS_MESH_VALUE(u8, "spread_factor", config.spread_factor);
    TMS_MESH_VALUE(u8, "coding_rate", config.coding_rate);
    TMS_MESH_VALUE(i8, "tx_power", config.tx_power);
    TMS_MESH_VALUE(u8, "hop_limit", config.hop_limit);
    TMS_MESH_VALUE(boolean, "tx_enabled", config.tx_enabled);
    TMS_MESH_VALUE(boolean, "override_duty_cycle", config.override_duty_cycle);
    TMS_MESH_VALUE(u16, "channel_num", config.channel_num);
    TMS_MESH_VALUE(f32, "frequency_offset_mhz", config.frequency_offset_mhz);
    TMS_MESH_VALUE(f32, "override_frequency_mhz", config.override_frequency_mhz);
    TMS_MESH_VALUE(boolean, "enable_relay", config.enable_relay);
    TMS_MESH_VALUE(boolean, "ignore_mqtt", config.ignore_mqtt);
    TMS_MESH_VALUE(boolean, "config_ok_to_mqtt", config.config_ok_to_mqtt);
    TMS_MESH_VALUE(text, "primary_channel_name", config.primary_channel_name);
    TMS_MESH_VALUE(text, "secondary_channel_name", config.secondary_channel_name);
    TMS_MESH_VALUE(u32, "primary_channel_id", config.primary_channel_id);
    TMS_MESH_VALUE(u32, "secondary_channel_id", config.secondary_channel_id);
#undef TMS_MESH_VALUE
    std::snprintf(key, sizeof(key), "%sprimary_psk", prefix);
    if (!encoder.blob(key, config.primary_key, config.primary_key_len))
    {
        return false;
    }
    std::snprintf(key, sizeof(key), "%ssecondary_psk", prefix);
    return encoder.blob(key, config.secondary_key, config.secondary_key_len);
}

bool writeMqtt(Encoder& encoder,
               const char* prefix,
               bool enabled,
               bool uplink,
               bool downlink,
               const char* host,
               uint16_t port,
               const char* root,
               const char* username,
               const char* password)
{
    char key[48] = {};
#define TMS_MQTT_VALUE(method, suffix, value)                                              \
    do                                                                                       \
    {                                                                                        \
        std::snprintf(key, sizeof(key), "%s%s", prefix, suffix);                          \
        if (!encoder.method(key, value))                                                     \
        {                                                                                    \
            return false;                                                                    \
        }                                                                                    \
    } while (false)
    TMS_MQTT_VALUE(boolean, "enabled", enabled);
    TMS_MQTT_VALUE(boolean, "uplink", uplink);
    TMS_MQTT_VALUE(boolean, "downlink", downlink);
    TMS_MQTT_VALUE(text, "host", host);
    TMS_MQTT_VALUE(u16, "port", port);
    TMS_MQTT_VALUE(text, "root", root);
    TMS_MQTT_VALUE(text, "username", username);
    TMS_MQTT_VALUE(text, "password", password);
#undef TMS_MQTT_VALUE
    return true;
}

bool writeMeshCore(Encoder& encoder, const AppConfig& config)
{
    if (!writeMeshBase(encoder, "mc.base.", config.meshcore_config))
    {
        return false;
    }
    const chat::MeshConfig& mesh = config.meshcore_config;
    if (!encoder.u8("mc.region_preset", mesh.meshcore_region_preset) ||
        !encoder.f32("mc.frequency_mhz", mesh.meshcore_freq_mhz) ||
        !encoder.f32("mc.bandwidth_khz", mesh.meshcore_bw_khz) ||
        !encoder.u8("mc.spread_factor", mesh.meshcore_sf) ||
        !encoder.u8("mc.coding_rate", mesh.meshcore_cr) ||
        !encoder.boolean("mc.client_repeat", mesh.meshcore_client_repeat) ||
        !encoder.f32("mc.rx_delay_base", mesh.meshcore_rx_delay_base) ||
        !encoder.f32("mc.airtime_factor", mesh.meshcore_airtime_factor) ||
        !encoder.u8("mc.flood_max", mesh.meshcore_flood_max) ||
        !encoder.boolean("mc.multi_acks", mesh.meshcore_multi_acks) ||
        !encoder.u8("mc.send_profile",
                    static_cast<uint8_t>(mesh.meshcore_send_profile)) ||
        !encoder.u8("mc.forward_profile",
                    static_cast<uint8_t>(mesh.meshcore_forward_profile)) ||
        !encoder.u8("mc.active_slot", mesh.meshcore_channel_slot))
    {
        return false;
    }
    for (std::size_t slot = 0U; slot < chat::kMeshCoreChannelMaxCount; ++slot)
    {
        char key[48] = {};
        const chat::MeshCoreChannelConfig& channel = mesh.meshCoreChannel(static_cast<uint8_t>(slot));
        std::snprintf(key, sizeof(key), "mc.channel.%u.enabled", static_cast<unsigned>(slot));
        if (!encoder.boolean(key, channel.enabled))
        {
            return false;
        }
        std::snprintf(key, sizeof(key), "mc.channel.%u.name", static_cast<unsigned>(slot));
        if (!encoder.text(key, channel.name))
        {
            return false;
        }
        std::snprintf(key, sizeof(key), "mc.channel.%u.key", static_cast<unsigned>(slot));
        if (!encoder.blob(key, channel.key, sizeof(channel.key)))
        {
            return false;
        }
    }
    return writeMqtt(encoder,
                     "mc.mqtt.",
                     mesh.meshcore_mqtt_enabled,
                     mesh.meshcore_mqtt_uplink_enabled,
                     mesh.meshcore_mqtt_downlink_enabled,
                     mesh.meshcore_mqtt_host,
                     mesh.meshcore_mqtt_port,
                     mesh.meshcore_mqtt_root,
                     mesh.meshcore_mqtt_username,
                     mesh.meshcore_mqtt_password);
}

bool writeReticulum(Encoder& encoder, const AppConfig& config)
{
    const chat::MeshConfig& mesh = config.reticulumConfig();
    return writeMeshBase(encoder, "rt.", mesh) &&
           encoder.boolean("rt.lora_enabled", mesh.reticulum_lora_enabled) &&
           encoder.boolean("rt.wifi_enabled", mesh.reticulum_wifi_gateway_enabled) &&
           encoder.boolean("rt.wifi_auto_connect", mesh.reticulum_wifi_auto_connect) &&
           encoder.boolean("rt.anonymous_peer", mesh.reticulum_anonymous_peer) &&
           encoder.text("rt.wifi_host", mesh.reticulum_wifi_gateway_host) &&
           encoder.u16("rt.wifi_port", mesh.reticulum_wifi_gateway_port) &&
           encoder.u8("rt.interface_policy",
                      static_cast<uint8_t>(mesh.reticulum_interface_policy)) &&
           encoder.boolean("rt.allow_location_requests",
                           mesh.reticulum_allow_location_requests);
}

bool writeGeneral(Encoder& encoder, const AppConfig& config)
{
    const app::AprsConfig& aprs = config.aprs;
    return encoder.boolean("policy.relay", config.chat_policy.enable_relay) &&
           encoder.u8("policy.hop_limit", config.chat_policy.hop_limit_default) &&
           encoder.boolean("policy.ack_broadcast", config.chat_policy.ack_for_broadcast) &&
           encoder.boolean("policy.ack_squad", config.chat_policy.ack_for_squad) &&
           encoder.u8("policy.max_retries", config.chat_policy.max_tx_retries) &&
           encoder.u8("policy.max_channels", config.chat_policy.max_channels) &&
           encoder.u8("protocol.active", static_cast<uint8_t>(config.mesh_protocol)) &&
           encoder.text("device.node_name", config.node_name) &&
           encoder.text("device.short_name", config.short_name) &&
           encoder.boolean("device.ble_enabled", config.ble_enabled) &&
           encoder.boolean("channel.primary_enabled", config.primary_enabled) &&
           encoder.boolean("channel.secondary_enabled", config.secondary_enabled) &&
           encoder.boolean("channel.primary_uplink", config.primary_uplink_enabled) &&
           encoder.boolean("channel.primary_downlink", config.primary_downlink_enabled) &&
           encoder.boolean("channel.secondary_uplink", config.secondary_uplink_enabled) &&
           encoder.boolean("channel.secondary_downlink", config.secondary_downlink_enabled) &&
           encoder.boolean("channel.primary_has_module", config.primary_channel_has_module_settings) &&
           encoder.u32("channel.primary_position_precision",
                       config.primary_channel_position_precision) &&
           encoder.boolean("channel.primary_muted", config.primary_channel_is_muted) &&
           encoder.boolean("channel.secondary_has_module", config.secondary_channel_has_module_settings) &&
           encoder.u32("channel.secondary_position_precision",
                       config.secondary_channel_position_precision) &&
           encoder.boolean("channel.secondary_muted", config.secondary_channel_is_muted) &&
           encoder.blob("legacy.secondary_psk",
                        config.secondary_key,
                        sizeof(config.secondary_key)) &&
           encoder.boolean("gps.enabled", config.gps_enabled) &&
           encoder.u32("gps.init_baud", config.gps_init_baud) &&
           encoder.u32("gps.init_probe_ms", config.gps_init_probe_ms) &&
           encoder.u8("gps.init_profile", config.gps_init_profile) &&
           encoder.u8("gps.init_rxm_policy", config.gps_init_rxm_policy) &&
           encoder.u8("gps.init_gnss_policy", config.gps_init_gnss_policy) &&
           encoder.u8("gps.init_nmea_policy", config.gps_init_nmea_policy) &&
           encoder.u32("gps.interval_ms", config.gps_interval_ms) &&
           encoder.u8("gps.mode", config.gps_mode) &&
           encoder.u8("gps.sat_mask", config.gps_sat_mask) &&
           encoder.u8("gps.strategy", config.gps_strategy) &&
           encoder.u8("gps.alt_ref", config.gps_alt_ref) &&
           encoder.u8("gps.coord_format", config.gps_coord_format) &&
           encoder.u32("gps.motion_idle_ms", config.motion_config.idle_timeout_ms) &&
           encoder.u8("gps.motion_sensor", config.motion_config.sensor_id) &&
           encoder.u8("gps.external_nmea_hz", config.external_nmea_output_hz) &&
           encoder.u8("gps.external_nmea_mask", config.external_nmea_sentence_mask) &&
           encoder.u8("map.coord_system", config.map_coord_system) &&
           encoder.u8("map.source", config.map_source) &&
           encoder.boolean("map.contour_enabled", config.map_contour_enabled) &&
           encoder.boolean("map.track_enabled", config.map_track_enabled) &&
           encoder.u8("map.track_interval", config.map_track_interval) &&
           encoder.u8("map.track_format", config.map_track_format) &&
           encoder.u8("chat.channel", config.chat_channel) &&
           encoder.boolean("network.duty_cycle", config.net_duty_cycle) &&
           encoder.u8("network.channel_util", config.net_channel_util) &&
           encoder.u8("privacy.encrypt_mode", config.privacy_encrypt_mode) &&
           encoder.boolean("route.enabled", config.route_enabled) &&
           encoder.text("route.path", config.route_path) &&
           encoder.boolean("aprs.enabled", aprs.enabled) &&
           encoder.text("aprs.igate_callsign", aprs.igate_callsign) &&
           encoder.u8("aprs.igate_ssid", aprs.igate_ssid) &&
           encoder.text("aprs.tocall", aprs.tocall) &&
           encoder.text("aprs.path", aprs.path) &&
           encoder.u16("aprs.tx_min_interval_s", aprs.tx_min_interval_s) &&
           encoder.u16("aprs.dedupe_window_s", aprs.dedupe_window_s) &&
           encoder.u8("aprs.symbol_table", static_cast<uint8_t>(aprs.symbol_table)) &&
           encoder.u8("aprs.symbol_code", static_cast<uint8_t>(aprs.symbol_code)) &&
           encoder.u16("aprs.position_interval_s", aprs.position_interval_s) &&
           encoder.blob("aprs.node_map", aprs.node_map, aprs.node_map_len) &&
           encoder.boolean("aprs.self_enabled", aprs.self_enable) &&
           encoder.text("aprs.self_callsign", aprs.self_callsign);
}

bool applyMeshBase(const char* field,
                   const char* type,
                   const char* value,
                   chat::MeshConfig* target)
{
    if (std::strcmp(field, "region") == 0)
        return assignU8(type, value, target ? &target->region : nullptr);
    if (std::strcmp(field, "use_preset") == 0)
        return assignBool(type, value, target ? &target->use_preset : nullptr);
    if (std::strcmp(field, "modem_preset") == 0)
        return assignU8(type, value, target ? &target->modem_preset : nullptr);
    if (std::strcmp(field, "bandwidth_khz") == 0)
        return assignFloat(type, value, target ? &target->bandwidth_khz : nullptr);
    if (std::strcmp(field, "spread_factor") == 0)
        return assignU8(type, value, target ? &target->spread_factor : nullptr);
    if (std::strcmp(field, "coding_rate") == 0)
        return assignU8(type, value, target ? &target->coding_rate : nullptr);
    if (std::strcmp(field, "tx_power") == 0)
        return assignI8(type, value, target ? &target->tx_power : nullptr);
    if (std::strcmp(field, "hop_limit") == 0)
        return assignU8(type, value, target ? &target->hop_limit : nullptr);
    if (std::strcmp(field, "tx_enabled") == 0)
        return assignBool(type, value, target ? &target->tx_enabled : nullptr);
    if (std::strcmp(field, "override_duty_cycle") == 0)
        return assignBool(type, value, target ? &target->override_duty_cycle : nullptr);
    if (std::strcmp(field, "channel_num") == 0)
        return assignU16(type, value, target ? &target->channel_num : nullptr);
    if (std::strcmp(field, "frequency_offset_mhz") == 0)
        return assignFloat(type, value, target ? &target->frequency_offset_mhz : nullptr);
    if (std::strcmp(field, "override_frequency_mhz") == 0)
        return assignFloat(type, value, target ? &target->override_frequency_mhz : nullptr);
    if (std::strcmp(field, "enable_relay") == 0)
        return assignBool(type, value, target ? &target->enable_relay : nullptr);
    if (std::strcmp(field, "ignore_mqtt") == 0)
        return assignBool(type, value, target ? &target->ignore_mqtt : nullptr);
    if (std::strcmp(field, "config_ok_to_mqtt") == 0)
        return assignBool(type, value, target ? &target->config_ok_to_mqtt : nullptr);
    if (std::strcmp(field, "primary_channel_name") == 0)
        return assignText(type,
                          value,
                          target ? target->primary_channel_name : nullptr,
                          sizeof(chat::MeshConfig::primary_channel_name));
    if (std::strcmp(field, "secondary_channel_name") == 0)
        return assignText(type,
                          value,
                          target ? target->secondary_channel_name : nullptr,
                          sizeof(chat::MeshConfig::secondary_channel_name));
    if (std::strcmp(field, "primary_channel_id") == 0)
        return assignU32(type, value, target ? &target->primary_channel_id : nullptr);
    if (std::strcmp(field, "secondary_channel_id") == 0)
        return assignU32(type, value, target ? &target->secondary_channel_id : nullptr);
    if (std::strcmp(field, "primary_psk") == 0 || std::strcmp(field, "secondary_psk") == 0)
    {
        uint8_t* key = nullptr;
        uint8_t* key_len = nullptr;
        if (target)
        {
            const bool primary = std::strcmp(field, "primary_psk") == 0;
            key = primary ? target->primary_key : target->secondary_key;
            key_len = primary ? &target->primary_key_len : &target->secondary_key_len;
        }
        std::size_t decoded = 0U;
        if (!assignBlob(type, value, key, chat::kMeshtasticChannelKeyMaxLen, &decoded) ||
            (decoded != 0U && decoded != chat::kMeshtasticChannelKeyDefaultLen &&
             decoded != chat::kMeshtasticChannelKeyMaxLen))
        {
            return false;
        }
        if (key_len)
        {
            *key_len = static_cast<uint8_t>(decoded);
        }
        return true;
    }
    return false;
}

bool applyMqtt(const char* field,
               const char* type,
               const char* value,
               bool* enabled,
               bool* uplink,
               bool* downlink,
               char* host,
               std::size_t host_size,
               uint16_t* port,
               char* root,
               std::size_t root_size,
               char* username,
               std::size_t username_size,
               char* password,
               std::size_t password_size)
{
    if (std::strcmp(field, "enabled") == 0)
        return assignBool(type, value, enabled);
    if (std::strcmp(field, "uplink") == 0)
        return assignBool(type, value, uplink);
    if (std::strcmp(field, "downlink") == 0)
        return assignBool(type, value, downlink);
    if (std::strcmp(field, "host") == 0)
        return assignText(type, value, host, host_size);
    if (std::strcmp(field, "port") == 0)
        return assignU16(type, value, port);
    if (std::strcmp(field, "root") == 0)
        return assignText(type, value, root, root_size);
    if (std::strcmp(field, "username") == 0)
        return assignText(type, value, username, username_size);
    if (std::strcmp(field, "password") == 0)
        return assignText(type, value, password, password_size);
    return false;
}

bool isMqttField(const char* field)
{
    return std::strcmp(field, "enabled") == 0 || std::strcmp(field, "uplink") == 0 ||
           std::strcmp(field, "downlink") == 0 || std::strcmp(field, "host") == 0 ||
           std::strcmp(field, "port") == 0 || std::strcmp(field, "root") == 0 ||
           std::strcmp(field, "username") == 0 || std::strcmp(field, "password") == 0;
}

bool isMeshBaseField(const char* field)
{
    static constexpr const char* kFields[] = {
        "region", "use_preset", "modem_preset", "bandwidth_khz", "spread_factor",
        "coding_rate", "tx_power", "hop_limit", "tx_enabled", "override_duty_cycle",
        "channel_num", "frequency_offset_mhz", "override_frequency_mhz", "enable_relay",
        "ignore_mqtt", "config_ok_to_mqtt", "primary_channel_name",
        "secondary_channel_name", "primary_channel_id", "secondary_channel_id",
        "primary_psk", "secondary_psk",
    };
    for (const char* candidate : kFields)
    {
        if (std::strcmp(field, candidate) == 0)
        {
            return true;
        }
    }
    return false;
}

bool isMeshCoreChannelField(const char* field)
{
    return std::strcmp(field, "enabled") == 0 || std::strcmp(field, "name") == 0 ||
           std::strcmp(field, "key") == 0;
}

bool parseMeshCoreChannel(const char* key,
                          const char* type,
                          const char* value,
                          chat::MeshConfig* target)
{
    static constexpr const char* kPrefix = "mc.channel.";
    if (std::strncmp(key, kPrefix, std::strlen(kPrefix)) != 0)
    {
        return false;
    }
    const char* slot_text = key + std::strlen(kPrefix);
    if (slot_text[0] < '0' || slot_text[0] > '7' || slot_text[1] != '.')
    {
        return false;
    }
    const uint8_t slot = static_cast<uint8_t>(slot_text[0] - '0');
    const char* field = slot_text + 2;
    chat::MeshCoreChannelConfig* channel =
        target ? &target->meshCoreChannel(slot) : nullptr;
    if (std::strcmp(field, "enabled") == 0)
    {
        return assignBool(type, value, channel ? &channel->enabled : nullptr);
    }
    if (std::strcmp(field, "name") == 0)
    {
        return assignText(type,
                          value,
                          channel ? channel->name : nullptr,
                          chat::kMeshCoreChannelNameMaxLen);
    }
    if (std::strcmp(field, "key") == 0)
    {
        return assignBlob(type,
                          value,
                          channel ? channel->key : nullptr,
                          chat::kMeshCoreChannelKeyLen,
                          nullptr,
                          chat::kMeshCoreChannelKeyLen);
    }
    return false;
}

} // namespace

RecordWriter::RecordWriter(Output output, LineScratch& scratch, DocumentInfo* info)
    : output_(output), scratch_(scratch), info_(info)
{
}

bool RecordWriter::boolean(const char* key, bool value)
{
    return Encoder(output_, scratch_, info_).boolean(key, value);
}

bool RecordWriter::u8(const char* key, uint8_t value)
{
    return Encoder(output_, scratch_, info_).u8(key, value);
}

bool RecordWriter::i8(const char* key, int8_t value)
{
    return Encoder(output_, scratch_, info_).i8(key, value);
}

bool RecordWriter::i32(const char* key, int32_t value)
{
    return Encoder(output_, scratch_, info_).i32(key, value);
}

bool RecordWriter::u16(const char* key, uint16_t value)
{
    return Encoder(output_, scratch_, info_).u16(key, value);
}

bool RecordWriter::u32(const char* key, uint32_t value)
{
    return Encoder(output_, scratch_, info_).u32(key, value);
}

bool RecordWriter::f32(const char* key, float value)
{
    return Encoder(output_, scratch_, info_).f32(key, value);
}

bool RecordWriter::enumeration(const char* key, const char* value)
{
    return Encoder(output_, scratch_, info_).enumeration(key, value);
}

bool RecordWriter::text(const char* key, const char* value)
{
    return Encoder(output_, scratch_, info_).text(key, value);
}

bool RecordWriter::blob(const char* key, const uint8_t* value, std::size_t length)
{
    return Encoder(output_, scratch_, info_).blob(key, value, length);
}

bool writeDocument(const AppConfig& config,
                   DocumentKind kind,
                   Output output,
                   LineScratch& scratch,
                   DocumentInfo* info,
                   RecordExtension extension,
                   void* extension_context)
{
    if (info)
    {
        *info = DocumentInfo{};
    }
    Encoder encoder(output, scratch, info);
    if (!encoder.raw(kMagic) || !encoder.u16("schema.version", kSchemaVersion) ||
        !encoder.enumeration("document.kind", kind == DocumentKind::Working ? kWorkingKind
                                                                             : kBackupKind) ||
        !writeGeneral(encoder, config) || !writeMeshBase(encoder, "mt.", config.meshtastic_config) ||
        !writeMqtt(encoder,
                   "mt.mqtt.",
                   config.meshtastic_mqtt_enabled,
                   config.meshtastic_mqtt_uplink_enabled,
                   config.meshtastic_mqtt_downlink_enabled,
                   config.meshtastic_mqtt_host,
                   config.meshtastic_mqtt_port,
                   config.meshtastic_mqtt_root,
                   config.meshtastic_mqtt_username,
                   config.meshtastic_mqtt_password) ||
        !writeMeshCore(encoder, config) || !writeReticulum(encoder, config))
    {
        return false;
    }
    RecordWriter extension_writer(output, scratch, info);
    return (!extension || extension(extension_context, extension_writer)) && encoder.raw("END");
}

Decoder::Decoder(AppConfig* target, DocumentKind expected_kind)
    : target_(target), expected_kind_(expected_kind)
{
}

bool Decoder::consumeLine(char* line)
{
    if (!line || info_.error != DecodeError::None)
    {
        return false;
    }
    if (std::strlen(line) >= kMaxLineBytes)
    {
        info_.error = DecodeError::MalformedRecord;
        return false;
    }
    if (saw_end_)
    {
        if (line[0] == '\0' || line[0] == '#')
        {
            return true;
        }
        info_.error = DecodeError::LineAfterEnd;
        return false;
    }
    if (line[0] == '\0' || line[0] == '#')
    {
        return true;
    }
    if (!saw_magic_)
    {
        if (std::strcmp(line, kMagic) != 0)
        {
            info_.error = DecodeError::MissingMagic;
            return false;
        }
        saw_magic_ = true;
        return true;
    }
    if (std::strcmp(line, "END") == 0)
    {
        saw_end_ = true;
        return true;
    }
    char* separator = std::strchr(line, '=');
    if (!separator || separator == line)
    {
        info_.error = DecodeError::MalformedRecord;
        return false;
    }
    *separator = '\0';
    char* type = separator + 1;
    char* colon = std::strchr(type, ':');
    if (!colon || colon == type)
    {
        info_.error = DecodeError::MalformedRecord;
        return false;
    }
    *colon = '\0';
    if (++info_.records > kMaxRecords)
    {
        info_.error = DecodeError::TooManyRecords;
        return false;
    }
    const bool accepted = consumeRecord(line, type, colon + 1);
    if (!accepted && info_.error == DecodeError::None)
    {
        info_.error = DecodeError::InvalidKnownValue;
    }
    return accepted;
}

bool Decoder::finish()
{
    if (info_.error != DecodeError::None)
    {
        return false;
    }
    if (!saw_magic_)
    {
        info_.error = DecodeError::MissingMagic;
        return false;
    }
    if (!saw_schema_)
    {
        info_.error = DecodeError::UnsupportedSchema;
        return false;
    }
    if (!saw_kind_)
    {
        info_.error = DecodeError::InvalidDocumentKind;
        return false;
    }
    if (!saw_end_)
    {
        info_.error = DecodeError::MissingEnd;
        return false;
    }
    if (target_)
    {
        target_->meshcore_config.meshCoreChannel(0).enabled = true;
        target_->meshcore_config.syncMeshCoreLegacyChannelMirror();
    }
    return true;
}

bool Decoder::consumeRecord(char* key, char* type, char* value)
{
    if (std::strcmp(key, "schema.version") == 0)
    {
        uint16_t version = 0U;
        if (!assignU16(type, value, &version) || version != kSchemaVersion)
        {
            info_.error = DecodeError::UnsupportedSchema;
            return false;
        }
        saw_schema_ = true;
        return true;
    }
    if (std::strcmp(key, "document.kind") == 0)
    {
        const char* expected = expected_kind_ == DocumentKind::Working ? kWorkingKind : kBackupKind;
        if (!typeIs(type, "enum") || std::strcmp(value, expected) != 0)
        {
            info_.error = DecodeError::InvalidDocumentKind;
            return false;
        }
        saw_kind_ = true;
        return true;
    }

    bool known = false;
#define TMS_APPLY_BOOL(name, member)                                                        \
    if (std::strcmp(key, name) == 0)                                                        \
        return assignBool(type, value, target_ ? &target_->member : nullptr)
#define TMS_APPLY_U8(name, member)                                                          \
    if (std::strcmp(key, name) == 0)                                                        \
        return assignU8(type, value, target_ ? &target_->member : nullptr)
#define TMS_APPLY_U16(name, member)                                                         \
    if (std::strcmp(key, name) == 0)                                                        \
        return assignU16(type, value, target_ ? &target_->member : nullptr)
#define TMS_APPLY_U32(name, member)                                                         \
    if (std::strcmp(key, name) == 0)                                                        \
        return assignU32(type, value, target_ ? &target_->member : nullptr)
#define TMS_APPLY_TEXT(name, member)                                                        \
    if (std::strcmp(key, name) == 0)                                                        \
        return assignText(type, value, target_ ? target_->member : nullptr,                 \
                          sizeof(AppConfig::member))
    TMS_APPLY_BOOL("policy.relay", chat_policy.enable_relay);
    TMS_APPLY_U8("policy.hop_limit", chat_policy.hop_limit_default);
    TMS_APPLY_BOOL("policy.ack_broadcast", chat_policy.ack_for_broadcast);
    TMS_APPLY_BOOL("policy.ack_squad", chat_policy.ack_for_squad);
    TMS_APPLY_U8("policy.max_retries", chat_policy.max_tx_retries);
    TMS_APPLY_U8("policy.max_channels", chat_policy.max_channels);
    if (std::strcmp(key, "protocol.active") == 0)
    {
        uint8_t raw = 0U;
        if (!assignU8(type, value, &raw) || !chat::infra::isValidMeshProtocolValue(raw))
        {
            info_.error = DecodeError::InvalidKnownValue;
            return false;
        }
        if (target_)
        {
            target_->mesh_protocol = chat::infra::meshProtocolFromRaw(raw);
        }
        return true;
    }
    TMS_APPLY_TEXT("device.node_name", node_name);
    TMS_APPLY_TEXT("device.short_name", short_name);
    TMS_APPLY_BOOL("device.ble_enabled", ble_enabled);
    TMS_APPLY_BOOL("channel.primary_enabled", primary_enabled);
    TMS_APPLY_BOOL("channel.secondary_enabled", secondary_enabled);
    TMS_APPLY_BOOL("channel.primary_uplink", primary_uplink_enabled);
    TMS_APPLY_BOOL("channel.primary_downlink", primary_downlink_enabled);
    TMS_APPLY_BOOL("channel.secondary_uplink", secondary_uplink_enabled);
    TMS_APPLY_BOOL("channel.secondary_downlink", secondary_downlink_enabled);
    TMS_APPLY_BOOL("channel.primary_has_module", primary_channel_has_module_settings);
    TMS_APPLY_U32("channel.primary_position_precision", primary_channel_position_precision);
    TMS_APPLY_BOOL("channel.primary_muted", primary_channel_is_muted);
    TMS_APPLY_BOOL("channel.secondary_has_module", secondary_channel_has_module_settings);
    TMS_APPLY_U32("channel.secondary_position_precision", secondary_channel_position_precision);
    TMS_APPLY_BOOL("channel.secondary_muted", secondary_channel_is_muted);
    if (std::strcmp(key, "legacy.secondary_psk") == 0)
    {
        return assignBlob(type,
                          value,
                          target_ ? target_->secondary_key : nullptr,
                          sizeof(AppConfig::secondary_key),
                          nullptr,
                          sizeof(AppConfig::secondary_key));
    }
    TMS_APPLY_BOOL("gps.enabled", gps_enabled);
    TMS_APPLY_U32("gps.init_baud", gps_init_baud);
    TMS_APPLY_U32("gps.init_probe_ms", gps_init_probe_ms);
    TMS_APPLY_U8("gps.init_profile", gps_init_profile);
    TMS_APPLY_U8("gps.init_rxm_policy", gps_init_rxm_policy);
    TMS_APPLY_U8("gps.init_gnss_policy", gps_init_gnss_policy);
    TMS_APPLY_U8("gps.init_nmea_policy", gps_init_nmea_policy);
    TMS_APPLY_U32("gps.interval_ms", gps_interval_ms);
    TMS_APPLY_U8("gps.mode", gps_mode);
    TMS_APPLY_U8("gps.sat_mask", gps_sat_mask);
    TMS_APPLY_U8("gps.strategy", gps_strategy);
    TMS_APPLY_U8("gps.alt_ref", gps_alt_ref);
    TMS_APPLY_U8("gps.coord_format", gps_coord_format);
    if (std::strcmp(key, "gps.motion_idle_ms") == 0)
        return assignU32(type, value, target_ ? &target_->motion_config.idle_timeout_ms : nullptr);
    if (std::strcmp(key, "gps.motion_sensor") == 0)
        return assignU8(type, value, target_ ? &target_->motion_config.sensor_id : nullptr);
    TMS_APPLY_U8("gps.external_nmea_hz", external_nmea_output_hz);
    TMS_APPLY_U8("gps.external_nmea_mask", external_nmea_sentence_mask);
    TMS_APPLY_U8("map.coord_system", map_coord_system);
    TMS_APPLY_U8("map.source", map_source);
    TMS_APPLY_BOOL("map.contour_enabled", map_contour_enabled);
    TMS_APPLY_BOOL("map.track_enabled", map_track_enabled);
    TMS_APPLY_U8("map.track_interval", map_track_interval);
    TMS_APPLY_U8("map.track_format", map_track_format);
    TMS_APPLY_U8("chat.channel", chat_channel);
    TMS_APPLY_BOOL("network.duty_cycle", net_duty_cycle);
    TMS_APPLY_U8("network.channel_util", net_channel_util);
    TMS_APPLY_U8("privacy.encrypt_mode", privacy_encrypt_mode);
    TMS_APPLY_BOOL("route.enabled", route_enabled);
    TMS_APPLY_TEXT("route.path", route_path);
    if (std::strcmp(key, "aprs.enabled") == 0)
        return assignBool(type, value, target_ ? &target_->aprs.enabled : nullptr);
    if (std::strcmp(key, "aprs.igate_callsign") == 0)
        return assignText(type, value, target_ ? target_->aprs.igate_callsign : nullptr,
                          sizeof(AprsConfig::igate_callsign));
    if (std::strcmp(key, "aprs.igate_ssid") == 0)
        return assignU8(type, value, target_ ? &target_->aprs.igate_ssid : nullptr);
    if (std::strcmp(key, "aprs.tocall") == 0)
        return assignText(type, value, target_ ? target_->aprs.tocall : nullptr,
                          sizeof(AprsConfig::tocall));
    if (std::strcmp(key, "aprs.path") == 0)
        return assignText(type, value, target_ ? target_->aprs.path : nullptr,
                          sizeof(AprsConfig::path));
    if (std::strcmp(key, "aprs.tx_min_interval_s") == 0)
        return assignU16(type, value, target_ ? &target_->aprs.tx_min_interval_s : nullptr);
    if (std::strcmp(key, "aprs.dedupe_window_s") == 0)
        return assignU16(type, value, target_ ? &target_->aprs.dedupe_window_s : nullptr);
    if (std::strcmp(key, "aprs.symbol_table") == 0)
    {
        uint8_t raw = 0U;
        if (!assignU8(type, value, &raw)) return false;
        if (target_) target_->aprs.symbol_table = static_cast<char>(raw);
        return true;
    }
    if (std::strcmp(key, "aprs.symbol_code") == 0)
    {
        uint8_t raw = 0U;
        if (!assignU8(type, value, &raw)) return false;
        if (target_) target_->aprs.symbol_code = static_cast<char>(raw);
        return true;
    }
    if (std::strcmp(key, "aprs.position_interval_s") == 0)
        return assignU16(type, value, target_ ? &target_->aprs.position_interval_s : nullptr);
    if (std::strcmp(key, "aprs.node_map") == 0)
    {
        std::size_t length = 0U;
        if (!assignBlob(type,
                        value,
                        target_ ? target_->aprs.node_map : nullptr,
                        sizeof(AprsConfig::node_map),
                        &length))
            return false;
        if (target_) target_->aprs.node_map_len = static_cast<uint8_t>(length);
        return true;
    }
    if (std::strcmp(key, "aprs.self_enabled") == 0)
        return assignBool(type, value, target_ ? &target_->aprs.self_enable : nullptr);
    if (std::strcmp(key, "aprs.self_callsign") == 0)
        return assignText(type, value, target_ ? target_->aprs.self_callsign : nullptr,
                          sizeof(AprsConfig::self_callsign));
#undef TMS_APPLY_BOOL
#undef TMS_APPLY_U8
#undef TMS_APPLY_U16
#undef TMS_APPLY_U32
#undef TMS_APPLY_TEXT

    if (std::strncmp(key, "mt.mqtt.", 8U) == 0)
    {
        known = applyMqtt(key + 8U,
                          type,
                          value,
                          target_ ? &target_->meshtastic_mqtt_enabled : nullptr,
                          target_ ? &target_->meshtastic_mqtt_uplink_enabled : nullptr,
                          target_ ? &target_->meshtastic_mqtt_downlink_enabled : nullptr,
                          target_ ? target_->meshtastic_mqtt_host : nullptr,
                          sizeof(AppConfig::meshtastic_mqtt_host),
                          target_ ? &target_->meshtastic_mqtt_port : nullptr,
                          target_ ? target_->meshtastic_mqtt_root : nullptr,
                          sizeof(AppConfig::meshtastic_mqtt_root),
                          target_ ? target_->meshtastic_mqtt_username : nullptr,
                          sizeof(AppConfig::meshtastic_mqtt_username),
                          target_ ? target_->meshtastic_mqtt_password : nullptr,
                          sizeof(AppConfig::meshtastic_mqtt_password));
        if (!known && isMqttField(key + 8U))
        {
            info_.error = DecodeError::InvalidKnownValue;
            return false;
        }
    }
    else if (std::strncmp(key, "mt.", 3U) == 0)
    {
        known = applyMeshBase(key + 3U, type, value,
                              target_ ? &target_->meshtastic_config : nullptr);
        if (!known && isMeshBaseField(key + 3U))
        {
            info_.error = DecodeError::InvalidKnownValue;
            return false;
        }
    }
    else if (std::strncmp(key, "mc.base.", 8U) == 0)
    {
        known = applyMeshBase(key + 8U, type, value,
                              target_ ? &target_->meshcore_config : nullptr);
        if (!known && isMeshBaseField(key + 8U))
        {
            info_.error = DecodeError::InvalidKnownValue;
            return false;
        }
    }
    else if (std::strncmp(key, "mc.channel.", 11U) == 0)
    {
        const char* field = key + 11U;
        const bool recognizable = field[0] >= '0' && field[0] <= '7' && field[1] == '.' &&
                                  isMeshCoreChannelField(field + 2U);
        known = parseMeshCoreChannel(key, type, value,
                                     target_ ? &target_->meshcore_config : nullptr);
        if (!known && recognizable)
        {
            info_.error = DecodeError::InvalidKnownValue;
            return false;
        }
    }
    else if (std::strncmp(key, "mc.mqtt.", 8U) == 0)
    {
        chat::MeshConfig* mesh = target_ ? &target_->meshcore_config : nullptr;
        known = applyMqtt(key + 8U,
                          type,
                          value,
                          mesh ? &mesh->meshcore_mqtt_enabled : nullptr,
                          mesh ? &mesh->meshcore_mqtt_uplink_enabled : nullptr,
                          mesh ? &mesh->meshcore_mqtt_downlink_enabled : nullptr,
                          mesh ? mesh->meshcore_mqtt_host : nullptr,
                          sizeof(chat::MeshConfig::meshcore_mqtt_host),
                          mesh ? &mesh->meshcore_mqtt_port : nullptr,
                          mesh ? mesh->meshcore_mqtt_root : nullptr,
                          sizeof(chat::MeshConfig::meshcore_mqtt_root),
                          mesh ? mesh->meshcore_mqtt_username : nullptr,
                          sizeof(chat::MeshConfig::meshcore_mqtt_username),
                          mesh ? mesh->meshcore_mqtt_password : nullptr,
                          sizeof(chat::MeshConfig::meshcore_mqtt_password));
        if (!known && isMqttField(key + 8U))
        {
            info_.error = DecodeError::InvalidKnownValue;
            return false;
        }
    }
    else if (std::strncmp(key, "mc.", 3U) == 0)
    {
        chat::MeshConfig* mesh = target_ ? &target_->meshcore_config : nullptr;
        const char* field = key + 3U;
        known = true;
        if (std::strcmp(field, "region_preset") == 0)
            known = assignU8(type, value, mesh ? &mesh->meshcore_region_preset : nullptr);
        else if (std::strcmp(field, "frequency_mhz") == 0)
            known = assignFloat(type, value, mesh ? &mesh->meshcore_freq_mhz : nullptr);
        else if (std::strcmp(field, "bandwidth_khz") == 0)
            known = assignFloat(type, value, mesh ? &mesh->meshcore_bw_khz : nullptr);
        else if (std::strcmp(field, "spread_factor") == 0)
            known = assignU8(type, value, mesh ? &mesh->meshcore_sf : nullptr);
        else if (std::strcmp(field, "coding_rate") == 0)
            known = assignU8(type, value, mesh ? &mesh->meshcore_cr : nullptr);
        else if (std::strcmp(field, "client_repeat") == 0)
            known = assignBool(type, value, mesh ? &mesh->meshcore_client_repeat : nullptr);
        else if (std::strcmp(field, "rx_delay_base") == 0)
            known = assignFloat(type, value, mesh ? &mesh->meshcore_rx_delay_base : nullptr);
        else if (std::strcmp(field, "airtime_factor") == 0)
            known = assignFloat(type, value, mesh ? &mesh->meshcore_airtime_factor : nullptr);
        else if (std::strcmp(field, "flood_max") == 0)
            known = assignU8(type, value, mesh ? &mesh->meshcore_flood_max : nullptr);
        else if (std::strcmp(field, "multi_acks") == 0)
            known = assignBool(type, value, mesh ? &mesh->meshcore_multi_acks : nullptr);
        else if (std::strcmp(field, "send_profile") == 0 ||
                 std::strcmp(field, "forward_profile") == 0 ||
                 std::strcmp(field, "active_slot") == 0)
        {
            uint8_t raw = 0U;
            known = assignU8(type, value, &raw);
            if (known && mesh)
            {
                if (std::strcmp(field, "send_profile") == 0)
                    mesh->meshcore_send_profile = static_cast<chat::MeshCorePayloadSendProfile>(raw);
                else if (std::strcmp(field, "forward_profile") == 0)
                    mesh->meshcore_forward_profile = static_cast<chat::MeshCoreForwardProfile>(raw);
                else if (raw < chat::kMeshCoreChannelMaxCount)
                    mesh->meshcore_channel_slot = raw;
                else
                    known = false;
            }
            else if (known && std::strcmp(field, "active_slot") == 0 &&
                     raw >= chat::kMeshCoreChannelMaxCount)
            {
                known = false;
            }
        }
        else
            known = applyMeshBase(field, type, value, mesh);
        const bool custom_field = std::strcmp(field, "region_preset") == 0 ||
                                  std::strcmp(field, "frequency_mhz") == 0 ||
                                  std::strcmp(field, "bandwidth_khz") == 0 ||
                                  std::strcmp(field, "spread_factor") == 0 ||
                                  std::strcmp(field, "coding_rate") == 0 ||
                                  std::strcmp(field, "client_repeat") == 0 ||
                                  std::strcmp(field, "rx_delay_base") == 0 ||
                                  std::strcmp(field, "airtime_factor") == 0 ||
                                  std::strcmp(field, "flood_max") == 0 ||
                                  std::strcmp(field, "multi_acks") == 0 ||
                                  std::strcmp(field, "send_profile") == 0 ||
                                  std::strcmp(field, "forward_profile") == 0 ||
                                  std::strcmp(field, "active_slot") == 0;
        if (!known && (custom_field || isMeshBaseField(field)))
        {
            info_.error = DecodeError::InvalidKnownValue;
            return false;
        }
    }
    else if (std::strncmp(key, "rt.", 3U) == 0)
    {
        chat::MeshConfig* mesh = target_ ? &target_->reticulumConfig() : nullptr;
        const char* field = key + 3U;
        known = true;
        if (std::strcmp(field, "lora_enabled") == 0)
            known = assignBool(type, value, mesh ? &mesh->reticulum_lora_enabled : nullptr);
        else if (std::strcmp(field, "wifi_enabled") == 0)
            known = assignBool(type, value, mesh ? &mesh->reticulum_wifi_gateway_enabled : nullptr);
        else if (std::strcmp(field, "wifi_auto_connect") == 0)
            known = assignBool(type, value, mesh ? &mesh->reticulum_wifi_auto_connect : nullptr);
        else if (std::strcmp(field, "anonymous_peer") == 0)
            known = assignBool(type, value, mesh ? &mesh->reticulum_anonymous_peer : nullptr);
        else if (std::strcmp(field, "wifi_host") == 0)
            known = assignText(type,
                               value,
                               mesh ? mesh->reticulum_wifi_gateway_host : nullptr,
                               sizeof(chat::MeshConfig::reticulum_wifi_gateway_host));
        else if (std::strcmp(field, "wifi_port") == 0)
            known = assignU16(type, value, mesh ? &mesh->reticulum_wifi_gateway_port : nullptr);
        else if (std::strcmp(field, "interface_policy") == 0)
        {
            uint8_t raw = 0U;
            known = assignU8(type, value, &raw,
                             static_cast<uint8_t>(chat::ReticulumInterfacePolicy::WifiGatewayOnly));
            if (known && mesh)
                mesh->reticulum_interface_policy = static_cast<chat::ReticulumInterfacePolicy>(raw);
        }
        else if (std::strcmp(field, "allow_location_requests") == 0)
            known = assignBool(type, value, mesh ? &mesh->reticulum_allow_location_requests : nullptr);
        else
            known = applyMeshBase(field, type, value, mesh);
        const bool reticulum_field = std::strcmp(field, "lora_enabled") == 0 ||
                                     std::strcmp(field, "wifi_enabled") == 0 ||
                                     std::strcmp(field, "wifi_auto_connect") == 0 ||
                                     std::strcmp(field, "anonymous_peer") == 0 ||
                                     std::strcmp(field, "wifi_host") == 0 ||
                                     std::strcmp(field, "wifi_port") == 0 ||
                                     std::strcmp(field, "interface_policy") == 0 ||
                                     std::strcmp(field, "allow_location_requests") == 0;
        if (!known && (reticulum_field || isMeshBaseField(field)))
        {
            info_.error = DecodeError::InvalidKnownValue;
            return false;
        }
    }
    if (!known)
    {
        // Unknown keys are forward-compatible.  A known namespace with a
        // malformed value has already returned false from its handler.
        ++info_.unknown_records;
    }
    return true;
}

const char* decodeErrorName(DecodeError error)
{
    switch (error)
    {
    case DecodeError::None:
        return "none";
    case DecodeError::MissingMagic:
        return "missing_magic";
    case DecodeError::UnsupportedSchema:
        return "unsupported_schema";
    case DecodeError::InvalidDocumentKind:
        return "invalid_document_kind";
    case DecodeError::LineAfterEnd:
        return "line_after_end";
    case DecodeError::MissingEnd:
        return "missing_end";
    case DecodeError::MalformedRecord:
        return "malformed_record";
    case DecodeError::InvalidKnownValue:
        return "invalid_known_value";
    case DecodeError::TooManyRecords:
        return "too_many_records";
    }
    return "unknown";
}

} // namespace app::tms
