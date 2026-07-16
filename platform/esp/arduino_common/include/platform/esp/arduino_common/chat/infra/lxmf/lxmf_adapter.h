/**
 * @file lxmf_adapter.h
 * @brief Device-side LXMF adapter over the existing RNode raw carrier
 */

#pragma once

#include "board/LoraBoard.h"
#include "chat/infra/lxmf/lxmf_wire.h"
#include "chat/infra/mesh_incoming_queue.h"
#include "chat/ports/i_mesh_adapter.h"
#include "chat/ports/i_mesh_peer_directory.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_identity.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_propagation_stamp_runtime.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_runtime_state.h"
#include "platform/esp/arduino_common/chat/infra/reticulum/reticulum_interfaces.h"
#include "platform/ui/reticulum_page_runtime.h"
#include "sys/ringbuf.h"

#include <array>
#include <cstddef>
#include <vector>

namespace chat::lxmf
{

class LxmfAdapter : public IMeshAdapter
{
  public:
    explicit LxmfAdapter(LoraBoard& board,
                         IMeshPeerDirectory* peer_directory = nullptr);

    static void* operator new(std::size_t size);
    static void operator delete(void* ptr) noexcept;
    static void operator delete(void* ptr, std::size_t size) noexcept;

    MeshCapabilities getCapabilities() const override;
    bool sendText(ChannelId channel, const std::string& text,
                  MessageId* out_msg_id, NodeId peer = 0) override;
    MeshSendResult sendTextDetailed(ChannelId channel, const std::string& text,
                                    MessageId forced_msg_id = 0,
                                    NodeId peer = 0) override;
    MeshSendResult sendTextToReticulumDestination(
        ChannelId channel,
        const std::string& text,
        MessageId forced_msg_id,
        const ReticulumPeerIdentity& destination) override;
    bool pollIncomingText(MeshIncomingText* out) override;
    void commitIncomingText(const MeshIncomingText& message,
                            bool durably_accepted);
    bool sendAppData(ChannelId channel, uint32_t portnum,
                     const uint8_t* payload, size_t len,
                     NodeId dest = 0, bool want_ack = false,
                     MessageId packet_id = 0,
                     bool want_response = false) override;
    bool pollIncomingData(MeshIncomingData* out) override;
    bool requestNodeInfo(NodeId dest, bool want_response) override;
    bool broadcastSelfIdentity() override;
    NodeId getNodeId() const override;
    bool getReticulumLocalIdentityInfo(ReticulumLocalIdentityInfo* out) const override;
    MeshActionResult startReticulumAudioCall(
        const ReticulumPeerIdentity& destination) override;
    MeshActionResult pingReticulumDestination(
        const ReticulumPeerIdentity& destination) override;
    MeshActionResult persistReticulumPeer(
        const ReticulumPeerIdentity& destination,
        bool favorite) override;
    MeshActionResult requestNomadPage(
        const uint8_t destination_hash[reticulum::kTruncatedHashSize],
        const char* path);
    void applyConfig(const MeshConfig& config) override;
    void setUserInfo(const char* long_name, const char* short_name) override;
    bool setWifiTransportEnabled(bool enabled) override;
    bool isReady() const override;
    bool pollIncomingRawPacket(uint8_t* out_data, size_t& out_len, size_t max_len) override;
    void handleRawPacket(const uint8_t* data, size_t size) override;
    void setLastRxStats(float rssi, float snr) override;
    void processSendQueue() override;

  private:
    using PeerInfo = runtime::PeerInfo;
    using PathEntry = runtime::PathEntry;
    using PacketFilterEntry = runtime::PacketFilterEntry;
    using ReverseEntry = runtime::ReverseEntry;
    using PendingPathRequest = runtime::PendingPathRequest;
    using LinkRelayEntry = runtime::LinkRelayEntry;
    using LocalDestinationKind = runtime::LocalDestinationKind;
    using LinkState = runtime::LinkState;
    using LinkCloseReason = runtime::LinkCloseReason;
    using LinkPendingRequest = runtime::LinkPendingRequest;
    using LinkResourceTransfer = runtime::LinkResourceTransfer;
    using LinkResourceAssembly = runtime::LinkResourceAssembly;
    using LinkSession = runtime::LinkSession;
    using PropagationEntry = runtime::PropagationEntry;
    using PropagationTransientEntry = runtime::PropagationTransientEntry;
    using PropagationPeerState = runtime::PropagationPeerState;
    using PendingPropagationUpload = runtime::PendingPropagationUpload;
    using PropagationSyncStage = runtime::PropagationSyncStage;

