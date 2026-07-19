/**
 * @file mt_adapter.h
 * @brief Meshtastic mesh adapter
 */

#pragma once

#include "board/LoraBoard.h"
#include "chat/domain/chat_types.h"
#include "chat/infra/mesh_incoming_queue.h"
#include "chat/infra/meshtastic/mt_codec_pb.h" // Use protobuf-based codec
#include "chat/infra/meshtastic/mt_dedup.h"
#include "chat/infra/meshtastic/mt_mqtt_proxy_runtime.h"
#include "chat/infra/meshtastic/mt_packet_wire.h" // Wire packet format
#include "chat/infra/meshtastic/mt_pending_wire_table.h"
#include "chat/ports/i_mesh_adapter.h"
#include "chat/ports/i_mesh_peer_directory.h"
#include "chat/runtime/meshtastic_runtime.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "mesh/domain/peer_identity.h"
#include "meshtastic/mqtt.pb.h"
#include "platform/esp/arduino_common/mesh/esp_meshtastic_adapter_bridge.h"
#include "sys/ringbuf.h"
#include <array>
#include <cstddef>
#include <memory>
#include <string>

namespace chat
{
namespace meshtastic
{

/**
 * @brief Meshtastic mesh adapter
 * Implements IMeshAdapter using Meshtastic protocol over LoRa
 */
class MtAdapter : public chat::IMeshAdapter
{
  public:
    using MqttProxySettings = ::chat::meshtastic::MqttProxyRuntimeSettings;

    MtAdapter(LoraBoard& board, IMeshPeerDirectory* peer_directory = nullptr);
    virtual ~MtAdapter();

    static void* operator new(std::size_t size);
    static void operator delete(void* ptr) noexcept;
    static void operator delete(void* ptr, std::size_t size) noexcept;

    MeshCapabilities getCapabilities() const override;

    bool sendText(ChannelId channel, const std::string& text,
                  MessageId* out_msg_id, NodeId peer = 0) override;
    bool sendTextWithId(ChannelId channel, const std::string& text,
                        MessageId forced_msg_id,
                        MessageId* out_msg_id, NodeId peer = 0) override;
    bool pollIncomingText(MeshIncomingText* out) override;
    bool sendAppData(ChannelId channel, uint32_t portnum,
                     const uint8_t* payload, size_t len,
                     NodeId dest = 0, bool want_ack = false,
                     MessageId packet_id = 0,
                     bool want_response = false) override;
    bool pollIncomingData(MeshIncomingData* out) override;
    bool requestNodeInfo(NodeId dest, bool want_response) override;
    bool startKeyVerification(NodeId node_id) override;
    bool submitKeyVerificationNumber(NodeId node_id, uint64_t nonce, uint32_t number) override;
    bool isPkiReady() const override;
    bool hasPkiKey(NodeId dest) const override;
    bool getNodePublicKey(NodeId node_id, uint8_t out_key[32]) const;
    bool getOwnPublicKey(uint8_t out_key[32]) const;
    void rememberNodePublicKey(NodeId node_id, const uint8_t* key, size_t key_len);
    void forgetNodePublicKey(NodeId node_id);
    meshtastic_Routing_Error getLastRoutingError() const;
    void setMqttProxySettings(const MqttProxySettings& settings);
    bool pollMqttProxyMessage(meshtastic_MqttClientProxyMessage* out);
    bool hasMqttProxyMessage() const;
    bool handleMqttProxyMessage(const meshtastic_MqttClientProxyMessage& msg);
    void applyConfig(const MeshConfig& config) override;
    void setUserInfo(const char* long_name, const char* short_name) override;
    void setNetworkLimits(bool duty_cycle_enabled, uint8_t util_percent) override;
    void setPrivacyConfig(uint8_t encrypt_mode) override;
    void setLastRxStats(float rssi, float snr) override;
    bool isReady() const override;
    NodeId getNodeId() const override { return node_id_; }

