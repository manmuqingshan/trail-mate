#pragma once

#include "chat/domain/chat_types.h"

#include <cstdint>
#include <cstring>
#include <string>

namespace chat::meshtastic
{

struct MqttProxyRuntimeSettings
{
    bool enabled = false;
    bool proxy_to_client_enabled = false;
    bool encryption_enabled = true;
    bool primary_uplink_enabled = false;
    bool primary_downlink_enabled = false;
    bool secondary_uplink_enabled = false;
    bool secondary_downlink_enabled = false;
    std::string root;
    std::string primary_channel_id;
    std::string secondary_channel_id;
};

enum class MqttProxyRejectReason : uint8_t
{
    None,
    ProxyDisabled,
    NonDataMessage,
    EmptyPayload,
    DecodeFailed,
    UnknownOrDisabledChannel,
    DecodedPayloadWhileEncrypted,
    AdminPayload,
    PayloadTooLarge,
    DataEncodeFailed,
    WireBuildFailed,
};

inline const char* mqttProxyRejectReasonName(MqttProxyRejectReason reason)
{
    switch (reason)
    {
    case MqttProxyRejectReason::None:
        return "none";
    case MqttProxyRejectReason::ProxyDisabled:
        return "proxy_disabled";
    case MqttProxyRejectReason::NonDataMessage:
        return "non_data_message";
    case MqttProxyRejectReason::EmptyPayload:
        return "empty_payload";
    case MqttProxyRejectReason::DecodeFailed:
        return "decode_failed";
    case MqttProxyRejectReason::UnknownOrDisabledChannel:
        return "unknown_or_disabled_channel";
    case MqttProxyRejectReason::DecodedPayloadWhileEncrypted:
        return "decoded_payload_while_encrypted";
    case MqttProxyRejectReason::AdminPayload:
        return "admin_payload";
    case MqttProxyRejectReason::PayloadTooLarge:
        return "payload_too_large";
    case MqttProxyRejectReason::DataEncodeFailed:
        return "data_encode_failed";
    case MqttProxyRejectReason::WireBuildFailed:
        return "wire_build_failed";
    default:
        return "unknown";
    }
}

inline bool mqttProxyRuntimeEnabled(const MqttProxyRuntimeSettings& settings)
{
    return settings.enabled && settings.proxy_to_client_enabled;
}

inline MqttProxyRejectReason validateMqttProxyInbound(const MqttProxyRuntimeSettings& settings,
                                                      bool is_data_message,
                                                      bool has_payload)
{
    if (!mqttProxyRuntimeEnabled(settings))
    {
        return MqttProxyRejectReason::ProxyDisabled;
    }
    if (!is_data_message)
    {
        return MqttProxyRejectReason::NonDataMessage;
    }
    if (!has_payload)
    {
        return MqttProxyRejectReason::EmptyPayload;
    }
    return MqttProxyRejectReason::None;
}

inline bool hasAnyMqttDownlinkEnabled(const MqttProxyRuntimeSettings& settings)
{
    return settings.primary_downlink_enabled || settings.secondary_downlink_enabled;
}

struct MqttProxyDownlinkChannel
{
    bool known = false;
    bool pki = false;
    ChannelId channel = ChannelId::PRIMARY;
};

inline MqttProxyDownlinkChannel resolveMqttProxyDownlinkChannel(
    const MqttProxyRuntimeSettings& settings,
    const char* channel_id)
{
    MqttProxyDownlinkChannel result{};
    if (channel_id && std::strcmp(channel_id, "PKI") == 0)
    {
        result.known = hasAnyMqttDownlinkEnabled(settings);
        result.pki = true;
        return result;
    }
    if (channel_id && !settings.primary_channel_id.empty() &&
        std::strcmp(channel_id, settings.primary_channel_id.c_str()) == 0)
    {
        result.known = settings.primary_downlink_enabled;
        result.channel = ChannelId::PRIMARY;
        return result;
    }
    if (channel_id && !settings.secondary_channel_id.empty() &&
        std::strcmp(channel_id, settings.secondary_channel_id.c_str()) == 0)
    {
        result.known = settings.secondary_downlink_enabled;
        result.channel = ChannelId::SECONDARY;
        return result;
    }
    return result;
}

inline bool shouldPublishToMqtt(const MqttProxyRuntimeSettings& settings,
                                ChannelId channel,
                                bool from_mqtt,
                                bool is_pki)
{
    if (!mqttProxyRuntimeEnabled(settings) || from_mqtt)
    {
        return false;
    }
    if (is_pki)
    {
        return true;
    }
    if (channel == ChannelId::SECONDARY)
    {
        return settings.secondary_uplink_enabled;
    }
    return settings.primary_uplink_enabled;
}

inline const char* mqttChannelIdFor(const MqttProxyRuntimeSettings& settings, ChannelId channel)
{
    if (channel == ChannelId::SECONDARY && !settings.secondary_channel_id.empty())
    {
        return settings.secondary_channel_id.c_str();
    }
    if (!settings.primary_channel_id.empty())
    {
        return settings.primary_channel_id.c_str();
    }
    return nullptr;
}

inline MqttProxyRejectReason validateMqttDownlinkChannel(bool known_channel)
{
    return known_channel ? MqttProxyRejectReason::None
                         : MqttProxyRejectReason::UnknownOrDisabledChannel;
}

inline MqttProxyRejectReason validateMqttDecodedDownlinkPayload(
    const MqttProxyRuntimeSettings& settings,
    bool is_decoded_payload,
    bool is_admin_payload)
{
    if (!is_decoded_payload)
    {
        return MqttProxyRejectReason::None;
    }
    if (settings.encryption_enabled)
    {
        return MqttProxyRejectReason::DecodedPayloadWhileEncrypted;
    }
    if (is_admin_payload)
    {
        return MqttProxyRejectReason::AdminPayload;
    }
    return MqttProxyRejectReason::None;
}

} // namespace chat::meshtastic
