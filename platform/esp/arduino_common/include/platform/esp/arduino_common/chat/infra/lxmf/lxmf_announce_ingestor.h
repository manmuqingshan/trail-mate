/**
 * @file lxmf_announce_ingestor.h
 * @brief Verified announce ingestion owner for embedded LXMF.
 */

#pragma once

#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_destination_registry.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_identity.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_path_manager.h"
#include "platform/esp/arduino_common/chat/infra/reticulum/reticulum_interfaces.h"

namespace chat::lxmf::runtime
{

using LocalDestinationResolver =
    bool (*)(void* context,
             const uint8_t destination_hash[reticulum::kTruncatedHashSize],
             LocalDestinationKind* out_kind);

struct AnnounceIngestOptions
{
    uint32_t now_ms = 0;
    uint32_t now_s = 0;
    uint32_t path_ttl_ms = 0;
    uint32_t directory_address_refresh_interval_s = 0;
    std::size_t max_paths = 0;
    uint8_t max_transport_hops = 0;
    reticulum::interfaces::InterfaceId ingress_interface_id =
        reticulum::interfaces::kInvalidInterfaceId;
    reticulum::interfaces::InterfaceKind ingress_interface =
        reticulum::interfaces::InterfaceKind::LoRa;
    void* local_destination_context = nullptr;
    LocalDestinationResolver resolve_local_destination = nullptr;
};

struct AnnounceIngestResult
{
    enum class Status
    {
        Rejected,
        Ignored,
        Accepted,
    };

    Status status = Status::Rejected;
    const char* reason = "invalid";
    reticulum::ParsedAnnounce announce{};
    uint8_t identity_hash[reticulum::kTruncatedHashSize] = {};
    uint8_t expected_destination_hash[reticulum::kTruncatedHashSize] = {};
    bool local_destination = false;
    LocalDestinationKind local_kind = LocalDestinationKind::Delivery;
    PathAnnounceDecision path_decision = PathAnnounceDecision::RejectReplay;
    PathEntry* path = nullptr;
    bool delivery_announce = false;
    bool propagation_announce = false;
    bool call_audio_announce = false;
    bool lxst_telephony_announce = false;
    bool nomad_node_announce = false;
    bool contact_announce = false;
    bool packet_has_ratchet = false;
    char display_name[32] = {};
    PeerInfo* learned_peer = nullptr;
    bool identity_changed = false;
    bool ratchet_changed = false;
    bool display_changed = false;
    bool address_refresh_due = false;
    bool should_store_address = false;
};

class AnnounceIngestor
{
  public:
    bool ingest(const uint8_t* raw_packet,
                std::size_t raw_len,
                const reticulum::ParsedPacket& packet,
                const LxmfIdentity& local_identity,
                DestinationRegistry& destination_registry,
                PathManager& path_manager,
                const AnnounceIngestOptions& options,
                AnnounceIngestResult* out_result);

  private:
    uint8_t signed_scratch_[reticulum::kReticulumMtu] = {};
};

} // namespace chat::lxmf::runtime