    /**
     * @brief Poll for incoming raw packet data
     * @param out_data Output buffer for raw packet data
     * @param out_len Output packet length
     * @param max_len Maximum buffer size
     * @return true if raw packet data is available
     */
    bool pollIncomingRawPacket(uint8_t* out_data, size_t& out_len, size_t max_len) override;

    /**
     * @brief Handle raw packet data (from radio task)
     * @param data Raw packet data
     * @param size Packet size
     */
    void handleRawPacket(const uint8_t* data, size_t size) override;

    /**
     * @brief Process received packets (call from radio task)
     */
    void processReceivedPacket(const uint8_t* data, size_t size);

    /**
     * @brief Process send queue (call periodically)
     */
    void processSendQueue() override;

  private:
    LoraBoard& board_;
    IMeshPeerDirectory* peer_directory_;
    MeshConfig config_;
    MtDedup dedup_;
    MessageId next_packet_id_;
    bool ready_;
    NodeId node_id_;
    uint8_t mac_addr_[6];
    uint32_t last_nodeinfo_ms_;
    uint8_t primary_channel_hash_;
    uint8_t primary_psk_[chat::kMeshtasticChannelKeyMaxLen];
    size_t primary_psk_len_;
    uint8_t secondary_channel_hash_;
    uint8_t secondary_psk_[chat::kMeshtasticChannelKeyMaxLen];
    size_t secondary_psk_len_;
    bool pki_ready_;
    std::array<uint8_t, 32> pki_public_key_;
    std::array<uint8_t, 32> pki_private_key_;
    std::string user_long_name_;
    std::string user_short_name_;
    float last_rx_rssi_;
    float last_rx_snr_;
    uint32_t radio_freq_hz_ = 0;
    uint32_t radio_bw_hz_ = 0;
    uint8_t radio_sf_ = 0;
    uint8_t radio_cr_ = 0;

    static constexpr std::size_t kPendingAckSlotCount = 8;
    static constexpr std::size_t kPendingAckWireMaxLen = 512;

    struct PendingAckState
    {
        uint32_t dest = 0;
        ChannelId channel = ChannelId::PRIMARY;
        uint8_t channel_hash = 0;
        uint32_t last_attempt_ms = 0;
        uint8_t retransmit_count = 0;
    };

    using PendingAckTable =
        PendingWireTable<PendingAckState, kPendingAckSlotCount, kPendingAckWireMaxLen>;
    using PendingAckSlot = PendingAckTable::Slot;

    enum class KeyVerificationState : uint8_t
    {
        Idle,
        SenderInitiated,
        SenderAwaitingNumber,
        SenderAwaitingUser,
        ReceiverAwaitingHash1,
        ReceiverAwaitingUser
    };

    KeyVerificationState kv_state_;
    uint64_t kv_nonce_;
    uint32_t kv_nonce_ms_;
    uint32_t kv_security_number_;
    uint32_t kv_remote_node_;
    std::array<uint8_t, 32> kv_hash1_;
    std::array<uint8_t, 32> kv_hash2_;

    // Raw packet data storage for protocol detection
    uint8_t last_raw_packet_[256];
    size_t last_raw_packet_len_;
    bool has_pending_raw_packet_;

    struct PendingSend
    {
        ChannelId channel;
        uint32_t portnum;
        std::array<char, ::chat::infra::kIncomingTextMaxLen + 1> text{};
        size_t text_len = 0;
        MessageId msg_id;
        NodeId dest;
        uint32_t retry_count;
        uint32_t last_attempt;
    };

    enum class PendingProtocolActionType : uint8_t
    {
        None,
        SendNodeInfo,
        SendRoutingAck,
        SendRoutingError,
        SendPacket
    };

