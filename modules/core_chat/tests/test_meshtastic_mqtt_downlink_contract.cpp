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

    assert(contains(header, "struct PendingMqttDownlinkTx"));
    assert(contains(header, "struct MqttDownlinkSeenEntry"));
    assert(contains(header, "mqtt_downlink_tx_queue_"));
    assert(contains(header, "mqtt_downlink_seen_"));
    assert(contains(header, "kMqttDownlinkTxDrainPerTick"));

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
    const std::size_t process_mqtt_begin =
        positionOfAfter(source, "void MtAdapter::processMqttDownlinkTxQueue", process_send_begin);
    assert(positionOfAfter(source, "processMqttDownlinkTxQueue(now);", process_send_begin) <
           process_mqtt_begin);
    assert(contains(source, "drained < kMqttDownlinkTxDrainPerTick"));
    assert(contains(source, "isMqttDownlinkRecentlySeen(header.from, header.id, header.channel"));
    assert(contains(source, "reason=pending_queue_full"));
    assert(contains(source, "reason=airtime_budget"));
    assert(contains(source, "\"radio_queue_full\""));

    return 0;
}