    static constexpr uint32_t kAnnounceIntervalMs = 120000;
    static constexpr uint32_t kInitialAnnounceDelayMs = 1500;
    static constexpr uint32_t kPendingAnnounceRetryMs = 30000;
    static constexpr uint8_t kMaxIngressPacketsPerPoll = 4;
    static constexpr uint8_t kCallIngressPacketsPerPoll = 8;
    static constexpr uint32_t kDiscoverySampleIntervalMs = 10000;
    static constexpr uint32_t kRxSummaryIntervalMs = 5000;
    static constexpr uint32_t kAnnounceRebroadcastIntervalMs = 60000;
    static constexpr uint32_t kPeerProjectionScreenIntervalMs = 2000;
    static constexpr uint32_t kPeerProjectionSleepIntervalMs = 250;
    static constexpr std::size_t kPendingPeerProjectionDepth = 24;
    static constexpr std::size_t kDeferredDiscoveryDepth = 8;
    static constexpr std::size_t kPeerDirectoryHotLoadRecords = 64;
    static constexpr std::size_t kMaxPendingPingRequests = 4;
    static constexpr std::size_t kMaxPendingNomadPageRequests = 4;
    static constexpr std::size_t kNomadPagePathMaxLen = 64;
    static constexpr uint32_t kNomadPageRequestTtlMs = 90000;
    static constexpr uint32_t kNomadPageSendRetryMs = 1500;

    struct RuntimeBudget
    {
        uint8_t live_packet_limit = 1;
        uint8_t deferred_discovery_limit = 0;
        bool allow_public_discovery = false;
        bool allow_persistence = false;
        bool allow_peer_projection = false;
        bool allow_announce_tx = true;
        bool drop_public_discovery = false;
        const char* phase = "screen";
    };

    struct DeferredDiscoveryPacket
    {
        uint8_t data[reticulum::kReticulumMtu] = {};
        size_t len = 0;
        RxMeta rx_meta{};
        reticulum::interfaces::InterfaceKind interface_kind =
            reticulum::interfaces::InterfaceKind::LoRa;
        reticulum::interfaces::InterfaceId interface_id =
            reticulum::interfaces::kInvalidInterfaceId;
        uint8_t packet_hash[reticulum::kFullHashSize] = {};
    };

    struct PendingNomadPageRequest
    {
        uint8_t destination_hash[reticulum::kTruncatedHashSize] = {};
        uint8_t request_id[reticulum::kTruncatedHashSize] = {};
        char path[kNomadPagePathMaxLen] = {};
        uint32_t created_ms = 0;
        uint32_t last_attempt_ms = 0;
        uint32_t last_path_request_ms = 0;
        bool path_requested = false;
        bool link_started = false;
        bool request_sent = false;
    };

    struct PendingPingRequest
    {
        uint8_t destination_hash[reticulum::kTruncatedHashSize] = {};
        uint32_t created_ms = 0;
        uint32_t last_path_request_ms = 0;
        uint32_t last_send_attempt_ms = 0;
    };

    struct OutboundLxmfDispatch
    {
        bool ok = false;
        bool result_event_deferred = false;
        MessageId message_id = 0;
        MeshOperationFailure failure = MeshOperationFailure::None;
        uint8_t message_hash[reticulum::kFullHashSize] = {};
        const char* path = "none";
    };

