#pragma once

#include "chat/infra/mesh_incoming_queue.h"
#include "chat/infra/meshtastic/mt_mqtt_proxy_runtime.h"
#include "chat/infra/meshtastic/mt_packet_wire.h"
#include "chat/infra/meshtastic/mt_pending_wire_table.h"
#include "chat/ports/i_mesh_adapter.h"
#include "chat/runtime/meshtastic_runtime.h"
#include "chat/runtime/self_identity_policy.h"
#include "chat/runtime/self_identity_provider.h"
#include "chat/usecase/contact_service.h"
#include "mesh/domain/peer_identity.h"
#include "meshtastic/mesh.pb.h"
#include "meshtastic/mqtt.pb.h"
#include "platform/nrf52/arduino_common/chat/infra/meshtastic/node_store.h"
#include "sys/ringbuf.h"

#include <array>
#include <cstdint>
#include <limits>
#include <string>

namespace platform::nrf52::arduino_common::chat::meshtastic
{

class MeshtasticRadioAdapter final : public ::chat::IMeshAdapter
{
  public:
    using MqttProxySettings = ::chat::meshtastic::MqttProxyRuntimeSettings;

    explicit MeshtasticRadioAdapter(const ::chat::runtime::SelfIdentityProvider* identity_provider = nullptr,
                                    NodeStore* node_store = nullptr,
                                    ::chat::contacts::ContactService* contact_service = nullptr);

    ::chat::MeshCapabilities getCapabilities() const override;
    bool sendText(::chat::ChannelId channel, const std::string& text,
                  ::chat::MessageId* out_msg_id, ::chat::NodeId peer = 0) override;
    bool sendTextWithId(::chat::ChannelId channel, const std::string& text,
                        ::chat::MessageId forced_msg_id,
                        ::chat::MessageId* out_msg_id, ::chat::NodeId peer = 0) override;
    bool pollIncomingText(::chat::MeshIncomingText* out) override;
    bool sendAppData(::chat::ChannelId channel, uint32_t portnum,
                     const uint8_t* payload, size_t len,
                     ::chat::NodeId dest = 0, bool want_ack = false,
                     ::chat::MessageId packet_id = 0,
                     bool want_response = false) override;
    bool pollIncomingData(::chat::MeshIncomingData* out) override;
    bool requestNodeInfo(::chat::NodeId dest, bool want_response) override;
    bool startKeyVerification(::chat::NodeId node_id) override;
    bool submitKeyVerificationNumber(::chat::NodeId node_id, uint64_t nonce, uint32_t number) override;
    bool isPkiReady() const override;
    bool hasPkiKey(::chat::NodeId dest) const override;
    void applyConfig(const ::chat::MeshConfig& config) override;
    void setUserInfo(const char* long_name, const char* short_name) override;
    void setNetworkLimits(bool duty_cycle_enabled, uint8_t util_percent) override;
    void setPrivacyConfig(uint8_t encrypt_mode) override;
    bool isReady() const override;
    ::chat::NodeId getNodeId() const override;
    void setMqttProxySettings(const MqttProxySettings& settings);
    bool pollMqttProxyMessage(meshtastic_MqttClientProxyMessage* out);
    bool hasMqttProxyMessage() const;
    bool handleMqttProxyMessage(const meshtastic_MqttClientProxyMessage& msg);
    bool pollIncomingRawPacket(uint8_t* out_data, size_t& out_len, size_t max_len) override;
    void handleRawPacket(const uint8_t* data, size_t size) override;
    void setLastRxStats(float rssi, float snr) override;
    void processSendQueue() override;
    void flushDeferredPersistence(bool force = false);

  private:
    struct PacketHistoryEntry
    {
        ::chat::NodeId sender = 0;
        ::chat::MessageId packet_id = 0;
        uint8_t next_hop = 0;
        std::array<uint8_t, 3> relayed_by{};
        uint8_t highest_hop_limit = 0;
        uint8_t our_tx_hop_limit = 0;
        uint32_t last_rx_ms = 0;
    };

    struct HistoryResult
    {
        bool seen_recently = false;
        bool was_fallback = false;
        bool we_were_next_hop = false;
        bool was_upgraded = false;
    };

    static constexpr std::size_t kPacketHistoryCapacity = 160;

    class PacketHistoryTable
    {
      public:
        PacketHistoryEntry* find(::chat::NodeId sender, ::chat::MessageId packet_id)
        {
            for (std::size_t index = 0; index < count_; ++index)
            {
                if (items_[index].sender == sender &&
                    items_[index].packet_id == packet_id)
                {
                    return &items_[index];
                }
            }
            return nullptr;
        }

