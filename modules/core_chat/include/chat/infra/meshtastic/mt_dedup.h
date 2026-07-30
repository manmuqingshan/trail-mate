/**
 * @file mt_dedup.h
 * @brief Meshtastic packet deduplication
 */

#pragma once

#include "chat/domain/chat_types.h"

#include <cstddef>
#include <cstdint>
#include <map>

namespace chat
{
namespace meshtastic
{

class MtDedup
{
  public:
    static constexpr size_t MAX_CACHE_SIZE = 100;
    static constexpr uint32_t CACHE_TIMEOUT_MS = 300000;

    MtDedup();
    ~MtDedup();

    bool isDuplicate(NodeId from_node, uint32_t packet_id, uint8_t channel_hash);
    void markSeen(NodeId from_node, uint32_t packet_id, uint8_t channel_hash);
    void cleanup();

  private:
    struct PacketKey
    {
        NodeId from;
        uint32_t id;
        uint8_t channel;

        bool operator<(const PacketKey& other) const
        {
            if (from != other.from)
            {
                return from < other.from;
            }
            if (id != other.id)
            {
                return id < other.id;
            }
            return channel < other.channel;
        }
    };

    struct PacketEntry
    {
        uint32_t timestamp;
    };

    std::map<PacketKey, PacketEntry> cache_;
    uint32_t last_cleanup_;
};

} // namespace meshtastic
} // namespace chat