    reticulum::interfaces::ReticulumInterfaceSet interfaces_;
    uint32_t network_config_generation_ = 0;
    IMeshPeerDirectory* peer_directory_ = nullptr;
    reticulum::interfaces::RxPacket rx_packet_scratch_{};
    uint8_t announce_tx_signed_scratch_[reticulum::kReticulumMtu] = {};
    uint8_t announce_tx_payload_scratch_[reticulum::kReticulumMtu] = {};
    uint8_t announce_tx_packet_scratch_[reticulum::kReticulumMtu] = {};
    uint8_t announce_rx_signed_scratch_[reticulum::kReticulumMtu] = {};
    sys::RingBuffer<DeferredDiscoveryPacket, kDeferredDiscoveryDepth> deferred_discovery_queue_;
    DeferredDiscoveryPacket deferred_discovery_scratch_{};
    LxmfIdentity identity_;
    MeshConfig config_{};
    static constexpr std::size_t kIncomingQueueDepth = 12;
    ::chat::infra::IncomingTextQueue<kIncomingQueueDepth, reticulum::kReticulumMtu> text_receive_queue_;
    ::chat::infra::IncomingDataQueue<kIncomingQueueDepth, reticulum::kReticulumMtu> data_receive_queue_;
    std::vector<PeerInfo> peers_;
    runtime::TransportRuntime transport_;
    runtime::LinkRuntime links_;
    runtime::PropagationRuntime propagation_;
    runtime::PropagationStampRuntime propagation_stamp_;
    PeerInfo propagation_peer_scratch_{};
    std::string user_long_name_;
    std::string user_short_name_;
    uint32_t last_announce_ms_ = 0;
    uint32_t last_announce_attempt_ms_ = 0;
    uint32_t last_lora_discovery_sample_ms_ = 0;
    uint32_t last_wifi_discovery_sample_ms_ = 0;
    uint32_t last_rx_summary_ms_ = 0;
    uint32_t last_announce_rebroadcast_ms_ = 0;
    uint32_t rx_summary_packets_ = 0;
    uint32_t rx_summary_wifi_skipped_ = 0;
    uint32_t rx_summary_duplicates_ = 0;
    uint32_t rx_summary_parse_failed_ = 0;
    uint32_t rx_summary_deferred_ = 0;
    uint32_t rx_summary_deferred_dropped_ = 0;
    uint32_t rx_summary_throttled_discovery_ = 0;
    uint32_t last_lora_discovery_detail_log_ms_ = 0;
    uint32_t suppressed_lora_discovery_detail_logs_ = 0;
    uint32_t last_lora_announce_ignore_log_ms_ = 0;
    uint32_t suppressed_lora_announce_ignore_logs_ = 0;
    std::array<NodeId, kPendingPeerProjectionDepth> pending_peer_projection_nodes_{};
    std::size_t pending_peer_projection_count_ = 0;
    std::array<MeshPeerRecord, kPeerDirectoryHotLoadRecords> peer_directory_load_entries_{};
    std::vector<PendingPingRequest> pending_ping_requests_;
    std::vector<PendingNomadPageRequest> pending_nomad_page_requests_;
    uint8_t nomad_page_request_payload_scratch_[reticulum::kReticulumMtu] = {};
    uint8_t nomad_page_wire_payload_scratch_[reticulum::kReticulumMtu] = {};
    uint8_t nomad_page_packet_scratch_[reticulum::kReticulumMtu] = {};
    uint8_t link_request_payload_scratch_[reticulum::kReticulumMtu] = {};
    uint8_t link_request_packet_scratch_[reticulum::kReticulumMtu] = {};
    uint8_t link_request_routed_scratch_[reticulum::kReticulumMtu] = {};
    std::size_t link_request_packet_len_ = 0;
    uint8_t call_wire_scratch_[reticulum::kReticulumMtu] = {};
    uint32_t last_peer_projection_ms_ = 0;
    uint32_t next_app_packet_id_ = 1;
    bool announce_pending_ = true;
    bool peers_loaded_ = false;
    RxMeta active_rx_meta_{};
    bool has_active_rx_meta_ = false;
    reticulum::interfaces::InterfaceId active_ingress_interface_id_ =
        reticulum::interfaces::kInvalidInterfaceId;

