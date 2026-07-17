/**
 * @file lxmf_destination_registry.cpp
 * @brief Destination and identity registry owner for the embedded LXMF runtime.
 */

#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_destination_registry.h"

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

void copyHash(uint8_t* out, const uint8_t* in, std::size_t len)
{
    if (!out || !in || len == 0)
    {
        return;
    }
    std::memcpy(out, in, len);
}

} // namespace

std::size_t DestinationRegistry::size() const
{
    return peers_.size();
}

void DestinationRegistry::clear()
{
    peers_.clear();
}

PeerInfo* DestinationRegistry::findByNodeId(NodeId node_id)
{
    if (node_id == 0)
    {
        return nullptr;
    }
    for (auto& peer : peers_)
    {
        if (peer.node_id == node_id)
        {
            return &peer;
        }
    }
    return nullptr;
}

const PeerInfo* DestinationRegistry::findByNodeId(NodeId node_id) const
{
    if (node_id == 0)
    {
        return nullptr;
    }
    for (const auto& peer : peers_)
    {
        if (peer.node_id == node_id)
        {
            return &peer;
        }
    }
    return nullptr;
}

PeerInfo* DestinationRegistry::findByDestinationHash(
    const uint8_t hash[reticulum::kTruncatedHashSize])
{
    if (!hash)
    {
        return nullptr;
    }
    for (auto& peer : peers_)
    {
        if (hashesEqual(peer.destination_hash,
                        hash,
                        reticulum::kTruncatedHashSize))
        {
            return &peer;
        }
    }
    return nullptr;
}

const PeerInfo* DestinationRegistry::findByDestinationHash(
    const uint8_t hash[reticulum::kTruncatedHashSize]) const
{
    if (!hash)
    {
        return nullptr;
    }
    for (const auto& peer : peers_)
    {
        if (hashesEqual(peer.destination_hash,
                        hash,
                        reticulum::kTruncatedHashSize))
        {
            return &peer;
        }
    }
    return nullptr;
}

const PeerInfo* DestinationRegistry::findByIdentityHash(
    const uint8_t hash[reticulum::kTruncatedHashSize]) const
{
    if (!hash)
    {
        return nullptr;
    }
    for (const auto& peer : peers_)
    {
        if (hashesEqual(peer.identity_hash,
                        hash,
                        reticulum::kTruncatedHashSize))
        {
            return &peer;
        }
    }
    return nullptr;
}

PeerInfo& DestinationRegistry::upsertDestination(
    const uint8_t destination_hash[reticulum::kTruncatedHashSize])
{
    if (PeerInfo* existing = findByDestinationHash(destination_hash))
    {
        return *existing;
    }

    peers_.push_back(PeerInfo{});
    PeerInfo& peer = peers_.back();
    copyHash(peer.destination_hash,
             destination_hash,
             reticulum::kTruncatedHashSize);
    peer.node_id = reticulum::nodeIdFromDestinationHash(destination_hash);
    return peer;
}

} // namespace chat::lxmf::runtime
