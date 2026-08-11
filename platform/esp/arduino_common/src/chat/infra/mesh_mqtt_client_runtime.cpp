#include "platform/esp/arduino_common/chat/infra/mesh_mqtt_client_runtime.h"

#include "app/app_config.h"
#include "app/app_facades.h"
#include "chat/infra/meshtastic/mt_radio_config.h"
#include "chat/infra/voice/vmp_mqtt_transport.h"
#include "meshtastic/mesh.pb.h"
#include "meshtastic/mqtt.pb.h"
#include "platform/esp/arduino_common/chat/infra/meshcore/meshcore_adapter.h"
#include "platform/esp/arduino_common/chat/infra/meshtastic/mt_adapter.h"
#include "platform/esp/arduino_common/voice/vmp_pager_session.h"
#include "platform/ui/wifi_access_runtime.h"
#include "platform/ui/wifi_runtime.h"
#include "sys/event_bus.h"

#include <Arduino.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <pb_decode.h>

#include "platform/esp/arduino_common/net/async_tcp_connector.h"
#include "platform/esp/common/memory_budget.h"
#ifndef TRAIL_MATE_ENABLE_BLE
#define TRAIL_MATE_ENABLE_BLE 0
#endif

#if TRAIL_MATE_ENABLE_BLE
#include "ble/ble_manager.h"
#endif

#if __has_include(<lwip/sockets.h>)
#include <cerrno>
#include <lwip/dns.h>
#include <lwip/ip_addr.h>
#include <lwip/sockets.h>
#include <lwip/tcp.h>
#define TRAIL_MATE_MESH_MQTT_HAS_SOCKET 1
#else
#define TRAIL_MATE_MESH_MQTT_HAS_SOCKET 0
#endif

namespace platform::esp::arduino_common::mesh_mqtt
{
namespace
{

constexpr uint16_t kDefaultMqttPort = 1883;
constexpr uint16_t kMqttKeepaliveSeconds = 60;
constexpr uint32_t kConfigRefreshMs = 5000;
constexpr uint32_t kMqttReconnectInitialMs = 2000;
constexpr uint32_t kMqttReconnectMaxMs = 60000;
constexpr uint32_t kMqttPingIntervalMs = 30000;
constexpr uint32_t kMqttConnackTimeoutMs = 10000;
constexpr uint32_t kMqttSubackTimeoutMs = 10000;
constexpr uint32_t kMqttPingResponseTimeoutMs = 10000;
constexpr int32_t kMqttSocketConnectTimeoutMs = 15000;
constexpr std::size_t kTxBufferSize = 512;
constexpr std::size_t kRxBufferSize = 512;
constexpr std::size_t kMeshCoreFrameBufferSize = 255;
constexpr const char* kDefaultMeshtasticMqttRoot =
    app::AppConfig::kDefaultMeshtasticMqttRoot;
constexpr const char* kDefaultMeshCoreMqttRoot =
    app::AppConfig::kDefaultMeshCoreMqttRoot;

constexpr std::size_t kMaxMtMqttTopicLen =
    sizeof(((meshtastic_MqttClientProxyMessage*)nullptr)->topic) - 1U;
constexpr std::size_t kMaxMtMqttPayloadLen =
    sizeof(((meshtastic_MqttClientProxyMessage*)nullptr)->payload_variant.data.bytes);
constexpr std::size_t kMaxMtMqttPublishRemaining =
    2U + kMaxMtMqttTopicLen + kMaxMtMqttPayloadLen;
static_assert(kTxBufferSize >= 1U + 2U + kMaxMtMqttPublishRemaining,
              "MQTT tx buffer must fit the largest Meshtastic proxy PUBLISH");
static_assert(kRxBufferSize >= kMaxMtMqttPublishRemaining,
              "MQTT rx buffer must fit the largest Meshtastic proxy PUBLISH payload");
static_assert(kRxBufferSize >= kMeshCoreFrameBufferSize,
              "MQTT rx buffer must fit one MeshCore raw bridge frame");

void copyBounded(char* dst, std::size_t dst_len, const char* src)
{
    if (!dst || dst_len == 0)
    {
        return;
    }
    if (!src)
    {
        dst[0] = '\0';
        return;
    }
    std::strncpy(dst, src, dst_len - 1);
    dst[dst_len - 1] = '\0';
}

bool elapsed(uint32_t now_ms, uint32_t last_ms, uint32_t interval_ms)
{
    // A timestamp captured before a callback must not look like a completed
    // interval when that callback records a slightly later timestamp. This is
    // still wrap-safe for every interval used here (all are below INT32_MAX).
    return last_ms == 0 ||
           static_cast<int32_t>(now_ms - last_ms) >=
               static_cast<int32_t>(interval_ms);
}

bool deadlinePending(uint32_t now_ms, uint32_t deadline_ms)
{
    return deadline_ms != 0 &&
           static_cast<int32_t>(deadline_ms - now_ms) > 0;
}

bool isDigits(const char* text)
{
    if (!text || text[0] == '\0')
    {
        return false;
    }
    for (const char* p = text; *p; ++p)
    {
        if (!std::isdigit(static_cast<unsigned char>(*p)))
        {
            return false;
        }
    }
    return true;
}

chat::meshtastic::MtAdapter* meshtasticBackend(app::IAppFacade& app_context)
{
    auto* adapter = app_context.getMeshAdapter();
    if (!adapter || adapter->backendProtocol() != chat::MeshProtocol::Meshtastic)
    {
        return nullptr;
    }
    auto* backend = adapter->backendForProtocol(chat::MeshProtocol::Meshtastic);
    return static_cast<chat::meshtastic::MtAdapter*>(backend);
}

chat::meshcore::MeshCoreAdapter* meshCoreBackend(app::IAppFacade& app_context)
{
    auto* adapter = app_context.getMeshAdapter();
    if (!adapter || adapter->backendProtocol() != chat::MeshProtocol::MeshCore)
    {
        return nullptr;
    }
    auto* backend = adapter->backendForProtocol(chat::MeshProtocol::MeshCore);
    return static_cast<chat::meshcore::MeshCoreAdapter*>(backend);
}

bool meshCoreMqttConfigured(const chat::MeshConfig& config)
{
    return config.meshcore_mqtt_enabled && config.meshcore_mqtt_host[0] != '\0';
}

bool meshtasticMqttConfigured(const app::AppConfig& config)
{
    return config.meshtastic_mqtt_enabled && config.meshtastic_mqtt_host[0] != '\0';
}

enum class RuntimeProtocol : uint8_t
{
    None,
    Meshtastic,
    MeshCore,
};

struct EffectiveConfig
{
    RuntimeProtocol protocol = RuntimeProtocol::None;
    bool configured = false;
    bool tls_requested = false;
    bool encrypted_payload_requested = false;
    bool uplink_enabled = true;
    bool downlink_enabled = true;
    char host[65] = {};
    uint16_t port = kDefaultMqttPort;
    char username[65] = {};
    char password[65] = {};
    char root[65] = {};
    char client_id[32] = {};
};

bool sameConfig(const EffectiveConfig& lhs, const EffectiveConfig& rhs)
{
    return lhs.protocol == rhs.protocol &&
           lhs.configured == rhs.configured &&
           lhs.tls_requested == rhs.tls_requested &&
           lhs.encrypted_payload_requested == rhs.encrypted_payload_requested &&
           lhs.uplink_enabled == rhs.uplink_enabled &&
           lhs.downlink_enabled == rhs.downlink_enabled &&
           lhs.port == rhs.port &&
           std::strcmp(lhs.host, rhs.host) == 0 &&
           std::strcmp(lhs.username, rhs.username) == 0 &&
           std::strcmp(lhs.password, rhs.password) == 0 &&
           std::strcmp(lhs.root, rhs.root) == 0 &&
           std::strcmp(lhs.client_id, rhs.client_id) == 0;
}

class PlainMqttRuntime
{
  public:
    void setWifiTransportEnabled(bool enabled)
    {
        if (!enabled)
        {
            stop("wifi_disabled");
        }
    }

    bool wantsStandaloneMode(app::IAppFacade& app_context)
    {
        if (app_context.getMeshProtocol() == chat::MeshProtocol::Meshtastic)
        {
            return meshtasticMqttConfigured(app_context.readConfig());
        }
        if (app_context.getMeshProtocol() == chat::MeshProtocol::MeshCore)
        {
            return meshCoreMqttConfigured(app_context.readConfig().meshcore_config);
        }
        return false;
    }

