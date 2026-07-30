#pragma once

#include "chat/domain/chat_types.h"

#include <cstddef>
#include <cstdint>

namespace chat::storage::v2
{

constexpr uint16_t kStorageSchemaVersion = 2;
constexpr std::size_t kMeshtasticTextMax = 233;
constexpr std::size_t kMeshCoreTextMax = 171;
constexpr std::size_t kReticulumTextMax = 255;
constexpr std::size_t kCatalogPreviewMax = 48;

enum class JournalKind : uint8_t
{
    MessageSegment = 1,
    CatalogSnapshot = 2,
    CatalogDelta = 3,
    ReadStateSnapshot = 4,
    ReadStateDelta = 5,
    StatusSnapshot = 6,
    StatusDelta = 7,
    ReticulumSeen = 8,
    PeerSnapshot = 9,
    PeerDelta = 10,
    ContactSnapshot = 11,
    ContactDelta = 12,
};

struct ChatCatalogProjection
{
    ConversationId conversation{};
    MessageId last_message_id = 0;
    uint32_t message_count = 0;
    uint32_t last_sequence = 0;
    uint32_t last_timestamp = 0;
    uint32_t unread = 0;
    MessageStatus last_status = MessageStatus::Incoming;
    bool deleted = false;
    char preview[kCatalogPreviewMax + 1] = {};
};

struct ChatReadProjection
{
    ConversationId conversation{};
    uint32_t last_read_sequence = 0;
    bool deleted = false;
};

struct ChatStatusProjection
{
    MessageId message_id = 0;
    MessageStatus status = MessageStatus::Queued;
    uint32_t sequence = 0;
};

struct ReticulumSeenProjection
{
    uint8_t hash[kReticulumLxmfHashSize] = {};
};

uint32_t crc32(const void* data, std::size_t len);

MeshProtocol canonicalProtocol(MeshProtocol protocol);
bool supportedProtocol(MeshProtocol protocol);

std::size_t messageSlotSize(MeshProtocol protocol);
std::size_t catalogSlotSize(MeshProtocol protocol);
std::size_t readStateSlotSize(MeshProtocol protocol);
std::size_t statusSlotSize();
std::size_t reticulumSeenSlotSize();

bool encodeMessageSlot(const ChatMessage& message,
                       uint32_t sequence,
                       void* out,
                       std::size_t out_len);
bool decodeMessageSlot(MeshProtocol protocol,
                       const void* data,
                       std::size_t len,
                       ChatMessage& out_message,
                       uint32_t* out_sequence = nullptr);

bool encodeCatalogSlot(MeshProtocol protocol,
                       const ChatCatalogProjection& projection,
                       void* out,
                       std::size_t out_len);
bool decodeCatalogSlot(MeshProtocol protocol,
                       const void* data,
                       std::size_t len,
                       ChatCatalogProjection& out_projection);

bool encodeReadStateSlot(MeshProtocol protocol,
                         const ChatReadProjection& projection,
                         void* out,
                         std::size_t out_len);
bool decodeReadStateSlot(MeshProtocol protocol,
                         const void* data,
                         std::size_t len,
                         ChatReadProjection& out_projection);

bool encodeStatusSlot(const ChatStatusProjection& projection,
                      void* out,
                      std::size_t out_len);
bool decodeStatusSlot(const void* data,
                      std::size_t len,
                      ChatStatusProjection& out_projection);

bool encodeReticulumSeenSlot(const ReticulumSeenProjection& projection,
                             void* out,
                             std::size_t out_len);
bool decodeReticulumSeenSlot(const void* data,
                             std::size_t len,
                             ReticulumSeenProjection& out_projection);

} // namespace chat::storage::v2
