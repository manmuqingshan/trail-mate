#include "ui/screens/settings/settings_channel_actions.h"

#include "chat/domain/chat_types.h"
#include <cstdlib>
#include <cstring>

#if defined(ESP_PLATFORM)
#include "esp_random.h"
#endif

namespace settings::ui::channel
{

namespace
{

void copy_bounded(char* out, std::size_t out_len, const char* text)
{
    if (!out || out_len == 0)
    {
        return;
    }
    if (!text)
    {
        out[0] = '\0';
        return;
    }
    std::strncpy(out, text, out_len - 1);
    out[out_len - 1] = '\0';
}

bool parse_hex_char(char c, std::uint8_t& out)
{
    if (c >= '0' && c <= '9')
    {
        out = static_cast<std::uint8_t>(c - '0');
        return true;
    }
    if (c >= 'a' && c <= 'f')
    {
        out = static_cast<std::uint8_t>(10 + (c - 'a'));
        return true;
    }
    if (c >= 'A' && c <= 'F')
    {
        out = static_cast<std::uint8_t>(10 + (c - 'A'));
        return true;
    }
    return false;
}

void fill_random_bytes(std::uint8_t* out, std::size_t len)
{
    if (!out)
    {
        return;
    }
    std::size_t offset = 0;
    while (offset < len)
    {
#if defined(ESP_PLATFORM)
        const std::uint32_t value = esp_random();
#else
        const std::uint32_t value = static_cast<std::uint32_t>(std::rand());
#endif
        for (std::size_t byte_index = 0; byte_index < sizeof(value) && offset < len;
             ++byte_index)
        {
            out[offset++] =
                static_cast<std::uint8_t>((value >> (byte_index * 8U)) & 0xFFU);
        }
    }
}

bool generate_hex_key(char* out, std::size_t out_len, std::size_t key_len)
{
    if (!out || out_len == 0)
    {
        return false;
    }
    out[0] = '\0';
    if (key_len == 0 || key_len > chat::kMeshtasticChannelKeyMaxLen)
    {
        return false;
    }
    std::uint8_t key[chat::kMeshtasticChannelKeyMaxLen] = {};
    fill_random_bytes(key, key_len);
    bytes_to_hex(key, key_len, out, out_len);
    return out[0] != '\0';
}

} // namespace

bool is_zero_key(const std::uint8_t* key, std::size_t len)
{
    if (!key || len == 0)
    {
        return true;
    }
    for (std::size_t i = 0; i < len; ++i)
    {
        if (key[i] != 0)
        {
            return false;
        }
    }
    return true;
}

void bytes_to_hex(const std::uint8_t* data, std::size_t len, char* out, std::size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }
    out[0] = '\0';
    if (!data || len == 0)
    {
        return;
    }
    constexpr char kHex[] = "0123456789ABCDEF";
    const std::size_t required = len * 2 + 1;
    if (out_len < required)
    {
        return;
    }
    for (std::size_t i = 0; i < len; ++i)
    {
        const std::uint8_t b = data[i];
        out[i * 2] = kHex[b >> 4];
        out[i * 2 + 1] = kHex[b & 0x0F];
    }
    out[len * 2] = '\0';
}

bool parse_psk(const char* text,
               std::uint8_t* out,
               std::size_t out_len,
               std::size_t* parsed_len)
{
    if (!out || out_len == 0)
    {
        return false;
    }
    if (parsed_len)
    {
        *parsed_len = 0;
    }
    if (!text || text[0] == '\0')
    {
        std::memset(out, 0, out_len);
        return true;
    }
    const std::size_t len = std::strlen(text);
    if ((len == 32 || len == 64) && out_len >= len / 2)
    {
        const std::size_t byte_len = len / 2;
        for (std::size_t i = 0; i < byte_len; ++i)
        {
            std::uint8_t hi = 0;
            std::uint8_t lo = 0;
            if (!parse_hex_char(text[i * 2], hi) || !parse_hex_char(text[i * 2 + 1], lo))
            {
                return false;
            }
            out[i] = static_cast<std::uint8_t>((hi << 4) | lo);
        }
        if (parsed_len)
        {
            *parsed_len = byte_len;
        }
        return true;
    }
    if ((len == 16 || len == 32) && out_len >= len)
    {
        std::memcpy(out, text, len);
        if (parsed_len)
        {
            *parsed_len = len;
        }
        return true;
    }
    return false;
}