    void update(app::IAppFacade& app_context)
    {
        const uint32_t now_ms = millis();
        const chat::MeshProtocol protocol = app_context.getMeshProtocol();
        if (protocol != chat::MeshProtocol::Meshtastic &&
            protocol != chat::MeshProtocol::MeshCore)
        {
            ::platform::esp::arduino_common::voice::vmp_session::setMqttUplinkEnabled(false);
            stop("protocol");
            have_config_ = false;
            return;
        }

        if (!have_config_ || elapsed(now_ms, last_config_refresh_ms_, kConfigRefreshMs))
        {
            refreshConfig(app_context, now_ms);
        }

        ::platform::esp::arduino_common::voice::vmp_session::setMqttUplinkEnabled(
            config_.configured && config_.protocol == RuntimeProtocol::Meshtastic &&
            config_.uplink_enabled);

        if (!config_.configured)
        {
            ::platform::esp::arduino_common::voice::vmp_session::setMqttUplinkOnline(
                false);
            stop("disabled");
            syncAdapterDisabled(app_context);
            return;
        }

        forceBleOff(app_context);

        auto* mt = meshtasticBackend(app_context);
        auto* mc = meshCoreBackend(app_context);
        if (config_.protocol == RuntimeProtocol::Meshtastic && !mt)
        {
            stop("adapter");
            syncAdapterDisabled(app_context);
            return;
        }
        if (config_.protocol == RuntimeProtocol::MeshCore && !mc)
        {
            stop("adapter");
            syncAdapterDisabled(app_context);
            return;
        }

        if (config_.tls_requested)
        {
            if (!logged_tls_unsupported_)
            {
                std::printf("[%s][MQTT] direct client disabled reason=tls_unsupported host=%s\n",
                            protocolTag(),
                            config_.host);
                logged_tls_unsupported_ = true;
            }
            stop("tls");
            return;
        }

        if (config_.encrypted_payload_requested && !logged_plaintext_forced_)
        {
            std::printf("[MT][MQTT] direct client forcing plaintext service envelopes\n");
            logged_plaintext_forced_ = true;
        }

        if (!ensureWifi(now_ms))
        {
            ::platform::esp::arduino_common::voice::vmp_session::setMqttUplinkOnline(
                false);
            syncAdapterDisabled(app_context);
            return;
        }

        if (mt)
        {
            syncAdapterSettings(app_context, *mt);
        }
        if (mc)
        {
            mc->setMqttBridgeEnabled(config_.configured && config_.uplink_enabled);
        }

        if (!ensureMqtt(now_ms))
        {
            ::platform::esp::arduino_common::voice::vmp_session::setMqttUplinkOnline(
                false);
            return;
        }

        pumpNetwork(mt, mc);
        // pumpNetwork can process CONNACK and stamp a control-plane deadline
        // with a later millis() value.  Reusing the turn's earlier now_ms here
        // underflows the unsigned elapsed calculation and falsely times out a
        // just-sent SUBSCRIBE or PINGREQ.
        const uint32_t after_network_ms = millis();
        if (!checkSessionLiveness(after_network_ms))
        {
            ::platform::esp::arduino_common::voice::vmp_session::setMqttUplinkOnline(
                false);
            return;
        }
        // VMP must distinguish the persisted MT uplink switch from a live
        // MQTT session. CONNACK is the earliest point at which the existing
        // client can publish, so only this state suppresses LR1121 2.4 GHz
        // voice for a newly admitted clip.
        ::platform::esp::arduino_common::voice::vmp_session::setMqttUplinkOnline(
            config_.protocol == RuntimeProtocol::Meshtastic && config_.uplink_enabled &&
            mqtt_ready_);
        flushPublishQueue(mt, mc);
        maybePing(after_network_ms);
    }

  private:
    enum class RxState : uint8_t
    {
        FixedHeader,
        RemainingLength,
        Payload,
        Discard,
    };

    enum class WifiGateState : uint8_t
    {
        Unknown,
        Ready,
        Unsupported,
        Disabled,
        NoCredentials,
        WaitingForConnection,
    };

    EffectiveConfig config_{};
    bool have_config_ = false;
    bool adapter_synced_disabled_ = false;
    bool mqtt_ready_ = false;
    bool subscribed_ = false;
    bool subscribe_pending_ = false;
    bool control_plane_uplink_sent_ = false;
    bool ping_outstanding_ = false;
    bool mt_publish_pending_ = false;
    bool mc_publish_pending_ = false;
    bool logged_tls_unsupported_ = false;
    bool logged_plaintext_forced_ = false;
    uint16_t packet_id_ = 1;
    uint16_t subscribe_packet_id_ = 0;
    uint32_t last_config_refresh_ms_ = 0;
    uint32_t mqtt_reconnect_not_before_ms_ = 0;
    uint32_t mqtt_reconnect_delay_ms_ = kMqttReconnectInitialMs;
    uint32_t last_io_ms_ = 0;
    uint32_t connect_sent_ms_ = 0;
    uint32_t subscribe_sent_ms_ = 0;
    uint32_t ping_sent_ms_ = 0;
    uint32_t wifi_retry_not_before_ms_ = 0;
    char address_scratch_[80] = {};
    char subscribe_topic_[96] = {};
    char publish_topic_[96] = {};
    std::array<char, 16> dns_primary_scratch_{};
    std::array<char, 16> dns_secondary_scratch_{};
    std::array<uint8_t, kTxBufferSize> tx_{};
    std::array<uint8_t, kRxBufferSize> rx_{};
    std::array<uint8_t, ::chat::voice::vmp::kMaxMqttEnvelopeSize>
        vmp_publish_envelope_{};
    std::array<uint8_t, 32> discard_{};
    meshtastic_MqttClientProxyMessage mt_proxy_ = meshtastic_MqttClientProxyMessage_init_zero;
    meshtastic_MqttClientProxyMessage mt_publish_proxy_ =
        meshtastic_MqttClientProxyMessage_init_zero;
    std::array<uint8_t, kMeshCoreFrameBufferSize> mc_publish_frame_{};
    std::size_t mc_publish_frame_len_ = 0;
    meshtastic_MeshPacket mt_publish_ack_packet_ = meshtastic_MeshPacket_init_zero;
    RxState rx_state_ = RxState::FixedHeader;
    uint8_t rx_header_ = 0;
    std::size_t rx_remaining_len_ = 0;
    std::size_t rx_multiplier_ = 1;
    std::size_t rx_payload_pos_ = 0;
    std::size_t rx_discard_remaining_ = 0;
    uint8_t rx_remaining_bytes_ = 0;
    WifiGateState last_wifi_gate_state_ = WifiGateState::Unknown;

#if TRAIL_MATE_MESH_MQTT_HAS_SOCKET
    int socket_ = -1;
    platform::esp::arduino_common::net::AsyncTcpConnector connector_{};
#endif

#if TRAIL_MATE_MESH_MQTT_HAS_SOCKET
    static bool socketWouldBlock(int error)
    {
        return error == EAGAIN || error == EWOULDBLOCK;
    }

    bool socketAlive() const
    {
        if (socket_ < 0)
        {
            return false;
        }
        uint8_t byte = 0;
        const int result = recv(socket_, &byte, sizeof(byte), MSG_PEEK | MSG_DONTWAIT);
        if (result > 0)
        {
            return true;
        }
        if (result == 0)
        {
            return false;
        }
        return socketWouldBlock(errno);
    }

    int readSocket(uint8_t* data, std::size_t len)
    {
        if (socket_ < 0 || !data || len == 0)
        {
            return -1;
        }
        const int result = recv(socket_, data, len, MSG_DONTWAIT);
        if (result < 0 && socketWouldBlock(errno))
        {
            return 0;
        }
        return result;
    }

    void closeSocket()
    {
        if (socket_ >= 0)
        {
            close(socket_);
            socket_ = -1;
        }
    }
#endif

    const char* protocolTag() const
    {
        return config_.protocol == RuntimeProtocol::MeshCore ? "MC" : "MT";
    }

    bool decodePublishedMeshtasticPacket(const meshtastic_MqttClientProxyMessage& msg,
                                         meshtastic_MeshPacket* out_packet)
    {
        if (!out_packet ||
            msg.which_payload_variant != meshtastic_MqttClientProxyMessage_data_tag ||
            msg.payload_variant.data.size == 0)
        {
            return false;
        }

        std::memset(out_packet, 0, sizeof(*out_packet));
        pb_istream_t stream = pb_istream_from_buffer(msg.payload_variant.data.bytes,
                                                     msg.payload_variant.data.size);
        while (stream.bytes_left > 0)
        {
            pb_wire_type_t wire_type = PB_WT_VARINT;
            uint32_t tag = 0;
            bool eof = false;
            if (!pb_decode_tag(&stream, &wire_type, &tag, &eof))
            {
                return false;
            }
            if (eof)
            {
                break;
            }

            if (tag == meshtastic_ServiceEnvelope_packet_tag)
            {
                if (wire_type != PB_WT_STRING)
                {
                    return false;
                }
                pb_istream_t substream;
                if (!pb_make_string_substream(&stream, &substream))
                {
                    return false;
                }
                const bool ok = pb_decode(&substream,
                                          meshtastic_MeshPacket_fields,
                                          out_packet);
                pb_close_string_substream(&stream, &substream);
                return ok;
            }
            if (!pb_skip_field(&stream, wire_type))
            {
                return false;
            }
        }
        return false;
    }

