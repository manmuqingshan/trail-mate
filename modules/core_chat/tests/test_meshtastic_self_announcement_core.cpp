#include "chat/infra/meshtastic/mt_packet_wire.h"
#include "chat/runtime/meshtastic_self_announcement_core.h"
#include "meshtastic/mesh.pb.h"
#include "pb_decode.h"

#include <cassert>
#include <cstring>
#include <string>

int main()
{
    chat::runtime::MeshtasticAnnouncementRequest request{};
    request.identity.node_id = 0xAABBCCDDUL;
    std::strncpy(request.identity.long_name,
                 "trailmate-node",
                 sizeof(request.identity.long_name) - 1);
    std::strncpy(request.identity.short_name,
                 "CCDD",
                 sizeof(request.identity.short_name) - 1);
    request.channel = chat::ChannelId::SECONDARY;
    request.packet_id = 0x11223344UL;
    request.dest_node = 0x01020304UL;
    request.hop_limit = 5;
    request.want_response = true;
    request.want_ack = true;
    request.user_id_override = "N0CALL";
    request.hw_model = meshtastic_HardwareModel_T_DECK;

    const uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    request.mac_addr = mac;

    chat::runtime::MeshtasticAnnouncementPacket packet{};
    assert(chat::runtime::MeshtasticSelfAnnouncementCore::buildNodeInfoPacket(request, &packet));
    assert(packet.wire_size > sizeof(chat::meshtastic::PacketHeaderWire));

    chat::meshtastic::PacketHeaderWire header{};
    uint8_t data_payload[256] = {};
    size_t data_payload_size = sizeof(data_payload);
    assert(chat::meshtastic::parseWirePacket(packet.wire,
                                             packet.wire_size,
                                             &header,
                                             data_payload,
                                             &data_payload_size));

    assert(header.from == request.identity.node_id);
    assert(header.to == request.dest_node);
    assert(header.id == request.packet_id);
    assert((header.flags & chat::meshtastic::PACKET_FLAGS_WANT_ACK_MASK) != 0);
    assert((header.flags & chat::meshtastic::PACKET_FLAGS_HOP_LIMIT_MASK) == request.hop_limit);

    meshtastic_Data data = meshtastic_Data_init_default;
    pb_istream_t data_stream = pb_istream_from_buffer(data_payload, data_payload_size);
    assert(pb_decode(&data_stream, meshtastic_Data_fields, &data));
    assert(data.portnum == meshtastic_PortNum_NODEINFO_APP);
    assert(data.want_response);
    assert(data.payload.size > 0);

    meshtastic_User user = meshtastic_User_init_default;
    pb_istream_t user_stream = pb_istream_from_buffer(data.payload.bytes, data.payload.size);
    assert(pb_decode(&user_stream, meshtastic_User_fields, &user));

    assert(std::string(user.id) == "N0CALL");
    assert(std::string(user.short_name) == "CCDD");
    assert(std::string(user.long_name) == "trailmate-node");
    assert(user.hw_model == meshtastic_HardwareModel_T_DECK);
    assert(std::memcmp(user.macaddr, mac, sizeof(mac)) == 0);

    return 0;
}
