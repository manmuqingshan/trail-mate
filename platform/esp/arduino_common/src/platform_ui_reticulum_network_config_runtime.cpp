#include "platform/ui/reticulum_network_config_runtime.h"

#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
#include "platform/ui/reticulum_call_runtime.h"

#if defined(ARDUINO)
#include <Arduino.h>
#include <Preferences.h>
#else
#include "esp_timer.h"
#include "nvs.h"
#include "platform/esp/idf_common/bsp_runtime.h"
#endif

#include "cJSON.h"

#include <cstdio>
#include <cstring>

namespace platform::ui::reticulum_network_config
{

namespace
{

constexpr const char* kConfigDirectory = "/trailmate/reticulum";
constexpr const char* kConfigPath = "/trailmate/reticulum/config.json";
constexpr const char* kConfigTempPath = "/trailmate/reticulum/config.tmp";
constexpr const char* kSchema = "trail-mate.reticulum";
constexpr const char* kPreferencesNamespace = "rt_net_cfg";
constexpr const char* kLastKnownGoodKey = "last_good";
constexpr std::size_t kMaxConfigBytes = 2U * 1024U;
constexpr std::size_t kMaxJsonDepth = 5U;
constexpr std::size_t kMaxJsonStructuralTokens = 128U;
constexpr std::size_t kMaxJsonStringBytes = 128U;
constexpr uint32_t kSdProbeIntervalMs = 5000;

chat::reticulum::ReticulumNetworkConfig g_active{};
chat::reticulum::ReticulumNetworkConfig g_parse_scratch{};
Status g_status{};
char g_file_buffer[kMaxConfigBytes + 1U] = {};
bool g_initialized = false;
bool g_sd_checked = false;
bool g_reload_deferred = false;
uint32_t g_last_sd_probe_ms = 0;

uint32_t uptime_ms()
{
#if defined(ARDUINO)
    return millis();
#else
    return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
#endif
}

void copy_bounded(char* out, std::size_t out_len, const char* value)
{
    if (!out || out_len == 0)
    {
        return;
    }
    out[0] = '\0';
    if (!value)
    {
        return;
    }
    const auto max_copy_len = out_len - 1U;
    const auto* terminator = static_cast<const char*>(std::memchr(value, '\0', max_copy_len));
    const std::size_t copy_len =
        terminator ? static_cast<std::size_t>(terminator - value) : max_copy_len;
    std::memcpy(out, value, copy_len);
    out[copy_len] = '\0';
}

void reset_network_config(
    chat::reticulum::ReticulumNetworkConfig& config)
{
    config.version =
        chat::reticulum::ReticulumNetworkConfig::kSchemaVersion;
    for (auto& interface_config : config.interfaces)
    {
        interface_config = chat::reticulum::NetworkInterfaceConfig{};
    }
    config.interface_count = 0;
    config.propagation = chat::reticulum::LxmfPropagationClientConfig{};
}

void set_status(const char* message, const char* detail = nullptr)
{
    copy_bounded(g_status.message, sizeof(g_status.message), message);
    copy_bounded(g_status.detail, sizeof(g_status.detail), detail);
    g_status.reload_deferred = g_reload_deferred;
}

bool is_zero_hash(const uint8_t* hash, std::size_t len)
{
    if (!hash)
    {
        return true;
    }
    for (std::size_t index = 0; index < len; ++index)
    {
        if (hash[index] != 0)
        {
            return false;
        }
    }
    return true;
}

int hex_nibble(char value)
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

bool parse_hash(const char* text, uint8_t* out, std::size_t out_len)
{
    if (!text || !out || std::strlen(text) != out_len * 2U)
    {
        return false;
    }
    for (std::size_t index = 0; index < out_len; ++index)
    {
        const int high = hex_nibble(text[index * 2U]);
        const int low = hex_nibble(text[index * 2U + 1U]);
        if (high < 0 || low < 0)
        {
            return false;
        }
        out[index] = static_cast<uint8_t>((high << 4) | low);
    }
    return true;
}

void format_hash(const uint8_t* hash, std::size_t len, char* out, std::size_t out_len)
{
    static constexpr char kHex[] = "0123456789ABCDEF";
    if (!hash || !out || out_len < len * 2U + 1U)
    {
        return;
    }
    for (std::size_t index = 0; index < len; ++index)
    {
        out[index * 2U] = kHex[(hash[index] >> 4U) & 0x0FU];
        out[index * 2U + 1U] = kHex[hash[index] & 0x0FU];
    }
    out[len * 2U] = '\0';
}

cJSON* object_item(cJSON* object, const char* key)
{
    return cJSON_IsObject(object)
               ? cJSON_GetObjectItemCaseSensitive(object, key)
               : nullptr;
}

bool json_bool(cJSON* object, const char* key, bool fallback)
{
    cJSON* item = object_item(object, key);
    return cJSON_IsBool(item) ? cJSON_IsTrue(item) : fallback;
}

int json_int(cJSON* object, const char* key, int fallback)
{
    cJSON* item = object_item(object, key);
    return cJSON_IsNumber(item) ? item->valueint : fallback;
}

const char* json_string(cJSON* object, const char* key)
{
    cJSON* item = object_item(object, key);
    return cJSON_IsString(item) ? item->valuestring : nullptr;
}

bool validate_json_budget(const char* data,
                          std::size_t len,
                          char* error,
                          std::size_t error_len)
{
    if (!data || len == 0)
    {
        copy_bounded(error, error_len, "Configuration is empty");
        return false;
    }
    if (len > kMaxConfigBytes)
    {
        copy_bounded(error, error_len, "Configuration exceeds 2 KB limit");
        return false;
    }

    std::size_t depth = 0;
    std::size_t structural_tokens = 0;
    std::size_t string_bytes = 0;
    bool in_string = false;
    bool escaped = false;
    for (std::size_t index = 0; index < len; ++index)
    {
        const unsigned char value = static_cast<unsigned char>(data[index]);
        if (value == '\0')
        {
            copy_bounded(error, error_len, "Configuration contains a null byte");
            return false;
        }
        if (in_string)
        {
            if (escaped)
            {
                escaped = false;
                ++string_bytes;
            }
            else if (value == '\\')
            {
                escaped = true;
                ++string_bytes;
            }
            else if (value == '"')
            {
                in_string = false;
            }
            else
            {
                if (value < 0x20U)
                {
                    copy_bounded(error, error_len, "Configuration string contains a control byte");
                    return false;
                }
                ++string_bytes;
            }
            if (string_bytes > kMaxJsonStringBytes)
            {
                copy_bounded(error, error_len, "Configuration string exceeds 128 bytes");
                return false;
            }
            continue;
        }

        if (value == '"')
        {
            in_string = true;
            string_bytes = 0;
            continue;
        }
        if (value == '{' || value == '[')
        {
            ++depth;
            ++structural_tokens;
            if (depth > kMaxJsonDepth)
            {
                copy_bounded(error, error_len, "Configuration nesting exceeds 5 levels");
                return false;
            }
        }
        else if (value == '}' || value == ']')
        {
            if (depth == 0)
            {
                copy_bounded(error, error_len, "Configuration JSON structure is invalid");
                return false;
            }
            --depth;
            ++structural_tokens;
        }
        else if (value == ',' || value == ':')
        {
            ++structural_tokens;
        }

        if (structural_tokens > kMaxJsonStructuralTokens)
        {
            copy_bounded(error, error_len, "Configuration structure exceeds embedded budget");
            return false;
        }
    }

    if (in_string || escaped || depth != 0)
    {
        copy_bounded(error, error_len, "Configuration JSON structure is incomplete");
        return false;
    }
    return true;
}

bool add_json_string(cJSON* object, const char* key, const char* value)
{
    return object && value && cJSON_AddStringToObject(object, key, value);
}

bool add_json_number(cJSON* object, const char* key, double value)
{
    return object && cJSON_AddNumberToObject(object, key, value);
}

bool add_json_bool(cJSON* object, const char* key, bool value)
{
    return object && cJSON_AddBoolToObject(object, key, value);
}

bool append_default_interface(chat::reticulum::NetworkInterfaceType type,
                              const char* id,
                              chat::reticulum::NetworkInterfaceConfig** out)
{
    if (g_parse_scratch.interface_count >= chat::reticulum::kMaxNetworkInterfaces)
    {
        return false;
    }
    auto& interface_config =
        g_parse_scratch.interfaces[g_parse_scratch.interface_count++];
    interface_config = chat::reticulum::NetworkInterfaceConfig{};
    interface_config.type = type;
    interface_config.enabled = true;
    interface_config.target_port = 4242;
    interface_config.discovery_port = 29716;
    interface_config.data_port = 42671;
    copy_bounded(interface_config.id, sizeof(interface_config.id), id);
    copy_bounded(interface_config.group_id,
                 sizeof(interface_config.group_id),
                 "reticulum");
    if (out)
    {
        *out = &interface_config;
    }
    return true;
}

void build_defaults(const chat::MeshConfig& legacy_config)
{
    reset_network_config(g_parse_scratch);
    g_parse_scratch.version =
        chat::reticulum::ReticulumNetworkConfig::kSchemaVersion;

    const bool allow_lora = legacy_config.reticulum_lora_enabled &&
                            legacy_config.reticulum_interface_policy !=
                                chat::ReticulumInterfacePolicy::WifiGatewayOnly;
    const bool allow_ip = legacy_config.reticulum_wifi_gateway_enabled &&
                          legacy_config.reticulum_interface_policy !=
                              chat::ReticulumInterfacePolicy::LoRaOnly;

    if (allow_lora)
    {
        (void)append_default_interface(
            chat::reticulum::NetworkInterfaceType::IntegratedLoRa,
            "integrated-lora",
            nullptr);
    }
    if (allow_ip)
    {
        (void)append_default_interface(chat::reticulum::NetworkInterfaceType::Auto,
                                       "local-wifi",
                                       nullptr);
        if (legacy_config.reticulum_wifi_gateway_host[0] != '\0')
        {
            chat::reticulum::NetworkInterfaceConfig* tcp = nullptr;
            if (append_default_interface(
                    chat::reticulum::NetworkInterfaceType::TcpClient,
                    "primary-tcp",
                    &tcp))
            {
                copy_bounded(tcp->target_host,
                             sizeof(tcp->target_host),
                             legacy_config.reticulum_wifi_gateway_host);
                tcp->target_port = legacy_config.reticulum_wifi_gateway_port != 0
                                       ? legacy_config.reticulum_wifi_gateway_port
                                       : 4242;
            }
        }
    }

    g_parse_scratch.propagation.enabled = true;
    g_parse_scratch.propagation.delivery =
        chat::reticulum::LxmfDeliveryPreference::Automatic;
    g_parse_scratch.propagation.automatic_node = true;
    g_parse_scratch.propagation.sync_on_start = true;
    g_parse_scratch.propagation.sync_interval_s = 15U * 60U;
    g_parse_scratch.propagation.max_messages_per_sync = 32;
}

bool id_is_unique(const chat::reticulum::ReticulumNetworkConfig& config,
                  const char* id,
                  std::size_t count)
{
    for (std::size_t index = 0; index < count; ++index)
    {
        if (std::strcmp(config.interfaces[index].id, id) == 0)
        {
            return false;
        }
    }
    return true;
}

bool parse_interface(cJSON* object,
                     chat::reticulum::NetworkInterfaceConfig& out,
                     std::size_t prior_count,
                     uint8_t& lora_count,
                     uint8_t& auto_count,
                     uint8_t& tcp_count,
                     char* error,
                     std::size_t error_len)
{
    const char* id = json_string(object, "id");
    const char* type = json_string(object, "type");
    if (!id || id[0] == '\0' || std::strlen(id) > chat::reticulum::kInterfaceIdMaxLen)
    {
        copy_bounded(error, error_len, "Interface id is missing or too long");
        return false;
    }
    if (!id_is_unique(g_parse_scratch, id, prior_count))
    {
        copy_bounded(error, error_len, "Interface ids must be unique");
        return false;
    }
    if (!type)
    {
        copy_bounded(error, error_len, "Interface type is missing");
        return false;
    }

    out = chat::reticulum::NetworkInterfaceConfig{};
    copy_bounded(out.id, sizeof(out.id), id);
    out.enabled = json_bool(object, "enabled", true);
    out.target_port = 4242;
    out.discovery_port = 29716;
    out.data_port = 42671;
    copy_bounded(out.group_id, sizeof(out.group_id), "reticulum");

    if (std::strcmp(type, "IntegratedLoRaInterface") == 0)
    {
        out.type = chat::reticulum::NetworkInterfaceType::IntegratedLoRa;
        if (++lora_count > 1)
        {
            copy_bounded(error, error_len, "Only one integrated LoRa interface is supported");
            return false;
        }
    }
    else if (std::strcmp(type, "AutoInterface") == 0)
    {
        out.type = chat::reticulum::NetworkInterfaceType::Auto;
        if (++auto_count > 1)
        {
            copy_bounded(error, error_len, "Only one AutoInterface is supported");
            return false;
        }
        const char* group_id = json_string(object, "group_id");
        if (group_id)
        {
            if (group_id[0] == '\0' ||
                std::strlen(group_id) > chat::reticulum::kAutoInterfaceGroupMaxLen)
            {
                copy_bounded(error, error_len, "AutoInterface group_id is invalid");
                return false;
            }
            copy_bounded(out.group_id, sizeof(out.group_id), group_id);
        }
        const char* scope = json_string(object, "discovery_scope");
        if (scope && std::strcmp(scope, "link") != 0)
        {
            copy_bounded(error, error_len, "Embedded AutoInterface supports link scope only");
            return false;
        }
        const int discovery_port = json_int(object, "discovery_port", 29716);
        const int data_port = json_int(object, "data_port", 42671);
        if (discovery_port <= 0 || discovery_port >= 65535 ||
            data_port <= 0 || data_port > 65535)
        {
            copy_bounded(error, error_len, "AutoInterface port is invalid");
            return false;
        }
        out.discovery_port = static_cast<uint16_t>(discovery_port);
        out.data_port = static_cast<uint16_t>(data_port);
    }
    else if (std::strcmp(type, "TCPClientInterface") == 0)
    {
        out.type = chat::reticulum::NetworkInterfaceType::TcpClient;
        if (++tcp_count > chat::reticulum::kMaxTcpClientInterfaces)
        {
            copy_bounded(error, error_len, "Too many TCPClientInterface entries");
            return false;
        }
        const char* host = json_string(object, "target_host");
        const int port = json_int(object, "target_port", 4242);
        if (!host || host[0] == '\0' ||
            std::strlen(host) > chat::reticulum::kInterfaceHostMaxLen ||
            port <= 0 || port > 65535)
        {
            copy_bounded(error, error_len, "TCPClientInterface endpoint is invalid");
            return false;
        }
        copy_bounded(out.target_host, sizeof(out.target_host), host);
        out.target_port = static_cast<uint16_t>(port);
    }
    else
    {
        copy_bounded(error, error_len, "Unsupported Interface type");
        return false;
    }

    return true;
}

bool parse_propagation(cJSON* object,
                       chat::reticulum::LxmfPropagationClientConfig& out,
                       char* error,
                       std::size_t error_len)
{
    if (!object)
    {
        return true;
    }
    if (!cJSON_IsObject(object))
    {
        copy_bounded(error, error_len, "lxmf.propagation must be an object");
        return false;
    }

    out.enabled = json_bool(object, "enabled", out.enabled);
    out.service_enabled =
        json_bool(object, "service_enabled", out.service_enabled);
    out.sync_on_start = json_bool(object, "sync_on_start", out.sync_on_start);
    const char* delivery = json_string(object, "delivery_method");
    if (delivery)
    {
        if (std::strcmp(delivery, "direct") == 0)
        {
            out.delivery = chat::reticulum::LxmfDeliveryPreference::Direct;
        }
        else if (std::strcmp(delivery, "propagated") == 0)
        {
            out.delivery = chat::reticulum::LxmfDeliveryPreference::Propagated;
        }
        else if (std::strcmp(delivery, "auto") == 0)
        {
            out.delivery = chat::reticulum::LxmfDeliveryPreference::Automatic;
        }
        else
        {
            copy_bounded(error, error_len, "Unknown LXMF delivery_method");
            return false;
        }
    }

    const char* node = json_string(object, "propagation_node");
    if (node && std::strcmp(node, "auto") != 0)
    {
        if (!parse_hash(node, out.node_hash, sizeof(out.node_hash)))
        {
            copy_bounded(error, error_len, "propagation_node must be auto or a destination hash");
            return false;
        }
        out.automatic_node = false;
    }
    else
    {
        out.automatic_node = true;
        std::memset(out.node_hash, 0, sizeof(out.node_hash));
    }

    const int interval = json_int(object,
                                  "sync_interval_seconds",
                                  static_cast<int>(out.sync_interval_s));
    const int max_messages = json_int(object,
                                      "max_messages_per_sync",
                                      out.max_messages_per_sync);
    if (interval < 60 || interval > 24 * 60 * 60 ||
        max_messages < 1 || max_messages > 64)
    {
        copy_bounded(error, error_len, "Propagation sync limits are invalid");
        return false;
    }
    out.sync_interval_s = static_cast<uint32_t>(interval);
    out.max_messages_per_sync = static_cast<uint8_t>(max_messages);
    return true;
}

bool parse_document(const char* data,
                    std::size_t len,
                    chat::reticulum::ReticulumNetworkConfig* out,
                    char* error,
                    std::size_t error_len)
{
    if (!out)
    {
        copy_bounded(error, error_len, "Configuration is empty");
        return false;
    }
    if (!validate_json_budget(data, len, error, error_len))
    {
        return false;
    }

    const char* parse_end = nullptr;
    cJSON* root = cJSON_ParseWithLengthOpts(data, len, &parse_end, false);
    while (parse_end && parse_end < data + len &&
           (*parse_end == ' ' || *parse_end == '\t' || *parse_end == '\r' ||
            *parse_end == '\n'))
    {
        ++parse_end;
    }
    if (!cJSON_IsObject(root) || parse_end != data + len)
    {
        cJSON_Delete(root);
        copy_bounded(error, error_len, "Configuration JSON is invalid");
        return false;
    }

    const char* schema = json_string(root, "schema");
    const int version = json_int(root, "version", 0);
    if (!schema || std::strcmp(schema, kSchema) != 0 ||
        version != chat::reticulum::ReticulumNetworkConfig::kSchemaVersion)
    {
        cJSON_Delete(root);
        copy_bounded(error, error_len, "Unsupported Reticulum configuration schema");
        return false;
    }

    reset_network_config(g_parse_scratch);
    g_parse_scratch.version = static_cast<uint16_t>(version);
    g_parse_scratch.propagation = {};

    cJSON* interfaces = object_item(root, "interfaces");
    if (!cJSON_IsArray(interfaces))
    {
        cJSON_Delete(root);
        copy_bounded(error, error_len, "interfaces must be an array");
        return false;
    }

    const int count = cJSON_GetArraySize(interfaces);
    if (count <= 0 || count > static_cast<int>(chat::reticulum::kMaxNetworkInterfaces))
    {
        cJSON_Delete(root);
        copy_bounded(error, error_len, "Interface count is outside the supported range");
        return false;
    }

    uint8_t lora_count = 0;
    uint8_t auto_count = 0;
    uint8_t tcp_count = 0;
    uint8_t enabled_count = 0;
    for (int index = 0; index < count; ++index)
    {
        cJSON* item = cJSON_GetArrayItem(interfaces, index);
        if (!cJSON_IsObject(item) ||
            !parse_interface(item,
                             g_parse_scratch.interfaces[index],
                             static_cast<std::size_t>(index),
                             lora_count,
                             auto_count,
                             tcp_count,
                             error,
                             error_len))
        {
            cJSON_Delete(root);
            return false;
        }
        if (g_parse_scratch.interfaces[index].enabled)
        {
            ++enabled_count;
        }
    }
    if (enabled_count == 0)
    {
        cJSON_Delete(root);
        copy_bounded(error, error_len, "At least one Interface must be enabled");
        return false;
    }
    g_parse_scratch.interface_count = static_cast<uint8_t>(count);

    cJSON* lxmf = object_item(root, "lxmf");
    if (lxmf && !cJSON_IsObject(lxmf))
    {
        cJSON_Delete(root);
        copy_bounded(error, error_len, "lxmf must be an object");
        return false;
    }
    if (!parse_propagation(object_item(lxmf, "propagation"),
                           g_parse_scratch.propagation,
                           error,
                           error_len))
    {
        cJSON_Delete(root);
        return false;
    }

    cJSON_Delete(root);
    *out = g_parse_scratch;
    return true;
}

cJSON* create_document(const chat::reticulum::ReticulumNetworkConfig& config)
{
    cJSON* root = cJSON_CreateObject();
    if (!root)
    {
        return nullptr;
    }
    auto fail = [&root]() -> cJSON*
    {
        cJSON_Delete(root);
        return nullptr;
    };
    if (!add_json_string(root, "schema", kSchema) ||
        !add_json_number(root, "version", config.version))
    {
        return fail();
    }
    cJSON* interfaces = cJSON_AddArrayToObject(root, "interfaces");
    if (!interfaces)
    {
        return fail();
    }
    for (std::size_t index = 0; index < config.interface_count; ++index)
    {
        const auto& source = config.interfaces[index];
        cJSON* item = cJSON_CreateObject();
        if (!item)
        {
            return fail();
        }
        const char* type = "IntegratedLoRaInterface";
        if (source.type == chat::reticulum::NetworkInterfaceType::Auto)
        {
            type = "AutoInterface";
        }
        else if (source.type == chat::reticulum::NetworkInterfaceType::TcpClient)
        {
            type = "TCPClientInterface";
        }
        bool valid = add_json_string(item, "id", source.id) &&
                     add_json_string(item, "type", type) &&
                     add_json_bool(item, "enabled", source.enabled);
        if (source.type == chat::reticulum::NetworkInterfaceType::Auto)
        {
            valid = valid && add_json_string(item, "group_id", source.group_id) &&
                    add_json_string(item, "discovery_scope", "link") &&
                    add_json_number(item, "discovery_port", source.discovery_port) &&
                    add_json_number(item, "data_port", source.data_port);
        }
        else if (source.type == chat::reticulum::NetworkInterfaceType::TcpClient)
        {
            valid = valid &&
                    add_json_string(item, "target_host", source.target_host) &&
                    add_json_number(item, "target_port", source.target_port);
        }
        if (!valid || !cJSON_AddItemToArray(interfaces, item))
        {
            cJSON_Delete(item);
            return fail();
        }
    }

    cJSON* lxmf = cJSON_AddObjectToObject(root, "lxmf");
    if (!lxmf)
    {
        return fail();
    }
    cJSON* propagation = cJSON_AddObjectToObject(lxmf, "propagation");
    if (!propagation ||
        !add_json_bool(propagation, "enabled", config.propagation.enabled) ||
        !add_json_bool(propagation,
                       "service_enabled",
                       config.propagation.service_enabled))
    {
        return fail();
    }
    const char* delivery = "auto";
    if (config.propagation.delivery == chat::reticulum::LxmfDeliveryPreference::Direct)
    {
        delivery = "direct";
    }
    else if (config.propagation.delivery ==
             chat::reticulum::LxmfDeliveryPreference::Propagated)
    {
        delivery = "propagated";
    }
    if (!add_json_string(propagation, "delivery_method", delivery))
    {
        return fail();
    }
    if (config.propagation.automatic_node ||
        is_zero_hash(config.propagation.node_hash,
                     sizeof(config.propagation.node_hash)))
    {
        if (!add_json_string(propagation, "propagation_node", "auto"))
        {
            return fail();
        }
    }
    else
    {
        char hash[(chat::reticulum::kTruncatedHashSize * 2U) + 1U] = {};
        format_hash(config.propagation.node_hash,
                    sizeof(config.propagation.node_hash),
                    hash,
                    sizeof(hash));
        if (!add_json_string(propagation, "propagation_node", hash))
        {
            return fail();
        }
    }
    if (!add_json_bool(propagation,
                       "sync_on_start",
                       config.propagation.sync_on_start) ||
        !add_json_number(propagation,
                         "sync_interval_seconds",
                         config.propagation.sync_interval_s) ||
        !add_json_number(propagation,
                         "max_messages_per_sync",
                         config.propagation.max_messages_per_sync))
    {
        return fail();
    }
    return root;
}

#if !defined(ARDUINO) && !defined(ESP_PLATFORM)
void native_path(const char* path, char* out, std::size_t out_len)
{
    const char* mount_point =
        ::platform::esp::idf_common::bsp_runtime::sdcard_mount_point();
    std::snprintf(out,
                  out_len,
                  "%s%s",
                  mount_point ? mount_point : "/sdcard",
                  path ? path : "");
}
#endif

bool sd_available()
{
#if defined(ARDUINO)
    return ::platform::esp::arduino_common::storage::sd_card_ready();
#else
    return ::platform::esp::idf_common::bsp_runtime::ensure_sdcard_ready() &&
           ::platform::esp::idf_common::bsp_runtime::sdcard_ready();
#endif
}

bool sd_exists(const char* path)
{
#if defined(ARDUINO) || defined(ESP_PLATFORM)
    return ::platform::esp::arduino_common::storage::sd_exists(path);
#else
    char native[192] = {};
    native_path(path, native, sizeof(native));
    struct stat info
    {
    };
    return stat(native, &info) == 0;
#endif
}

bool ensure_directory()
{
#if defined(ARDUINO) || defined(ESP_PLATFORM)
    if (!::platform::esp::arduino_common::storage::sd_exists("/trailmate") &&
        !::platform::esp::arduino_common::storage::sd_mkdir("/trailmate"))
    {
        return false;
    }
    return ::platform::esp::arduino_common::storage::sd_exists(kConfigDirectory) ||
           ::platform::esp::arduino_common::storage::sd_mkdir(kConfigDirectory);
#else
    char trailmate[192] = {};
    char directory[192] = {};
    native_path("/trailmate", trailmate, sizeof(trailmate));
    native_path(kConfigDirectory, directory, sizeof(directory));
    return (mkdir(trailmate, 0775) == 0 || errno == EEXIST) &&
           (mkdir(directory, 0775) == 0 || errno == EEXIST);
#endif
}

bool read_sd_file(const char* path, std::size_t* out_len)
{
    if (!out_len)
    {
        return false;
    }
    *out_len = 0;
#if defined(ARDUINO) || defined(ESP_PLATFORM)
    ::platform::esp::arduino_common::storage::SdRuntimeFile file;
    if (!file.open(path, "r"))
    {
        return false;
    }
    const std::size_t size = file.size();
    if (size == 0 || size > kMaxConfigBytes)
    {
        file.close();
        return false;
    }
    const std::size_t read = file.read_bytes(g_file_buffer, size);
    file.close();
#else
    char native[192] = {};
    native_path(path, native, sizeof(native));
    std::FILE* file = std::fopen(native, "rb");
    if (!file || std::fseek(file, 0, SEEK_END) != 0)
    {
        if (file)
        {
            std::fclose(file);
        }
        return false;
    }
    const long file_size = std::ftell(file);
    if (file_size <= 0 || file_size > static_cast<long>(kMaxConfigBytes) ||
        std::fseek(file, 0, SEEK_SET) != 0)
    {
        std::fclose(file);
        return false;
    }
    const std::size_t size = static_cast<std::size_t>(file_size);
    const std::size_t read = std::fread(g_file_buffer, 1, size, file);
    std::fclose(file);
#endif
    if (read != size)
    {
        return false;
    }
    g_file_buffer[size] = '\0';
    *out_len = size;
    return true;
}

bool write_sd_file_atomic(const char* text, std::size_t len)
{
    if (!text || len == 0 || len > kMaxConfigBytes || !ensure_directory())
    {
        return false;
    }
#if defined(ARDUINO) || defined(ESP_PLATFORM)
    if (::platform::esp::arduino_common::storage::sd_exists(kConfigTempPath))
    {
        ::platform::esp::arduino_common::storage::sd_remove(kConfigTempPath);
    }
    ::platform::esp::arduino_common::storage::SdRuntimeFile file;
    if (!file.open(kConfigTempPath, "w"))
    {
        return false;
    }
    const bool wrote =
        file.write(reinterpret_cast<const uint8_t*>(text), len) == len;
    file.close();
    if (!wrote)
    {
        ::platform::esp::arduino_common::storage::sd_remove(kConfigTempPath);
        return false;
    }
    if (::platform::esp::arduino_common::storage::sd_exists(kConfigPath))
    {
        ::platform::esp::arduino_common::storage::sd_remove(kConfigPath);
    }
    return ::platform::esp::arduino_common::storage::sd_rename(kConfigTempPath,
                                                               kConfigPath);
#else
    char path[192] = {};
    char temp_path[192] = {};
    native_path(kConfigPath, path, sizeof(path));
    native_path(kConfigTempPath, temp_path, sizeof(temp_path));
    std::remove(temp_path);
    std::FILE* file = std::fopen(temp_path, "wb");
    if (!file)
    {
        return false;
    }
    const bool wrote = std::fwrite(text, 1, len, file) == len &&
                       std::fflush(file) == 0;
    std::fclose(file);
    if (!wrote)
    {
        std::remove(temp_path);
        return false;
    }
    std::remove(path);
    return std::rename(temp_path, path) == 0;
#endif
}

bool read_last_known_good(std::size_t* out_len)
{
    if (!out_len)
    {
        return false;
    }
    *out_len = 0;
#if defined(ARDUINO)
    Preferences preferences;
    if (!preferences.begin(kPreferencesNamespace, true))
    {
        return false;
    }
    const std::size_t size = preferences.getBytesLength(kLastKnownGoodKey);
    const bool valid_size = size > 0 && size <= kMaxConfigBytes;
    const std::size_t read = valid_size
                                 ? preferences.getBytes(kLastKnownGoodKey,
                                                        g_file_buffer,
                                                        size)
                                 : 0;
    preferences.end();
#else
    nvs_handle_t handle = 0;
    if (nvs_open(kPreferencesNamespace, NVS_READONLY, &handle) != ESP_OK)
    {
        return false;
    }
    std::size_t size = 0;
    esp_err_t error = nvs_get_blob(handle, kLastKnownGoodKey, nullptr, &size);
    const bool valid_size = error == ESP_OK && size > 0 && size <= kMaxConfigBytes;
    if (valid_size)
    {
        error = nvs_get_blob(handle, kLastKnownGoodKey, g_file_buffer, &size);
    }
    nvs_close(handle);
    const std::size_t read = error == ESP_OK && valid_size ? size : 0;
#endif
    if (!valid_size || read != size)
    {
        return false;
    }
    g_file_buffer[size] = '\0';
    *out_len = size;
    return true;
}

bool write_last_known_good(const char* text, std::size_t len)
{
    if (!text || len == 0 || len > kMaxConfigBytes)
    {
        return false;
    }
#if defined(ARDUINO)
    Preferences preferences;
    if (!preferences.begin(kPreferencesNamespace, false))
    {
        return false;
    }
    const bool wrote =
        preferences.putBytes(kLastKnownGoodKey, text, len) == len;
    preferences.end();
    return wrote;
#else
    nvs_handle_t handle = 0;
    if (nvs_open(kPreferencesNamespace, NVS_READWRITE, &handle) != ESP_OK)
    {
        return false;
    }
    const bool wrote = nvs_set_blob(handle, kLastKnownGoodKey, text, len) == ESP_OK &&
                       nvs_commit(handle) == ESP_OK;
    nvs_close(handle);
    return wrote;
#endif
}

bool persist_last_known_good(const chat::reticulum::ReticulumNetworkConfig& config)
{
    cJSON* root = create_document(config);
    g_file_buffer[0] = '\0';
    const bool printed = root &&
                         cJSON_PrintPreallocated(root,
                                                 g_file_buffer,
                                                 sizeof(g_file_buffer),
                                                 false);
    cJSON_Delete(root);
    return printed &&
           write_last_known_good(g_file_buffer, std::strlen(g_file_buffer));
}

void activate(const chat::reticulum::ReticulumNetworkConfig& config,
              Source source)
{
    g_active = config;
    g_status.source = source;
    g_status.valid = true;
    g_status.configured_interfaces = config.interface_count;
    ++g_status.generation;
}

bool same_interface_config(const chat::reticulum::NetworkInterfaceConfig& lhs,
                           const chat::reticulum::NetworkInterfaceConfig& rhs)
{
    return std::strcmp(lhs.id, rhs.id) == 0 && lhs.type == rhs.type &&
           lhs.enabled == rhs.enabled &&
           std::strcmp(lhs.target_host, rhs.target_host) == 0 &&
           lhs.target_port == rhs.target_port &&
           std::strcmp(lhs.group_id, rhs.group_id) == 0 &&
           lhs.discovery_port == rhs.discovery_port &&
           lhs.data_port == rhs.data_port;
}

bool same_propagation_config(
    const chat::reticulum::LxmfPropagationClientConfig& lhs,
    const chat::reticulum::LxmfPropagationClientConfig& rhs)
{
    return lhs.enabled == rhs.enabled &&
           lhs.service_enabled == rhs.service_enabled &&
           lhs.delivery == rhs.delivery &&
           lhs.automatic_node == rhs.automatic_node &&
           std::memcmp(lhs.node_hash, rhs.node_hash, sizeof(lhs.node_hash)) == 0 &&
           lhs.sync_on_start == rhs.sync_on_start &&
           lhs.sync_interval_s == rhs.sync_interval_s &&
           lhs.max_messages_per_sync == rhs.max_messages_per_sync;
}

bool same_network_config(const chat::reticulum::ReticulumNetworkConfig& lhs,
                         const chat::reticulum::ReticulumNetworkConfig& rhs)
{
    if (lhs.version != rhs.version || lhs.interface_count != rhs.interface_count ||
        !same_propagation_config(lhs.propagation, rhs.propagation))
    {
        return false;
    }
    for (std::size_t index = 0; index < lhs.interface_count; ++index)
    {
        if (!same_interface_config(lhs.interfaces[index], rhs.interfaces[index]))
        {
            return false;
        }
    }
    return true;
}

void refresh_default_projection(const chat::MeshConfig& legacy_config,
                                bool force)
{
    build_defaults(legacy_config);
    if (!force && g_status.source == Source::Defaults &&
        same_network_config(g_active, g_parse_scratch))
    {
        return;
    }

    activate(g_parse_scratch, Source::Defaults);
    set_status("Using Reticulum defaults", kConfigPath);
}

bool load_sd_config()
{
    g_status.sd_present = sd_available();
    if (!g_status.sd_present)
    {
        set_status("SD unavailable", kConfigPath);
        return false;
    }
    g_status.file_present = sd_exists(kConfigPath);
    if (!g_status.file_present)
    {
        set_status("Reticulum config not found", kConfigPath);
        return false;
    }

    std::size_t len = 0;
    if (!read_sd_file(kConfigPath, &len))
    {
        set_status("Cannot read Reticulum config", kConfigPath);
        return false;
    }
    char error[96] = {};
    if (!parse_document(g_file_buffer,
                        len,
                        &g_parse_scratch,
                        error,
                        sizeof(error)))
    {
        set_status("Invalid Reticulum config", error);
        return false;
    }

    activate(g_parse_scratch, Source::SdCard);
    (void)persist_last_known_good(g_active);
    set_status("Reticulum config loaded", kConfigPath);
    return true;
}

} // namespace

void initialize(const chat::MeshConfig& legacy_config)
{
    if (g_initialized)
    {
        return;
    }
    g_status = {};
    g_status.supported = true;
    build_defaults(legacy_config);
    activate(g_parse_scratch, Source::Defaults);
    set_status("Using Reticulum defaults", kConfigPath);

    std::size_t len = 0;
    char error[96] = {};
    if (read_last_known_good(&len) &&
        parse_document(g_file_buffer,
                       len,
                       &g_parse_scratch,
                       error,
                       sizeof(error)))
    {
        activate(g_parse_scratch, Source::LastKnownGood);
        set_status("Using cached Reticulum config", kConfigPath);
    }
    g_initialized = true;
}

void poll(const chat::MeshConfig& legacy_config)
{
    initialize(legacy_config);
    if (g_reload_deferred && !::platform::ui::reticulum_call::resource_preempt_active())
    {
        g_reload_deferred = false;
        g_status.reload_deferred = false;
        g_sd_checked = true;
        (void)load_sd_config();
        return;
    }
    if (g_sd_checked)
    {
        refresh_default_projection(legacy_config, false);
        return;
    }
    const uint32_t now_ms = uptime_ms();
    if (g_last_sd_probe_ms != 0 &&
        now_ms - g_last_sd_probe_ms < kSdProbeIntervalMs)
    {
        return;
    }
    g_last_sd_probe_ms = now_ms;
    if (sd_available())
    {
        if (::platform::ui::reticulum_call::resource_preempt_active())
        {
            g_reload_deferred = true;
            set_status("Reload deferred until call closes", kConfigPath);
            return;
        }
        g_sd_checked = true;
        if (!load_sd_config() && !g_status.file_present)
        {
            refresh_default_projection(legacy_config, true);
        }
    }
}

const chat::reticulum::ReticulumNetworkConfig& active()
{
    return g_active;
}

Status status()
{
    g_status.reload_deferred = g_reload_deferred;
    return g_status;
}

bool reload(const chat::MeshConfig& legacy_config)
{
    initialize(legacy_config);
    if (::platform::ui::reticulum_call::resource_preempt_active())
    {
        g_reload_deferred = true;
        set_status("Reload deferred until call closes", kConfigPath);
        return true;
    }
    g_sd_checked = true;
    return load_sd_config();
}

bool export_template(const chat::MeshConfig& legacy_config)
{
    initialize(legacy_config);
    if (::platform::ui::reticulum_call::resource_preempt_active() ||
        !sd_available())
    {
        return false;
    }
    cJSON* root = create_document(g_active);
    g_file_buffer[0] = '\0';
    const bool printed = root &&
                         cJSON_PrintPreallocated(root,
                                                 g_file_buffer,
                                                 sizeof(g_file_buffer),
                                                 true);
    const bool wrote = printed &&
                       write_sd_file_atomic(g_file_buffer,
                                            std::strlen(g_file_buffer));
    cJSON_Delete(root);
    if (wrote)
    {
        g_status.file_present = true;
        set_status("Reticulum config exported", kConfigPath);
    }
    return wrote;
}

const char* config_path()
{
    return kConfigPath;
}

const char* source_name(Source source)
{
    switch (source)
    {
    case Source::SdCard:
        return "SD";
    case Source::LastKnownGood:
        return "Cached";
    case Source::Defaults:
    default:
        return "Defaults";
    }
}

} // namespace platform::ui::reticulum_network_config