        const PacketHistoryEntry* find(::chat::NodeId sender,
                                       ::chat::MessageId packet_id) const
        {
            for (std::size_t index = 0; index < count_; ++index)
            {
                if (items_[index].sender == sender &&
                    items_[index].packet_id == packet_id)
                {
                    return &items_[index];
                }
            }
            return nullptr;
        }

        void pruneExpired(uint32_t now_ms, uint32_t ttl_ms)
        {
            for (std::size_t index = 0; index < count_;)
            {
                const PacketHistoryEntry& entry = items_[index];
                if (entry.last_rx_ms != 0 && (now_ms - entry.last_rx_ms) > ttl_ms)
                {
                    removeAt(index);
                    continue;
                }
                ++index;
            }
        }

        PacketHistoryEntry* allocateDropOldest()
        {
            if (count_ >= kPacketHistoryCapacity)
            {
                removeOldestByLastRx();
            }
            PacketHistoryEntry& entry = items_[count_++];
            entry = PacketHistoryEntry{};
            return &entry;
        }

        PacketHistoryEntry* allocateDropFirst()
        {
            if (count_ >= kPacketHistoryCapacity)
            {
                removeAt(0);
            }
            PacketHistoryEntry& entry = items_[count_++];
            entry = PacketHistoryEntry{};
            return &entry;
        }

      private:
        void removeOldestByLastRx()
        {
            if (count_ == 0)
            {
                return;
            }
            std::size_t oldest = 0;
            for (std::size_t index = 1; index < count_; ++index)
            {
                if (items_[index].last_rx_ms < items_[oldest].last_rx_ms)
                {
                    oldest = index;
                }
            }
            removeAt(oldest);
        }

        void removeAt(std::size_t index)
        {
            if (index >= count_)
            {
                return;
            }
            for (std::size_t move = index + 1; move < count_; ++move)
            {
                items_[move - 1] = items_[move];
            }
            --count_;
            items_[count_] = PacketHistoryEntry{};
        }

        std::array<PacketHistoryEntry, kPacketHistoryCapacity> items_{};
        std::size_t count_ = 0;
    };

    static constexpr std::size_t kPendingRetransmitSlotCount = 8;
    static constexpr std::size_t kPendingRetransmitWireMaxLen = 384;

    struct PendingRetransmitMeta
    {
        ::chat::NodeId original_from = 0;
        ::chat::NodeId dest = 0;
        ::chat::MessageId packet_id = 0;
        ::chat::ChannelId channel = ::chat::ChannelId::PRIMARY;
        uint8_t channel_hash = 0;
        uint8_t retries_left = 0;
        uint32_t next_tx_ms = 0;
        bool want_ack = false;
        bool local_origin = false;
        bool fallback_sent = false;
        bool observe_only = false;
    };

    using PendingRetransmitTable =
        ::chat::meshtastic::PendingWireTable<PendingRetransmitMeta,
                                             kPendingRetransmitSlotCount,
                                             kPendingRetransmitWireMaxLen>;
    using PendingRetransmitSlot = PendingRetransmitTable::Slot;

    struct TxScratchBuffers
    {
        std::array<uint8_t, 256> app_data{};
        std::array<uint8_t, 256> aux_data{};
        std::array<uint8_t, 384> wire{};
        std::array<char, 771> wire_hex{};
        meshtastic_Data decoded = meshtastic_Data_init_default;
    };

    struct RxScratchBuffers
    {
        ::chat::meshtastic::PacketHeaderWire header{};
        std::array<uint8_t, 256> payload{};
        std::array<uint8_t, 256> plain{};
        meshtastic_Data decoded = meshtastic_Data_init_zero;
    };

    struct MqttDownlinkScratchBuffers
    {
        std::array<uint8_t, 256> buffer{};
        std::array<uint8_t, sizeof(::chat::meshtastic::PacketHeaderWire) + 256> wire{};
        char channel_id[32] = {};
        char gateway_id[16] = {};
        meshtastic_Data decoded = meshtastic_Data_init_zero;
        meshtastic_MeshPacket packet = meshtastic_MeshPacket_init_zero;
    };

    struct PendingMqttDownlink
    {
        meshtastic_MeshPacket packet = meshtastic_MeshPacket_init_zero;
        char channel_id[32] = {};
        char gateway_id[16] = {};
    };

