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

std::size_t positionOf(const std::string& text, const char* needle)
{
    const std::size_t position = text.find(needle);
    assert(position != std::string::npos);
    return position;
}

std::size_t positionOfAfter(const std::string& text,
                            const char* needle,
                            std::size_t offset)
{
    const std::size_t position = text.find(needle, offset);
    assert(position != std::string::npos);
    return position;
}

} // namespace

// VMP MQTT is deliberately a store-and-play carrier.  It must never enter the
// generic Meshtastic MQTT bridge because that path may inject a packet onto
// Sub-GHz.  Keep this source-level contract near the VMP unit tests so a future
// bridge refactor cannot silently turn a received voice message into a relay.
int main(int argc, char** argv)
{
    assert(argc == 2);
    const std::string source = readFile(
        std::filesystem::path(argv[1]) /
        "platform/esp/arduino_common/src/chat/infra/mesh_mqtt_client_runtime.cpp");
    const std::string session = readFile(
        std::filesystem::path(argv[1]) /
        "platform/esp/arduino_common/src/voice/vmp_pager_session.cpp");
    const std::string session_header = readFile(
        std::filesystem::path(argv[1]) /
        "platform/esp/arduino_common/include/platform/esp/arduino_common/voice/vmp_pager_session.h");

    const std::size_t vmp_publish_begin = positionOf(source, "bool flushVmpPublish()");
    const std::size_t publish_queue_begin =
        positionOf(source, "void flushPublishQueue(chat::meshtastic::MtAdapter* mt,");
    const std::string vmp_publish =
        source.substr(vmp_publish_begin, publish_queue_begin - vmp_publish_begin);
    const std::size_t peek = positionOf(vmp_publish, "peekMqttEnvelope(");
    const std::size_t socket_write = positionOf(vmp_publish, "sendPublishRaw(");
    const std::size_t commit = positionOf(vmp_publish, "acknowledgeMqttEnvelope()");
    assert(peek < socket_write);
    assert(socket_write < commit);
    assert(vmp_publish.find("retained_for_retry=1") != std::string::npos);

    const std::size_t vmp_inbound_begin = positionOf(
        source,
        "if (config_.protocol == RuntimeProtocol::Meshtastic &&\n"
        "            isVmpTopic(topic, topic_len))");
    const std::size_t meshcore_begin = positionOfAfter(
        source,
        "if (config_.protocol == RuntimeProtocol::MeshCore)",
        vmp_inbound_begin);
    const std::size_t generic_mt_begin = positionOfAfter(
        source,
        "const bool ok = mt->handleMqttProxyMessage(mt_proxy_);",
        vmp_inbound_begin);
    const std::string vmp_inbound =
        source.substr(vmp_inbound_begin, meshcore_begin - vmp_inbound_begin);

    assert(vmp_inbound_begin < generic_mt_begin);
    assert(vmp_inbound.find("acceptMqttEnvelope(") != std::string::npos);
    assert(vmp_inbound.find("local_only=%u") != std::string::npos);
    assert(vmp_inbound.find("return;") != std::string::npos);
    assert(vmp_inbound.find("handleMqttProxyMessage") == std::string::npos);
    assert(vmp_inbound.find("sendPublishRaw") == std::string::npos);

    // MQTT configuration and an active broker session are different states.
    // Only a CONNACK-established session can select MQTT over LR1121 RF; any
    // stop/liveness failure immediately withdraws that preference. This must
    // remain inside the isolated VMP bridge, never the generic MT path.
    const std::size_t liveness = positionOf(source, "if (!checkSessionLiveness(after_network_ms))");
    const std::size_t online_sync = positionOfAfter(
        source, "vmp_session::setMqttUplinkOnline(", liveness);
    const std::size_t flush = positionOfAfter(source, "flushPublishQueue(mt, mc);", online_sync);
    assert(liveness < online_sync);
    assert(online_sync < flush);
    const std::size_t stop = positionOf(source, "void stop(const char* reason)");
    const std::size_t reset = positionOfAfter(source, "void resetConnectionState()", stop);
    const std::string stop_body = source.substr(stop, reset - stop);
    assert(stop_body.find("vmp_session::setMqttUplinkOnline(\n            false)") !=
           std::string::npos);

    assert(session_header.find("void setMqttUplinkOnline(bool online);") !=
           std::string::npos);
    assert(session.find("bool mqtt_uplink_online_ = false;") != std::string::npos);
    assert(session.find("mqtt_uplink_enabled_ && mqtt_uplink_online_") !=
           std::string::npos);
    assert(session.find("carrier=mqtt plan_begin rf_suppressed=%u") !=
           std::string::npos);
    assert(session.find("automatic RF duplicate") != std::string::npos);
    assert(session.find("commitMqttDeliveryLocked(vmp::VoiceDeliveryState::Sent)") !=
           std::string::npos);

    return 0;
}
