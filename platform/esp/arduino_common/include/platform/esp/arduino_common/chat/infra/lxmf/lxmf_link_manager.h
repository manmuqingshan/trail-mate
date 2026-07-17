/**
 * @file lxmf_link_manager.h
 * @brief Link session owner for the embedded LXMF runtime.
 */

#pragma once

#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_link_runtime.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_resource_runtime.h"

namespace chat::lxmf::runtime
{

class LinkManager
{
  public:
    LinkManager() = default;
    LinkManager(const LinkManager&) = delete;
    LinkManager& operator=(const LinkManager&) = delete;
    LinkManager(LinkManager&&) = delete;
    LinkManager& operator=(LinkManager&&) = delete;

    std::size_t size() const;
    void clear();

    LinkSession* findSession(
        const uint8_t link_id[reticulum::kTruncatedHashSize]);
    LinkSession* findOpenSessionByDestination(
        const uint8_t destination_hash[reticulum::kTruncatedHashSize],
        LocalDestinationKind kind);
    LinkSession* findActiveSessionByDestination(
        const uint8_t destination_hash[reticulum::kTruncatedHashSize],
        LocalDestinationKind kind);

    LinkSession* appendSession(std::size_t max_link_sessions);
    LinkSession* appendSessionPreserving(
        std::size_t max_link_sessions,
        const uint8_t preserve_link_id[reticulum::kTruncatedHashSize]);
    void discardLastSession();

    bool closeSession(LinkSession& session,
                      LinkCloseReason reason,
                      uint32_t now_ms);
    void cullSessionTables(LinkSession& session,
                           uint32_t now_ms,
                           const LinkRuntimeLimits& limits);
    void cullResources(LinkSession& session,
                       uint32_t now_ms,
                       const ResourceRuntimeLimits& limits);
    LinkRuntimeMaintenance advanceSessionLifecycle(
        LinkSession& session,
        uint32_t now_ms,
        const LinkRuntimeLimits& limits);
    void markSessionStale(LinkSession& session);
    void removeExpiredSessions(uint32_t now_ms,
                               const LinkRuntimeLimits& limits);
    LinkResourceTransfer* findIncomingResource(
        LinkSession& session,
        const uint8_t resource_hash[reticulum::kFullHashSize]);
    const LinkResourceTransfer* findIncomingResource(
        const LinkSession& session,
        const uint8_t resource_hash[reticulum::kFullHashSize]) const;
    LinkResourceTransfer* findOutgoingResource(
        LinkSession& session,
        const uint8_t resource_hash[reticulum::kFullHashSize]);
    const LinkResourceTransfer* findOutgoingResource(
        const LinkSession& session,
        const uint8_t resource_hash[reticulum::kFullHashSize]) const;
    bool eraseIncomingResource(
        LinkSession& session,
        const uint8_t resource_hash[reticulum::kFullHashSize]);
    bool eraseOutgoingResource(
        LinkSession& session,
        const uint8_t resource_hash[reticulum::kFullHashSize]);
    LinkResourceTransfer* startIncomingResource(
        LinkSession& session,
        const uint8_t resource_hash[reticulum::kFullHashSize],
        const uint8_t random_hash[kResourceMapHashLen],
        const uint8_t original_hash[reticulum::kFullHashSize],
        const uint8_t* request_id,
        std::size_t request_id_len,
        const uint8_t* hashmap,
        std::size_t hashmap_len,
        uint32_t data_size,
        uint32_t transfer_size,
        uint32_t part_count,
        uint32_t segment_index,
        uint32_t total_segments,
        uint8_t flags,
        bool encrypted,
        bool compressed,
        bool has_metadata,
        bool split,
        uint32_t now_ms,
        uint32_t window_size);
    bool initialiseOutgoingResource(LinkResourceTransfer& resource,
                                    const uint8_t* request_id,
                                    std::size_t request_id_len,
                                    uint32_t data_size,
                                    uint32_t transfer_size,
                                    uint32_t part_count,
                                    uint8_t flags,
                                    uint32_t now_ms,
                                    uint32_t window_size);
    LinkResourceTransfer* appendOutgoingResource(LinkSession& session,
                                                 LinkResourceTransfer&& resource);
    bool discardLastOutgoingResource(LinkSession& session);
    ResourceWindowRequest buildNextResourceWindowRequest(
        const LinkResourceTransfer& resource) const;
    void noteResourceWindowRequested(LinkResourceTransfer& resource,
                                     bool waiting_for_hashmap,
                                     uint32_t now_ms);
    bool applyIncomingResourceHashmapUpdate(LinkResourceTransfer& resource,
                                            uint32_t segment,
                                            const uint8_t* hashmap,
                                            std::size_t hashmap_len,
                                            std::size_t segment_capacity,
                                            uint32_t now_ms);
    bool recordIncomingResourcePart(LinkResourceTransfer& resource,
                                    const uint8_t* payload,
                                    std::size_t payload_len,
                                    const uint8_t full_hash[reticulum::kFullHashSize],
                                    uint32_t now_ms,
                                    std::size_t* out_matched_index,
                                    bool* out_complete);
    void markResourceComplete(LinkResourceTransfer& resource, uint32_t now_ms);
    ResourceAssemblyResult appendResourceAssemblySegment(
        LinkSession& session,
        LinkResourceTransfer& resource,
        ResourcePayloadBuffer& payload_data,
        uint32_t now_ms);
    bool markOutgoingResourceProofReceived(
        LinkResourceTransfer& resource,
        const uint8_t expected_proof[reticulum::kFullHashSize],
        uint32_t now_ms);
    uint32_t takeResourceMessageId(LinkResourceTransfer& resource);
    void touchResource(LinkResourceTransfer& resource, uint32_t now_ms);

    template <typename Fn>
    void takeTrackedOutgoingResourceMessageIds(LinkSession& session, Fn&& fn)
    {
        for (auto& resource : session.outgoing_resources)
        {
            if (resource.message_id != 0)
            {
                fn(resource.message_id);
                resource.message_id = 0;
            }
        }
    }

    template <typename Fn>
    void takeExpiredOutgoingResourceMessageIds(LinkSession& session,
                                               uint32_t now_ms,
                                               uint32_t ttl_ms,
                                               Fn&& fn)
    {
        for (auto& resource : session.outgoing_resources)
        {
            if (resource.message_id != 0 &&
                (resource.last_activity_ms == 0 ||
                 now_ms - resource.last_activity_ms > ttl_ms))
            {
                fn(resource.message_id);
                resource.message_id = 0;
            }
        }
    }

    template <typename Fn>
    void forEachIncomingResource(LinkSession& session, Fn&& fn)
    {
        for (auto& resource : session.incoming_resources)
        {
            if (!fn(resource))
            {
                break;
            }
        }
    }

    template <typename Fn>
    void forEachSession(Fn&& fn)
    {
        for (auto& session : links_.sessions)
        {
            fn(session);
        }
    }

    template <typename Fn>
    void forEachSession(Fn&& fn) const
    {
        for (const auto& session : links_.sessions)
        {
            fn(session);
        }
    }

  private:
    bool ensureCapacity(std::size_t max_link_sessions,
                        const uint8_t preserve_link_id[reticulum::kTruncatedHashSize]);

    LinkRuntime links_;
};

} // namespace chat::lxmf::runtime