    struct MqttPublishScratchBuffers
    {
        std::array<uint8_t, 256> buffer{};
        meshtastic_MeshPacket packet = meshtastic_MeshPacket_init_zero;
        meshtastic_MqttClientProxyMessage proxy = meshtastic_MqttClientProxyMessage_init_zero;
        meshtastic_ServiceEnvelope envelope = meshtastic_ServiceEnvelope_init_zero;
    };

    ::chat::runtime::EffectiveSelfIdentity buildEffectiveIdentity() const;
    bool transmitWire(const uint8_t* data, size_t size);
    bool transmitPreparedWire(uint8_t* data, size_t size, ::chat::ChannelId channel,
                              const meshtastic_Data* decoded, bool track_retransmit,
                              bool local_origin, uint8_t retries_override = 0,
                              bool observe_broadcast_ack = false);
    bool buildAndQueueNodeInfo(::chat::NodeId dest, bool want_response,
                               ::chat::ChannelId channel = ::chat::ChannelId::PRIMARY);
    bool buildAndQueueRoutingPacket(::chat::NodeId dest, uint32_t request_id,
                                    uint8_t channel_hash, ::chat::ChannelId channel,
                                    meshtastic_Routing_Error reason,
                                    const uint8_t* key, size_t key_len,
                                    uint8_t hop_limit);
    bool sendRoutingAck(::chat::NodeId dest, uint32_t request_id, uint8_t channel_hash,
                        ::chat::ChannelId channel, const uint8_t* key, size_t key_len,
                        uint8_t hop_limit);
    bool sendRoutingError(::chat::NodeId dest, uint32_t request_id, uint8_t channel_hash,
                          ::chat::ChannelId channel, const uint8_t* key, size_t key_len,
                          meshtastic_Routing_Error reason, uint8_t hop_limit);
    ::chat::runtime::RuntimeContext buildProtocolRuntimeContext() const;
    bool sendProtocolPacketEffect(const ::chat::runtime::SendPacketEffect& packet);
    bool executeProtocolEffects(const ::chat::runtime::ProtocolEffects& effects);
    bool executeProtocolEffect(const ::chat::runtime::ProtocolEffect& effect);
    bool executePkiResync(::chat::runtime::MeshtasticPkiResyncCause cause,
                          ::chat::NodeId peer,
                          ::chat::MessageId request_id,
                          ::chat::ChannelId channel);
    void maybeBroadcastNodeInfo(uint32_t now_ms);
    void maybeBroadcastNodeInfoAfterPeerAnnouncement(::chat::NodeId from_node,
                                                     uint32_t now_ms,
                                                     ::chat::ChannelId channel,
                                                     bool from_mqtt);
    void emitRoutingResult(uint32_t request_id, meshtastic_Routing_Error reason,
                           ::chat::NodeId from, ::chat::NodeId to,
                           ::chat::ChannelId channel, uint8_t channel_hash,
                           const ::chat::RxMeta* rx_meta);
    uint8_t ourRelayId() const;
    uint8_t getLearnedNextHop(::chat::NodeId dest, uint8_t relay_node) const;
    void learnNextHop(::chat::NodeId dest, uint8_t next_hop);
    PacketHistoryEntry* findHistory(::chat::NodeId sender, ::chat::MessageId packet_id);
    const PacketHistoryEntry* findHistory(::chat::NodeId sender, ::chat::MessageId packet_id) const;
    static bool hasRelayer(const PacketHistoryEntry& entry, uint8_t relayer, bool* sole = nullptr);
    HistoryResult updatePacketHistory(const ::chat::meshtastic::PacketHeaderWire& header, bool allow_update);
    void rememberLocalPacket(const ::chat::meshtastic::PacketHeaderWire& header);
    bool maybeRebroadcast(const ::chat::meshtastic::PacketHeaderWire& header,
                          const uint8_t* payload, size_t payload_size,
                          ::chat::ChannelId channel, const meshtastic_Data* decoded);
    void updateNodeLastSeen(::chat::NodeId node_id, uint8_t hops_away, ::chat::ChannelId channel);
    void handleRoutingPacket(const ::chat::meshtastic::PacketHeaderWire& header,
                             const meshtastic_Data& decoded,
                             ::chat::ChannelId channel,
                             const uint8_t* key, size_t key_len,
                             const ::chat::RxMeta& rx_meta);
    void queuePendingRetransmit(const ::chat::meshtastic::PacketHeaderWire& header,
                                const uint8_t* wire, size_t wire_size,
                                ::chat::ChannelId channel,
                                bool local_origin, uint8_t retries_override,
                                bool observe_only = false);
    bool stopPendingRetransmit(::chat::NodeId from, ::chat::MessageId packet_id);
    void maybeHandleObservedRelay(const ::chat::meshtastic::PacketHeaderWire& header);
    static uint64_t pendingKey(::chat::NodeId from, ::chat::MessageId packet_id);
    bool initPkiKeys();
    void loadPkiNodeKeys();
    void savePkiNodeKey(::chat::NodeId node_id, const uint8_t* key, size_t key_len);
    void markPkiKeysDirty();
    void forgetNodePublicKey(::chat::NodeId node_id);
    bool savePkiKeysToPrefs();
    void touchPkiNodeKey(::chat::NodeId node_id);
    bool decryptPkiPayload(::chat::NodeId from, ::chat::MessageId packet_id,
                           const uint8_t* cipher, size_t cipher_len,
                           uint8_t* out_plain, size_t* out_plain_len);
    bool encryptPkiPayload(::chat::NodeId dest, ::chat::MessageId packet_id,
                           const uint8_t* plain, size_t plain_len,
                           uint8_t* out_cipher, size_t* out_cipher_len);
    void updateKeyVerificationState();
    void resetKeyVerificationState();
    void buildVerificationCode(char* out, size_t out_len) const;
    bool handleKeyVerificationInit(const ::chat::meshtastic::PacketHeaderWire& header,
                                   const meshtastic_KeyVerification& kv);
    bool handleKeyVerificationReply(const ::chat::meshtastic::PacketHeaderWire& header,
                                    const meshtastic_KeyVerification& kv);
    bool handleKeyVerificationFinal(const ::chat::meshtastic::PacketHeaderWire& header,
                                    const meshtastic_KeyVerification& kv);
    bool sendKeyVerificationPacket(::chat::NodeId dest, const meshtastic_KeyVerification& kv,
                                   bool want_response);
    bool processKeyVerificationNumber(::chat::NodeId remote_node, uint64_t nonce, uint32_t number);
    std::string mqttNodeIdString() const;
    const char* mqttChannelIdFor(::chat::ChannelId channel) const;
    bool hasAnyMqttDownlinkEnabled() const;
    bool shouldPublishToMqtt(::chat::ChannelId channel, bool from_mqtt, bool is_pki) const;
    uint8_t mqttChannelHashForId(const char* channel_id, bool* out_known = nullptr,
                                 ::chat::ChannelId* out_channel = nullptr) const;
    bool decodeMqttServiceEnvelope(const uint8_t* payload, size_t payload_len,
                                   meshtastic_MeshPacket* out_packet,
                                   char* out_channel_id, size_t channel_id_len,
                                   char* out_gateway_id, size_t gateway_id_len) const;
    bool enqueueMqttEnvelope(const meshtastic_MeshPacket& packet,
                             const char* channel_id,
                             const char* gateway_id);
    void processPendingMqttDownlinks();
    bool injectMqttEnvelope(const meshtastic_MeshPacket& packet,
                            const char* channel_id,
                            const char* gateway_id);
    bool queueMqttProxyPublish(const meshtastic_MeshPacket& packet,
                               const char* channel_id);
    bool queueMqttProxyPublishFromWire(const uint8_t* wire_data,
                                       size_t wire_size,
                                       const meshtastic_Data* decoded,
                                       ::chat::ChannelId channel_index);

