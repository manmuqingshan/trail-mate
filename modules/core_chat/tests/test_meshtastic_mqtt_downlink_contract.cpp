#include <cassert>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace
{

std::string readFile(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    assert(stream.is_open());
    std::ostringstream out;
    out << stream.rdbuf();
    return out.str();
}

bool contains(const std::string& haystack, const char* needle)
{
    return haystack.find(needle) != std::string::npos;
}

bool notContains(const std::string& haystack, const char* needle)
{
    return !contains(haystack, needle);
}

std::size_t positionOf(const std::string& haystack, const char* needle)
{
    const auto pos = haystack.find(needle);
    assert(pos != std::string::npos);
    return pos;
}

std::size_t positionOfAfter(const std::string& haystack,
                            const char* needle,
                            std::size_t offset)
{
    const auto pos = haystack.find(needle, offset);
    assert(pos != std::string::npos);
    return pos;
}

} // namespace

int main(int argc, char** argv)
{
    assert(argc == 2);
    const std::filesystem::path repo_root = argv[1];
    const std::string header = readFile(
        repo_root /
        "platform/esp/arduino_common/include/platform/esp/arduino_common/chat/infra/meshtastic/mt_adapter.h");
    const std::string source = readFile(
        repo_root /
        "platform/esp/arduino_common/src/chat/infra/meshtastic/mt_adapter.cpp");
    const std::string helpers = readFile(
        repo_root /
        "modules/core_chat/src/infra/meshtastic/mt_protocol_helpers.cpp");

    assert(contains(header, "struct PendingMqttDownlinkTx"));
    assert(contains(header, "struct MqttDownlinkSeenEntry"));
    assert(contains(header, "mqtt_downlink_tx_queue_"));
    assert(contains(header, "mqtt_downlink_seen_"));
    assert(contains(header, "kSendQueueDrainPerTick"));
    assert(contains(header, "kLoRaAirTxBudgetPerTick"));
    assert(contains(header, "kPkiNodeTableDepth = 64"));
    assert(contains(header, "bool processProtocolActionQueue(uint32_t now_ms,"));
    assert(contains(header, "bool processMqttDownlinkTxQueue(uint32_t now_ms,"));
    assert(contains(header, "uint8_t& tx_budget_remaining"));

    const std::size_t inject_begin =
        positionOf(source, "bool MtAdapter::injectMqttEnvelope");
    const std::size_t handle_begin =
        positionOfAfter(source, "bool MtAdapter::handleMqttProxyMessage", inject_begin);
    const std::string inject_body =
        source.substr(inject_begin, handle_begin - inject_begin);
    assert(contains(inject_body, "enqueueMqttDownlinkTx(wire_buffer, wire_size, *tx_header)"));
    assert(contains(inject_body, "processReceivedPacket(wire_buffer, wire_size);"));
    assert(notContains(inject_body, "transmitWirePacket(wire_buffer, wire_size)"));
    assert(positionOf(inject_body, "enqueueMqttDownlinkTx(wire_buffer, wire_size, *tx_header)") <
           positionOf(inject_body, "processReceivedPacket(wire_buffer, wire_size);"));

    const std::size_t process_send_begin =
        positionOf(source, "void MtAdapter::processSendQueue()");
    const std::size_t process_mqtt_begin = positionOfAfter(
        source, "bool MtAdapter::processMqttDownlinkTxQueue", process_send_begin);
    assert(positionOfAfter(source,
                           "uint8_t tx_budget_remaining = kLoRaAirTxBudgetPerTick;",
                           process_send_begin) < process_mqtt_begin);
    assert(positionOfAfter(source,
                           "processProtocolActionQueue(now, tx_budget_remaining);",
                           process_send_begin) < process_mqtt_begin);
    assert(positionOfAfter(source, "drained < kSendQueueDrainPerTick", process_send_begin) <
           process_mqtt_begin);
    assert(positionOfAfter(source,
                           "processMqttDownlinkTxQueue(now, tx_budget_remaining);",
                           process_send_begin) < process_mqtt_begin);
    assert(contains(source, "tx_budget_remaining > 0"));
    assert(contains(source, "--tx_budget_remaining;"));
    assert(contains(source, "isMqttDownlinkRecentlySeen(header.from, header.id, header.channel"));
    assert(contains(source, "reason=pending_queue_full"));
    assert(contains(source, "reason=airtime_budget"));
    assert(contains(source, "\"radio_queue_full\""));
    assert(contains(source, "const bool is_broadcast = dest == kBroadcastNodeId;"));
    assert(contains(source, "bool track_ack = !is_broadcast && !dest_last_seen_via_mqtt;"));
    assert(contains(source, "tx_ok || dest_last_seen_via_mqtt || is_broadcast"));
    assert(contains(source, "header.to == kBroadcastNodeId && pending_slot && !from_is"));
    assert(contains(source, "waiting_for_peer_key"));
    assert(contains(source, "MAX_PKI_KEY_EXCHANGE_RETRIES"));
    assert(contains(source, "executePkiResync(runtime::MeshtasticPkiResyncCause::PeerKeyMissing"));
    assert(contains(source, "isPermanentQueuedTextFailure"));
    assert(contains(source, "readPkiNodeKeyFromDirectory"));
    assert(contains(source, "loadPkiNodeKeyFromDirectory"));
    assert(contains(source, "findCachedPkiNodeKey"));
    assert(contains(source, "std::min<std::size_t>(send_queue_.size(), kPendingSendQueueDepth)"));
    assert(contains(helpers, "return want_ack && dest != kBroadcastNodeId;"));

    const std::size_t public_app_data =
        positionOf(source, "bool MtAdapter::sendAppData(ChannelId channel");
    const std::size_t app_data_now =
        positionOfAfter(source, "bool MtAdapter::sendAppDataNow", public_app_data);
    const std::string public_app_data_body =
        source.substr(public_app_data, app_data_now - public_app_data);
    assert(contains(public_app_data_body, "runtime::SendPacketEffect packet{};"));
    assert(contains(public_app_data_body, "return enqueueSendPacketAction(packet);"));
    assert(notContains(public_app_data_body, "transmitWirePacket("));

    const std::size_t key_verify_begin =
        positionOf(source, "bool MtAdapter::sendKeyVerificationPacket");
    const std::size_t routing_ack_begin =
        positionOfAfter(source, "bool MtAdapter::sendRoutingAck", key_verify_begin);
    const std::string key_verify_body =
        source.substr(key_verify_begin, routing_ack_begin - key_verify_begin);
    assert(contains(key_verify_body, "runtime::SendPacketEffect packet{};"));
    assert(contains(key_verify_body, "return enqueueSendPacketAction(packet);"));
    assert(notContains(key_verify_body, "transmitWirePacket("));

    return 0;
}
