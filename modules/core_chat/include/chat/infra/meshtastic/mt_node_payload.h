#pragma once

#include "chat/domain/chat_types.h"
#include "chat/domain/contact_types.h"
#include "meshtastic/mesh.pb.h"

#include <array>
#include <cstdint>
#include <string>

namespace chat
{
namespace meshtastic
{

struct NodePayloadDecodeContext
{
    NodeId fallback_node_id = 0;
    float snr = 0.0f;
    float rssi = 0.0f;
    std::uint32_t timestamp = 0;
    std::uint8_t hops_away = 0xFF;
    std::uint8_t channel = 0xFF;
    bool via_mqtt = false;
};

struct DecodedNodePayload
{
    NodeId node_id = 0;
    std::string short_name{};
    std::string long_name{};
    float snr = 0.0f;
    float rssi = 0.0f;
    std::uint32_t timestamp = 0;
    std::uint8_t protocol = static_cast<std::uint8_t>(
        contacts::NodeProtocolType::Meshtastic);
    std::uint8_t role = 0xFF;
    std::uint8_t hops_away = 0xFF;
    std::uint8_t hw_model = 0;
    std::uint8_t channel = 0xFF;
    bool has_user = false;
    bool has_macaddr = false;
    std::array<std::uint8_t, 6> macaddr{};
    bool via_mqtt = false;
    bool is_ignored = false;
    bool has_public_key_state = false;
    bool has_public_key = false;
    std::array<std::uint8_t, 32> public_key{};
    bool key_manually_verified = false;
    bool has_device_metrics = false;
    contacts::NodeDeviceMetrics device_metrics{};
    bool has_position = false;
    contacts::NodePosition position{};

    [[nodiscard]] contacts::NodeUpdate toNodeUpdate() const;
};

struct DecodedPositionPayload
{
    NodeId node_id = 0;
    contacts::NodePosition position{};
};

bool isNodeMetadataPayload(meshtastic_PortNum portnum);

bool decodeNodeMetadataPayload(const meshtastic_Data& data,
                               const NodePayloadDecodeContext& context,
                               DecodedNodePayload* out);

bool decodeNodeInfoPayload(const meshtastic_Data& data,
                           const NodePayloadDecodeContext& context,
                           DecodedNodePayload* out);

bool decodePositionPayload(const meshtastic_Data& data,
                           NodeId node_id,
                           std::uint32_t fallback_timestamp,
                           DecodedPositionPayload* out);

} // namespace meshtastic
} // namespace chat