    ::chat::MeshConfig config_{};
    ::chat::NodeId node_id_ = 0;
    ::chat::MessageId next_packet_id_ = 1;
    std::string long_name_;
    std::string short_name_;
    const ::chat::runtime::SelfIdentityProvider* identity_provider_ = nullptr;
    NodeStore* node_store_ = nullptr;
    ::chat::contacts::ContactService* contact_service_ = nullptr;
    float last_rx_rssi_ = std::numeric_limits<float>::quiet_NaN();
    float last_rx_snr_ = std::numeric_limits<float>::quiet_NaN();
    static constexpr std::size_t kIncomingQueueDepth = 12;
    static constexpr std::size_t kMqttProxyQueueDepth = 12;
    static constexpr std::size_t kPendingMqttDownlinkDepth = 4;
    static constexpr std::size_t kPendingMqttDownlinkDrainPerTick = 2;
    static constexpr std::size_t kPkiNodeTableDepth = 16;
    static constexpr std::size_t kNodeRuntimeTableDepth = 64;

    struct PkiNodeKeyEntry
    {
        bool used = false;
        ::chat::NodeId node_id = 0;
        std::array<uint8_t, 32> key{};
        uint32_t last_seen_s = 0;
    };

    struct NodeRuntimeEntry
    {
        bool used = false;
        ::chat::NodeId node_id = 0;
        ::chat::ChannelId last_channel = ::chat::ChannelId::PRIMARY;
        bool has_last_channel = false;
        uint32_t nodeinfo_reply_ms = 0;
        uint32_t last_touch_ms = 0;
    };