    bool isPublishedLocalTextPacket(chat::meshtastic::MtAdapter& mt,
                                    const meshtastic_MqttClientProxyMessage& msg,
                                    uint32_t* out_msg_id)
    {
        if (!out_msg_id)
        {
            return false;
        }
        *out_msg_id = 0;
        if (!decodePublishedMeshtasticPacket(msg, &mt_publish_ack_packet_))
        {
            return false;
        }

        const auto& packet = mt_publish_ack_packet_;
        if (packet.id == 0 || packet.from != mt.getNodeId() ||
            packet.which_payload_variant != meshtastic_MeshPacket_decoded_tag)
        {
            return false;
        }
        if (packet.decoded.portnum != meshtastic_PortNum_TEXT_MESSAGE_APP &&
            packet.decoded.portnum != meshtastic_PortNum_TEXT_MESSAGE_COMPRESSED_APP)
        {
            return false;
        }

        *out_msg_id = packet.id;
        return true;
    }

    void notifyMeshtasticPublishSuccess(chat::meshtastic::MtAdapter& mt,
                                        const meshtastic_MqttClientProxyMessage& msg)
    {
        uint32_t msg_id = 0;
        if (!isPublishedLocalTextPacket(mt, msg, &msg_id))
        {
            return;
        }
        sys::EventBus::publish(
            new sys::ChatSendResultEvent(msg_id,
                                         chat::MessageStatus::Sent,
                                         chat::MeshProtocol::Meshtastic),
            0);
        std::printf("[MT][MQTT] publish ack msg=%08lX\n",
                    static_cast<unsigned long>(msg_id));
    }

    void refreshConfig(app::IAppFacade& app_context, uint32_t now_ms)
    {
        last_config_refresh_ms_ = now_ms;

        EffectiveConfig next{};
        buildEffectiveConfig(app_context, next);
        if (!have_config_ || !sameConfig(config_, next))
        {
            const bool was_configured = config_.configured;
            if (mt_publish_pending_ || mc_publish_pending_)
            {
                std::printf("[%s][MQTT] config changed drop in-flight publish\n",
                            protocolTag());
            }
            mt_publish_pending_ = false;
            mc_publish_pending_ = false;
            mc_publish_frame_len_ = 0;
            config_ = next;
            have_config_ = true;
#if TRAIL_MATE_MESH_MQTT_HAS_SOCKET
            connector_.cancel();
#endif
            logged_tls_unsupported_ = false;
            logged_plaintext_forced_ = false;
            stop("config");
            resetMqttReconnectBackoff();
            if (config_.configured || was_configured)
            {
                std::printf("[%s][MQTT] config enabled=%u host=%s port=%u root=%s tls=%u enc_requested=%u up=%u down=%u\n",
                            protocolTag(),
                            config_.configured ? 1U : 0U,
                            config_.host[0] ? config_.host : "<unset>",
                            static_cast<unsigned>(config_.port),
                            config_.root[0] ? config_.root : "<unset>",
                            config_.tls_requested ? 1U : 0U,
                            config_.encrypted_payload_requested ? 1U : 0U,
                            config_.uplink_enabled ? 1U : 0U,
                            config_.downlink_enabled ? 1U : 0U);
            }
        }
    }

    void buildEffectiveConfig(app::IAppFacade& app_context, EffectiveConfig& out)
    {
        out = EffectiveConfig{};
        const chat::MeshProtocol protocol = app_context.getMeshProtocol();
        if (protocol == chat::MeshProtocol::MeshCore)
        {
            buildMeshCoreEffectiveConfig(app_context, out);
            return;
        }

        if (protocol != chat::MeshProtocol::Meshtastic)
        {
            return;
        }
        buildMeshtasticEffectiveConfig(app_context, out);
    }

    void buildMeshtasticEffectiveConfig(app::IAppFacade& app_context, EffectiveConfig& out)
    {
        const app::AppConfig& config = app_context.readConfig();
        if (!meshtasticMqttConfigured(config))
        {
            return;
        }

        out.protocol = RuntimeProtocol::Meshtastic;
        out.configured = true;
        out.tls_requested = false;
        out.encrypted_payload_requested = false;
        out.uplink_enabled = config.meshtastic_mqtt_uplink_enabled;
        out.downlink_enabled = config.meshtastic_mqtt_downlink_enabled;
        out.port = config.meshtastic_mqtt_port != 0 ? config.meshtastic_mqtt_port : kDefaultMqttPort;
        parseConfiguredAddress(config.meshtastic_mqtt_host,
                               out.host,
                               sizeof(out.host),
                               &out.port);
        copyBounded(out.username, sizeof(out.username), config.meshtastic_mqtt_username);
        copyBounded(out.password, sizeof(out.password), config.meshtastic_mqtt_password);
        copyBounded(out.root,
                    sizeof(out.root),
                    config.meshtastic_mqtt_root[0] ? config.meshtastic_mqtt_root
                                                   : kDefaultMeshtasticMqttRoot);
        std::snprintf(out.client_id,
                      sizeof(out.client_id),
                      "tm-%08lx",
                      static_cast<unsigned long>(app_context.getSelfNodeId()));
    }

    void buildMeshCoreEffectiveConfig(app::IAppFacade& app_context, EffectiveConfig& out)
    {
        const chat::MeshConfig& config = app_context.readConfig().meshcore_config;
        if (!meshCoreMqttConfigured(config))
        {
            return;
        }

        out.protocol = RuntimeProtocol::MeshCore;
        out.configured = true;
        out.tls_requested = false;
        out.uplink_enabled = config.meshcore_mqtt_uplink_enabled;
        out.downlink_enabled = config.meshcore_mqtt_downlink_enabled;
        out.port = config.meshcore_mqtt_port != 0 ? config.meshcore_mqtt_port : kDefaultMqttPort;
        parseConfiguredAddress(config.meshcore_mqtt_host,
                               out.host,
                               sizeof(out.host),
                               &out.port);
        copyBounded(out.username, sizeof(out.username), config.meshcore_mqtt_username);
        copyBounded(out.password, sizeof(out.password), config.meshcore_mqtt_password);
        copyBounded(out.root,
                    sizeof(out.root),
                    config.meshcore_mqtt_root[0] ? config.meshcore_mqtt_root
                                                 : kDefaultMeshCoreMqttRoot);
        std::snprintf(out.client_id,
                      sizeof(out.client_id),
                      "tm-mc-%08lx",
                      static_cast<unsigned long>(app_context.getSelfNodeId()));
    }

    void parseConfiguredAddress(const char* address,
                                char* out_host,
                                std::size_t out_host_len,
                                uint16_t* out_port)
    {
        if (!out_host || out_host_len == 0 || !out_port)
        {
            return;
        }
        out_host[0] = '\0';
        if (!address || address[0] == '\0')
        {
            return;
        }

        copyBounded(address_scratch_, sizeof(address_scratch_), address);
        char* start = address_scratch_;
        if (std::strncmp(start, "mqtt://", 7) == 0)
        {
            start += 7;
        }
        else if (std::strncmp(start, "tcp://", 6) == 0)
        {
            start += 6;
        }
        char* colon = std::strrchr(start, ':');
        if (colon && isDigits(colon + 1))
        {
            const unsigned long parsed = std::strtoul(colon + 1, nullptr, 10);
            if (parsed > 0 && parsed <= 65535)
            {
                *out_port = static_cast<uint16_t>(parsed);
                *colon = '\0';
            }
        }
        copyBounded(out_host, out_host_len, start);
    }

    void forceBleOff(app::IAppFacade& app_context)
    {
#if TRAIL_MATE_ENABLE_BLE
        if (app_context.isBleEnabled())
        {
            std::printf("[%s][MQTT] disabling BLE for standalone MQTT mode\n",
                        protocolTag());
            app_context.setBleEnabled(false);
            return;
        }
        if (auto* ble_manager = app_context.getBleManager())
        {
            if (ble_manager->isEnabled())
            {
                std::printf("[%s][MQTT] stopping active BLE service for standalone MQTT mode\n",
                            protocolTag());
                ble_manager->setEnabled(false);
            }
        }
#else
        (void)app_context;
#endif
    }

