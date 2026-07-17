/**
 * @file lxmf_path_manager.cpp
 * @brief Path, packet-filter, proof-route, receipt, and link-relay owner.
 */

#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_path_manager.h"

#include <algorithm>
#include <cstring>

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

bool PathManager::isDuplicatePacket(
    const uint8_t packet_hash[reticulum::kFullHashSize]) const
{
    return runtime::isDuplicatePacket(transport_, packet_hash);
}

void PathManager::rememberPacket(
    const uint8_t packet_hash[reticulum::kFullHashSize],
    uint32_t now_ms,
    std::size_t max_packet_filter)
{
    runtime::rememberPacket(transport_, packet_hash, now_ms, max_packet_filter);
}

void PathManager::forgetPacket(
    const uint8_t packet_hash[reticulum::kFullHashSize])
{
    runtime::forgetPacket(transport_, packet_hash);
}

void PathManager::rememberReversePath(
    const uint8_t proof_hash[reticulum::kTruncatedHashSize],
    uint8_t interface_id,
    uint8_t expected_hops,
    uint32_t now_ms,
    std::size_t max_reverse_entries)
{
    runtime::rememberReversePath(transport_,
                                 proof_hash,
                                 interface_id,
                                 expected_hops,
                                 now_ms,
                                 max_reverse_entries);
}

ReverseEntry* PathManager::findReversePath(
    const uint8_t proof_hash[reticulum::kTruncatedHashSize])
{
    return runtime::findReversePath(transport_, proof_hash);
}

PendingPathRequest* PathManager::findPendingPathRequest(
    const uint8_t destination_hash[reticulum::kTruncatedHashSize])
{
    return runtime::findPendingPathRequest(transport_, destination_hash);
}

const PendingPathRequest* PathManager::findPendingPathRequest(
    const uint8_t destination_hash[reticulum::kTruncatedHashSize]) const
{
    return runtime::findPendingPathRequest(transport_, destination_hash);
}

void PathManager::notePendingPathRequest(
    const uint8_t destination_hash[reticulum::kTruncatedHashSize],
    uint32_t now_ms,
    std::size_t max_pending_path_requests)
{
    runtime::notePendingPathRequest(transport_,
                                    destination_hash,
                                    now_ms,
                                    max_pending_path_requests);
}

void PathManager::resolvePendingPathRequest(
    const uint8_t destination_hash[reticulum::kTruncatedHashSize])
{
    runtime::resolvePendingPathRequest(transport_, destination_hash);
}

void PathManager::notePendingPingReceipt(
    const uint8_t packet_hash[reticulum::kFullHashSize],
    const uint8_t destination_hash[reticulum::kTruncatedHashSize],
    const uint8_t peer_sig_pub[LxmfIdentity::kSigPubKeySize],
    uint32_t now_ms,
    std::size_t max_pending_ping_receipts)
{
    runtime::notePendingPingReceipt(transport_,
                                    packet_hash,
                                    destination_hash,
                                    peer_sig_pub,
                                    now_ms,
                                    max_pending_ping_receipts);
}

PendingPingReceipt* PathManager::findPendingPingReceipt(
    const uint8_t proof_hash[reticulum::kTruncatedHashSize])
{
    return runtime::findPendingPingReceipt(transport_, proof_hash);
}

void PathManager::removePendingPingReceipt(
    const uint8_t proof_hash[reticulum::kTruncatedHashSize])
{
    runtime::removePendingPingReceipt(transport_, proof_hash);
}

void PathManager::notePendingDeliveryReceipt(
    const uint8_t packet_hash[reticulum::kFullHashSize],
    const uint8_t destination_hash[reticulum::kTruncatedHashSize],
    const uint8_t peer_sig_pub[LxmfIdentity::kSigPubKeySize],
    MessageId message_id,
    uint32_t now_ms,
    std::size_t max_pending_delivery_receipts)
{
    runtime::notePendingDeliveryReceipt(transport_,
                                        packet_hash,
                                        destination_hash,
                                        peer_sig_pub,
                                        message_id,
                                        now_ms,
                                        max_pending_delivery_receipts);
}

PendingDeliveryReceipt* PathManager::findPendingDeliveryReceipt(
    const uint8_t proof_hash[reticulum::kTruncatedHashSize])
{
    return runtime::findPendingDeliveryReceipt(transport_, proof_hash);
}

void PathManager::removePendingDeliveryReceipt(
    const uint8_t proof_hash[reticulum::kTruncatedHashSize])
{
    runtime::removePendingDeliveryReceipt(transport_, proof_hash);
}

PathEntry& PathManager::upsertPath(
    const uint8_t destination_hash[reticulum::kTruncatedHashSize],
    std::size_t max_paths)
{
    return runtime::upsertPath(transport_, destination_hash, max_paths);
}

const PathEntry* PathManager::findPath(
    const uint8_t destination_hash[reticulum::kTruncatedHashSize],
    uint32_t now_ms,
    uint32_t path_ttl_ms) const
{
    const PathEntry* path = runtime::findPath(transport_, destination_hash);
    return path && !runtime::pathExpired(*path, now_ms, path_ttl_ms)
               ? path
               : nullptr;
}

const PathEntry* PathManager::findAnyPath(
    const uint8_t destination_hash[reticulum::kTruncatedHashSize]) const
{
    return runtime::findPath(transport_, destination_hash);
}

void PathManager::expirePath(
    const uint8_t destination_hash[reticulum::kTruncatedHashSize])
{
    if (!destination_hash)
    {
        return;
    }
    transport_.paths.erase(
        std::remove_if(transport_.paths.begin(),
                       transport_.paths.end(),
                       [destination_hash](const PathEntry& path)
                       {
                           return hashesEqual(path.destination_hash,
                                              destination_hash,
                                              sizeof(path.destination_hash));
                       }),
        transport_.paths.end());
    resolvePendingPathRequest(destination_hash);
}

void PathManager::clearPaths()
{
    transport_.paths.clear();
}

LinkRelayEntry& PathManager::upsertLinkRelay(
    const uint8_t link_id[reticulum::kTruncatedHashSize],
    std::size_t max_link_relays)
{
    return runtime::upsertLinkRelay(transport_, link_id, max_link_relays);
}

LinkRelayEntry* PathManager::findLinkRelay(
    const uint8_t link_id[reticulum::kTruncatedHashSize])
{
    return runtime::findLinkRelay(transport_, link_id);
}

void PathManager::removeLinkRelay(
    const uint8_t link_id[reticulum::kTruncatedHashSize])
{
    if (!link_id)
    {
        return;
    }
    transport_.link_relays.erase(
        std::remove_if(transport_.link_relays.begin(),
                       transport_.link_relays.end(),
                       [link_id](const LinkRelayEntry& relay)
                       {
                           return hashesEqual(relay.link_id,
                                              link_id,
                                              sizeof(relay.link_id));
                       }),
        transport_.link_relays.end());
}

void PathManager::clearReversePathAndRelays()
{
    transport_.reverse_table.clear();
    transport_.pending_path_requests.clear();
    transport_.link_relays.clear();
}

void PathManager::cull(uint32_t now_ms, const TransportRuntimeLimits& limits)
{
    runtime::cullTransportRuntime(transport_, now_ms, limits);
}

void PathManager::clear()
{
    transport_ = TransportRuntime{};
}

} // namespace chat::lxmf::runtime