    struct PendingProtocolAction
    {
        PendingProtocolActionType type = PendingProtocolActionType::None;
        NodeId peer = 0;
        MessageId request_id = 0;
        ChannelId channel = ChannelId::PRIMARY;
        uint8_t channel_hash = 0;
        bool want_response = false;
        meshtastic_Routing_Error routing_error = meshtastic_Routing_Error_NONE;
        runtime::SendPacketEffect packet{};
        bool mark_nodeinfo_reply = false;
        uint32_t nodeinfo_reply_ms = 0;
        uint8_t retry_count = 0;
        uint32_t last_attempt = 0;
    };

    static constexpr std::size_t kIncomingQueueDepth = 12;
    static constexpr std::size_t kPendingSendQueueDepth = 8;
    static constexpr uint8_t kSendQueueDrainPerTick = 1;
    static constexpr uint8_t kLoRaAirTxBudgetPerTick = 1;

    sys::RingBuffer<PendingSend, kPendingSendQueueDepth> send_queue_;
    ::chat::infra::IncomingTextQueue<kIncomingQueueDepth> receive_queue_;
    ::chat::infra::IncomingDataQueue<kIncomingQueueDepth> app_receive_queue_;
    static constexpr std::size_t kMqttProxyQueueDepth = 12;
    sys::RingBuffer<meshtastic_MqttClientProxyMessage, kMqttProxyQueueDepth> mqtt_proxy_queue_;
    MqttProxySettings mqtt_proxy_settings_;

    static constexpr std::size_t kMqttDownlinkWireMaxLen = 255;
    static constexpr std::size_t kPendingMqttDownlinkTxDepth = 8;
    static constexpr std::size_t kMqttDownlinkSeenDepth = 24;
    static constexpr uint8_t kMqttDownlinkTxMaxRetries = 2;
    static constexpr uint32_t kMqttDownlinkSeenTtlMs = 300000;

    struct PendingMqttDownlinkTx
    {
        std::array<uint8_t, kMqttDownlinkWireMaxLen> wire{};
        size_t wire_size = 0;
        NodeId from = 0;
        NodeId to = 0;
        MessageId msg_id = 0;
        uint8_t channel_hash = 0;
        uint8_t retry_count = 0;
        uint32_t first_seen_ms = 0;
        uint32_t last_attempt_ms = 0;
    };

    struct MqttDownlinkSeenEntry
    {
        bool used = false;
        NodeId from = 0;
        MessageId msg_id = 0;
        uint8_t channel_hash = 0;
        uint32_t seen_ms = 0;
    };

    sys::RingBuffer<PendingMqttDownlinkTx, kPendingMqttDownlinkTxDepth>
        mqtt_downlink_tx_queue_;
    std::array<MqttDownlinkSeenEntry, kMqttDownlinkSeenDepth> mqtt_downlink_seen_{};
    size_t mqtt_downlink_seen_next_ = 0;

    struct MqttDownlinkScratchBuffers
    {
        meshtastic_MeshPacket packet = meshtastic_MeshPacket_init_zero;
        char channel_id[32] = {};
        char gateway_id[16] = {};
    };

    struct MqttPublishScratchBuffers
    {
        std::array<uint8_t, 256> payload{};
        meshtastic_MeshPacket packet = meshtastic_MeshPacket_init_zero;
        meshtastic_MqttClientProxyMessage proxy = meshtastic_MqttClientProxyMessage_init_zero;
        meshtastic_ServiceEnvelope envelope = meshtastic_ServiceEnvelope_init_zero;
    };

    struct TxScratchBuffers
    {
        std::array<uint8_t, 256> data{};
        std::array<uint8_t, 256> pki{};
        std::array<uint8_t, 512> wire{};
        meshtastic_Data decoded = meshtastic_Data_init_default;
    };

    struct RxScratchBuffers
    {
        std::array<uint8_t, 256> payload{};
        std::array<uint8_t, 256> plaintext{};
        std::array<uint8_t, 256> candidate_plaintext{};
        meshtastic_Data decoded = meshtastic_Data_init_default;
        meshtastic_Data candidate_decoded = meshtastic_Data_init_default;
    };