    void syncAdapterDisabled(app::IAppFacade& app_context)
    {
        if (auto* mc = meshCoreBackend(app_context))
        {
            mc->setMqttBridgeEnabled(false);
        }
        if (adapter_synced_disabled_)
        {
            return;
        }
        if (auto* mt = meshtasticBackend(app_context))
        {
            chat::meshtastic::MtAdapter::MqttProxySettings settings;
            mt->setMqttProxySettings(settings);
            adapter_synced_disabled_ = true;
        }
    }

    void syncAdapterSettings(app::IAppFacade& app_context,
                             chat::meshtastic::MtAdapter& mt)
    {
        chat::meshtastic::MtAdapter::MqttProxySettings settings;
        const app::AppConfig& config = app_context.readConfig();
        settings.enabled = config_.configured;
        settings.proxy_to_client_enabled = config_.configured;
        settings.encryption_enabled = false;
        settings.primary_uplink_enabled =
            config_.uplink_enabled && config.primary_enabled &&
            config.primary_uplink_enabled;
        settings.primary_downlink_enabled =
            config_.downlink_enabled && config.primary_enabled &&
            config.primary_downlink_enabled;
        settings.secondary_uplink_enabled =
            config_.uplink_enabled && config.secondary_enabled &&
            config.secondary_uplink_enabled;
        settings.secondary_downlink_enabled =
            config_.downlink_enabled && config.secondary_enabled &&
            config.secondary_downlink_enabled;
        settings.root = config_.root[0] ? config_.root : kDefaultMeshtasticMqttRoot;
        settings.primary_channel_id =
            chat::meshtastic::channelName(config.meshtastic_config,
                                          chat::ChannelId::PRIMARY);
        settings.secondary_channel_id =
            chat::meshtastic::channelName(config.meshtastic_config,
                                          chat::ChannelId::SECONDARY);
        mt.setMqttProxySettings(settings);
        adapter_synced_disabled_ = false;
    }

    bool ensureWifi(uint32_t now_ms)
    {
        auto status = platform::ui::wifi::status();
        if (!status.supported)
        {
            stop("wifi_unsupported");
            logWifiGate(status, WifiGateState::Unsupported);
            return false;
        }
        if (!status.enabled)
        {
            stop("wifi_disabled");
            logWifiGate(status, WifiGateState::Disabled);
            return false;
        }
        if (!status.has_credentials)
        {
            stop("wifi_no_credentials");
            logWifiGate(status, WifiGateState::NoCredentials);
            return false;
        }
        if (status.connected)
        {
            wifi_retry_not_before_ms_ = 0;
            logWifiGate(status, WifiGateState::Ready);
            return true;
        }

        if (deadlinePending(now_ms, wifi_retry_not_before_ms_))
        {
            logWifiGate(status, WifiGateState::WaitingForConnection);
            return false;
        }

        std::printf("[%s][MQTT] requesting Wi-Fi access for MQTT ssid=%s\n",
                    protocolTag(),
                    status.ssid[0] ? status.ssid : "<unset>");
        logWifiGate(status, WifiGateState::WaitingForConnection);

        platform::ui::wifi_access::Request request{};
        request.client = platform::ui::wifi_access::Client::MeshMqtt;
        request.kind = platform::ui::wifi_access::AccessKind::WifiConnect;
        request.priority = platform::ui::wifi_access::Priority::Messaging;
        request.allow_connect = true;
        request.reason = "mqtt";
        platform::ui::wifi_access::ConnectResult connect_result{};
        if (platform::ui::wifi_access::ensure_connected(request, &connect_result))
        {
            wifi_retry_not_before_ms_ = 0;
            status = platform::ui::wifi::status();
            logWifiGate(status, WifiGateState::Ready);
            return true;
        }
        if (connect_result.retry_after_ms > 0)
        {
            wifi_retry_not_before_ms_ =
                now_ms + connect_result.retry_after_ms;
        }
        status = platform::ui::wifi::status();
        std::printf("[%s][MQTT] Wi-Fi access denied decision=%s retry_after_ms=%lu state=%u message='%s'\n",
                    protocolTag(),
                    platform::ui::wifi_access::decision_name(
                        connect_result.decision),
                    static_cast<unsigned long>(
                        connect_result.retry_after_ms),
                    static_cast<unsigned>(status.state),
                    status.message);
        logWifiGate(status, WifiGateState::WaitingForConnection);
        return false;
    }

    const char* wifiGateStateName(WifiGateState state) const
    {
        switch (state)
        {
        case WifiGateState::Ready:
            return "ready";
        case WifiGateState::Unsupported:
            return "unsupported";
        case WifiGateState::Disabled:
            return "disabled";
        case WifiGateState::NoCredentials:
            return "no_credentials";
        case WifiGateState::WaitingForConnection:
            return "waiting_for_connection";
        case WifiGateState::Unknown:
        default:
            return "unknown";
        }
    }

    void logWifiGate(const platform::ui::wifi::Status& status, WifiGateState state)
    {
        if (last_wifi_gate_state_ == state)
        {
            return;
        }
        last_wifi_gate_state_ = state;
        std::printf("[%s][MQTT] wifi gate state=%s enabled=%u connected=%u creds=%u ssid=%s\n",
                    protocolTag(),
                    wifiGateStateName(state),
                    status.enabled ? 1U : 0U,
                    status.connected ? 1U : 0U,
                    status.has_credentials ? 1U : 0U,
                    status.ssid[0] ? status.ssid : "<unset>");
    }

    void armMqttReconnectBackoff(uint32_t now_ms)
    {
        const uint32_t delay_ms = std::max(mqtt_reconnect_delay_ms_,
                                           kMqttReconnectInitialMs);
        mqtt_reconnect_not_before_ms_ = now_ms + delay_ms;
        mqtt_reconnect_delay_ms_ = std::min(delay_ms * 2U,
                                            kMqttReconnectMaxMs);
    }

    void resetMqttReconnectBackoff()
    {
        mqtt_reconnect_not_before_ms_ = 0;
        mqtt_reconnect_delay_ms_ = kMqttReconnectInitialMs;
    }

    const char* dnsServerText(u8_t server_index,
                              std::array<char, 16>& scratch)
    {
        scratch[0] = '\0';
        if (server_index >= DNS_MAX_SERVERS)
        {
            return "<none>";
        }
        const ip_addr_t* server = dns_getserver(server_index);
        if (!server || ip_addr_isany(server) ||
            !ipaddr_ntoa_r(server,
                           scratch.data(),
                           static_cast<int>(scratch.size())))
        {
            return "<none>";
        }
        return scratch.data();
    }

    void logDnsConfiguration()
    {
        const auto wifi_status = platform::ui::wifi::status();
        std::printf("[%s][MQTT] resolver state wifi_ip=%s dns0=%s dns1=%s\n",
                    protocolTag(),
                    wifi_status.ip[0] ? wifi_status.ip : "<none>",
                    dnsServerText(0, dns_primary_scratch_),
                    dnsServerText(1, dns_secondary_scratch_));
    }