    RuntimeBudget makeRuntimeBudget() const;
    void processRuntime();
    void processRadioPackets(const RuntimeBudget& budget);
    bool processOneRadioPacket(const reticulum::interfaces::RxPacket& packet,
                               const RuntimeBudget& budget,
                               bool deferred_replay);
    bool shouldDeferDiscoveryPacket(
        const reticulum::ParsedPacket& packet,
        reticulum::interfaces::InterfaceKind ingress_interface,
        const RuntimeBudget& budget);
    bool isPublicDiscoveryPacket(const reticulum::ParsedPacket& packet) const;
    bool enqueueDeferredDiscoveryPacket(
        const reticulum::interfaces::RxPacket& packet,
        const uint8_t packet_hash[reticulum::kFullHashSize]);
    bool hasDeferredDiscoveryPacket(
        const uint8_t packet_hash[reticulum::kFullHashSize]) const;
    void processDeferredDiscoveryPackets(const RuntimeBudget& budget);
    void maybeAnnounce();
    bool sendAnnounce(LocalDestinationKind kind = LocalDestinationKind::Delivery,
                      reticulum::PacketContext context = reticulum::PacketContext::None);
    bool lastAnnounceTxReachedRequiredInterfaces(bool sent) const;
    bool handleAnnouncePacket(const uint8_t* raw_packet, size_t raw_len,
                              const reticulum::ParsedPacket& packet,
                              reticulum::interfaces::InterfaceKind ingress_interface,
                              bool allow_persistence);
    bool handleDataPacket(const uint8_t* raw_packet, size_t raw_len,
                          const reticulum::ParsedPacket& packet);
    bool handleProofPacket(const uint8_t* raw_packet, size_t raw_len,
                           const reticulum::ParsedPacket& packet,
                           reticulum::interfaces::InterfaceKind ingress_interface);
    bool handleLinkRequestPacket(
        const uint8_t* raw_packet, size_t raw_len,
        const reticulum::ParsedPacket& packet,
        reticulum::interfaces::InterfaceKind ingress_interface);
    bool handlePathRequestPacket(const reticulum::ParsedPacket& packet);
    bool handleCacheRequestPacket(const reticulum::ParsedPacket& packet);
    bool maybeForwardTransportPacket(const uint8_t* raw_packet, size_t raw_len,
                                     const reticulum::ParsedPacket& packet);
    bool maybeForwardLinkPacket(const uint8_t* raw_packet, size_t raw_len,
                                const reticulum::ParsedPacket& packet);
    bool handleLocalLinkPacket(
        const uint8_t* raw_packet, size_t raw_len,
        const reticulum::ParsedPacket& packet,
        reticulum::interfaces::InterfaceKind ingress_interface);
    bool sendProofForPacket(const uint8_t* raw_packet, size_t raw_len);
    bool sendPathRequest(PeerInfo& peer);
    bool sendPathRequestForDestination(
        const uint8_t destination_hash[reticulum::kTruncatedHashSize]);
    bool shouldRequestPath(const PeerInfo& peer) const;
    LinkSession* ensureOutboundLinkSession(PeerInfo& peer,
                                           LocalDestinationKind kind,
                                           bool* out_started = nullptr);
    bool prepareLinkRequest(LinkSession& session);
    bool sendLinkRequest(LinkSession& session);
    bool buildSignedMessagePacket(const PeerInfo& peer,
                                  const uint8_t* packed_payload, size_t packed_payload_len,
                                  uint8_t* out_packet, size_t* inout_len,
                                  uint8_t out_message_hash[reticulum::kFullHashSize]);
    bool buildGroupMessagePacket(
        const ReticulumPeerIdentity& destination,
        const uint8_t* packed_payload, size_t packed_payload_len,
        uint8_t* out_packet, size_t* inout_len,
        uint8_t out_message_hash[reticulum::kFullHashSize]);
    bool dispatchLxmfPayload(PeerInfo& peer,
                             const uint8_t* packed_payload,
                             size_t packed_payload_len,
                             bool track_user_message,
                             OutboundLxmfDispatch* out_dispatch);
    bool queuePropagationUpload(PeerInfo& recipient,
                                const uint8_t* lxmf_message,
                                size_t lxmf_message_len,
                                MessageId message_id,
                                const uint8_t message_hash[reticulum::kFullHashSize],
                                bool track_user_message,
                                OutboundLxmfDispatch* out_dispatch);
    void processPropagationClient();
    const PropagationPeerState* selectActivePropagationPeer();
    bool preparePropagationPeer(const PropagationPeerState& source,
                                PeerInfo* out_peer) const;
    bool encryptForPeer(const PeerInfo& peer,
                        const uint8_t* plaintext,
                        size_t plaintext_len,
                        uint8_t* out_payload,
                        size_t* inout_len);
    bool queueReadyPropagationUpload(PendingPropagationUpload& upload,
                                     const PropagationPeerState& node);
    bool sendPropagationSyncRequest(LinkSession& session,
                                    PropagationSyncStage next_stage,
                                    const std::vector<std::vector<uint8_t>>* wants,
                                    const std::vector<std::vector<uint8_t>>* haves,
                                    bool include_transfer_limit);
    void processPropagationSyncResponse(LinkSession& session);
    bool respondToSidebandTelemetryRequest(
        PeerInfo& peer,
        const SidebandTelemetryRequest& request);
    bool buildEncryptedPacketForPeer(const PeerInfo& peer,
                                     const uint8_t* plaintext, size_t plaintext_len,
                                     uint8_t* out_packet, size_t* inout_len);
    bool routeAndSendPacket(const uint8_t* raw_packet, size_t raw_len,
                            bool allow_transport,
                            bool wifi_only = false);
    bool sendCachedAnnounceResponse(const PathEntry& path,
                                    reticulum::PacketContext context);
    bool sendCachedPacketReplay(const uint8_t packet_hash[reticulum::kFullHashSize]);
    bool shouldProcessWifiIngressPacket(const reticulum::ParsedPacket& packet,
                                        const RuntimeBudget& budget);
    bool shouldLogRxDetail(const reticulum::ParsedPacket& packet,
                           reticulum::interfaces::InterfaceKind ingress_interface,
                           const RuntimeBudget& budget);
    bool consumeDiscoveryBudget(reticulum::interfaces::InterfaceKind ingress_interface);
    bool isForegroundDiscoveryDestination(
        const uint8_t destination_hash[reticulum::kTruncatedHashSize]) const;
    void noteRxSummary(bool wifi_skipped = false,
                       bool duplicate = false,
                       bool parse_failed = false,
                       bool deferred = false,
                       bool deferred_dropped = false,
                       bool throttled_discovery = false);
    bool shouldRebroadcastAnnounce(
        const reticulum::ParsedPacket& packet,
        reticulum::interfaces::InterfaceKind ingress_interface) const;
    bool rebroadcastAnnounce(const PathEntry& path, const reticulum::ParsedPacket& packet);
    bool isDuplicatePacket(const uint8_t packet_hash[reticulum::kFullHashSize]);
    void rememberPacket(const uint8_t packet_hash[reticulum::kFullHashSize]);
    void rememberReversePath(const uint8_t proof_hash[reticulum::kTruncatedHashSize],
                             reticulum::interfaces::InterfaceId interface_id,
                             uint8_t expected_hops);
    ReverseEntry* findReversePath(const uint8_t proof_hash[reticulum::kTruncatedHashSize]);
    PendingPathRequest* findPendingPathRequest(
        const uint8_t destination_hash[reticulum::kTruncatedHashSize]);
    const PendingPathRequest* findPendingPathRequest(
        const uint8_t destination_hash[reticulum::kTruncatedHashSize]) const;
    void notePendingPathRequest(const uint8_t destination_hash[reticulum::kTruncatedHashSize],
                                uint32_t now_ms);
    void resolvePendingPathRequest(const uint8_t destination_hash[reticulum::kTruncatedHashSize]);
    void cullTransportState();
    void cullLinkSessions();
    PathEntry& upsertPath(const uint8_t destination_hash[reticulum::kTruncatedHashSize]);
    const PathEntry* findPath(const uint8_t destination_hash[reticulum::kTruncatedHashSize]) const;
    LinkRelayEntry& upsertLinkRelay(const uint8_t link_id[reticulum::kTruncatedHashSize]);
    LinkRelayEntry* findLinkRelay(const uint8_t link_id[reticulum::kTruncatedHashSize]);
    LinkSession* findLinkSession(const uint8_t link_id[reticulum::kTruncatedHashSize]);
    LinkSession* findActiveLinkSessionByDestination(const uint8_t destination_hash[reticulum::kTruncatedHashSize],
                                                    LocalDestinationKind kind);
    PeerInfo* findPeerByNodeId(NodeId node_id);
    const PeerInfo* findPeerByDestinationHash(const uint8_t hash[reticulum::kTruncatedHashSize]) const;
    const PeerInfo* findPeerByIdentityHash(const uint8_t hash[reticulum::kTruncatedHashSize]) const;
    const ReticulumGroupDestinationConfig* findConfiguredGroupDestination(
        const uint8_t hash[reticulum::kTruncatedHashSize]) const;
    bool isConfiguredGroupDestination(
        const ReticulumPeerIdentity& destination) const;
    PeerInfo& upsertPeer(const uint8_t destination_hash[reticulum::kTruncatedHashSize]);
    PeerInfo* upsertPeerFromDirectoryRecord(const MeshPeerRecord& record,
                                            bool queue_update);
    PeerInfo* findOrLoadPeerByNodeId(NodeId node_id);
    PeerInfo* findOrLoadPeerByDestinationHash(
        const uint8_t destination_hash[reticulum::kTruncatedHashSize]);
    MeshActionResult persistPeerAddressNow(const PeerInfo& peer, bool favorite) const;
    bool recordPeerInDirectory(const PeerInfo& peer,
                               MeshPeerSource source,
                               bool update_favorite,
                               bool favorite) const;
    PeerInfo* rememberPeerIdentity(const uint8_t combined_pub[reticulum::kCombinedPublicKeySize],
                                   const char* display_name = nullptr);
    void queuePeerUpdate(const PeerInfo& peer);
    void pumpPendingPeerUpdates();
    void publishPeerUpdate(const PeerInfo& peer) const;
    void loadPersistedPeers();
    void loadDirectoryPeers();
    uint32_t currentTimestampSeconds() const;
    const char* effectiveDisplayName() const;
    void populateRxMeta(RxMeta* out) const;
    void localDestinationHash(LocalDestinationKind kind,
                              uint8_t out_hash[reticulum::kTruncatedHashSize]) const;
    bool isLocalDestinationHash(const uint8_t hash[reticulum::kTruncatedHashSize],
                                LocalDestinationKind* out_kind) const;
    static uint16_t linkMduForMtu(uint16_t mtu);
    static bool generateLinkSigningKey(uint8_t out_pub[LxmfIdentity::kSigPubKeySize],
                                       uint8_t out_priv[LxmfIdentity::kSigPrivKeySize]);
    static bool signWithKey(const uint8_t sign_pub[LxmfIdentity::kSigPubKeySize],
                            const uint8_t sign_priv[LxmfIdentity::kSigPrivKeySize],
                            const uint8_t* message,
                            size_t message_len,
                            uint8_t out_signature[reticulum::kSignatureSize]);
    bool deriveLinkKey(LinkSession& session);
    bool encryptLinkPayload(const LinkSession& session,
                            const uint8_t* plaintext, size_t plaintext_len,
                            uint8_t* out_payload, size_t* inout_len) const;
    bool decryptLinkPayload(const LinkSession& session,
                            const uint8_t* payload, size_t payload_len,
                            std::vector<uint8_t>* out_plaintext) const;
    bool sendLinkPacket(LinkSession& session,
                        reticulum::PacketType packet_type,
                        reticulum::PacketContext context,
                        const uint8_t* payload, size_t payload_len,
                        bool encrypt_payload,
                        bool call_admission_control = false,
                        uint8_t out_packet_hash[reticulum::kFullHashSize] = nullptr);
    bool sendNomadPageRequestPacket(LinkSession& session,
                                    PendingNomadPageRequest& request);
    MeshActionResult sendReticulumPingToPeer(PeerInfo& peer,
                                             uint32_t operation_started_ms);
    MeshActionResult queuePendingReticulumPing(
        const uint8_t destination_hash[reticulum::kTruncatedHashSize]);
    void pumpPendingPingRequests();
    void pumpNomadPageRequests();
    void completeNomadPageRequest(PendingNomadPageRequest& request,
                                  const std::vector<uint8_t>& packed_response);
    PendingNomadPageRequest* findPendingNomadPageRequestById(
        const uint8_t destination_hash[reticulum::kTruncatedHashSize],
        const uint8_t* request_id,
        std::size_t request_id_len);
    void updateNomadPageProgress(const PendingNomadPageRequest& request,
                                 int progress_percent,
                                 const char* message,
                                 const char* detail,
                                 bool active,
                                 bool complete,
                                 platform::ui::reticulum_page::RequestProgress::
                                     FailureKind failure);
    void updateNomadPageProgressForDestination(
        const uint8_t destination_hash[reticulum::kTruncatedHashSize],
        int progress_percent,
        const char* message,
        const char* detail,
        bool active,
        bool complete,
        platform::ui::reticulum_page::RequestProgress::FailureKind failure);
    bool sendLinkHandshakeProof(LinkSession& session,
                                bool call_admission_control = false);
    bool sendLinkRtt(LinkSession& session);
    bool sendLinkKeepalive(LinkSession& session);
    bool sendLinkKeepaliveAck(LinkSession& session);
    bool sendLinkIdentify(LinkSession& session);
    bool sendLinkPacketProof(LinkSession& session,
                             const uint8_t* raw_packet, size_t raw_len);
    void updateCallRuntimePeer(LinkSession& session,
                               const PeerInfo* peer = nullptr);
    bool beginIncomingCallRuntime(LinkSession& session,
                                  const PeerInfo& peer);
    bool sendLxstSignal(LinkSession& session,
                        uint16_t signal,
                        bool call_admission_control = false);
    bool dispatchLxstCallEvent(
        LinkSession& session,
        const reticulum::lxst::call::Event& event);
    bool handleLxstPacket(LinkSession& session,
                          const uint8_t* payload,
                          size_t payload_len);
    bool sendCallAudioPacket(LinkSession& session,
                             const uint8_t* payload,
                             size_t payload_len);
    void pumpReticulumAudioCall();
    void closeLinkSession(LinkSession& session,
                          LinkCloseReason reason = LinkCloseReason::LocalClose);
    void flushDeferredLinkPayloads(LinkSession& session);
    void expirePath(const uint8_t destination_hash[reticulum::kTruncatedHashSize]);
    bool handleLinkDataPacket(LinkSession& session,
                              const uint8_t* raw_packet, size_t raw_len,
                              const reticulum::ParsedPacket& packet);
    bool handleLinkProofPacket(LinkSession& session,
                               const uint8_t* raw_packet, size_t raw_len,
                               const reticulum::ParsedPacket& packet);
    bool acceptVerifiedEnvelope(const uint8_t* plaintext, size_t plaintext_len,
                                const uint8_t* raw_packet, size_t raw_len,
                                uint8_t* out_message_hash = nullptr,
                                bool* out_awaiting_commit = nullptr);
    bool acceptVerifiedEnvelopeForDestination(
        const uint8_t expected_destination_hash[reticulum::kTruncatedHashSize],
        const ReticulumPeerIdentity& conversation_identity,
        bool destination_is_group,
        bool encrypted,
        const uint8_t* plaintext, size_t plaintext_len,
        const uint8_t* raw_packet, size_t raw_len,
        uint8_t* out_message_hash = nullptr,
        bool* out_awaiting_commit = nullptr);
    bool handleLinkResourceAdvertisement(LinkSession& session,
                                         const uint8_t* plaintext, size_t plaintext_len);
    bool handleLinkResourceRequest(LinkSession& session,
                                   const uint8_t* plaintext, size_t plaintext_len);
    bool handleLinkResourceHashmapUpdate(LinkSession& session,
                                         const uint8_t* plaintext, size_t plaintext_len);
    bool handleLinkResourcePart(LinkSession& session,
                                const reticulum::ParsedPacket& packet);
    bool handleLinkResourceProof(LinkSession& session,
                                 const reticulum::ParsedPacket& packet);
    bool handlePropagationBatch(LinkSession& session,
                                const uint8_t* plaintext, size_t plaintext_len);
    bool handlePropagationRequest(LinkSession& session,
                                  const DecodedLinkRequest& request,
                                  const uint8_t* request_id,
                                  size_t request_id_len);
    bool acceptPropagatedDelivery(const uint8_t* propagated_payload,
                                  size_t propagated_payload_len,
                                  uint8_t* out_message_hash = nullptr,
                                  bool* out_awaiting_commit = nullptr);
    bool requestNextResourceWindow(LinkSession& session,
                                   LinkResourceTransfer& resource);
    bool advertiseLinkResource(LinkSession& session,
                               LinkResourceTransfer& resource,
                               uint32_t hashmap_segment = 0);
    bool queueOutgoingResource(LinkSession& session,
                               const uint8_t* data, size_t len,
                               uint8_t flags,
                               const uint8_t* request_id,
                               size_t request_id_len,
                               uint32_t message_id = 0);
    bool sendLinkResponse(LinkSession& session,
                          const uint8_t* request_id,
                          size_t request_id_len,
                          const uint8_t* packed_data,
                          size_t packed_data_len,
                          bool data_is_nil);
    static uint32_t messageIdFromHash(const uint8_t hash[reticulum::kFullHashSize]);
    static void pathRequestDestinationHash(uint8_t out_hash[reticulum::kTruncatedHashSize]);
};

} // namespace chat::lxmf