    MqttDownlinkScratchBuffers mqtt_downlink_scratch_;
    MqttPublishScratchBuffers mqtt_publish_scratch_;
    TxScratchBuffers tx_scratch_;
    RxScratchBuffers rx_scratch_;
    meshtastic_MeshPacket protocol_effect_packet_scratch_ = meshtastic_MeshPacket_init_zero;
    PendingAckTable pending_ack_states_;
    std::unique_ptr<::platform::esp::arduino_common::mesh::EspMeshtasticAdapterBridge> core_bridge_;

    static constexpr size_t MAX_PACKET_SIZE = 255;
    static constexpr uint32_t RETRY_DELAY_MS = 1000;
    static constexpr uint8_t MAX_RETRIES = 3;
    static constexpr uint32_t NODEINFO_INTERVAL_MS = 3 * 60 * 60 * 1000;
    static constexpr uint32_t PKI_BACKOFF_MS = 5 * 60 * 1000;
    static constexpr uint32_t ACK_TIMEOUT_MS = 15000;
    static constexpr uint8_t MAX_ACK_RETRIES = 3;
    static constexpr size_t kPkiNodeTableDepth = 16;
    static constexpr size_t kNodeRuntimeTableDepth = 64;
    static constexpr size_t kProtocolActionQueueSize = 8;
    struct PkiNodeKeyEntry
    {
        bool used = false;
        NodeId node_id = 0;
        std::array<uint8_t, 32> key{};
        uint32_t last_seen_s = 0;
    };

    struct NodeRuntimeEntry
    {
        bool used = false;
        NodeId node_id = 0;
        ChannelId last_channel = ChannelId::PRIMARY;
        bool has_last_channel = false;
        bool last_seen_via_mqtt = false;
        uint32_t nodeinfo_reply_ms = 0;
        uint32_t last_touch_ms = 0;
    };

    std::array<PkiNodeKeyEntry, kPkiNodeTableDepth> pki_node_keys_{};
    std::array<MeshPeerRecord, kPkiNodeTableDepth> pki_directory_load_entries_{};
    std::array<NodeRuntimeEntry, kNodeRuntimeTableDepth> node_runtime_{};

    uint32_t min_tx_interval_ms_ = 0;
    uint32_t last_tx_ms_ = 0;
    uint8_t encrypt_mode_ = 1;
    meshtastic_Routing_Error last_send_error_ = meshtastic_Routing_Error_NONE;
    runtime::MeshtasticRuntime protocol_runtime_{};
    runtime::ProtocolEffectWorkspace protocol_effect_workspace_{};
    std::array<PendingProtocolAction, kProtocolActionQueueSize> protocol_action_queue_{};
    size_t protocol_action_head_ = 0;
    size_t protocol_action_count_ = 0;