    bool ensureMqtt(uint32_t now_ms)
    {
#if !TRAIL_MATE_MESH_MQTT_HAS_SOCKET
        return false;
#else
        const auto budget = platform::ui::wifi_access::traffic_budget(
            platform::ui::wifi_access::Client::MeshMqtt,
            platform::ui::wifi_access::Priority::Messaging);
        if (socketAlive() &&
            !budget.allow_connect &&
            !budget.allow_read &&
            !budget.allow_write)
        {
            stop("wifi_revoke");
            return false;
        }
        if (socketAlive())
        {
            if (!mqtt_ready_ && connect_sent_ms_ != 0 &&
                elapsed(now_ms, connect_sent_ms_, kMqttConnackTimeoutMs))
            {
                std::printf("[%s][MQTT] broker handshake timeout stage=connack\n",
                            protocolTag());
                stop("connack_timeout");
                return false;
            }
            return true;
        }
        // A peer close used to fall through to closeSocket() without going
        // through stop().  That skipped reconnect backoff, so a broker that
        // immediately rejects a CONNECT caused a tight reconnect loop.  Treat
        // every already-established socket loss as a session stop so the
        // bounded exponential backoff applies consistently.
        if (socket_ >= 0)
        {
            stop("socket_closed");
        }
        closeSocket();
        if (!connector_.pending() &&
            connector_.status().phase !=
                platform::esp::arduino_common::net::TcpConnectPhase::Connected &&
            deadlinePending(now_ms, mqtt_reconnect_not_before_ms_))
        {
            return false;
        }
        if (!budget.allow_connect)
        {
            return false;
        }

        if (config_.host[0] == '\0')
        {
            return false;
        }

        platform::ui::wifi_access::Request request{};
        request.client = platform::ui::wifi_access::Client::MeshMqtt;
        request.kind = platform::ui::wifi_access::AccessKind::LongLivedSocket;
        request.priority = platform::ui::wifi_access::Priority::Messaging;
        request.reason = "mqtt_socket";
        const auto lease = platform::ui::wifi_access::acquire(request);
        if (!lease.granted)
        {
            std::printf("[%s][MQTT] socket connect deferred decision=%s\n",
                        protocolTag(),
                        platform::ui::wifi_access::decision_name(lease.decision));
            return false;
        }

        const auto wifi_status = platform::ui::wifi::status();
        if (!wifi_status.connected)
        {
            std::printf("[%s][MQTT] socket connect deferred wifi_disconnected state=%u message='%s'\n",
                        protocolTag(),
                        static_cast<unsigned>(wifi_status.state),
                        wifi_status.message);
            logWifiGate(wifi_status, WifiGateState::WaitingForConnection);
            return false;
        }

        using platform::esp::arduino_common::net::TcpConnectPhase;
        if (!connector_.pending() &&
            connector_.status().phase != TcpConnectPhase::Connected)
        {
            armMqttReconnectBackoff(now_ms);
            resetConnectionState();
            closeSocket();
            std::printf("[%s][MQTT] broker connect start host=%s port=%u client=%s\n",
                        protocolTag(),
                        config_.host,
                        static_cast<unsigned>(config_.port),
                        config_.client_id);
            logDnsConfiguration();
            if (!connector_.start(config_.host,
                                  config_.port,
                                  now_ms,
                                  static_cast<uint32_t>(
                                      kMqttSocketConnectTimeoutMs)))
            {
                const auto status = connector_.status();
                std::printf("[%s][MQTT] broker connect failed host=%s port=%u stage=%s err=%d\n",
                            protocolTag(),
                            config_.host,
                            static_cast<unsigned>(config_.port),
                            platform::esp::arduino_common::net::
                                tcpConnectFailureName(status.failure),
                            status.detail);
                return false;
            }
        }

        const auto connect_status = connector_.poll(now_ms);
        if (connect_status.phase == TcpConnectPhase::Resolving ||
            connect_status.phase == TcpConnectPhase::Connecting)
        {
            return false;
        }
        if (connect_status.phase != TcpConnectPhase::Connected)
        {
            std::printf("[%s][MQTT] broker connect failed host=%s port=%u stage=%s err=%d\n",
                        protocolTag(),
                        config_.host,
                        static_cast<unsigned>(config_.port),
                        platform::esp::arduino_common::net::
                            tcpConnectFailureName(connect_status.failure),
                        connect_status.detail);
            connector_.cancel();
            return false;
        }

        socket_ = connector_.takeNonBlockingSocket();
        if (socket_ < 0)
        {
            return false;
        }
        const int no_delay = 1;
        if (setsockopt(socket_, IPPROTO_TCP, TCP_NODELAY, &no_delay, sizeof(no_delay)) != 0)
        {
            std::printf("[%s][MQTT] socket option failed option=nodelay err=%d\n",
                        protocolTag(),
                        errno);
        }
        if (!sendConnect())
        {
            closeSocket();
            resetConnectionState();
            return false;
        }
        connect_sent_ms_ = now_ms;
        last_io_ms_ = now_ms;
        return true;
#endif
    }

    void stop(const char* reason)
    {
        ::platform::esp::arduino_common::voice::vmp_session::setMqttUplinkOnline(
            false);
#if TRAIL_MATE_MESH_MQTT_HAS_SOCKET
        const bool retry_after_live_transport = socket_ >= 0;
        connector_.cancel();
        if (socket_ >= 0)
        {
            std::printf("[%s][MQTT] broker stop reason=%s\n",
                        protocolTag(),
                        reason ? reason : "unknown");
            closeSocket();
        }
        if (retry_after_live_transport)
        {
            armMqttReconnectBackoff(millis());
        }
#else
        (void)reason;
#endif
        wifi_retry_not_before_ms_ = 0;
        resetConnectionState();
    }

    void resetConnectionState()
    {
        mqtt_ready_ = false;
        subscribed_ = false;
        subscribe_pending_ = false;
        control_plane_uplink_sent_ = false;
        ping_outstanding_ = false;
        subscribe_packet_id_ = 0;
        connect_sent_ms_ = 0;
        subscribe_sent_ms_ = 0;
        ping_sent_ms_ = 0;
        rx_state_ = RxState::FixedHeader;
        rx_header_ = 0;
        rx_remaining_len_ = 0;
        rx_multiplier_ = 1;
        rx_payload_pos_ = 0;
        rx_discard_remaining_ = 0;
        rx_remaining_bytes_ = 0;
    }

    bool sendConnect()
    {
        const bool has_pass = config_.password[0] != '\0';
        const bool has_user = config_.username[0] != '\0' || has_pass;
        const std::size_t remaining_len =
            10U +
            mqttStringSize(config_.client_id) +
            (has_user ? mqttStringSize(config_.username) : 0U) +
            (has_pass ? mqttStringSize(config_.password) : 0U);

        std::size_t pos = 0;
        if (!beginPacket(0x10, remaining_len, pos))
        {
            return false;
        }
        if (!appendMqttString("MQTT", pos) ||
            !appendByte(4, pos))
        {
            return false;
        }
        uint8_t flags = 0x02;
        if (has_pass)
        {
            flags |= 0x40;
        }
        if (has_user)
        {
            flags |= 0x80;
        }
        if (!appendByte(flags, pos) ||
            !appendU16(kMqttKeepaliveSeconds, pos) ||
            !appendMqttString(config_.client_id, pos))
        {
            return false;
        }
        if (has_user && !appendMqttString(config_.username, pos))
        {
            return false;
        }
        if (has_pass && !appendMqttString(config_.password, pos))
        {
            return false;
        }
        return writePacket(pos, "connect");
    }

    bool sendSubscribe()
    {
        if (subscribed_)
        {
            return true;
        }
        if (!config_.downlink_enabled)
        {
            subscribed_ = true;
            return true;
        }
        if (subscribe_pending_)
        {
            return true;
        }
        if (config_.protocol == RuntimeProtocol::MeshCore)
        {
            std::snprintf(subscribe_topic_,
                          sizeof(subscribe_topic_),
                          "%s/raw/#",
                          config_.root[0] ? config_.root : kDefaultMeshCoreMqttRoot);
        }
        else
        {
            std::snprintf(subscribe_topic_,
                          sizeof(subscribe_topic_),
                          "%s/2/e/#",
                          config_.root[0] ? config_.root : kDefaultMeshtasticMqttRoot);
        }
        const uint16_t packet_id = nextPacketId();
        const std::size_t remaining_len = 2U + mqttStringSize(subscribe_topic_) + 1U;
        std::size_t pos = 0;
        if (!beginPacket(0x82, remaining_len, pos) ||
            !appendU16(packet_id, pos) ||
            !appendMqttString(subscribe_topic_, pos) ||
            !appendByte(0, pos))
        {
            return false;
        }
        if (!writePacket(pos, "subscribe"))
        {
            return false;
        }
        subscribe_pending_ = true;
        subscribe_packet_id_ = packet_id;
        subscribe_sent_ms_ = millis();
        std::printf("[%s][MQTT] subscribe sent topic=%s packet_id=%u\n",
                    protocolTag(),
                    subscribe_topic_,
                    static_cast<unsigned>(packet_id));
        return true;
    }

    bool sendPublish(const meshtastic_MqttClientProxyMessage& msg)
    {
        if (msg.which_payload_variant != meshtastic_MqttClientProxyMessage_data_tag ||
            msg.topic[0] == '\0')
        {
            return false;
        }
        const std::size_t topic_len = std::strlen(msg.topic);
        const std::size_t payload_len = msg.payload_variant.data.size;
        const std::size_t remaining_len = 2U + topic_len + payload_len;
        std::size_t pos = 0;
        const uint8_t header = static_cast<uint8_t>(0x30 | (msg.retained ? 0x01 : 0));
        if (!beginPacket(header, remaining_len, pos) ||
            !appendMqttString(msg.topic, pos) ||
            !appendBytes(msg.payload_variant.data.bytes, payload_len, pos))
        {
            return false;
        }
        return writePacket(pos, "publish");
    }

    bool sendPublishRaw(const char* topic, const uint8_t* payload, std::size_t payload_len)
    {
        if (!topic || topic[0] == '\0' || (!payload && payload_len > 0))
        {
            return false;
        }
        const std::size_t topic_len = std::strlen(topic);
        const std::size_t remaining_len = 2U + topic_len + payload_len;
        std::size_t pos = 0;
        if (!beginPacket(0x30, remaining_len, pos) ||
            !appendMqttString(topic, pos) ||
            !appendBytes(payload, payload_len, pos))
        {
            return false;
        }
        return writePacket(pos, "publish_raw");
    }

