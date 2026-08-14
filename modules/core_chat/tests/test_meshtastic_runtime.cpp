#include "chat/infra/meshtastic/mt_packet_wire.h"
#include "chat/infra/meshtastic/mt_protocol_helpers.h"
#include "chat/runtime/meshtastic_runtime.h"

#include "pb_decode.h"
#include "pb_encode.h"

#include <cassert>
#include <cstring>
#include <string>
#include <vector>

namespace
{

template <typename T, typename Effects>
const T* effectAt(const Effects& effects, size_t index)
{
    assert(index < effects.items.size());
    return std::get_if<T>(&effects.items[index]);
}

struct CapturedIncomingResult
{
    chat::runtime::PacketHandling handling = chat::runtime::PacketHandling::NotHandled;
    chat::runtime::ProtocolEffects effects{};
};

chat::runtime::ProtocolEffects collectPrepareOutgoing(
    chat::runtime::MeshtasticRuntime& runtime,
    const chat::runtime::ProtocolIntent& intent,
    const chat::runtime::RuntimeContext& context)
{
    chat::runtime::ProtocolEffects effects{};
    runtime.prepareOutgoing(intent, context, effects);
    return effects;
}

CapturedIncomingResult collectHandleIncomingPacket(
    chat::runtime::MeshtasticRuntime& runtime,
    const chat::runtime::IncomingPacket& packet,
    const chat::runtime::RuntimeContext& context)
{
    CapturedIncomingResult captured{};
    const auto result = runtime.handleIncomingPacket(packet, context, captured.effects);
    captured.handling = result.handling;
    return captured;
}

chat::runtime::ProtocolTxFeedbackEffects collectHandleTxResult(
    chat::runtime::MeshtasticRuntime& runtime,
    const chat::runtime::TxResult& result,
    const chat::runtime::RuntimeContext& context)
{
    chat::runtime::ProtocolTxFeedbackEffects effects{};
    runtime.handleTxResult(result, context, effects);
    return effects;
}

chat::runtime::ProtocolEffects collectTick(
    chat::runtime::MeshtasticRuntime& runtime,
    const chat::runtime::RuntimeContext& context)
{
    chat::runtime::ProtocolEffects effects{};
    runtime.tick(context, effects);
    return effects;
}

chat::runtime::ProtocolEffects collectPkiResync(
    chat::runtime::MeshtasticRuntime& runtime,
    const chat::runtime::MeshtasticPkiResyncInput& input)
{
    chat::runtime::ProtocolEffects effects{};
    runtime.handlePkiResync(input, effects);
    return effects;
}

std::vector<uint8_t> encodeRouting(meshtastic_Routing_Error reason)
{
    meshtastic_Routing routing = meshtastic_Routing_init_zero;
    routing.which_variant = meshtastic_Routing_error_reason_tag;
    routing.error_reason = reason;

    uint8_t buffer[32] = {};
    pb_ostream_t stream = pb_ostream_from_buffer(buffer, sizeof(buffer));
    assert(pb_encode(&stream, meshtastic_Routing_fields, &routing));
    return std::vector<uint8_t>(buffer, buffer + stream.bytes_written);
}

} // namespace

