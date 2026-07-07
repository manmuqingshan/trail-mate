/**
 * @file lxmf_resource_runtime.h
 * @brief Resource runtime lifecycle helpers for the embedded Reticulum/LXMF adapter
 */

#pragma once

#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_runtime_state.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace chat::lxmf::runtime
{

constexpr std::size_t kResourceMapHashLen = 4;

struct ResourceRuntimeLimits
{
    uint32_t resource_transfer_ttl_ms = 0;
};

struct ResourceWindowRequest
{
    bool valid = false;
    bool needs_more_hashmap = false;
    std::array<uint8_t, kResourceMapHashLen> last_known_hash = {};
    std::vector<std::array<uint8_t, kResourceMapHashLen>> requested_hashes;
};

enum class ResourceAssemblyResult : uint8_t
{
    Complete = 0,
    WaitingForNextSegment = 1,
    Rejected = 2
};

LinkResourceTransfer* findLinkResource(
    std::vector<LinkResourceTransfer>& resources,
    const uint8_t resource_hash[reticulum::kFullHashSize]);
const LinkResourceTransfer* findLinkResource(
    const std::vector<LinkResourceTransfer>& resources,
    const uint8_t resource_hash[reticulum::kFullHashSize]);
bool eraseLinkResourceByHash(std::vector<LinkResourceTransfer>& resources,
                             const uint8_t resource_hash[reticulum::kFullHashSize]);

LinkResourceAssembly* findLinkResourceAssembly(
    LinkSession& session,
    const uint8_t original_hash[reticulum::kFullHashSize]);
bool eraseLinkResourceAssemblyByOriginalHash(
    LinkSession& session,
    const uint8_t original_hash[reticulum::kFullHashSize]);

bool initialiseIncomingResourceTransfer(
    LinkResourceTransfer& resource,
    const uint8_t resource_hash[reticulum::kFullHashSize],
    const uint8_t random_hash[kResourceMapHashLen],
    const uint8_t original_hash[reticulum::kFullHashSize],
    std::vector<uint8_t>&& request_id,
    std::vector<uint8_t>&& hashmap,
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

bool initialiseOutgoingResourceTransfer(
    LinkResourceTransfer& resource,
    const uint8_t* request_id,
    std::size_t request_id_len,
    uint32_t data_size,
    uint32_t transfer_size,
    uint32_t part_count,
    uint8_t flags,
    uint32_t now_ms,
    uint32_t window_size);

ResourceWindowRequest buildNextResourceWindowRequest(
    const LinkResourceTransfer& resource);
void noteResourceWindowRequest(LinkResourceTransfer& resource,
                               bool waiting_for_hashmap,
                               uint32_t now_ms);

bool applyResourceHashmapUpdate(LinkResourceTransfer& resource,
                                uint32_t segment,
                                const std::vector<uint8_t>& hashmap,
                                std::size_t segment_capacity,
                                uint32_t now_ms);

bool recordResourcePart(LinkResourceTransfer& resource,
                        const uint8_t* payload,
                        std::size_t payload_len,
                        const uint8_t full_hash[reticulum::kFullHashSize],
                        uint32_t now_ms,
                        std::size_t* out_matched_index = nullptr,
                        bool* out_complete = nullptr);

ResourceAssemblyResult appendResourceAssemblySegment(
    LinkSession& session,
    LinkResourceTransfer& resource,
    std::vector<uint8_t>& payload_data,
    uint32_t now_ms);

void markResourceComplete(LinkResourceTransfer& resource, uint32_t now_ms);
bool markResourceProofReceived(LinkResourceTransfer& resource,
                               const uint8_t expected_proof[reticulum::kFullHashSize],
                               uint32_t now_ms);
void cullLinkResources(LinkSession& session,
                       uint32_t now_ms,
                       const ResourceRuntimeLimits& limits);

} // namespace chat::lxmf::runtime