    bool sendPacket(const PendingSend& pending);
    bool sendAppDataNow(ChannelId channel,
                        uint32_t portnum,
                        const uint8_t* payload,
                        size_t len,
                        NodeId dest,
                        bool want_ack,
                        MessageId packet_id,
                        bool want_response);
    bool sendMeshPacket(const meshtastic_MeshPacket& packet);
    bool sendNodeInfoTo(uint32_t dest, bool want_response,
                        ChannelId channel = ChannelId::PRIMARY);
    void maybeBroadcastNodeInfo(uint32_t now_ms);
    void maybeBroadcastNodeInfoAfterPeerAnnouncement(uint32_t from_node,
                                                     uint32_t now_ms,
                                                     ChannelId channel,
                                                     bool from_mqtt);
    void configureRadio();
    void initNodeIdentity();
    void updateChannelKeys();
    bool transmitWirePacket(const uint8_t* wire_data, size_t wire_size);
    bool enqueueProtocolAction(const PendingProtocolAction& action);
    bool enqueueNodeInfoAction(NodeId peer, bool want_response, ChannelId channel,
                               bool mark_reply = false, uint32_t reply_ms = 0);
    bool enqueueRoutingAckAction(NodeId peer, MessageId request_id, uint8_t channel_hash);
    bool enqueueRoutingErrorAction(NodeId peer, MessageId request_id, ChannelId channel,
                                   meshtastic_Routing_Error reason);
    bool enqueueSendPacketAction(const runtime::SendPacketEffect& packet);
    bool popProtocolAction();
    bool processProtocolActionQueue(uint32_t now_ms,
                                    uint8_t& tx_budget_remaining);
    bool executeProtocolAction(const PendingProtocolAction& action);
    bool resolvePskForChannelHash(uint8_t channel_hash,
                                  const uint8_t** out_psk,
                                  size_t* out_psk_len) const;
    bool sendChannelAppDataViaCore(uint32_t portnum,
                                   const uint8_t* payload,
                                   size_t len,
                                   uint32_t dest_node,
                                   bool effective_want_response,
                                   MessageId msg_id,
                                   uint8_t channel_hash,
                                   const uint8_t* psk,
                                   size_t psk_len,
                                   uint8_t hop_limit,
                                   bool air_want_ack,
                                   uint8_t* out_wire_data,
                                   size_t* inout_wire_size);
    void startRadioReceive();
    void trackPendingAck(uint32_t msg_id, uint32_t dest, ChannelId channel, uint8_t channel_hash,
                         const uint8_t* wire_data, size_t wire_size);
    void clearPendingAck(uint32_t msg_id);
    bool retryPendingAck(uint32_t msg_id,
                         PendingAckSlot& slot,
                         uint32_t now_ms,
                         uint8_t& tx_budget_remaining);
    bool initPkiKeys();
    void loadPkiNodeKeys();
    void savePkiNodeKey(uint32_t node_id, const uint8_t* key, size_t key_len);
    bool decryptPkiPayload(uint32_t from, uint32_t packet_id,
                           const uint8_t* cipher, size_t cipher_len,
                           uint8_t* out_plain, size_t* out_plain_len);
    bool encryptPkiPayload(uint32_t dest, uint32_t packet_id,
                           const uint8_t* plain, size_t plain_len,
                           uint8_t* out_cipher, size_t* out_cipher_len);
    void savePkiNodeKeyToDirectory(uint32_t node_id,
                                   const uint8_t* key,
                                   uint32_t last_seen_s);
    void touchPkiNodeKey(uint32_t node_id);
    PkiNodeKeyEntry* findPkiNodeKey(uint32_t node_id);
    const PkiNodeKeyEntry* findPkiNodeKey(uint32_t node_id) const;
    PkiNodeKeyEntry* upsertPkiNodeKey(uint32_t node_id, const uint8_t* key, uint32_t last_seen_s,
                                      bool* out_changed = nullptr, bool* out_evicted = nullptr);
    void clearPkiNodeKeys();
    bool erasePkiNodeKey(uint32_t node_id);
    size_t pkiNodeKeyCount() const;
    NodeRuntimeEntry* findNodeRuntime(uint32_t node_id);
    const NodeRuntimeEntry* findNodeRuntime(uint32_t node_id) const;
    NodeRuntimeEntry* upsertNodeRuntime(uint32_t node_id, uint32_t now_ms);
    void eraseNodeRuntime(uint32_t node_id);
    bool getNodeLastChannel(uint32_t node_id, ChannelId* out) const;
    void rememberNodeLastChannel(uint32_t node_id, ChannelId channel, uint32_t now_ms);
    void rememberNodeRuntimeRx(uint32_t node_id,
                               ChannelId channel,
                               bool via_mqtt,
                               uint32_t now_ms);
    bool nodeLastSeenViaMqtt(uint32_t node_id) const;
    uint32_t getNodeInfoReplyMs(uint32_t node_id) const;
    void setNodeInfoReplyMs(uint32_t node_id, uint32_t now_ms);
    bool sendRoutingAck(uint32_t dest, uint32_t request_id, uint8_t channel_hash,
                        const uint8_t* psk, size_t psk_len);
    bool sendRoutingError(uint32_t dest, uint32_t request_id, uint8_t channel_hash,
                          const uint8_t* psk, size_t psk_len,
                          meshtastic_Routing_Error reason);
    runtime::RuntimeContext buildProtocolRuntimeContext() const;
    bool sendProtocolPacketEffect(const runtime::SendPacketEffect& packet);
    bool executeProtocolEffects(const runtime::ProtocolEffects& effects);
    bool executeProtocolEffect(const runtime::ProtocolEffect& effect);
    bool executePkiResync(runtime::MeshtasticPkiResyncCause cause,
                          NodeId peer,
                          MessageId request_id,
                          ChannelId channel);
    void emitRoutingResultToPhone(uint32_t request_id,
                                  meshtastic_Routing_Error reason,
                                  uint32_t from,
                                  uint32_t to,
                                  ChannelId channel,
                                  uint8_t channel_hash,
                                  const chat::RxMeta* rx_meta);

