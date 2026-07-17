/**
 * @file lxmf_path_manager.h
 * @brief Path, packet-filter, proof-route, receipt, and link-relay owner.
 */

#pragma once

#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_transport_runtime.h"

namespace chat::lxmf::runtime
{

class PathManager
{
  public:
    PathManager() = default;
    PathManager(const PathManager&) = delete;
    PathManager& operator=(const PathManager&) = delete;
    PathManager(PathManager&&) = delete;
    PathManager& operator=(PathManager&&) = delete;

    bool isDuplicatePacket(
        const uint8_t packet_hash[reticulum::kFullHashSize]) const;
    void rememberPacket(
        const uint8_t packet_hash[reticulum::kFullHashSize],
        uint32_t now_ms,
        std::size_t max_packet_filter);
    void forgetPacket(
        const uint8_t packet_hash[reticulum::kFullHashSize]);

    void rememberReversePath(
        const uint8_t proof_hash[reticulum::kTruncatedHashSize],
        uint8_t interface_id,
        uint8_t expected_hops,
        uint32_t now_ms,
        std::size_t max_reverse_entries);
    ReverseEntry* findReversePath(
        const uint8_t proof_hash[reticulum::kTruncatedHashSize]);

    PendingPathRequest* findPendingPathRequest(
        const uint8_t destination_hash[reticulum::kTruncatedHashSize]);
    const PendingPathRequest* findPendingPathRequest(
        const uint8_t destination_hash[reticulum::kTruncatedHashSize]) const;
    void notePendingPathRequest(
        const uint8_t destination_hash[reticulum::kTruncatedHashSize],
        uint32_t now_ms,
        std::size_t max_pending_path_requests);
    void resolvePendingPathRequest(
        const uint8_t destination_hash[reticulum::kTruncatedHashSize]);

    void notePendingPingReceipt(
        const uint8_t packet_hash[reticulum::kFullHashSize],
        const uint8_t destination_hash[reticulum::kTruncatedHashSize],
        const uint8_t peer_sig_pub[LxmfIdentity::kSigPubKeySize],
        uint32_t now_ms,
        std::size_t max_pending_ping_receipts);
    PendingPingReceipt* findPendingPingReceipt(
        const uint8_t proof_hash[reticulum::kTruncatedHashSize]);
    void removePendingPingReceipt(
        const uint8_t proof_hash[reticulum::kTruncatedHashSize]);

    void notePendingDeliveryReceipt(
        const uint8_t packet_hash[reticulum::kFullHashSize],
        const uint8_t destination_hash[reticulum::kTruncatedHashSize],
        const uint8_t peer_sig_pub[LxmfIdentity::kSigPubKeySize],
        MessageId message_id,
        uint32_t now_ms,
        std::size_t max_pending_delivery_receipts);
    PendingDeliveryReceipt* findPendingDeliveryReceipt(
        const uint8_t proof_hash[reticulum::kTruncatedHashSize]);
    void removePendingDeliveryReceipt(
        const uint8_t proof_hash[reticulum::kTruncatedHashSize]);

    PathEntry& upsertPath(
        const uint8_t destination_hash[reticulum::kTruncatedHashSize],
        std::size_t max_paths);
    const PathEntry* findPath(
        const uint8_t destination_hash[reticulum::kTruncatedHashSize],
        uint32_t now_ms,
        uint32_t path_ttl_ms) const;
    const PathEntry* findAnyPath(
        const uint8_t destination_hash[reticulum::kTruncatedHashSize]) const;
    void expirePath(
        const uint8_t destination_hash[reticulum::kTruncatedHashSize]);
    void clearPaths();

    LinkRelayEntry& upsertLinkRelay(
        const uint8_t link_id[reticulum::kTruncatedHashSize],
        std::size_t max_link_relays);
    LinkRelayEntry* findLinkRelay(
        const uint8_t link_id[reticulum::kTruncatedHashSize]);
    void removeLinkRelay(
        const uint8_t link_id[reticulum::kTruncatedHashSize]);
    void clearReversePathAndRelays();

    void cull(uint32_t now_ms, const TransportRuntimeLimits& limits);
    void clear();

    template <typename Fn>
    void forEachPath(Fn&& fn) const
    {
        for (const auto& path : transport_.paths)
        {
            fn(path);
        }
    }

    template <typename Fn>
    void forEachPendingPingReceipt(Fn&& fn) const
    {
        for (const auto& receipt : transport_.pending_ping_receipts)
        {
            fn(receipt);
        }
    }

    template <typename Fn>
    void forEachPendingDeliveryReceipt(Fn&& fn) const
    {
        for (const auto& receipt : transport_.pending_delivery_receipts)
        {
            fn(receipt);
        }
    }

  private:
    TransportRuntime transport_;
};

} // namespace chat::lxmf::runtime
