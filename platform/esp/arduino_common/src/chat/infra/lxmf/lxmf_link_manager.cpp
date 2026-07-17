/**
 * @file lxmf_link_manager.cpp
 * @brief Link session owner for the embedded LXMF runtime.
 */

#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_link_manager.h"

#include <algorithm>
#include <cstring>
#include <utility>

namespace chat::lxmf::runtime
{
namespace
{

bool hashesEqual(const uint8_t* a, const uint8_t* b, std::size_t len)
{
    if ((!a || !b) && len != 0)
    {
        return false;
    }
    for (std::size_t i = 0; i < len; ++i)
    {
        if (a[i] != b[i])
        {
            return false;
        }
    }
    return true;
}

} // namespace

std::size_t LinkManager::size() const
{
    return links_.sessions.size();
}

void LinkManager::clear()
{
    links_.sessions.clear();
}

LinkSession* LinkManager::findSession(
    const uint8_t link_id[reticulum::kTruncatedHashSize])
{
    return runtime::findLinkSession(links_, link_id);
}

LinkSession* LinkManager::findOpenSessionByDestination(
    const uint8_t destination_hash[reticulum::kTruncatedHashSize],
    LocalDestinationKind kind)
{
    return runtime::findOpenLinkSessionByDestination(links_,
                                                     destination_hash,
                                                     kind);
}

LinkSession* LinkManager::findActiveSessionByDestination(
    const uint8_t destination_hash[reticulum::kTruncatedHashSize],
    LocalDestinationKind kind)
{
    return runtime::findActiveLinkSessionByDestination(links_,
                                                       destination_hash,
                                                       kind);
}

bool LinkManager::ensureCapacity(
    std::size_t max_link_sessions,
    const uint8_t preserve_link_id[reticulum::kTruncatedHashSize])
{
    if (max_link_sessions == 0 || links_.sessions.size() < max_link_sessions)
    {
        return true;
    }

    auto discard = links_.sessions.begin();
    if (preserve_link_id)
    {
        discard = std::find_if(
            links_.sessions.begin(),
            links_.sessions.end(),
            [preserve_link_id](const LinkSession& candidate)
            {
                return !hashesEqual(candidate.link_id,
                                    preserve_link_id,
                                    sizeof(candidate.link_id));
            });
        if (discard == links_.sessions.end())
        {
            return false;
        }
    }
    links_.sessions.erase(discard);
    return true;
}

LinkSession* LinkManager::appendSession(std::size_t max_link_sessions)
{
    return appendSessionPreserving(max_link_sessions, nullptr);
}

LinkSession* LinkManager::appendSessionPreserving(
    std::size_t max_link_sessions,
    const uint8_t preserve_link_id[reticulum::kTruncatedHashSize])
{
    if (!ensureCapacity(max_link_sessions, preserve_link_id))
    {
        return nullptr;
    }
    links_.sessions.push_back(LinkSession{});
    return &links_.sessions.back();
}

void LinkManager::discardLastSession()
{
    if (!links_.sessions.empty())
    {
        links_.sessions.pop_back();
    }
}

bool LinkManager::closeSession(LinkSession& session,
                               LinkCloseReason reason,
                               uint32_t now_ms)
{
    return runtime::closeLinkSession(session, reason, now_ms);
}

void LinkManager::cullSessionTables(LinkSession& session,
                                    uint32_t now_ms,
                                    const LinkRuntimeLimits& limits)
{
    runtime::cullLinkSessionTables(session, now_ms, limits);
}

void LinkManager::cullResources(LinkSession& session,
                                uint32_t now_ms,
                                const ResourceRuntimeLimits& limits)
{
    runtime::cullLinkResources(session, now_ms, limits);
}

LinkRuntimeMaintenance LinkManager::advanceSessionLifecycle(
    LinkSession& session,
    uint32_t now_ms,
    const LinkRuntimeLimits& limits)
{
    return runtime::advanceLinkSessionLifecycle(session, now_ms, limits);
}

void LinkManager::markSessionStale(LinkSession& session)
{
    runtime::markLinkSessionStale(session);
}

void LinkManager::removeExpiredSessions(uint32_t now_ms,
                                        const LinkRuntimeLimits& limits)
{
    runtime::removeExpiredLinkSessions(links_, now_ms, limits);
}

LinkResourceTransfer* LinkManager::findIncomingResource(
    LinkSession& session,
    const uint8_t resource_hash[reticulum::kFullHashSize])
{
    return runtime::findLinkResource(session.incoming_resources, resource_hash);
}

const LinkResourceTransfer* LinkManager::findIncomingResource(
    const LinkSession& session,
    const uint8_t resource_hash[reticulum::kFullHashSize]) const
{
    return runtime::findLinkResource(session.incoming_resources, resource_hash);
}

LinkResourceTransfer* LinkManager::findOutgoingResource(
    LinkSession& session,
    const uint8_t resource_hash[reticulum::kFullHashSize])
{
    return runtime::findLinkResource(session.outgoing_resources, resource_hash);
}

const LinkResourceTransfer* LinkManager::findOutgoingResource(
    const LinkSession& session,
    const uint8_t resource_hash[reticulum::kFullHashSize]) const
{
    return runtime::findLinkResource(session.outgoing_resources, resource_hash);
}

bool LinkManager::eraseIncomingResource(
    LinkSession& session,
    const uint8_t resource_hash[reticulum::kFullHashSize])
{
    return runtime::eraseLinkResourceByHash(session.incoming_resources, resource_hash);
}

bool LinkManager::eraseOutgoingResource(
    LinkSession& session,
    const uint8_t resource_hash[reticulum::kFullHashSize])
{
    return runtime::eraseLinkResourceByHash(session.outgoing_resources, resource_hash);
}

LinkResourceTransfer* LinkManager::startIncomingResource(
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
    uint32_t window_size)
{
    LinkResourceTransfer resource{};
    if (!runtime::initialiseIncomingResourceTransfer(resource,
                                                     resource_hash,
                                                     random_hash,
                                                     original_hash,
                                                     request_id,
                                                     request_id_len,
                                                     hashmap,
                                                     hashmap_len,
                                                     data_size,
                                                     transfer_size,
                                                     part_count,
                                                     segment_index,
                                                     total_segments,
                                                     flags,
                                                     encrypted,
                                                     compressed,
                                                     has_metadata,
                                                     split,
                                                     now_ms,
                                                     window_size))
    {
        return nullptr;
    }
    session.incoming_resources.push_back(std::move(resource));
    return &session.incoming_resources.back();
}

bool LinkManager::initialiseOutgoingResource(LinkResourceTransfer& resource,
                                             const uint8_t* request_id,
                                             std::size_t request_id_len,
                                             uint32_t data_size,
                                             uint32_t transfer_size,
                                             uint32_t part_count,
                                             uint8_t flags,
                                             uint32_t now_ms,
                                             uint32_t window_size)
{
    return runtime::initialiseOutgoingResourceTransfer(resource,
                                                       request_id,
                                                       request_id_len,
                                                       data_size,
                                                       transfer_size,
                                                       part_count,
                                                       flags,
                                                       now_ms,
                                                       window_size);
}

LinkResourceTransfer* LinkManager::appendOutgoingResource(
    LinkSession& session,
    LinkResourceTransfer&& resource)
{
    session.outgoing_resources.push_back(std::move(resource));
    return &session.outgoing_resources.back();
}

bool LinkManager::discardLastOutgoingResource(LinkSession& session)
{
    if (session.outgoing_resources.empty())
    {
        return false;
    }
    session.outgoing_resources.pop_back();
    return true;
}

ResourceWindowRequest LinkManager::buildNextResourceWindowRequest(
    const LinkResourceTransfer& resource) const
{
    return runtime::buildNextResourceWindowRequest(resource);
}

void LinkManager::noteResourceWindowRequested(LinkResourceTransfer& resource,
                                              bool waiting_for_hashmap,
                                              uint32_t now_ms)
{
    runtime::noteResourceWindowRequest(resource, waiting_for_hashmap, now_ms);
}

bool LinkManager::applyIncomingResourceHashmapUpdate(
    LinkResourceTransfer& resource,
    uint32_t segment,
    const uint8_t* hashmap,
    std::size_t hashmap_len,
    std::size_t segment_capacity,
    uint32_t now_ms)
{
    return runtime::applyResourceHashmapUpdate(resource,
                                               segment,
                                               hashmap,
                                               hashmap_len,
                                               segment_capacity,
                                               now_ms);
}

bool LinkManager::recordIncomingResourcePart(
    LinkResourceTransfer& resource,
    const uint8_t* payload,
    std::size_t payload_len,
    const uint8_t full_hash[reticulum::kFullHashSize],
    uint32_t now_ms,
    std::size_t* out_matched_index,
    bool* out_complete)
{
    return runtime::recordResourcePart(resource,
                                       payload,
                                       payload_len,
                                       full_hash,
                                       now_ms,
                                       out_matched_index,
                                       out_complete);
}

void LinkManager::markResourceComplete(LinkResourceTransfer& resource,
                                       uint32_t now_ms)
{
    runtime::markResourceComplete(resource, now_ms);
}

ResourceAssemblyResult LinkManager::appendResourceAssemblySegment(
    LinkSession& session,
    LinkResourceTransfer& resource,
    ResourcePayloadBuffer& payload_data,
    uint32_t now_ms)
{
    return runtime::appendResourceAssemblySegment(session,
                                                  resource,
                                                  payload_data,
                                                  now_ms);
}

bool LinkManager::markOutgoingResourceProofReceived(
    LinkResourceTransfer& resource,
    const uint8_t expected_proof[reticulum::kFullHashSize],
    uint32_t now_ms)
{
    return runtime::markResourceProofReceived(resource, expected_proof, now_ms);
}

uint32_t LinkManager::takeResourceMessageId(LinkResourceTransfer& resource)
{
    const uint32_t message_id = resource.message_id;
    resource.message_id = 0;
    return message_id;
}

void LinkManager::touchResource(LinkResourceTransfer& resource, uint32_t now_ms)
{
    resource.last_activity_ms = now_ms;
}

} // namespace chat::lxmf::runtime