    void updateKeyVerificationState();
    void resetKeyVerificationState();
    bool handleKeyVerificationInit(const PacketHeaderWire& header,
                                   const meshtastic_KeyVerification& kv);
    bool handleKeyVerificationReply(const PacketHeaderWire& header,
                                    const meshtastic_KeyVerification& kv);
    bool handleKeyVerificationFinal(const PacketHeaderWire& header,
                                    const meshtastic_KeyVerification& kv);
    bool sendKeyVerificationPacket(uint32_t dest, const meshtastic_KeyVerification& kv,
                                   bool want_response);
    bool processKeyVerificationNumber(uint32_t remote_node, uint64_t nonce, uint32_t number);
    void buildVerificationCode(char* out, size_t out_len) const;
    bool decodeMqttServiceEnvelope(const uint8_t* payload, size_t payload_len,
                                   meshtastic_MeshPacket* out_packet,
                                   char* out_channel_id, size_t channel_id_len,
                                   char* out_gateway_id, size_t gateway_id_len) const;
    bool injectMqttEnvelope(const meshtastic_MeshPacket& packet,
                            const char* channel_id,
                            const char* gateway_id);
    bool enqueueMqttDownlinkTx(const uint8_t* wire_data,
                               size_t wire_size,
                               const PacketHeaderWire& header);
    bool isMqttDownlinkRecentlySeen(NodeId from,
                                    MessageId msg_id,
                                    uint8_t channel_hash,
                                    uint32_t now_ms);
    void rememberMqttDownlinkSeen(NodeId from,
                                  MessageId msg_id,
                                  uint8_t channel_hash,
                                  uint32_t now_ms);
    bool processMqttDownlinkTxQueue(uint32_t now_ms,
                                    uint8_t& tx_budget_remaining);
    bool queueMqttProxyPublish(const meshtastic_MeshPacket& packet,
                               const char* channel_id);
    bool queueMqttProxyPublishFromWire(const uint8_t* wire_data,
                                       size_t wire_size,
                                       const meshtastic_Data* decoded,
                                       ChannelId channel_index);
    bool shouldPublishToMqtt(ChannelId channel, bool from_mqtt, bool is_pki) const;
    bool hasAnyMqttDownlinkEnabled() const;
    const char* mqttChannelIdFor(ChannelId channel) const;
    uint8_t mqttChannelHashForId(const char* channel_id, bool* out_known = nullptr,
                                 ChannelId* out_channel = nullptr) const;
    std::string mqttNodeIdString() const;
};

} // namespace meshtastic
} // namespace chat