    ::chat::infra::IncomingTextQueue<kIncomingQueueDepth> text_queue_;
    ::chat::infra::IncomingDataQueue<kIncomingQueueDepth> data_queue_;
    sys::RingBuffer<meshtastic_MqttClientProxyMessage, kMqttProxyQueueDepth> mqtt_proxy_queue_;
    sys::RingBuffer<PendingMqttDownlink, kPendingMqttDownlinkDepth> pending_mqtt_downlinks_;
    MqttProxySettings mqtt_proxy_settings_{};
    PacketHistoryTable packet_history_;
    PendingRetransmitTable pending_retransmits_;
    uint32_t last_nodeinfo_ms_ = 0;
    uint8_t last_raw_packet_[256] = {};
    size_t last_raw_packet_len_ = 0;
    bool has_pending_raw_packet_ = false;
    uint8_t encrypt_mode_ = 1;
    bool pki_ready_ = false;
    std::array<uint8_t, 32> pki_public_key_{};
    std::array<uint8_t, 32> pki_private_key_{};
    std::array<PkiNodeKeyEntry, kPkiNodeTableDepth> pki_node_keys_{};
    std::array<::mesh::PeerPublicKey, kPkiNodeTableDepth> pki_save_entries_{};
    uint32_t pki_node_keys_save_due_ms_ = 0;
    bool pki_node_keys_dirty_ = false;
    ::chat::runtime::MeshtasticRuntime protocol_runtime_{};
    ::chat::runtime::ProtocolEffectWorkspace protocol_effect_workspace_{};
    std::array<NodeRuntimeEntry, kNodeRuntimeTableDepth> node_runtime_{};
    TxScratchBuffers tx_scratch_{};
    RxScratchBuffers rx_scratch_{};
    MqttDownlinkScratchBuffers mqtt_downlink_scratch_{};
    MqttPublishScratchBuffers mqtt_publish_scratch_{};

    enum class KeyVerificationState : uint8_t
    {
        Idle,
        SenderInitiated,
        SenderAwaitingNumber,
        SenderAwaitingUser,
        ReceiverAwaitingHash1,
        ReceiverAwaitingUser
    };

    KeyVerificationState kv_state_ = KeyVerificationState::Idle;
    uint64_t kv_nonce_ = 0;
    uint32_t kv_nonce_ms_ = 0;
    uint32_t kv_security_number_ = 0;
    ::chat::NodeId kv_remote_node_ = 0;
    std::array<uint8_t, 32> kv_hash1_{};
    std::array<uint8_t, 32> kv_hash2_{};

    PkiNodeKeyEntry* findPkiNodeKey(::chat::NodeId node_id);
    const PkiNodeKeyEntry* findPkiNodeKey(::chat::NodeId node_id) const;
    PkiNodeKeyEntry* upsertPkiNodeKey(::chat::NodeId node_id, const uint8_t* key, uint32_t last_seen_s,
                                      bool* out_changed = nullptr, bool* out_evicted = nullptr);
    void clearPkiNodeKeys();
    bool erasePkiNodeKey(::chat::NodeId node_id);
    std::size_t pkiNodeKeyCount() const;
    NodeRuntimeEntry* findNodeRuntime(::chat::NodeId node_id);
    const NodeRuntimeEntry* findNodeRuntime(::chat::NodeId node_id) const;
    NodeRuntimeEntry* upsertNodeRuntime(::chat::NodeId node_id, uint32_t now_ms);
    void eraseNodeRuntime(::chat::NodeId node_id);
    bool getNodeLastChannel(::chat::NodeId node_id, ::chat::ChannelId* out) const;
    void rememberNodeLastChannel(::chat::NodeId node_id, ::chat::ChannelId channel, uint32_t now_ms);
    uint32_t getNodeInfoReplyMs(::chat::NodeId node_id) const;
    void setNodeInfoReplyMs(::chat::NodeId node_id, uint32_t now_ms);
};

} // namespace platform::nrf52::arduino_common::chat::meshtastic
