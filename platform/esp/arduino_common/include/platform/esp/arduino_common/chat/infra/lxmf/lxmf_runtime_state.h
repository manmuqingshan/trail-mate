/**
 * @file lxmf_runtime_state.h
 * @brief Shared runtime state models for the embedded Reticulum/LXMF runtime
 */

#pragma once

#include "chat/domain/chat_types.h"
#include "chat/infra/lxmf/lxmf_wire.h"
#include "chat/infra/reticulum/lxst_call_state_machine.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_identity.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace chat::lxmf::runtime
{

constexpr std::size_t kAnnounceRandomBlobSize = 10;
constexpr std::size_t kPathRandomBlobHistory = 4;

struct PeerInfo
{
    uint32_t node_id = 0;
    uint8_t destination_hash[reticulum::kTruncatedHashSize] = {};
    uint8_t identity_hash[reticulum::kTruncatedHashSize] = {};
    uint8_t enc_pub[LxmfIdentity::kEncPubKeySize] = {};
    uint8_t sig_pub[LxmfIdentity::kSigPubKeySize] = {};
    uint8_t ratchet_pub[reticulum::kRatchetSize] = {};
    char display_name[32] = {};
    uint32_t last_seen_s = 0;
    uint32_t last_path_request_ms = 0;
    uint32_t ratchet_seen_s = 0;
    bool has_ratchet = false;
};

struct PathEntry
{
    uint8_t destination_hash[reticulum::kTruncatedHashSize] = {};
    uint8_t next_hop_transport[reticulum::kTruncatedHashSize] = {};
    uint8_t cached_packet_hash[reticulum::kFullHashSize] = {};
    uint8_t cached_announce[reticulum::kReticulumMtu] = {};
    size_t cached_announce_len = 0;
    uint8_t announce_random_blobs[kPathRandomBlobHistory][kAnnounceRandomBlobSize] = {};
    uint8_t announce_random_blob_count = 0;
    uint8_t interface_id = 0;
    uint8_t hops = 0;
    uint32_t last_seen_s = 0;
    uint32_t updated_ms = 0;
    uint64_t announce_timebase = 0;
    bool direct = false;
};

struct PacketFilterEntry
{
    uint8_t packet_hash[reticulum::kFullHashSize] = {};
    uint32_t seen_ms = 0;
};

struct ReverseEntry
{
    uint8_t proof_hash[reticulum::kTruncatedHashSize] = {};
    uint8_t interface_id = 0;
    uint8_t expected_hops = 0;
    uint32_t created_ms = 0;
};

struct PendingPathRequest
{
    uint8_t destination_hash[reticulum::kTruncatedHashSize] = {};
    uint32_t created_ms = 0;
    uint32_t last_attempt_ms = 0;
    uint8_t attempts = 0;
    bool resolved = false;
};

struct PendingPingReceipt
{
    uint8_t packet_hash[reticulum::kFullHashSize] = {};
    uint8_t proof_hash[reticulum::kTruncatedHashSize] = {};
    uint8_t destination_hash[reticulum::kTruncatedHashSize] = {};
    uint8_t peer_sig_pub[LxmfIdentity::kSigPubKeySize] = {};
    uint32_t created_ms = 0;
};

struct PendingDeliveryReceipt
{
    uint8_t packet_hash[reticulum::kFullHashSize] = {};
    uint8_t proof_hash[reticulum::kTruncatedHashSize] = {};
    uint8_t destination_hash[reticulum::kTruncatedHashSize] = {};
    uint8_t peer_sig_pub[LxmfIdentity::kSigPubKeySize] = {};
    MessageId message_id = 0;
    uint32_t created_ms = 0;
};

struct LinkRelayEntry
{
    uint8_t link_id[reticulum::kTruncatedHashSize] = {};
    uint8_t initiator_interface_id = 0;
    uint8_t responder_interface_id = 0;
    uint8_t initiator_hops = 0;
    uint8_t responder_hops = 0;
    uint32_t last_seen_ms = 0;
};

enum class LocalDestinationKind : uint8_t
{
    Delivery = 0,
    Propagation = 1,
    CallAudio = 2,
    NomadPage = 3
};

enum class LinkState : uint8_t
{
    Pending = 0,
    Handshake = 1,
    Active = 2,
    Stale = 3,
    Closed = 4
};

enum class LinkCloseReason : uint8_t
{
    None = 0,
    LocalClose = 1,
    RemoteClose = 2,
    Timeout = 3,
    Error = 4
};

struct LinkPendingRequest
{
    std::vector<uint8_t> request_id;
    uint32_t created_ms = 0;
    bool awaiting_resource = false;
    bool response_ready = false;
    std::vector<uint8_t> response;
};

struct DeferredLinkPayload
{
    std::vector<uint8_t> payload;
    std::vector<uint8_t> request_id;
    uint32_t message_id = 0;
    uint8_t resource_flags = 0;
};

struct LinkPacketReceipt
{
    uint8_t packet_hash[reticulum::kFullHashSize] = {};
    uint32_t message_id = 0;
    uint32_t created_ms = 0;
};

struct LinkResourceTransfer
{
    uint8_t resource_hash[reticulum::kFullHashSize] = {};
    uint8_t random_hash[4] = {};
    uint8_t original_hash[reticulum::kFullHashSize] = {};
    uint8_t expected_proof[reticulum::kFullHashSize] = {};
    std::vector<uint8_t> request_id;
    std::vector<uint8_t> hashmap;
    std::vector<std::array<uint8_t, 4>> map_hashes;
    std::vector<uint8_t> map_hash_known;
    std::vector<std::vector<uint8_t>> parts;
    std::vector<uint8_t> received_bitmap;
    uint32_t data_size = 0;
    uint32_t transfer_size = 0;
    uint32_t part_count = 0;
    uint32_t hashmap_height = 0;
    uint32_t next_request_index = 0;
    uint32_t window_size = 4;
    uint32_t segment_index = 1;
    uint32_t total_segments = 1;
    uint32_t created_ms = 0;
    uint32_t last_activity_ms = 0;
    uint32_t message_id = 0;
    uint8_t flags = 0;
    bool incoming = true;
    bool encrypted = false;
    bool compressed = false;
    bool has_metadata = false;
    bool split = false;
    bool waiting_for_hashmap = false;
    bool complete = false;
    bool waiting_for_proof = false;
    int32_t consecutive_complete_index = -1;
};

struct LinkResourceAssembly
{
    uint8_t original_hash[reticulum::kFullHashSize] = {};
    std::vector<uint8_t> request_id;
    std::vector<uint8_t> payload;
    uint32_t next_segment_index = 1;
    uint32_t total_segments = 1;
    uint32_t last_activity_ms = 0;
    uint8_t flags = 0;
};

struct LinkSession
{
    uint8_t link_id[reticulum::kTruncatedHashSize] = {};
    uint8_t remote_destination_hash[reticulum::kTruncatedHashSize] = {};
    uint8_t remote_identity_hash[reticulum::kTruncatedHashSize] = {};
    uint8_t local_enc_pub[LxmfIdentity::kEncPubKeySize] = {};
    uint8_t local_enc_priv[LxmfIdentity::kEncPrivKeySize] = {};
    uint8_t local_sig_pub[LxmfIdentity::kSigPubKeySize] = {};
    uint8_t local_sig_priv[LxmfIdentity::kSigPrivKeySize] = {};
    uint8_t peer_enc_pub[LxmfIdentity::kEncPubKeySize] = {};
    uint8_t peer_link_sig_pub[LxmfIdentity::kSigPubKeySize] = {};
    uint8_t peer_identity_sig_pub[LxmfIdentity::kSigPubKeySize] = {};
    uint8_t derived_key[reticulum::kDerivedTokenKeySize] = {};
    uint32_t created_ms = 0;
    uint32_t request_ms = 0;
    uint32_t last_inbound_ms = 0;
    uint32_t last_outbound_ms = 0;
    uint16_t mtu = reticulum::kReticulumMtu;
    uint16_t mdu = reticulum::kReticulumMdu;
    float rtt_s = 0.0f;
    uint32_t keepalive_interval_ms = 15000;
    uint32_t stale_timeout_ms = 30000;
    uint32_t last_keepalive_ms = 0;
    uint8_t interface_id = 0;
    uint8_t expected_hops = 0;
    bool initiator = false;
    bool local_identity_sent = false;
    bool remote_identity_known = false;
    bool validated = false;
    ReticulumCallWireProfile call_wire_profile =
        ReticulumCallWireProfile::SidebandLxst;
    reticulum::lxst::call::State lxst_call{};
    bool call_runtime_started = false;
    LocalDestinationKind destination = LocalDestinationKind::Delivery;
    LinkState state = LinkState::Pending;
    LinkCloseReason close_reason = LinkCloseReason::None;
    bool propagation_offer_validated = false;
    std::vector<LinkPendingRequest> pending_requests;
    std::vector<DeferredLinkPayload> deferred_payloads;
    std::vector<LinkPacketReceipt> pending_packet_receipts;
    std::vector<LinkResourceTransfer> incoming_resources;
    std::vector<LinkResourceAssembly> incoming_resource_assemblies;
    std::vector<LinkResourceTransfer> outgoing_resources;
};

struct PropagationEntry
{
    uint8_t transient_id[reticulum::kFullHashSize] = {};
    uint8_t destination_hash[reticulum::kTruncatedHashSize] = {};
    std::vector<uint8_t> lxmf_data;
    uint32_t created_s = 0;
    uint32_t served_count = 0;
};

struct PropagationTransientEntry
{
    uint8_t transient_id[reticulum::kFullHashSize] = {};
    uint32_t seen_s = 0;
    bool delivered = false;
};

enum class PropagationUploadState : uint8_t
{
    WaitingNode = 0,
    NeedsStamp = 1,
    Stamping = 2,
    Ready = 3,
    QueuedToLink = 4,
    Failed = 5,
};

struct PendingPropagationUpload
{
    uint8_t node_hash[reticulum::kTruncatedHashSize] = {};
    uint8_t destination_hash[reticulum::kTruncatedHashSize] = {};
    uint8_t message_hash[reticulum::kFullHashSize] = {};
    uint8_t transient_id[reticulum::kFullHashSize] = {};
    std::vector<uint8_t> transient_data;
    uint32_t created_ms = 0;
    uint32_t message_id = 0;
    uint8_t stamp_cost = 0;
    PropagationUploadState state = PropagationUploadState::WaitingNode;
    bool track_user_message = false;
};

enum class PropagationDeliveryCommitState : uint8_t
{
    AwaitingPersistence = 0,
    Accepted = 1,
    Rejected = 2,
};

struct PendingPropagationDelivery
{
    uint8_t transient_id[reticulum::kFullHashSize] = {};
    uint8_t message_hash[reticulum::kFullHashSize] = {};
    PropagationDeliveryCommitState state =
        PropagationDeliveryCommitState::AwaitingPersistence;
};

enum class PropagationSyncStage : uint8_t
{
    Idle = 0,
    NeedList = 1,
    Listing = 2,
    NeedMessages = 3,
    Downloading = 4,
    AwaitingPersistence = 5,
    NeedAcknowledge = 6,
    Acknowledging = 7,
    Complete = 8,
    Failed = 9,
};

struct PropagationPeerState
{
    uint8_t propagation_hash[reticulum::kTruncatedHashSize] = {};
    uint8_t delivery_hash[reticulum::kTruncatedHashSize] = {};
    uint8_t identity_hash[reticulum::kTruncatedHashSize] = {};
    uint8_t enc_pub[LxmfIdentity::kEncPubKeySize] = {};
    uint8_t sig_pub[LxmfIdentity::kSigPubKeySize] = {};
    char display_name[32] = {};
    uint32_t announce_timebase_s = 0;
    uint32_t last_seen_s = 0;
    uint32_t transfer_limit_kb = 0;
    uint32_t sync_limit_kb = 0;
    uint32_t incoming_messages = 0;
    uint32_t served_messages = 0;
    uint8_t hops = 0;
    uint8_t stamp_cost = 0;
    uint8_t stamp_cost_flexibility = 0;
    uint8_t peering_cost = 0;
    bool node_active = false;
};

struct TransportRuntime
{
    std::vector<PathEntry> paths;
    std::vector<PacketFilterEntry> packet_filter;
    std::vector<ReverseEntry> reverse_table;
    std::vector<PendingPathRequest> pending_path_requests;
    std::vector<PendingPingReceipt> pending_ping_receipts;
    std::vector<PendingDeliveryReceipt> pending_delivery_receipts;
    std::vector<LinkRelayEntry> link_relays;
};

struct LinkRuntime
{
    std::vector<LinkSession> sessions;
};

struct PropagationRuntime
{
    std::vector<PropagationEntry> entries;
    std::vector<PropagationTransientEntry> transients;
    std::vector<PropagationPeerState> peers;
    std::vector<PendingPropagationUpload> pending_uploads;
    std::vector<PendingPropagationDelivery> pending_deliveries;
    std::vector<std::vector<uint8_t>> sync_wants;
    std::vector<std::vector<uint8_t>> sync_haves;
    uint8_t active_node_hash[reticulum::kTruncatedHashSize] = {};
    uint8_t sync_request_id[reticulum::kTruncatedHashSize] = {};
    uint32_t last_sync_s = 0;
    uint32_t sync_started_ms = 0;
    uint32_t persistence_started_ms = 0;
    PropagationSyncStage sync_stage = PropagationSyncStage::Idle;
    bool has_active_node = false;
    bool initial_sync_pending = true;
};

} // namespace chat::lxmf::runtime
