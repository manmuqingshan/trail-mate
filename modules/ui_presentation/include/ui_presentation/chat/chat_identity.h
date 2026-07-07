#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace ui::chat
{

constexpr size_t kReticulumDestinationHashSize = 16;

enum class ConversationKind : uint8_t
{
    None = 0,
    DirectPeer,
    Channel,
    Team,
    Broadcast,
    System,
};

enum class ChatProtocolKind : uint8_t
{
    None = 0,
    Meshtastic,
    MeshCore,
    RNode,
    LXMF,
    // Product-facing alias for the legacy LXMF presentation value.
    Reticulum = LXMF,
    TrailMate,
    Mixed,
};

struct ConversationId
{
    ConversationKind kind = ConversationKind::None;
    ChatProtocolKind protocol = ChatProtocolKind::None;

    // DirectPeer: primary = node id, secondary = channel id / slot.
    // Reticulum DirectPeer: primary remains a compatibility projection; when
    // present, reticulum_destination_hash is the stable conversation key.
    // Channel: primary = channel id / slot, secondary = 0.
    // Team: primary = team id / slot, secondary = optional member/group scope.
    // Broadcast: primary = channel id / slot, secondary = 0.
    // System: primary = system row id, secondary = 0.
    uint32_t primary = 0;
    uint32_t secondary = 0;
    bool has_reticulum_destination_hash = false;
    uint8_t reticulum_destination_hash[kReticulumDestinationHashSize] = {};

    bool isValid() const
    {
        return kind != ConversationKind::None;
    }

    bool hasReticulumDestinationHash() const
    {
        return protocol == ChatProtocolKind::Reticulum &&
               has_reticulum_destination_hash;
    }

    bool operator==(const ConversationId& other) const
    {
        if (kind != other.kind ||
            protocol != other.protocol ||
            secondary != other.secondary ||
            has_reticulum_destination_hash !=
                other.has_reticulum_destination_hash)
        {
            return false;
        }
        if (hasReticulumDestinationHash() &&
            other.hasReticulumDestinationHash())
        {
            return memcmp(reticulum_destination_hash,
                          other.reticulum_destination_hash,
                          kReticulumDestinationHashSize) == 0;
        }
        return primary == other.primary;
    }

    bool operator!=(const ConversationId& other) const
    {
        return !(*this == other);
    }
};

} // namespace ui::chat