    bool sendPuback(uint16_t packet_id)
    {
        std::size_t pos = 0;
        return beginPacket(0x40U, 2U, pos) &&
               appendU16(packet_id, pos) &&
               writePacket(pos, "puback");
    }

    bool buildVmpTopic(char* out, std::size_t out_len) const
    {
        if (!out || out_len == 0U || config_.protocol != RuntimeProtocol::Meshtastic)
        {
            return false;
        }
        const int written = std::snprintf(
            out,
            out_len,
            "%s/2/e/vmp",
            config_.root[0] ? config_.root : kDefaultMeshtasticMqttRoot);
        return written > 0 && static_cast<std::size_t>(written) < out_len;
    }

    bool isVmpTopic(const uint8_t* topic, std::size_t topic_len)
    {
        if (!topic || topic_len == 0U ||
            !buildVmpTopic(publish_topic_, sizeof(publish_topic_)))
        {
            return false;
        }
        const std::size_t expected_len = std::strlen(publish_topic_);
        return topic_len == expected_len &&
               std::memcmp(topic, publish_topic_, expected_len) == 0;
    }

    bool flushVmpPublish()
    {
        if (config_.protocol != RuntimeProtocol::Meshtastic ||
            !config_.uplink_enabled ||
            !buildVmpTopic(publish_topic_, sizeof(publish_topic_)))
        {
            return false;
        }
        std::size_t envelope_len = vmp_publish_envelope_.size();
        if (!::platform::esp::arduino_common::voice::vmp_session::peekMqttEnvelope(
                vmp_publish_envelope_.data(), &envelope_len))
        {
            return false;
        }
        if (!sendPublishRaw(publish_topic_,
                            vmp_publish_envelope_.data(),
                            envelope_len))
        {
            std::printf("[VMP][MQTT] publish failed retained_for_retry=1\n");
            stop("vmp_publish");
            return false;
        }
        if (!::platform::esp::arduino_common::voice::vmp_session::acknowledgeMqttEnvelope())
        {
            std::printf("[VMP][MQTT] publish acknowledgement lost\n");
            stop("vmp_publish_ack");
            return false;
        }
        std::printf("[VMP][MQTT] publish topic=%s bytes=%u\n",
                    publish_topic_,
                    static_cast<unsigned>(envelope_len));
        return true;
    }

    void flushPublishQueue(chat::meshtastic::MtAdapter* mt,
                           chat::meshcore::MeshCoreAdapter* mc)
    {
        // A successful CONNACK authorizes the client to publish.  SUBACK only
        // governs inbound routing; making uplink depend on it turns a delayed
        // or lost subscription acknowledgement into a complete bridge outage.
        if (!mqtt_ready_)
        {
            return;
        }
        const auto budget = platform::ui::wifi_access::traffic_budget(
            platform::ui::wifi_access::Client::MeshMqtt,
            platform::ui::wifi_access::Priority::Messaging);
        // During a pending SUBACK the connection is still MQTT-ready, but a
        // normal traffic budget may not be granted before the subscription
        // timeout.  Permit one queued uplink to establish useful forwarding;
        // subsequent calls remain subject to normal arbitration.
        const bool control_plane_uplink =
            subscribe_pending_ && !control_plane_uplink_sent_;
        if ((!budget.allow_write || budget.tx_packet_budget == 0) &&
            !control_plane_uplink)
        {
            return;
        }
        const std::size_t tx_packet_budget =
            control_plane_uplink ? std::max<std::size_t>(budget.tx_packet_budget, 1U)
                                 : budget.tx_packet_budget;

        if (flushVmpPublish())
        {
            if (control_plane_uplink)
            {
                control_plane_uplink_sent_ = true;
            }
            return;
        }

        if (config_.protocol == RuntimeProtocol::MeshCore)
        {
            if (flushMeshCorePublishQueue(mc, tx_packet_budget) &&
                control_plane_uplink)
            {
                control_plane_uplink_sent_ = true;
            }
            return;
        }
        if (!mt)
        {
            return;
        }

        for (std::size_t sent = 0; sent < tx_packet_budget; ++sent)
        {
            if (!mt_publish_pending_)
            {
                std::memset(&mt_publish_proxy_, 0, sizeof(mt_publish_proxy_));
                if (!mt->pollMqttProxyMessage(&mt_publish_proxy_))
                {
                    return;
                }
                mt_publish_pending_ = true;
            }
            if (!sendPublish(mt_publish_proxy_))
            {
                std::printf("[MT][MQTT] publish failed topic=%s retained_for_retry=1\n",
                            mt_publish_proxy_.topic);
                stop("publish");
                return;
            }
            std::printf("[MT][MQTT] publish topic=%s bytes=%u\n",
                        mt_publish_proxy_.topic,
                        static_cast<unsigned>(mt_publish_proxy_.payload_variant.data.size));
            notifyMeshtasticPublishSuccess(*mt, mt_publish_proxy_);
            mt_publish_pending_ = false;
            if (control_plane_uplink)
            {
                control_plane_uplink_sent_ = true;
                return;
            }
        }
    }

    bool flushMeshCorePublishQueue(chat::meshcore::MeshCoreAdapter* mc,
                                   std::size_t packet_budget)
    {
        if (!mc || !config_.uplink_enabled)
        {
            return false;
        }
        if (rx_state_ != RxState::FixedHeader)
        {
            return false;
        }

        std::snprintf(publish_topic_,
                      sizeof(publish_topic_),
                      "%s/raw/%s",
                      config_.root[0] ? config_.root : kDefaultMeshCoreMqttRoot,
                      config_.client_id[0] ? config_.client_id : "trail-mate");
        bool sent_any = false;
        for (std::size_t sent = 0; sent < packet_budget; ++sent)
        {
            if (!mc_publish_pending_)
            {
                mc_publish_frame_len_ = 0;
                if (!mc->pollMqttBridgePacket(mc_publish_frame_.data(),
                                              mc_publish_frame_len_,
                                              mc_publish_frame_.size()))
                {
                    return sent_any;
                }
                mc_publish_pending_ = true;
            }
            if (!sendPublishRaw(publish_topic_,
                                mc_publish_frame_.data(),
                                mc_publish_frame_len_))
            {
                std::printf("[MC][MQTT] publish failed topic=%s bytes=%u\n",
                            publish_topic_,
                            static_cast<unsigned>(mc_publish_frame_len_));
                stop("publish");
                return false;
            }
            std::printf("[MC][MQTT] publish topic=%s bytes=%u\n",
                        publish_topic_,
                        static_cast<unsigned>(mc_publish_frame_len_));
            mc_publish_pending_ = false;
            mc_publish_frame_len_ = 0;
            sent_any = true;
        }
        return sent_any;
    }

    void maybePing(uint32_t now_ms)
    {
        if (!mqtt_ready_ || ping_outstanding_ ||
            !elapsed(now_ms, last_io_ms_, kMqttPingIntervalMs))
        {
            return;
        }
        const auto budget = platform::ui::wifi_access::traffic_budget(
            platform::ui::wifi_access::Client::MeshMqtt,
            platform::ui::wifi_access::Priority::Messaging);
        if (!budget.allow_write)
        {
            return;
        }
        std::size_t pos = 0;
        if (beginPacket(0xC0, 0, pos) && writePacket(pos, "ping"))
        {
            ping_outstanding_ = true;
            ping_sent_ms_ = now_ms;
            return;
        }
        stop("ping");
    }

    bool checkSessionLiveness(uint32_t now_ms)
    {
#if !TRAIL_MATE_MESH_MQTT_HAS_SOCKET
        (void)now_ms;
        return false;
#else
        if (!socketAlive())
        {
            return false;
        }
        if (subscribe_pending_ &&
            elapsed(now_ms, subscribe_sent_ms_, kMqttSubackTimeoutMs))
        {
            std::printf("[%s][MQTT] broker handshake timeout stage=suback\n",
                        protocolTag());
            stop("suback_timeout");
            return false;
        }
        if (ping_outstanding_ &&
            elapsed(now_ms, ping_sent_ms_, kMqttPingResponseTimeoutMs))
        {
            std::printf("[%s][MQTT] broker liveness timeout stage=pingresp\n",
                        protocolTag());
            stop("pingresp_timeout");
            return false;
        }
        return true;
#endif
    }

