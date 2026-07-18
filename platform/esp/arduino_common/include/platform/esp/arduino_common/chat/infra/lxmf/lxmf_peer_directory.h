/**
 * @file lxmf_peer_directory.h
 * @brief Reticulum peer directory application service.
 */

#pragma once

#include "chat/ports/i_mesh_peer_directory.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_destination_registry.h"

#include <cstddef>
#include <cstdint>

namespace chat::lxmf::runtime
{

struct PeerDirectoryLoadResult
{
    PeerInfo* peer = nullptr;
    MeshPeerDirectoryStatus status = MeshPeerDirectoryStatus::success();
    bool loaded_from_directory = false;
};

struct PeerDirectoryWriteResult
{
    MeshPeerDirectoryStatus record_status = MeshPeerDirectoryStatus::success();
    MeshPeerDirectoryStatus flags_status = MeshPeerDirectoryStatus::success();
    bool flags_attempted = false;

    bool succeeded() const
    {
        return record_status.succeeded() && flags_status.succeeded();
    }
};

struct PeerDirectoryLoadRecentResult
{
    MeshPeerDirectoryStatus status = MeshPeerDirectoryStatus::success();
    std::size_t scanned = 0;
    std::size_t loaded = 0;
};

ReticulumPeerIdentity reticulumIdentityForPeer(const PeerInfo& peer);

class PeerDirectoryService
{
  public:
    explicit PeerDirectoryService(IMeshPeerDirectory* directory = nullptr);
    PeerDirectoryService(const PeerDirectoryService&) = delete;
    PeerDirectoryService& operator=(const PeerDirectoryService&) = delete;
    PeerDirectoryService(PeerDirectoryService&&) = delete;
    PeerDirectoryService& operator=(PeerDirectoryService&&) = delete;

    void setDirectory(IMeshPeerDirectory* directory);
    bool hasDirectory() const;

    PeerInfo* applyRecord(DestinationRegistry& registry,
                          const MeshPeerRecord& record,
                          uint32_t now_s) const;

    PeerDirectoryLoadResult findOrLoadByNodeId(
        DestinationRegistry& registry,
        NodeId node_id,
        uint32_t now_s) const;

    PeerDirectoryLoadResult findOrLoadByDestinationHash(
        DestinationRegistry& registry,
        const uint8_t destination_hash[reticulum::kTruncatedHashSize],
        uint32_t now_s) const;

    MeshActionResult persistPeerAddressNow(const PeerInfo& peer,
                                           bool favorite,
                                           uint32_t now_s) const;

    PeerDirectoryWriteResult recordPeer(const PeerInfo& peer,
                                        MeshPeerSource source,
                                        bool update_favorite,
                                        bool favorite,
                                        uint32_t now_s) const;

    PeerDirectoryLoadRecentResult loadRecent(DestinationRegistry& registry,
                                             MeshPeerRecord* scratch,
                                             std::size_t scratch_count,
                                             NodeId* out_loaded_nodes,
                                             std::size_t max_loaded_nodes,
                                             uint32_t now_s) const;

  private:
    IMeshPeerDirectory* directory_ = nullptr;
};

} // namespace chat::lxmf::runtime