int main()
{
    using chat::ChannelId;
    using chat::MeshProtocol;
    using chat::runtime::EmitActionResultEffect;
    using chat::runtime::ExchangePositionIntent;
    using chat::runtime::ForgetPeerKeyEffect;
    using chat::runtime::IncomingPacket;
    using chat::runtime::kMeshtasticActionDetailInvalidInput;
    using chat::runtime::kMeshtasticActionDetailLocalSendFailed;
    using chat::runtime::kMeshtasticAppActionTimeoutMs;
    using chat::runtime::MeshtasticPkiResyncCause;
    using chat::runtime::MeshtasticPkiResyncInput;
    using chat::runtime::MeshtasticRuntime;
    using chat::runtime::PacketHandling;
    using chat::runtime::ProtocolActionKind;
    using chat::runtime::ProtocolActionState;
    using chat::runtime::PublishIncomingDataEffect;
    using chat::runtime::RuntimeContext;
    using chat::runtime::SendNodeInfoEffect;
    using chat::runtime::SendPacketEffect;
    using chat::runtime::SendRoutingErrorEffect;
    using chat::runtime::SendTextEffect;
    using chat::runtime::SendTextIntent;
    using chat::runtime::SharePositionIntent;
    using chat::runtime::ShareWaypointIntent;
    using chat::runtime::TraceRouteIntent;
    using chat::runtime::TxResult;

    MeshtasticRuntime runtime;
    RuntimeContext context{};
    context.protocol = MeshProtocol::Meshtastic;
    context.self_node = 0x11111111UL;
    context.now_ms = 0x20240614UL;

    {
        constexpr uint8_t psk[] = {
            0x00,
            0x01,
            0x02,
            0x03,
            0x04,
            0x05,
            0x06,
            0x07,
            0x08,
            0x09,
            0x0A,
            0x0B,
            0x0C,
            0x0D,
            0x0E,
            0x0F,
        };
        constexpr uint8_t payload[] = {
            0x00,
            0x11,
            0x22,
            0x33,
            0x44,
            0x55,
            0x66,
            0x77,
            0x88,
            0x99,
            0xAA,
            0xBB,
            0xCC,
            0xDD,
            0xEE,
            0xFF,
            0x00,
            0x11,
            0x22,
            0x33,
            0x44,
            0x55,
            0x66,
            0x77,
            0x88,
            0x99,
            0xAA,
            0xBB,
            0xCC,
            0xDD,
            0xEE,
            0xFF,
        };
        constexpr uint8_t expected_cipher[] = {
            0x88,
            0x64,
            0x4D,
            0x6D,
            0x53,
            0x54,
            0xE1,
            0x34,
            0x01,
            0x4D,
            0x91,
            0x1E,
            0x6F,
            0x6F,
            0xF7,
            0x03,
            0x6C,
            0xE7,
            0xFE,
            0x34,
            0xA7,
            0xF9,
            0x0F,
            0xA2,
            0xF0,
            0x55,
            0x1A,
            0xA0,
            0xAA,
            0xBF,
            0x04,
            0x8B,
        };

        uint8_t wire[sizeof(chat::meshtastic::PacketHeaderWire) + sizeof(payload)] = {};
        size_t wire_size = sizeof(wire);
        assert(chat::meshtastic::buildWirePacket(payload, sizeof(payload),
                                                 0x07060504UL, 0x03020100UL,
                                                 0xFFFFFFFFUL, 0x08, 3, false,
                                                 psk, sizeof(psk), wire, &wire_size));
        assert(wire_size == sizeof(wire));
        assert(memcmp(wire + sizeof(chat::meshtastic::PacketHeaderWire),
                      expected_cipher, sizeof(expected_cipher)) == 0);

        chat::meshtastic::PacketHeaderWire header{};
        memcpy(&header, wire, sizeof(header));
        uint8_t plaintext[sizeof(payload)] = {};
        size_t plaintext_size = sizeof(plaintext);
        assert(chat::meshtastic::decryptPayload(header,
                                                wire + sizeof(header), sizeof(payload),
                                                psk, sizeof(psk), plaintext, &plaintext_size));
        assert(plaintext_size == sizeof(plaintext));
        assert(memcmp(plaintext, payload, sizeof(payload)) == 0);

        meshtastic_Data expected_data = meshtastic_Data_init_zero;
        expected_data.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
        expected_data.payload.size = 1;
        expected_data.payload.bytes[0] = 0x42;
        uint8_t encoded_data[64] = {};
        pb_ostream_t encoded_stream =
            pb_ostream_from_buffer(encoded_data, sizeof(encoded_data));
        assert(pb_encode(&encoded_stream, meshtastic_Data_fields, &expected_data));

        uint8_t data_wire[96] = {};
        size_t data_wire_size = sizeof(data_wire);
        assert(chat::meshtastic::buildWirePacket(encoded_data,
                                                 encoded_stream.bytes_written,
                                                 0x07060504UL,
                                                 0x10203040UL,
                                                 0xFFFFFFFFUL,
                                                 0x08,
                                                 3,
                                                 false,
                                                 psk,
                                                 sizeof(psk),
                                                 data_wire,
                                                 &data_wire_size));

        chat::meshtastic::PacketHeaderWire data_header{};
        uint8_t data_cipher[64] = {};
        size_t data_cipher_size = sizeof(data_cipher);
        assert(chat::meshtastic::parseWirePacket(data_wire,
                                                 data_wire_size,
                                                 &data_header,
                                                 data_cipher,
                                                 &data_cipher_size));
        uint8_t data_plaintext[64] = {};
        size_t data_plaintext_size = sizeof(data_plaintext);
        meshtastic_Data decoded_data = meshtastic_Data_init_zero;
        assert(chat::meshtastic::decryptAndValidateDataPayload(data_header,
                                                               data_cipher,
                                                               data_cipher_size,
                                                               psk,
                                                               sizeof(psk),
                                                               data_plaintext,
                                                               &data_plaintext_size,
                                                               &decoded_data));
        assert(decoded_data.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP);
        assert(decoded_data.payload.size == 1);
        assert(decoded_data.payload.bytes[0] == 0x42);

        uint8_t wrong_psk[sizeof(psk)] = {};
        memcpy(wrong_psk, psk, sizeof(wrong_psk));
        wrong_psk[0] ^= 0xFF;
        data_plaintext_size = sizeof(data_plaintext);
        assert(!chat::meshtastic::decryptAndValidateDataPayload(data_header,
                                                                data_cipher,
                                                                data_cipher_size,
                                                                wrong_psk,
                                                                sizeof(wrong_psk),
                                                                data_plaintext,
                                                                &data_plaintext_size,
                                                                &decoded_data));

        data_plaintext_size = sizeof(data_plaintext);
        assert(!chat::meshtastic::decryptAndValidateDataPayload(data_header,
                                                                data_cipher,
                                                                data_cipher_size,
                                                                nullptr,
                                                                0,
                                                                data_plaintext,
                                                                &data_plaintext_size,
                                                                &decoded_data));

        expected_data.portnum = meshtastic_PortNum_UNKNOWN_APP;
        encoded_stream = pb_ostream_from_buffer(encoded_data, sizeof(encoded_data));
        assert(pb_encode(&encoded_stream, meshtastic_Data_fields, &expected_data));
        data_wire_size = sizeof(data_wire);
        assert(chat::meshtastic::buildWirePacket(encoded_data,
                                                 encoded_stream.bytes_written,
                                                 0x07060504UL,
                                                 0x50607080UL,
                                                 0xFFFFFFFFUL,
                                                 0x08,
                                                 3,
                                                 false,
                                                 psk,
                                                 sizeof(psk),
                                                 data_wire,
                                                 &data_wire_size));
        data_cipher_size = sizeof(data_cipher);
        assert(chat::meshtastic::parseWirePacket(data_wire,
                                                 data_wire_size,
                                                 &data_header,
                                                 data_cipher,
                                                 &data_cipher_size));
        data_plaintext_size = sizeof(data_plaintext);
        assert(!chat::meshtastic::decryptAndValidateDataPayload(data_header,
                                                                data_cipher,
                                                                data_cipher_size,
                                                                psk,
                                                                sizeof(psk),
                                                                data_plaintext,
                                                                &data_plaintext_size,
                                                                &decoded_data));

        constexpr uint8_t psk256[] = {
            0x00,
            0x01,
            0x02,
            0x03,
            0x04,
            0x05,
            0x06,
            0x07,
            0x08,
            0x09,
            0x0A,
            0x0B,
            0x0C,
            0x0D,
            0x0E,
            0x0F,
            0x10,
            0x11,
            0x12,
            0x13,
            0x14,
            0x15,
            0x16,
            0x17,
            0x18,
            0x19,
            0x1A,
            0x1B,
            0x1C,
            0x1D,
            0x1E,
            0x1F,
        };
        constexpr uint8_t expected_cipher256[] = {
            0xBD,
            0x10,
            0x19,
            0x41,
            0x7D,
            0x2E,
            0x6F,
            0x27,
            0x81,
            0x57,
            0x63,
            0xFC,
            0x3E,
            0x90,
            0x03,
            0xC7,
            0x80,
            0x6F,
            0x2B,
            0x61,
            0x37,
            0xF5,
            0x44,
            0x38,
            0x8B,
            0x22,
            0xAC,
            0x2A,
            0xB9,
            0xB6,
            0xCF,
            0x8C,
        };

        wire_size = sizeof(wire);
        assert(chat::meshtastic::buildWirePacket(payload, sizeof(payload),
                                                 0x07060504UL, 0x03020100UL,
                                                 0xFFFFFFFFUL, 0x08, 3, false,
                                                 psk256, sizeof(psk256), wire, &wire_size));
        assert(wire_size == sizeof(wire));
        assert(memcmp(wire + sizeof(chat::meshtastic::PacketHeaderWire),
                      expected_cipher256, sizeof(expected_cipher256)) == 0);
    }

    {
        SendTextIntent intent{};
        intent.channel = ChannelId::SECONDARY;
        intent.peer = 0x22222222UL;
        intent.message_id = 0x01020304UL;
        intent.text = "hello runtime";

        const auto effects = collectPrepareOutgoing(runtime, intent, context);
        assert(effects.items.size() == 1);
        const auto* text = effectAt<SendTextEffect>(effects, 0);
        assert(text);
        assert(text->protocol == MeshProtocol::Meshtastic);
        assert(text->channel == intent.channel);
        assert(text->peer == intent.peer);
        assert(text->message_id == intent.message_id);
        assert(text->text == intent.text);
    }

    {
        SendTextIntent intent{};
        intent.peer = 0xFFFFFFFFUL;

        const auto effects = collectPrepareOutgoing(runtime, intent, context);
        assert(effects.items.size() == 1);
        const auto* failed = effectAt<EmitActionResultEffect>(effects, 0);
        assert(failed);
        assert(failed->protocol == MeshProtocol::Meshtastic);
        assert(failed->action == ProtocolActionKind::SendText);
        assert(failed->state == ProtocolActionState::Failed);
        assert(failed->peer == 0);
        assert(failed->request_id != 0);
        assert(failed->detail == kMeshtasticActionDetailInvalidInput);
    }

    {
        TraceRouteIntent intent{};
        intent.channel = ChannelId::SECONDARY;
        intent.peer = 0x22222222UL;
        intent.request_id = 0x01020304UL;
        intent.timeout_ms = 9000;

        const auto effects = collectPrepareOutgoing(runtime, intent, context);
        assert(effects.items.size() == 1);
        const auto* packet = effectAt<SendPacketEffect>(effects, 0);
        assert(packet);
        assert(packet->protocol == MeshProtocol::Meshtastic);
        assert(packet->channel == intent.channel);
        assert(packet->dest == intent.peer);
        assert(packet->portnum == meshtastic_PortNum_TRACEROUTE_APP);
        assert(packet->request_id == intent.request_id);
        assert(packet->want_ack);
        assert(packet->want_response);

        meshtastic_RouteDiscovery decoded = meshtastic_RouteDiscovery_init_zero;
        pb_istream_t stream = pb_istream_from_buffer(packet->payload.data(),
                                                     packet->payload.size());
        assert(pb_decode(&stream, meshtastic_RouteDiscovery_fields, &decoded));
    }

    {
        meshtastic_Data decoded = meshtastic_Data_init_zero;
        decoded.portnum = meshtastic_PortNum_TRACEROUTE_APP;
        decoded.want_response = true;
        decoded.has_bitfield = true;
        decoded.bitfield = 0;
        decoded.payload.size = 0;

        chat::RxMeta rx_meta{};
        rx_meta.snr_db_x10 = 58;

        meshtastic_RouteDiscovery route = meshtastic_RouteDiscovery_init_zero;
        assert(chat::meshtastic::updateTraceRoutePayload(&decoded,
                                                         0x4A,
                                                         0xA1B3B57CUL,
                                                         &rx_meta,
                                                         false,
                                                         true,
                                                         &route));
        assert(decoded.payload.size > 0);
        assert(route.route_count == 0);
        assert(route.snr_towards_count == 1);
        assert(route.snr_towards[0] == 23);
    }

    {
        ExchangePositionIntent intent{};
        intent.channel = ChannelId::PRIMARY;
        intent.peer = 0x33333333UL;
        intent.request_id = 0x05060708UL;

        const auto effects = collectPrepareOutgoing(runtime, intent, context);
        assert(effects.items.size() == 1);
        const auto* packet = effectAt<SendPacketEffect>(effects, 0);
        assert(packet);
        assert(packet->protocol == MeshProtocol::Meshtastic);
        assert(packet->channel == intent.channel);
        assert(packet->dest == intent.peer);
        assert(packet->portnum == meshtastic_PortNum_POSITION_APP);
        assert(packet->request_id == intent.request_id);
        assert(!packet->want_ack);
        assert(packet->want_response);
        assert(packet->payload.empty());
    }

    {
        MeshtasticRuntime reply_runtime;
        RuntimeContext reply_context = context;
        reply_context.self_node = 0xA1B3B57CUL;
        reply_context.now_ms = 10000;

        IncomingPacket request{};
        request.protocol = MeshProtocol::Meshtastic;
        request.channel = ChannelId::PRIMARY;
        request.from = 0xE2A7B711UL;
        request.to = reply_context.self_node;
        request.packet_id = 0x9578D958UL;
        request.portnum = meshtastic_PortNum_TRACEROUTE_APP;
        request.want_response = true;
        request.rx_meta.wire_flags = 0x4A;
        request.rx_meta.snr_db_x10 = 58;

        const auto result = collectHandleIncomingPacket(reply_runtime, request, reply_context);
        assert(result.handling == PacketHandling::HandledContinue);
        assert(result.effects.items.size() == 2);

        const auto* publish = effectAt<PublishIncomingDataEffect>(result.effects, 0);
        assert(publish);
        assert(publish->data.portnum == meshtastic_PortNum_TRACEROUTE_APP);
        assert(publish->data.from == request.from);
        assert(publish->data.to == request.to);
        assert(publish->data.packet_id == request.packet_id);
        assert(!publish->data.payload.empty());

        const auto* reply = effectAt<SendPacketEffect>(result.effects, 1);
        assert(reply);
        assert(reply->protocol == MeshProtocol::Meshtastic);
        assert(reply->channel == request.channel);
        assert(reply->dest == request.from);
        assert(reply->portnum == meshtastic_PortNum_TRACEROUTE_APP);
        assert(reply->request_id == 0);
        assert(reply->response_request_id == request.packet_id);
        assert(!reply->want_response);
        assert(reply->payload == publish->data.payload);
    }

    {
        MeshtasticRuntime reply_runtime;
        RuntimeContext reply_context = context;
        reply_context.self_node = 0xA1B3B57CUL;
        reply_context.now_ms = 11000;
        reply_context.self_position_valid = true;
        reply_context.self_latitude_deg = 26.67773;
        reply_context.self_longitude_deg = 107.28225;
        reply_context.self_has_altitude = true;
        reply_context.self_altitude_m = 1903.6;
        reply_context.self_position_timestamp_s = 1710001234U;

        IncomingPacket request{};
        request.protocol = MeshProtocol::Meshtastic;
        request.channel = ChannelId::SECONDARY;
        request.from = 0xE2A7B711UL;
        request.to = reply_context.self_node;
        request.packet_id = 0x69112BFEUL;
        request.portnum = meshtastic_PortNum_POSITION_APP;
        request.want_response = true;

        const auto result = collectHandleIncomingPacket(reply_runtime, request, reply_context);
        assert(result.handling == PacketHandling::HandledContinue);
        assert(result.effects.items.size() == 1);

        const auto* reply = effectAt<SendPacketEffect>(result.effects, 0);
        assert(reply);
        assert(reply->protocol == MeshProtocol::Meshtastic);
        assert(reply->channel == request.channel);
        assert(reply->dest == request.from);
        assert(reply->portnum == meshtastic_PortNum_POSITION_APP);
        assert(reply->request_id == 0);
        assert(reply->response_request_id == request.packet_id);
        assert(!reply->want_ack);
        assert(!reply->want_response);
        assert(!reply->payload.empty());

        meshtastic_Position decoded = meshtastic_Position_init_zero;
        pb_istream_t stream = pb_istream_from_buffer(reply->payload.data(),
                                                     reply->payload.size());
        assert(pb_decode(&stream, meshtastic_Position_fields, &decoded));
        assert(decoded.has_latitude_i);
        assert(decoded.latitude_i == static_cast<int32_t>(reply_context.self_latitude_deg * 1e7));
        assert(decoded.has_longitude_i);
        assert(decoded.longitude_i == static_cast<int32_t>(reply_context.self_longitude_deg * 1e7));
        assert(decoded.has_altitude);
        assert(decoded.altitude == 1904);
        assert(decoded.timestamp == reply_context.self_position_timestamp_s);
    }

    {
        SharePositionIntent intent{};
        intent.channel = ChannelId::SECONDARY;
        intent.peer = 0x44444444UL;
        intent.valid = true;
        intent.latitude_deg = 26.67773;
        intent.longitude_deg = 107.28225;
        intent.has_altitude = true;
        intent.altitude_m = 1903.6;
        intent.timestamp_s = 1710000000U;

        const auto effects = collectPrepareOutgoing(runtime, intent, context);
        assert(effects.items.size() == 1);
        const auto* packet = effectAt<SendPacketEffect>(effects, 0);
        assert(packet);
        assert(packet->protocol == MeshProtocol::Meshtastic);
        assert(packet->channel == intent.channel);
        assert(packet->dest == intent.peer);
        assert(packet->portnum == meshtastic_PortNum_POSITION_APP);
        assert(!packet->want_ack);
        assert(!packet->want_response);
        assert(!packet->payload.empty());

        meshtastic_Position decoded = meshtastic_Position_init_zero;
        pb_istream_t stream = pb_istream_from_buffer(packet->payload.data(),
                                                     packet->payload.size());
        assert(pb_decode(&stream, meshtastic_Position_fields, &decoded));
        assert(decoded.has_latitude_i);
        assert(decoded.latitude_i == static_cast<int32_t>(intent.latitude_deg * 1e7));
        assert(decoded.has_longitude_i);
        assert(decoded.longitude_i == static_cast<int32_t>(intent.longitude_deg * 1e7));
        assert(decoded.has_altitude);
        assert(decoded.altitude == 1904);
        assert(decoded.timestamp == intent.timestamp_s);
    }

    {
        SharePositionIntent intent{};
        intent.peer = 0x55555555UL;
        intent.valid = false;

        const auto effects = collectPrepareOutgoing(runtime, intent, context);
        assert(effects.items.size() == 1);
        const auto* failed = effectAt<EmitActionResultEffect>(effects, 0);
        assert(failed);
        assert(failed->protocol == MeshProtocol::Meshtastic);
        assert(failed->action == ProtocolActionKind::SharePosition);
        assert(failed->state == ProtocolActionState::Failed);
        assert(failed->peer == intent.peer);
    }

    {
        ShareWaypointIntent intent{};
        intent.channel = ChannelId::PRIMARY;
        intent.peer = 0x66666666UL;
        intent.valid = true;
        intent.latitude_deg = 26.67773;
        intent.longitude_deg = 107.28225;
        intent.id = 1710000000U;
        intent.expire = intent.id + 86400U;
        intent.name = "Trail Mate POI";
        intent.description = "Shared from uConsole current GPS fix";

        const auto effects = collectPrepareOutgoing(runtime, intent, context);
        assert(effects.items.size() == 1);
        const auto* packet = effectAt<SendPacketEffect>(effects, 0);
        assert(packet);
        assert(packet->protocol == MeshProtocol::Meshtastic);
        assert(packet->channel == intent.channel);
        assert(packet->dest == intent.peer);
        assert(packet->portnum == meshtastic_PortNum_WAYPOINT_APP);
        assert(!packet->want_ack);
        assert(!packet->want_response);
        assert(!packet->payload.empty());

        meshtastic_Waypoint decoded = meshtastic_Waypoint_init_zero;
        pb_istream_t stream = pb_istream_from_buffer(packet->payload.data(),
                                                     packet->payload.size());
        assert(pb_decode(&stream, meshtastic_Waypoint_fields, &decoded));
        assert(decoded.id == intent.id);
        assert(decoded.has_latitude_i);
        assert(decoded.latitude_i == static_cast<int32_t>(intent.latitude_deg * 1e7));
        assert(decoded.has_longitude_i);
        assert(decoded.longitude_i == static_cast<int32_t>(intent.longitude_deg * 1e7));
        assert(decoded.expire == intent.expire);
        assert(std::string(decoded.name) == intent.name);
        assert(std::string(decoded.description) == intent.description);
    }

    {
        ShareWaypointIntent intent{};
        intent.peer = 0x77777777UL;
        intent.valid = false;

        const auto effects = collectPrepareOutgoing(runtime, intent, context);
        assert(effects.items.size() == 1);
        const auto* failed = effectAt<EmitActionResultEffect>(effects, 0);
        assert(failed);
        assert(failed->protocol == MeshProtocol::Meshtastic);
        assert(failed->action == ProtocolActionKind::ShareWaypoint);
        assert(failed->state == ProtocolActionState::Failed);
        assert(failed->peer == intent.peer);
    }

    {
        TraceRouteIntent intent{};
        intent.peer = context.self_node;
        intent.request_id = 0x0A0B0C0DUL;

        const auto effects = collectPrepareOutgoing(runtime, intent, context);
        assert(effects.items.size() == 1);
        const auto* failed = effectAt<EmitActionResultEffect>(effects, 0);
        assert(failed);
        assert(failed->protocol == MeshProtocol::Meshtastic);
        assert(failed->action == ProtocolActionKind::TraceRoute);
        assert(failed->state == ProtocolActionState::Failed);
        assert(failed->peer == context.self_node);
        assert(failed->request_id == intent.request_id);
    }

    {
        MeshtasticRuntime action_runtime;
        RuntimeContext action_context = context;
        action_context.now_ms = 1000;

        TraceRouteIntent intent{};
        intent.peer = 0x24242424UL;
        intent.request_id = 0x6006UL;
        intent.timeout_ms = 5000;
        assert(collectPrepareOutgoing(action_runtime, intent, action_context).items.size() == 1);

        IncomingPacket routing{};
        routing.protocol = MeshProtocol::Meshtastic;
        routing.portnum = meshtastic_PortNum_ROUTING_APP;
        routing.request_id = intent.request_id;
        const auto routing_payload = encodeRouting(meshtastic_Routing_Error_NONE);
        assert(routing.payload.assign(routing_payload));
        action_context.now_ms = 1200;

        const auto delivered_result = collectHandleIncomingPacket(action_runtime, routing, action_context);
        assert(delivered_result.handling == PacketHandling::HandledStop);
        const auto& delivered_effects = delivered_result.effects;
        assert(delivered_effects.items.size() == 1);
        const auto* delivered = effectAt<EmitActionResultEffect>(delivered_effects, 0);
        assert(delivered);
        assert(delivered->action == ProtocolActionKind::TraceRoute);
        assert(delivered->state == ProtocolActionState::Delivered);
        assert(delivered->request_id == intent.request_id);

        IncomingPacket response{};
        response.protocol = MeshProtocol::Meshtastic;
        response.portnum = meshtastic_PortNum_TRACEROUTE_APP;
        response.request_id = intent.request_id;
        response.payload.push_back(0);
        action_context.now_ms = 1500;

        const auto completed_result = collectHandleIncomingPacket(action_runtime, response, action_context);
        assert(completed_result.handling == PacketHandling::HandledStop);
        const auto& completed_effects = completed_result.effects;
        assert(completed_effects.items.size() == 1);
        const auto* completed = effectAt<EmitActionResultEffect>(completed_effects, 0);
        assert(completed);
        assert(completed->action == ProtocolActionKind::TraceRoute);
        assert(completed->state == ProtocolActionState::Completed);
        assert(completed->request_id == intent.request_id);
    }

    {
        MeshtasticRuntime action_runtime;
        RuntimeContext action_context = context;
        action_context.now_ms = 2000;

        ExchangePositionIntent intent{};
        intent.peer = 0x35353535UL;
        intent.request_id = 0x7007UL;
        assert(collectPrepareOutgoing(action_runtime, intent, action_context).items.size() == 1);

        TxResult tx{};
        tx.protocol = MeshProtocol::Meshtastic;
        tx.request_id = intent.request_id;
        tx.ok = false;
        action_context.now_ms = 2100;

        const auto failed_effects = collectHandleTxResult(action_runtime, tx, action_context);
        assert(failed_effects.items.size() == 1);
        const auto* failed = effectAt<EmitActionResultEffect>(failed_effects, 0);
        assert(failed);
        assert(failed->action == ProtocolActionKind::ExchangePosition);
        assert(failed->state == ProtocolActionState::Failed);
        assert(failed->detail == kMeshtasticActionDetailLocalSendFailed);
    }

    {
        MeshtasticRuntime action_runtime;
        RuntimeContext action_context = context;
        action_context.now_ms = 3000;

        ExchangePositionIntent intent{};
        intent.peer = 0x46464646UL;
        intent.request_id = 0x8008UL;
        assert(collectPrepareOutgoing(action_runtime, intent, action_context).items.size() == 1);

        action_context.now_ms = 3000 + kMeshtasticAppActionTimeoutMs - 1;
        assert(collectTick(action_runtime, action_context).items.empty());

        action_context.now_ms = 3000 + kMeshtasticAppActionTimeoutMs;
        const auto timeout_effects = collectTick(action_runtime, action_context);
        assert(timeout_effects.items.size() == 1);
        const auto* timed_out = effectAt<EmitActionResultEffect>(timeout_effects, 0);
        assert(timed_out);
        assert(timed_out->action == ProtocolActionKind::ExchangePosition);
        assert(timed_out->state == ProtocolActionState::TimedOut);
    }

    {
        IncomingPacket packet{};
        packet.protocol = MeshProtocol::Meshtastic;
        packet.portnum = 0xFEEDUL;

        const auto result = collectHandleIncomingPacket(runtime, packet, context);
        assert(result.handling == PacketHandling::NotHandled);
        assert(result.effects.empty());
    }

    {
        MeshtasticPkiResyncInput input{};
        input.cause = MeshtasticPkiResyncCause::PeerKeyMissing;
        input.peer = 0xAABBCCDDUL;
        input.request_id = 0x1001UL;
        input.channel = ChannelId::PRIMARY;

        const auto effects = collectPkiResync(runtime, input);
        assert(effects.items.size() == 2);

        const auto* node_info = effectAt<SendNodeInfoEffect>(effects, 0);
        assert(node_info);
        assert(node_info->protocol == MeshProtocol::Meshtastic);
        assert(node_info->peer == input.peer);
        assert(node_info->want_response);

        const auto* routing = effectAt<SendRoutingErrorEffect>(effects, 1);
        assert(routing);
        assert(routing->peer == input.peer);
        assert(routing->request_id == input.request_id);
        assert(routing->error_code == meshtastic_Routing_Error_PKI_UNKNOWN_PUBKEY);
    }

    {
        MeshtasticPkiResyncInput input{};
        input.cause = MeshtasticPkiResyncCause::PeerKeyStale;
        input.peer = 0x01020304UL;
        input.request_id = 0x2002UL;
        input.channel = ChannelId::SECONDARY;

        const auto effects = collectPkiResync(runtime, input);
        assert(effects.items.size() == 3);

        const auto* forget = effectAt<ForgetPeerKeyEffect>(effects, 0);
        assert(forget);
        assert(forget->peer == input.peer);

        const auto* node_info = effectAt<SendNodeInfoEffect>(effects, 1);
        assert(node_info);
        assert(node_info->channel == ChannelId::SECONDARY);

        const auto* routing = effectAt<SendRoutingErrorEffect>(effects, 2);
        assert(routing);
        assert(routing->channel == ChannelId::SECONDARY);
    }

    {
        MeshtasticPkiResyncInput input{};
        input.cause = MeshtasticPkiResyncCause::PeerReportsUnknownPubkey;
        input.peer = 0x0BADF00DUL;
        input.request_id = 0x3003UL;

        const auto effects = collectPkiResync(runtime, input);
        assert(effects.items.size() == 1);
        assert(effectAt<SendNodeInfoEffect>(effects, 0));
    }

    {
        MeshtasticPkiResyncInput input{};
        input.cause = MeshtasticPkiResyncCause::LocalNoChannel;
        input.peer = 0x11223344UL;
        input.request_id = 0x5005UL;

        const auto effects = collectPkiResync(runtime, input);
        assert(effects.items.size() == 2);
        assert(effectAt<SendNodeInfoEffect>(effects, 0));

        const auto* routing = effectAt<SendRoutingErrorEffect>(effects, 1);
        assert(routing);
        assert(routing->peer == input.peer);
        assert(routing->request_id == input.request_id);
        assert(routing->error_code == meshtastic_Routing_Error_NO_CHANNEL);
    }

    {
        MeshtasticPkiResyncInput input{};
        input.cause = MeshtasticPkiResyncCause::PeerKeyMissing;
        input.peer = 0;
        input.request_id = 0x4004UL;

        const auto effects = collectPkiResync(runtime, input);
        assert(effects.empty());
    }

    return 0;
}