    void pumpNetwork(chat::meshtastic::MtAdapter* mt,
                     chat::meshcore::MeshCoreAdapter* mc)
    {
#if !TRAIL_MATE_MESH_MQTT_HAS_SOCKET
        (void)mt;
        (void)mc;
#else
        if (!socketAlive())
        {
            stop("socket");
            return;
        }
        const auto budget = platform::ui::wifi_access::traffic_budget(
            platform::ui::wifi_access::Client::MeshMqtt,
            platform::ui::wifi_access::Priority::Messaging);
        const bool control_plane_pending =
            connect_sent_ms_ != 0 ||
            subscribe_pending_ ||
            ping_outstanding_;
        std::size_t packet_budget = budget.rx_packet_budget;
        std::size_t byte_budget = budget.rx_byte_budget;
        if (control_plane_pending)
        {
            // MQTT control responses are required to complete or validate the
            // session. Do not let ordinary application traffic arbitration
            // starve CONNACK, SUBACK, or PINGRESP until their timeout fires.
            packet_budget = std::max<std::size_t>(packet_budget, 1U);
            byte_budget = std::max<std::size_t>(byte_budget, 32U);
        }
        else if (!budget.allow_read)
        {
            return;
        }
        if (packet_budget == 0 || byte_budget == 0)
        {
            return;
        }

        std::size_t packets = 0;
        std::size_t bytes = 0;
        while (socketAlive() &&
               packets < packet_budget &&
               bytes < byte_budget)
        {
            switch (rx_state_)
            {
            case RxState::FixedHeader:
            {
                uint8_t value = 0;
                const int read = readSocket(&value, sizeof(value));
                if (read == 0)
                {
                    return;
                }
                if (read < 0)
                {
                    stop("socket_read");
                    return;
                }
                rx_header_ = value;
                rx_remaining_len_ = 0;
                rx_multiplier_ = 1;
                rx_remaining_bytes_ = 0;
                rx_state_ = RxState::RemainingLength;
                ++bytes;
                break;
            }
            case RxState::RemainingLength:
                if (!readRemainingLength(bytes, packets, mt, mc))
                {
                    return;
                }
                break;
            case RxState::Payload:
                if (readPayload(bytes, packets, mt, mc))
                {
                    ++packets;
                }
                break;
            case RxState::Discard:
                readDiscard(bytes);
                break;
            }
        }
        if (!socketAlive())
        {
            stop("socket");
        }
#endif
    }

    bool readRemainingLength(std::size_t& bytes,
                             std::size_t& packets,
                             chat::meshtastic::MtAdapter* mt,
                             chat::meshcore::MeshCoreAdapter* mc)
    {
#if !TRAIL_MATE_MESH_MQTT_HAS_SOCKET
        (void)bytes;
        (void)packets;
        (void)mt;
        (void)mc;
        return false;
#else
        uint8_t encoded = 0;
        const int read = readSocket(&encoded, sizeof(encoded));
        if (read == 0)
        {
            return false;
        }
        if (read < 0)
        {
            stop("socket_read");
            return false;
        }
        rx_remaining_len_ += (encoded & 0x7FU) * rx_multiplier_;
        rx_multiplier_ *= 128U;
        ++rx_remaining_bytes_;
        ++bytes;
        if (rx_remaining_bytes_ > 4)
        {
            stop("bad_remaining_length");
            return false;
        }
        if ((encoded & 0x80U) == 0)
        {
            if (rx_remaining_len_ > rx_.size())
            {
                std::printf("[%s][MQTT] drop packet reason=too_large type=0x%02X len=%u\n",
                            protocolTag(),
                            rx_header_,
                            static_cast<unsigned>(rx_remaining_len_));
                rx_discard_remaining_ = rx_remaining_len_;
                rx_state_ = RxState::Discard;
            }
            else if (rx_remaining_len_ == 0)
            {
                rx_payload_pos_ = 0;
                handlePacket(mt, mc);
                rx_state_ = RxState::FixedHeader;
                ++packets;
                last_io_ms_ = millis();
            }
            else
            {
                rx_payload_pos_ = 0;
                rx_state_ = RxState::Payload;
            }
            return true;
        }
        return false;
#endif
    }

    bool readPayload(std::size_t& bytes,
                     std::size_t& packets,
                     chat::meshtastic::MtAdapter* mt,
                     chat::meshcore::MeshCoreAdapter* mc)
    {
        (void)packets;
#if !TRAIL_MATE_MESH_MQTT_HAS_SOCKET
        (void)bytes;
        (void)mt;
        (void)mc;
        return false;
#else
        const std::size_t remaining = rx_remaining_len_ - rx_payload_pos_;
        const int read = readSocket(rx_.data() + rx_payload_pos_, remaining);
        if (read == 0)
        {
            return false;
        }
        if (read < 0)
        {
            stop("socket_read");
            return false;
        }
        rx_payload_pos_ += static_cast<std::size_t>(read);
        bytes += static_cast<std::size_t>(read);
        if (rx_payload_pos_ < rx_remaining_len_)
        {
            return false;
        }

        handlePacket(mt, mc);
        rx_state_ = RxState::FixedHeader;
        rx_remaining_len_ = 0;
        rx_payload_pos_ = 0;
        last_io_ms_ = millis();
        return true;
#endif
    }

    void readDiscard(std::size_t& bytes)
    {
#if TRAIL_MATE_MESH_MQTT_HAS_SOCKET
        const std::size_t want = std::min<std::size_t>(
            rx_discard_remaining_,
            discard_.size());
        const int read = readSocket(discard_.data(), want);
        if (read == 0)
        {
            return;
        }
        if (read < 0)
        {
            stop("socket_read");
            return;
        }
        rx_discard_remaining_ -= static_cast<std::size_t>(read);
        bytes += static_cast<std::size_t>(read);
        if (rx_discard_remaining_ == 0)
        {
            rx_state_ = RxState::FixedHeader;
        }
#else
        (void)bytes;
#endif
    }

    void handlePacket(chat::meshtastic::MtAdapter* mt,
                      chat::meshcore::MeshCoreAdapter* mc)
    {
        const uint8_t packet_type = rx_header_ & 0xF0U;
        switch (packet_type)
        {
        case 0x20:
            handleConnack();
            break;
        case 0x30:
            handlePublish(mt, mc);
            break;
        case 0x90:
            handleSuback();
            break;
        case 0xD0:
            ping_outstanding_ = false;
            ping_sent_ms_ = 0;
            break;
        default:
            break;
        }
    }

    void handleConnack()
    {
        if (rx_remaining_len_ < 2)
        {
            stop("connack_short");
            return;
        }
        const uint8_t rc = rx_[1];
        if (rc != 0)
        {
            std::printf("[%s][MQTT] connack rejected rc=%u\n",
                        protocolTag(),
                        static_cast<unsigned>(rc));
            stop("connack");
            return;
        }
        mqtt_ready_ = true;
        connect_sent_ms_ = 0;
        resetMqttReconnectBackoff();
        std::printf("[%s][MQTT] broker connected\n", protocolTag());
        if (!sendSubscribe())
        {
            stop("subscribe");
        }
    }

    void handleSuback()
    {
        if (rx_remaining_len_ < 3)
        {
            stop("suback_short");
            return;
        }
        const uint16_t packet_id =
            (static_cast<uint16_t>(rx_[0]) << 8U) | rx_[1];
        const uint8_t result = rx_[2];
        if (!subscribe_pending_ ||
            packet_id != subscribe_packet_id_ ||
            result != 0)
        {
            std::printf("[%s][MQTT] suback rejected packet_id=%u expected=%u result=0x%02X\n",
                        protocolTag(),
                        static_cast<unsigned>(packet_id),
                        static_cast<unsigned>(subscribe_packet_id_),
                        static_cast<unsigned>(result));
            stop("suback");
            return;
        }
        subscribe_pending_ = false;
        subscribed_ = true;
        subscribe_packet_id_ = 0;
        subscribe_sent_ms_ = 0;
        std::printf("[%s][MQTT] subscribed topic=%s\n",
                    protocolTag(),
                    subscribe_topic_);
    }