void meshtastic_key_to_text(const std::uint8_t* key,
                            std::size_t key_capacity,
                            std::uint8_t stored_len,
                            char* out,
                            std::size_t out_len)
{
    const std::uint8_t key_len =
        chat::normalizeMeshtasticChannelKeyLen(key, key_capacity, stored_len);
    if (key_len == 0)
    {
        if (out && out_len > 0)
        {
            out[0] = '\0';
        }
        return;
    }
    bytes_to_hex(key, key_len, out, out_len);
}

bool parse_meshtastic_key_text(const char* text,
                               std::uint8_t* key,
                               std::size_t key_capacity,
                               std::uint8_t* out_key_len)
{
    if (!key || key_capacity < chat::kMeshtasticChannelKeyDefaultLen || !out_key_len)
    {
        return false;
    }
    std::uint8_t parsed[chat::kMeshtasticChannelKeyMaxLen] = {};
    std::size_t parsed_len = 0;
    if (!parse_psk(text, parsed, key_capacity, &parsed_len))
    {
        return false;
    }
    std::memset(key, 0, key_capacity);
    if (parsed_len > 0)
    {
        std::memcpy(key, parsed, parsed_len);
    }
    *out_key_len =
        chat::normalizeMeshtasticChannelKeyLen(key,
                                               key_capacity,
                                               static_cast<std::uint8_t>(parsed_len));
    return true;
}

void sync_meshtastic_channel_fields(const app::AppConfig& config, SettingsData& settings)
{
    settings.mt_primary_enabled = config.primary_enabled;
    copy_bounded(settings.mt_primary_name,
                 sizeof(settings.mt_primary_name),
                 config.meshtastic_config.primary_channel_name);
    meshtastic_key_to_text(config.meshtastic_config.primary_key,
                           sizeof(config.meshtastic_config.primary_key),
                           config.meshtastic_config.primary_key_len,
                           settings.mt_primary_key,
                           sizeof(settings.mt_primary_key));
    settings.mt_primary_uplink = config.primary_uplink_enabled;
    settings.mt_primary_downlink = config.primary_downlink_enabled;

    settings.mt_secondary_enabled = config.secondary_enabled;
    copy_bounded(settings.mt_secondary_name,
                 sizeof(settings.mt_secondary_name),
                 config.meshtastic_config.secondary_channel_name);
    meshtastic_key_to_text(config.meshtastic_config.secondary_key,
                           sizeof(config.meshtastic_config.secondary_key),
                           config.meshtastic_config.secondary_key_len,
                           settings.mt_secondary_key,
                           sizeof(settings.mt_secondary_key));
    settings.mt_secondary_uplink = config.secondary_uplink_enabled;
    settings.mt_secondary_downlink = config.secondary_downlink_enabled;
}

bool generate_meshtastic_channel_key(app::AppConfig& config,
                                     SettingsData& settings,
                                     bool primary)
{
    char generated[65] = {};
    if (!generate_hex_key(generated,
                          sizeof(generated),
                          chat::kMeshtasticChannelKeyDefaultLen))
    {
        return false;
    }

    chat::MeshConfig& mesh = config.meshtastic_config;
    std::uint8_t* key = primary ? mesh.primary_key : mesh.secondary_key;
    std::uint8_t* key_len = primary ? &mesh.primary_key_len : &mesh.secondary_key_len;
    if (!parse_meshtastic_key_text(generated,
                                   key,
                                   chat::kMeshtasticChannelKeyMaxLen,
                                   key_len))
    {
        return false;
    }
    copy_bounded(primary ? settings.mt_primary_key : settings.mt_secondary_key,
                 primary ? sizeof(settings.mt_primary_key) : sizeof(settings.mt_secondary_key),
                 generated);
    return true;
}

bool generate_meshcore_channel_key(app::AppConfig& config, SettingsData& settings)
{
    char generated[33] = {};
    if (!generate_hex_key(generated, sizeof(generated), chat::kMeshCoreChannelKeyLen))
    {
        return false;
    }
    std::uint8_t key[chat::kMeshCoreChannelKeyLen] = {};
    if (!parse_psk(generated, key, sizeof(key)))
    {
        return false;
    }
    std::memcpy(config.meshcore_config.secondary_key, key, sizeof(key));
    copy_bounded(settings.mc_channel_key, sizeof(settings.mc_channel_key), generated);
    return true;
}

} // namespace settings::ui::channel