    void handlePublish(chat::meshtastic::MtAdapter* mt,
                       chat::meshcore::MeshCoreAdapter* mc)
    {
        const uint8_t qos = (rx_header_ >> 1U) & 0x03U;
        if (qos == 2U)
        {
            std::printf("[%s][MQTT] inbound reject reason=qos2\n",
                        protocolTag());
            stop("publish_qos2");
            return;
        }
        if (qos == 3U)
        {
            std::printf("[%s][MQTT] inbound reject reason=invalid_qos\n",
                        protocolTag());
            stop("publish_qos_invalid");
            return;
        }
        if (rx_remaining_len_ < 2)
        {
            return;
        }

        const std::size_t topic_len =
            (static_cast<std::size_t>(rx_[0]) << 8U) | rx_[1];
        if (2U + topic_len > rx_remaining_len_)
        {
            std::printf("[%s][MQTT] inbound drop reason=topic_len len=%u\n",
                        protocolTag(),
                        static_cast<unsigned>(topic_len));
            return;
        }

        std::size_t payload_offset = 2U + topic_len;
        if (qos == 1U)
        {
            if (payload_offset + 2U > rx_remaining_len_)
            {
                std::printf("[%s][MQTT] inbound reject reason=packet_id\n",
                            protocolTag());
                stop("publish_packet_id");
                return;
            }
            const uint16_t packet_id =
                (static_cast<uint16_t>(rx_[payload_offset]) << 8U) |
                rx_[payload_offset + 1U];
            payload_offset += 2U;
            if (!sendPuback(packet_id))
            {
                std::printf("[%s][MQTT] inbound ack failed packet_id=%u\n",
                            protocolTag(),
                            static_cast<unsigned>(packet_id));
                stop("puback");
                return;
            }
        }
        const std::size_t payload_len = rx_remaining_len_ - payload_offset;
        const uint8_t* const topic = rx_.data() + 2U;
        if (config_.protocol == RuntimeProtocol::Meshtastic &&
            isVmpTopic(topic, topic_len))
        {
            if (!config_.downlink_enabled || (rx_header_ & 0x01U) != 0U ||
                payload_len == 0U ||
                payload_len > ::chat::voice::vmp::kMaxMqttEnvelopeSize)
            {
                std::printf("[VMP][MQTT] inbound drop retained=%u bytes=%u\n",
                            (rx_header_ & 0x01U) != 0U ? 1U : 0U,
                            static_cast<unsigned>(payload_len));
                return;
            }
            const bool accepted =
                ::platform::esp::arduino_common::voice::vmp_session::acceptMqttEnvelope(
                    rx_.data() + payload_offset, payload_len);
            std::printf("[VMP][MQTT] inbound bytes=%u local_only=%u\n",
                        static_cast<unsigned>(payload_len),
                        accepted ? 1U : 0U);
            return;
        }
        if (config_.protocol == RuntimeProtocol::MeshCore)
        {
            handleMeshCorePublish(mc, payload_offset, payload_len, topic_len);
            return;
        }
        if (!mt)
        {
            return;
        }
        if (topic_len >= sizeof(mt_proxy_.topic))
        {
            std::printf("[MT][MQTT] inbound drop reason=topic_len len=%u\n",
                        static_cast<unsigned>(topic_len));
            return;
        }
        if (payload_len > sizeof(mt_proxy_.payload_variant.data.bytes))
        {
            std::printf("[MT][MQTT] inbound drop reason=payload_len len=%u\n",
                        static_cast<unsigned>(payload_len));
            return;
        }

        std::memset(&mt_proxy_, 0, sizeof(mt_proxy_));
        std::memcpy(mt_proxy_.topic, rx_.data() + 2U, topic_len);
        mt_proxy_.topic[topic_len] = '\0';
        mt_proxy_.which_payload_variant = meshtastic_MqttClientProxyMessage_data_tag;
        mt_proxy_.payload_variant.data.size = static_cast<pb_size_t>(payload_len);
        if (payload_len > 0)
        {
            std::memcpy(mt_proxy_.payload_variant.data.bytes,
                        rx_.data() + payload_offset,
                        payload_len);
        }
        mt_proxy_.retained = (rx_header_ & 0x01U) != 0;
        const bool ok = mt->handleMqttProxyMessage(mt_proxy_);
        std::printf("[MT][MQTT] inbound topic=%s bytes=%u ok=%u retained=%u\n",
                    mt_proxy_.topic,
                    static_cast<unsigned>(payload_len),
                    ok ? 1U : 0U,
                    mt_proxy_.retained ? 1U : 0U);
    }

    void handleMeshCorePublish(chat::meshcore::MeshCoreAdapter* mc,
                               std::size_t payload_offset,
                               std::size_t payload_len,
                               std::size_t topic_len)
    {
        if (!config_.downlink_enabled)
        {
            return;
        }
        if (!mc)
        {
            return;
        }
        if ((rx_header_ & 0x01U) != 0)
        {
            std::printf("[MC][MQTT] inbound drop reason=retained topic_len=%u bytes=%u\n",
                        static_cast<unsigned>(topic_len),
                        static_cast<unsigned>(payload_len));
            return;
        }
        if (payload_len == 0 || payload_len > kMeshCoreFrameBufferSize)
        {
            std::printf("[MC][MQTT] inbound drop reason=payload_len topic_len=%u bytes=%u\n",
                        static_cast<unsigned>(topic_len),
                        static_cast<unsigned>(payload_len));
            return;
        }
        mc->handleMqttBridgePacket(rx_.data() + payload_offset, payload_len);
        std::printf("[MC][MQTT] inbound raw bytes=%u retained=%u\n",
                    static_cast<unsigned>(payload_len),
                    (rx_header_ & 0x01U) != 0 ? 1U : 0U);
    }

    std::size_t mqttStringSize(const char* text) const
    {
        return 2U + (text ? std::strlen(text) : 0U);
    }

    uint16_t nextPacketId()
    {
        ++packet_id_;
        if (packet_id_ == 0)
        {
            packet_id_ = 1;
        }
        return packet_id_;
    }

    bool beginPacket(uint8_t header, std::size_t remaining_len, std::size_t& pos)
    {
        pos = 0;
        if (!appendByte(header, pos))
        {
            return false;
        }
        do
        {
            uint8_t encoded = remaining_len % 128U;
            remaining_len /= 128U;
            if (remaining_len > 0)
            {
                encoded |= 0x80U;
            }
            if (!appendByte(encoded, pos))
            {
                return false;
            }
        } while (remaining_len > 0);
        return true;
    }

    bool appendByte(uint8_t value, std::size_t& pos)
    {
        if (pos >= tx_.size())
        {
            return false;
        }
        tx_[pos++] = value;
        return true;
    }

    bool appendU16(uint16_t value, std::size_t& pos)
    {
        return appendByte(static_cast<uint8_t>((value >> 8U) & 0xFFU), pos) &&
               appendByte(static_cast<uint8_t>(value & 0xFFU), pos);
    }

    bool appendBytes(const uint8_t* data, std::size_t len, std::size_t& pos)
    {
        if (len == 0)
        {
            return true;
        }
        if (!data || pos + len > tx_.size())
        {
            return false;
        }
        std::memcpy(tx_.data() + pos, data, len);
        pos += len;
        return true;
    }

    bool appendMqttString(const char* text, std::size_t& pos)
    {
        const std::size_t len = text ? std::strlen(text) : 0U;
        if (len > 65535U)
        {
            return false;
        }
        return appendU16(static_cast<uint16_t>(len), pos) &&
               appendBytes(reinterpret_cast<const uint8_t*>(text), len, pos);
    }

    bool writePacket(std::size_t len, const char* op)
    {
#if !TRAIL_MATE_MESH_MQTT_HAS_SOCKET
        (void)len;
        (void)op;
        return false;
#else
        if (!socketAlive())
        {
            return false;
        }
        std::size_t written = 0;
        while (written < len)
        {
            const int result = send(socket_,
                                    tx_.data() + written,
                                    len - written,
                                    MSG_DONTWAIT);
            if (result > 0)
            {
                written += static_cast<std::size_t>(result);
                continue;
            }
            if (result < 0 && errno == EINTR)
            {
                continue;
            }
            std::printf("[%s][MQTT] write failed op=%s len=%u written=%u err=%d\n",
                        protocolTag(),
                        op ? op : "packet",
                        static_cast<unsigned>(len),
                        static_cast<unsigned>(written),
                        result < 0 ? errno : 0);
            return false;
        }
        last_io_ms_ = millis();
        return true;
#endif
    }
};

PlainMqttRuntime* createRuntime()
{
    void* storage =
        ::platform::esp::common::memory::allocatePreferred("mqtt_runtime",
                                                           sizeof(PlainMqttRuntime));
    if (!storage)
    {
        std::printf("[MT][MQTT][Mem] runtime unavailable bytes=%u\n",
                    static_cast<unsigned>(sizeof(PlainMqttRuntime)));
        return nullptr;
    }
    return new (storage) PlainMqttRuntime();
}

PlainMqttRuntime* runtime()
{
    static PlainMqttRuntime* instance = createRuntime();
    return instance;
}

} // namespace

bool wantsStandaloneMode(app::IAppFacade& app_context)
{
    PlainMqttRuntime* instance = runtime();
    return instance && instance->wantsStandaloneMode(app_context);
}

void setWifiTransportEnabled(bool enabled)
{
    PlainMqttRuntime* instance = runtime();
    if (instance)
    {
        instance->setWifiTransportEnabled(enabled);
    }
}

void update(app::IAppFacade& app_context)
{
    PlainMqttRuntime* instance = runtime();
    if (instance)
    {
        instance->update(app_context);
    }
}

} // namespace platform::esp::arduino_common::mesh_mqtt
