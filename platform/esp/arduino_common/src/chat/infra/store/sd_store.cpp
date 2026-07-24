/**
 * @file sd_store.cpp
 * @brief Protocol-partitioned append-only ESP chat storage.
 */

#include "platform/esp/arduino_common/chat/infra/store/sd_store.h"

#include "platform/esp/arduino_common/storage/scoped_state_lock.h"
#include "platform/esp/arduino_common/storage/sd_card_runtime.h"

#if defined(ARDUINO)
#include <Arduino.h>
#else
#include <esp_timer.h>
#endif

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace chat
{
namespace
{
namespace storage_runtime = ::platform::esp::arduino_common::storage;
namespace storage_v2 = ::chat::storage::v2;

uint32_t monotonic_millis()
{
#if defined(ARDUINO)
    return millis();
#else
    return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
#endif
}

#if defined(ARDUINO)
#define CHAT_STORE_LOG(...) Serial.printf(__VA_ARGS__)
#else
#define CHAT_STORE_LOG(...) std::printf(__VA_ARGS__)
#endif

constexpr MeshProtocol kProtocols[] = {
    MeshProtocol::Meshtastic,
    MeshProtocol::MeshCore,
    MeshProtocol::Reticulum,
};

std::size_t protocolIndex(MeshProtocol protocol)
{
    if (protocol == MeshProtocol::MeshCore)
    {
        return 1;
    }
    if (protocol == MeshProtocol::Reticulum ||
        protocol == MeshProtocol::RNode)
    {
        return 2;
    }
    return 0;
}

bool sameConversationKey(const ConversationId& lhs,
                         const ConversationId& rhs)
{
    const MeshProtocol lhs_protocol =
        lhs.protocol == MeshProtocol::RNode ? MeshProtocol::Reticulum
                                            : lhs.protocol;
    const MeshProtocol rhs_protocol =
        rhs.protocol == MeshProtocol::RNode ? MeshProtocol::Reticulum
                                            : rhs.protocol;
    if (lhs_protocol != rhs_protocol || lhs.channel != rhs.channel)
    {
        return false;
    }
    if (lhs_protocol == MeshProtocol::Reticulum)
    {
        const bool lhs_has_destination =
            hasReticulumDestinationIdentity(lhs.reticulum_identity);
        const bool rhs_has_destination =
            hasReticulumDestinationIdentity(rhs.reticulum_identity);
        if (lhs_has_destination || rhs_has_destination)
        {
            return lhs_has_destination && rhs_has_destination &&
                   sameReticulumDestinationHash(lhs.reticulum_identity,
                                                rhs.reticulum_identity);
        }
    }
    return lhs.peer == rhs.peer;
}

bool copyTextPreview(char* out,
                     std::size_t out_len,
                     const std::string& text)
{
    if (!out || out_len == 0U)
    {
        return false;
    }
    const std::size_t length = std::min(out_len - 1U, text.size());
    if (length != 0U)
    {
        std::memcpy(out, text.data(), length);
    }
    out[length] = '\0';
    return true;
}

bool hasSuffix(const char* value, const char* suffix)
{
    if (!value || !suffix)
    {
        return false;
    }
    const std::size_t value_len = std::strlen(value);
    const std::size_t suffix_len = std::strlen(suffix);
    return value_len >= suffix_len &&
           std::memcmp(value + value_len - suffix_len,
                       suffix,
                       suffix_len) == 0;
}

bool replaceSnapshot(const char* temp_path, const char* final_path)
{
    if (!temp_path || !final_path)
    {
        return false;
    }
    char backup_path[160] = {};
    std::snprintf(backup_path,
                  sizeof(backup_path),
                  "%s.bak",
                  final_path);
    return storage_v2::replaceFileAtomically(temp_path,
                                             final_path,
                                             backup_path);
}

void hashToHex(const uint8_t* hash, char* out, std::size_t out_len)
{
    static constexpr char kHex[] = "0123456789abcdef";
    if (!hash || !out || out_len < kReticulumPeerHashSize * 2U + 1U)
    {
        return;
    }
    for (std::size_t index = 0; index < kReticulumPeerHashSize; ++index)
    {
        out[index * 2U] = kHex[(hash[index] >> 4U) & 0x0FU];
        out[index * 2U + 1U] = kHex[hash[index] & 0x0FU];
    }
    out[kReticulumPeerHashSize * 2U] = '\0';
}

} // namespace

SdStore::SdStore()
    : mutex_(xSemaphoreCreateRecursiveMutex())
{
    scratch_.resize(kScratchCapacity);
    catalog_.reserve(64);
    read_state_.reserve(64);
    statuses_.reserve(256);
    seen_hot_.reserve(256);
    CHAT_STORE_LOG("[ChatStoreV2] constructed ready=0 hydration=pending root=%s\n", kRoot);
}

SdStore::~SdStore()
{
    if (mutex_)
    {
        vSemaphoreDelete(mutex_);
        mutex_ = nullptr;
    }
}

bool SdStore::hydrateFromStorage()
{
    if (ready_.load(std::memory_order_acquire))
    {
        return true;
    }
    if (hydrating_.exchange(true, std::memory_order_acq_rel))
    {
        return false;
    }

    const uint32_t started_ms = monotonic_millis();
    storage_runtime::ScopedRecursiveStateLock state_lock(mutex_, portMAX_DELAY);
    const bool ok = state_lock.locked() && storage_runtime::sd_card_ready() &&
                    ensureLayout() && loadRuntimeState();
    if (ok)
    {
        ready_.store(true, std::memory_order_release);
    }
    hydrating_.store(false, std::memory_order_release);
    CHAT_STORE_LOG("[ChatStoreV2] hydration ready=%u elapsed_ms=%lu conversations=%u statuses=%u seen_hot=%u\n",
                   ok ? 1U : 0U,
                   static_cast<unsigned long>(monotonic_millis() - started_ms),
                   static_cast<unsigned>(catalog_.size()),
                   static_cast<unsigned>(statuses_.size()),
                   static_cast<unsigned>(seen_hot_.size()));
    return ok;
}

bool SdStore::compactDeferred()
{
    if (!ready_.load(std::memory_order_acquire))
    {
        return false;
    }
    storage_runtime::ScopedRecursiveStateLock state_lock(mutex_, portMAX_DELAY);
    if (!state_lock.locked())
    {
        return false;
    }
    const uint32_t started_ms = monotonic_millis();
    bool ok = true;
    for (MeshProtocol protocol : kProtocols)
    {
        if (!compactProtocolProjections(protocol))
        {
            projection_dirty_[protocolIndex(protocol)] = true;
            ok = false;
        }
    }
    CHAT_STORE_LOG("[ChatStoreV2] deferred_compaction ok=%u elapsed_ms=%lu\n",
                   ok ? 1U : 0U,
                   static_cast<unsigned long>(monotonic_millis() - started_ms));
    return ok;
}

void SdStore::append(const ChatMessage& msg)
{
    if (!appendDurably(msg))
    {
        CHAT_STORE_LOG("[ChatStoreV2] append deferred protocol=%s msg=%08lX\n",
                       protocolSlug(msg.protocol),
                       static_cast<unsigned long>(msg.msg_id));
    }
}

bool SdStore::appendDurably(const ChatMessage& msg)
{
    if (!ready_.load(std::memory_order_acquire))
    {
        return false;
    }
    return appendInternal(msg, false);
}

bool SdStore::appendIncomingDurably(const ChatMessage& msg)
{
    if (!ready_.load(std::memory_order_acquire))
    {
        return false;
    }
    return appendInternal(msg, true);
}

bool SdStore::appendInternal(const ChatMessage& input, bool incoming_commit)
{
    if (!ready_.load(std::memory_order_acquire))
    {
        return false;
    }
    storage_runtime::ScopedRecursiveStateLock state_lock(mutex_);
    if (!state_lock.locked() || !ready_)
    {
        return false;
    }

    ChatMessage message = input;
    message.protocol = normalizeProtocol(message.protocol);
    if (!storage_v2::supportedProtocol(message.protocol) ||
        message.text.size() >
            (message.protocol == MeshProtocol::Meshtastic
                 ? storage_v2::kMeshtasticTextMax
             : message.protocol == MeshProtocol::MeshCore
                 ? storage_v2::kMeshCoreTextMax
                 : storage_v2::kReticulumTextMax) ||
        !ensureProtocolLayout(message.protocol))
    {
        return false;
    }

    const ConversationId conversation = conversationIdForMessage(message);
    bool already_stored = false;
    uint32_t stored_count = 0;
    if (!storedMessageMatches(message, &already_stored, &stored_count))
    {
        return false;
    }

    if (!already_stored)
    {
        const uint32_t sequence = stored_count + 1U;
        if (!appendMessageRecord(message, sequence))
        {
            return false;
        }
        stored_count = sequence;
    }

    if (incoming_commit && chat::hasReticulumLxmfMessageHash(message) &&
        !rememberReticulumHash(message.reticulum_lxmf_hash))
    {
        // The message record is authoritative and retry detection above is
        // idempotent. Do not publish until its Reticulum dedup identity is also
        // durable.
        return false;
    }

    storage_v2::ChatCatalogProjection* projection = findCatalog(conversation);
    const bool new_projection = projection == nullptr;
    if (!projection)
    {
        storage_v2::ChatCatalogProjection created{};
        created.conversation = conversation;
        catalog_.push_back(created);
        projection = &catalog_.back();
    }
    const bool projection_was_current =
        projection->message_count == stored_count &&
        projection->last_message_id == message.msg_id;
    if (!projection_was_current || new_projection)
    {
        projection->conversation = conversation;
        projection->message_count = stored_count;
        projection->last_sequence = stored_count;
        projection->last_message_id = message.msg_id;
        projection->last_timestamp = message.timestamp;
        projection->last_status = message.status;
        projection->deleted = false;
        const storage_v2::ChatReadProjection* read_state =
            findReadState(conversation);
        projection->unread = countUnreadAfter(
            conversation,
            read_state ? read_state->last_read_sequence : 0U);
        copyTextPreview(projection->preview,
                        sizeof(projection->preview),
                        message.text);
        if (!appendCatalogProjection(*projection))
        {
            projection_dirty_[protocolIndex(message.protocol)] = true;
            CHAT_STORE_LOG("[ChatStoreV2] projection deferred protocol=%s msg=%08lX authoritative=1\n",
                           protocolSlug(message.protocol),
                           static_cast<unsigned long>(message.msg_id));
        }
    }

    CHAT_STORE_LOG("[ChatStoreV2] commit protocol=%s msg=%08lX seq=%lu duplicate=%u publish=1\n",
                   protocolSlug(message.protocol),
                   static_cast<unsigned long>(message.msg_id),
                   static_cast<unsigned long>(stored_count),
                   already_stored ? 1U : 0U);
    return true;
}

std::vector<ChatMessage> SdStore::loadRecent(const ConversationId& conv,
                                             std::size_t n)
{
    if (!ready_.load(std::memory_order_acquire))
    {
        return {};
    }
    return loadPageFromLatest(conv, 0, n, nullptr);
}

std::vector<ChatMessage> SdStore::loadPageFromLatest(
    const ConversationId& input,
    std::size_t offset_from_latest,
    std::size_t limit,
    std::size_t* total)
{
    if (!ready_.load(std::memory_order_acquire))
    {
        if (total)
        {
            *total = 0U;
        }
        return {};
    }
    storage_runtime::ScopedRecursiveStateLock state_lock(mutex_);
    if (!state_lock.locked() || !ready_)
    {
        if (total)
        {
            *total = 0U;
        }
        return {};
    }
    ConversationId conversation = input;
    conversation.protocol = normalizeProtocol(conversation.protocol);
    const uint32_t count = messageCountOnDisk(conversation);
    if (total)
    {
        *total = count;
    }

    std::vector<ChatMessage> result;
    if (limit == 0U || offset_from_latest >= count)
    {
        return result;
    }
    const uint32_t end = count - static_cast<uint32_t>(offset_from_latest);
    const uint32_t start =
        end > limit ? end - static_cast<uint32_t>(limit) : 0U;
    result.reserve(end - start);
    for (uint32_t ordinal = start; ordinal < end; ++ordinal)
    {
        ChatMessage message{};
        if (readMessageByOrdinal(conversation, ordinal, message))
        {
            result.push_back(std::move(message));
        }
    }
    return result;
}

std::vector<ConversationMeta> SdStore::loadConversationPage(
    std::size_t offset,
    std::size_t limit,
    std::size_t* total)
{
    if (!ready_.load(std::memory_order_acquire))
    {
        if (total)
        {
            *total = 0U;
        }
        return {};
    }
    storage_runtime::ScopedRecursiveStateLock state_lock(mutex_);
    if (!state_lock.locked() || !ready_)
    {
        if (total)
        {
            *total = 0U;
        }
        return {};
    }
    std::vector<ConversationMeta> result;
    result.reserve(catalog_.size());
    for (const storage_v2::ChatCatalogProjection& projection : catalog_)
    {
        if (!projection.deleted && projection.message_count != 0U)
        {
            result.push_back(makeMeta(projection));
        }
    }
    std::stable_sort(result.begin(),
                     result.end(),
                     [](const ConversationMeta& lhs,
                        const ConversationMeta& rhs)
                     {
                         return lhs.last_timestamp > rhs.last_timestamp;
                     });
    if (total)
    {
        *total = result.size();
    }
    if (offset >= result.size())
    {
        return {};
    }
    const std::size_t end =
        limit == 0U ? result.size()
                    : std::min(result.size(), offset + limit);
    return std::vector<ConversationMeta>(
        result.begin() + static_cast<std::ptrdiff_t>(offset),
        result.begin() + static_cast<std::ptrdiff_t>(end));
}

std::vector<ConversationMeta> SdStore::loadConversationPageForProtocol(
    MeshProtocol protocol,
    std::size_t offset,
    std::size_t limit,
    std::size_t* total)
{
    if (!ready_.load(std::memory_order_acquire))
    {
        if (total)
        {
            *total = 0U;
        }
        return {};
    }
    storage_runtime::ScopedRecursiveStateLock state_lock(mutex_);
    if (!state_lock.locked() || !ready_)
    {
        if (total)
        {
            *total = 0U;
        }
        return {};
    }
    protocol = normalizeProtocol(protocol);
    std::vector<ConversationMeta> result;
    for (const storage_v2::ChatCatalogProjection& projection : catalog_)
    {
        if (!projection.deleted && projection.message_count != 0U &&
            sameProtocol(projection.conversation.protocol, protocol))
        {
            result.push_back(makeMeta(projection));
        }
    }
    std::stable_sort(result.begin(),
                     result.end(),
                     [](const ConversationMeta& lhs,
                        const ConversationMeta& rhs)
                     {
                         return lhs.last_timestamp > rhs.last_timestamp;
                     });
    if (total)
    {
        *total = result.size();
    }
    if (offset >= result.size())
    {
        return {};
    }
    const std::size_t end =
        limit == 0U ? result.size()
                    : std::min(result.size(), offset + limit);
    return std::vector<ConversationMeta>(
        result.begin() + static_cast<std::ptrdiff_t>(offset),
        result.begin() + static_cast<std::ptrdiff_t>(end));
}

bool SdStore::setUnread(const ConversationId& input, int unread)
{
    if (!ready_.load(std::memory_order_acquire))
    {
        return false;
    }
    storage_runtime::ScopedRecursiveStateLock state_lock(mutex_);
    if (!state_lock.locked() || !ready_)
    {
        return false;
    }
    ConversationId conversation = input;
    conversation.protocol = normalizeProtocol(conversation.protocol);
    storage_v2::ChatCatalogProjection* catalog = findCatalog(conversation);
    if (!catalog)
    {
        return unread == 0;
    }
    const uint32_t bounded_unread =
        unread <= 0 ? 0U : static_cast<uint32_t>(unread);
    storage_v2::ChatReadProjection projection{};
    projection.conversation = conversation;
    projection.last_read_sequence =
        sequenceForUnread(conversation, bounded_unread);
    if (!appendReadProjection(projection))
    {
        return false;
    }
    storage_v2::ChatReadProjection* current = findReadState(conversation);
    if (current)
    {
        *current = projection;
    }
    else
    {
        read_state_.push_back(projection);
    }
    catalog->unread = countUnreadAfter(conversation,
                                       projection.last_read_sequence);
    if (!appendCatalogProjection(*catalog))
    {
        projection_dirty_[protocolIndex(conversation.protocol)] = true;
    }
    return true;
}

int SdStore::getUnread(const ConversationId& input) const
{
    if (!ready_.load(std::memory_order_acquire))
    {
        return 0;
    }
    storage_runtime::ScopedRecursiveStateLock state_lock(mutex_);
    if (!state_lock.locked() || !ready_)
    {
        return 0;
    }
    ConversationId conversation = input;
    conversation.protocol = normalizeProtocol(conversation.protocol);
    const storage_v2::ChatCatalogProjection* projection =
        findCatalog(conversation);
    return projection ? static_cast<int>(projection->unread) : 0;
}

void SdStore::clearConversation(const ConversationId& input)
{
    if (!ready_.load(std::memory_order_acquire))
    {
        return;
    }
    storage_runtime::ScopedRecursiveStateLock state_lock(mutex_);
    if (!state_lock.locked() || !ready_)
    {
        return;
    }
    ConversationId conversation = input;
    conversation.protocol = normalizeProtocol(conversation.protocol);
    char path[128]{};
    buildConversationDirectory(conversation, path, sizeof(path));
    if (!removeTree(path))
    {
        return;
    }

    storage_v2::ChatCatalogProjection tombstone{};
    if (const storage_v2::ChatCatalogProjection* existing =
            findCatalog(conversation))
    {
        tombstone = *existing;
    }
    tombstone.conversation = conversation;
    tombstone.deleted = true;
    (void)appendCatalogProjection(tombstone);

    storage_v2::ChatReadProjection read_tombstone{};
    read_tombstone.conversation = conversation;
    read_tombstone.deleted = true;
    (void)appendReadProjection(read_tombstone);

    catalog_.erase(std::remove_if(catalog_.begin(),
                                  catalog_.end(),
                                  [&](const auto& value)
                                  {
                                      return sameConversationKey(
                                          value.conversation,
                                          conversation);
                                  }),
                   catalog_.end());
    read_state_.erase(std::remove_if(read_state_.begin(),
                                     read_state_.end(),
                                     [&](const auto& value)
                                     {
                                         return sameConversationKey(
                                             value.conversation,
                                             conversation);
                                     }),
                      read_state_.end());
}

void SdStore::clearAll()
{
    if (!ready_.load(std::memory_order_acquire))
    {
        return;
    }
    storage_runtime::ScopedRecursiveStateLock state_lock(mutex_);
    if (!state_lock.locked())
    {
        return;
    }
    for (MeshProtocol protocol : kProtocols)
    {
        (void)removeTree(protocolRoot(protocol));
    }
    catalog_.clear();
    read_state_.clear();
    statuses_.clear();
    seen_hot_.clear();
    std::memset(projection_dirty_, 0, sizeof(projection_dirty_));
    ready_ = ensureLayout();
}

bool SdStore::updateMessageStatus(MessageId msg_id, MessageStatus status)
{
    if (!ready_.load(std::memory_order_acquire))
    {
        return false;
    }
    ChatMessage message{};
    if (!getMessage(msg_id, &message))
    {
        return false;
    }
    return updateMessageStatusForProtocol(msg_id, message.protocol, status);
}

bool SdStore::updateMessageStatusForProtocol(MessageId msg_id,
                                             MeshProtocol protocol,
                                             MessageStatus status)
{
    if (!ready_.load(std::memory_order_acquire))
    {
        return false;
    }
    storage_runtime::ScopedRecursiveStateLock state_lock(mutex_);
    if (!state_lock.locked() || !ready_)
    {
        return false;
    }
    protocol = normalizeProtocol(protocol);
    if (msg_id == 0U || !storage_v2::supportedProtocol(protocol))
    {
        return false;
    }
    ChatMessage message{};
    if (!getMessageForProtocol(msg_id, protocol, &message) ||
        message.from != 0U)
    {
        return false;
    }

    storage_v2::ChatStatusProjection projection{};
    projection.message_id = msg_id;
    projection.status = status;
    if (const storage_v2::ChatStatusProjection* current =
            findStatus(msg_id, protocol))
    {
        projection.sequence = current->sequence + 1U;
    }
    else
    {
        projection.sequence = 1U;
    }
    if (!appendStatusProjection(protocol, projection))
    {
        return false;
    }
    storage_v2::ChatStatusProjection* current = findStatus(msg_id, protocol);
    if (current)
    {
        *current = projection;
    }
    else
    {
        ProtocolStatusProjection state{};
        state.protocol = protocol;
        state.value = projection;
        statuses_.push_back(state);
    }

    const ConversationId conversation = conversationIdForMessage(message);
    storage_v2::ChatCatalogProjection* catalog = findCatalog(conversation);
    if (catalog && catalog->last_message_id == msg_id)
    {
        catalog->last_status = status;
        if (!appendCatalogProjection(*catalog))
        {
            projection_dirty_[protocolIndex(protocol)] = true;
        }
    }
    return true;
}

bool SdStore::getMessage(MessageId msg_id, ChatMessage* out) const
{
    if (!ready_.load(std::memory_order_acquire))
    {
        return false;
    }
    storage_runtime::ScopedRecursiveStateLock state_lock(mutex_);
    if (!state_lock.locked() || !ready_)
    {
        return false;
    }
    for (MeshProtocol protocol : kProtocols)
    {
        if (getMessageForProtocol(msg_id, protocol, out))
        {
            return true;
        }
    }
    return false;
}

bool SdStore::getMessageForProtocol(MessageId msg_id,
                                    MeshProtocol protocol,
                                    ChatMessage* out) const
{
    if (!ready_.load(std::memory_order_acquire))
    {
        return false;
    }
    storage_runtime::ScopedRecursiveStateLock state_lock(mutex_);
    if (!state_lock.locked() || !ready_)
    {
        return false;
    }
    protocol = normalizeProtocol(protocol);
    if (msg_id == 0U)
    {
        return false;
    }
    for (const storage_v2::ChatCatalogProjection& projection : catalog_)
    {
        if (projection.deleted ||
            !sameProtocol(projection.conversation.protocol, protocol))
        {
            continue;
        }
        for (uint32_t ordinal = projection.message_count; ordinal > 0U;
             --ordinal)
        {
            ChatMessage message{};
            if (!readMessageByOrdinal(projection.conversation,
                                      ordinal - 1U,
                                      message) ||
                message.msg_id != msg_id)
            {
                continue;
            }
            if (out)
            {
                *out = std::move(message);
            }
            return true;
        }
    }
    return false;
}

bool SdStore::hasReticulumLxmfMessageHash(const uint8_t* hash) const
{
    if (!ready_.load(std::memory_order_acquire))
    {
        return false;
    }
    storage_runtime::ScopedRecursiveStateLock state_lock(mutex_);
    if (!state_lock.locked() || !ready_)
    {
        return false;
    }
    if (!hash || isAllZeroKeyBytes(hash, kReticulumLxmfHashSize))
    {
        return false;
    }
    for (const storage_v2::ReticulumSeenProjection& seen : seen_hot_)
    {
        if (std::memcmp(seen.hash, hash, sizeof(seen.hash)) == 0)
        {
            return true;
        }
    }

    for (const char* name : {"seen.delta", "seen.snapshot"})
    {
        char path[128]{};
        buildProjectionPath(MeshProtocol::Reticulum,
                            name,
                            path,
                            sizeof(path));
        const auto inspection = journal_.inspect(
            path,
            MeshProtocol::Reticulum,
            storage_v2::JournalKind::ReticulumSeen,
            storage_v2::reticulumSeenSlotSize());
        for (uint32_t index = inspection.slot_count; index > 0U; --index)
        {
            if (!journal_.read(path,
                               MeshProtocol::Reticulum,
                               storage_v2::JournalKind::ReticulumSeen,
                               storage_v2::reticulumSeenSlotSize(),
                               index - 1U,
                               scratch_.data()))
            {
                continue;
            }
            storage_v2::ReticulumSeenProjection projection{};
            if (storage_v2::decodeReticulumSeenSlot(
                    scratch_.data(),
                    storage_v2::reticulumSeenSlotSize(),
                    projection) &&
                std::memcmp(projection.hash, hash, sizeof(projection.hash)) == 0)
            {
                return true;
            }
        }
    }
    return false;
}

void SdStore::flush()
{
    if (!ready_.load(std::memory_order_acquire))
    {
        return;
    }
    storage_runtime::ScopedRecursiveStateLock state_lock(mutex_);
    if (!state_lock.locked())
    {
        return;
    }
    const uint32_t now_ms = monotonic_millis();
    if (last_projection_retry_ms_ != 0U &&
        now_ms - last_projection_retry_ms_ < kProjectionRetryIntervalMs)
    {
        return;
    }
    MeshProtocol protocol = MeshProtocol::Meshtastic;
    bool found_dirty = false;
    for (std::size_t offset = 0U; offset < 3U; ++offset)
    {
        const std::size_t index =
            (static_cast<std::size_t>(flush_protocol_cursor_) + offset) % 3U;
        if (projection_dirty_[index])
        {
            protocol = kProtocols[index];
            flush_protocol_cursor_ = static_cast<uint8_t>((index + 1U) % 3U);
            found_dirty = true;
            break;
        }
    }
    if (!found_dirty)
    {
        return;
    }
    last_projection_retry_ms_ = now_ms;
    (void)compactProtocolProjections(protocol);
}

bool SdStore::ensureLayout() const
{
    if (!storage_runtime::sd_card_ready() || !ensureDirectory("/data") ||
        !ensureDirectory(kRoot))
    {
        return false;
    }
    for (MeshProtocol protocol : kProtocols)
    {
        if (!ensureProtocolLayout(protocol))
        {
            return false;
        }
    }
    return true;
}

bool SdStore::ensureProtocolLayout(MeshProtocol protocol) const
{
    protocol = normalizeProtocol(protocol);
    const char* slug = protocolSlug(protocol);
    char protocol_dir[64]{};
    char conversations[96]{};
    std::snprintf(protocol_dir,
                  sizeof(protocol_dir),
                  "%s/%s",
                  kRoot,
                  slug);
    std::snprintf(conversations,
                  sizeof(conversations),
                  "%s/conversations",
                  protocolRoot(protocol));
    return ensureDirectory(protocol_dir) &&
           ensureDirectory(protocolRoot(protocol)) &&
           ensureDirectory(conversations);
}

bool SdStore::loadRuntimeState()
{
    catalog_.clear();
    read_state_.clear();
    statuses_.clear();
    seen_hot_.clear();
    bool ok = true;
    for (MeshProtocol protocol : kProtocols)
    {
        ok = loadProtocolState(protocol) && ok;
    }
    ok = loadSeenJournal() && ok;
    return ok;
}

bool SdStore::loadProtocolState(MeshProtocol protocol)
{
    if (!recoverProjectionSnapshot(protocol, "catalog") ||
        !recoverProjectionSnapshot(protocol, "read") ||
        !recoverProjectionSnapshot(protocol, "status"))
    {
        return false;
    }
    bool ok = true;
    ok = loadCatalogJournal(protocol, "catalog.snapshot") && ok;
    ok = loadCatalogJournal(protocol, "catalog.delta") && ok;
    ok = loadReadJournal(protocol, "read.snapshot") && ok;
    ok = loadReadJournal(protocol, "read.delta") && ok;
    ok = loadStatusJournal(protocol, "status.snapshot") && ok;
    ok = loadStatusJournal(protocol, "status.delta") && ok;
    return reconcileProtocolCatalog(protocol) && ok;
}

bool SdStore::loadCatalogJournal(MeshProtocol protocol, const char* name)
{
    char path[128]{};
    buildProjectionPath(protocol, name, path, sizeof(path));
    const auto inspection = journal_.inspect(
        path,
        protocol,
        hasSuffix(name, ".snapshot")
            ? storage_v2::JournalKind::CatalogSnapshot
            : storage_v2::JournalKind::CatalogDelta,
        storage_v2::catalogSlotSize(protocol));
    if (inspection.state == storage_v2::FixedSlotJournalEngine::State::Missing)
    {
        return true;
    }
    if (inspection.state != storage_v2::FixedSlotJournalEngine::State::Ready &&
        inspection.state !=
            storage_v2::FixedSlotJournalEngine::State::PartialTail)
    {
        projection_dirty_[protocolIndex(protocol)] = true;
        return true;
    }
    const storage_v2::JournalKind kind =
        hasSuffix(name, ".snapshot")
            ? storage_v2::JournalKind::CatalogSnapshot
            : storage_v2::JournalKind::CatalogDelta;
    for (uint32_t index = 0; index < inspection.slot_count; ++index)
    {
        storage_v2::ChatCatalogProjection projection{};
        if (!journal_.read(path,
                           protocol,
                           kind,
                           storage_v2::catalogSlotSize(protocol),
                           index,
                           scratch_.data()) ||
            !storage_v2::decodeCatalogSlot(protocol,
                                           scratch_.data(),
                                           storage_v2::catalogSlotSize(protocol),
                                           projection))
        {
            projection_dirty_[protocolIndex(protocol)] = true;
            continue;
        }
        storage_v2::ChatCatalogProjection* existing =
            findCatalog(projection.conversation);
        if (projection.deleted)
        {
            if (existing)
            {
                catalog_.erase(catalog_.begin() +
                               static_cast<std::ptrdiff_t>(existing -
                                                           catalog_.data()));
            }
        }
        else if (existing)
        {
            *existing = projection;
        }
        else
        {
            catalog_.push_back(projection);
        }
    }
    return true;
}

bool SdStore::loadReadJournal(MeshProtocol protocol, const char* name)
{
    char path[128]{};
    buildProjectionPath(protocol, name, path, sizeof(path));
    const storage_v2::JournalKind kind =
        hasSuffix(name, ".snapshot")
            ? storage_v2::JournalKind::ReadStateSnapshot
            : storage_v2::JournalKind::ReadStateDelta;
    const auto inspection = journal_.inspect(
        path,
        protocol,
        kind,
        storage_v2::readStateSlotSize(protocol));
    if (inspection.state == storage_v2::FixedSlotJournalEngine::State::Missing)
    {
        return true;
    }
    if (inspection.state != storage_v2::FixedSlotJournalEngine::State::Ready &&
        inspection.state !=
            storage_v2::FixedSlotJournalEngine::State::PartialTail)
    {
        projection_dirty_[protocolIndex(protocol)] = true;
        return true;
    }
    for (uint32_t index = 0; index < inspection.slot_count; ++index)
    {
        storage_v2::ChatReadProjection projection{};
        if (!journal_.read(path,
                           protocol,
                           kind,
                           storage_v2::readStateSlotSize(protocol),
                           index,
                           scratch_.data()) ||
            !storage_v2::decodeReadStateSlot(
                protocol,
                scratch_.data(),
                storage_v2::readStateSlotSize(protocol),
                projection))
        {
            projection_dirty_[protocolIndex(protocol)] = true;
            continue;
        }
        storage_v2::ChatReadProjection* existing =
            findReadState(projection.conversation);
        if (projection.deleted)
        {
            if (existing)
            {
                read_state_.erase(
                    read_state_.begin() +
                    static_cast<std::ptrdiff_t>(existing - read_state_.data()));
            }
        }
        else if (existing)
        {
            *existing = projection;
        }
        else
        {
            read_state_.push_back(projection);
        }
    }
    return true;
}

bool SdStore::loadStatusJournal(MeshProtocol protocol, const char* name)
{
    char path[128]{};
    buildProjectionPath(protocol, name, path, sizeof(path));
    const storage_v2::JournalKind kind =
        hasSuffix(name, ".snapshot")
            ? storage_v2::JournalKind::StatusSnapshot
            : storage_v2::JournalKind::StatusDelta;
    const auto inspection = journal_.inspect(path,
                                             protocol,
                                             kind,
                                             storage_v2::statusSlotSize());
    if (inspection.state == storage_v2::FixedSlotJournalEngine::State::Missing)
    {
        return true;
    }
    if (inspection.state != storage_v2::FixedSlotJournalEngine::State::Ready &&
        inspection.state !=
            storage_v2::FixedSlotJournalEngine::State::PartialTail)
    {
        projection_dirty_[protocolIndex(protocol)] = true;
        return true;
    }
    for (uint32_t index = 0; index < inspection.slot_count; ++index)
    {
        storage_v2::ChatStatusProjection projection{};
        if (!journal_.read(path,
                           protocol,
                           kind,
                           storage_v2::statusSlotSize(),
                           index,
                           scratch_.data()) ||
            !storage_v2::decodeStatusSlot(scratch_.data(),
                                          storage_v2::statusSlotSize(),
                                          projection))
        {
            projection_dirty_[protocolIndex(protocol)] = true;
            continue;
        }
        storage_v2::ChatStatusProjection* existing =
            findStatus(projection.message_id, protocol);
        if (existing)
        {
            *existing = projection;
        }
        else
        {
            ProtocolStatusProjection state{};
            state.protocol = normalizeProtocol(protocol);
            state.value = projection;
            statuses_.push_back(state);
        }
    }
    return true;
}

bool SdStore::loadSeenJournal()
{
    if (!recoverProjectionSnapshot(MeshProtocol::Reticulum, "seen"))
    {
        return false;
    }
    bool journal_found = false;
    bool rebuild_required = false;
    for (const char* name : {"seen.snapshot", "seen.delta"})
    {
        char path[128]{};
        buildProjectionPath(MeshProtocol::Reticulum,
                            name,
                            path,
                            sizeof(path));
        const auto inspection = journal_.inspect(
            path,
            MeshProtocol::Reticulum,
            storage_v2::JournalKind::ReticulumSeen,
            storage_v2::reticulumSeenSlotSize());
        if (inspection.state ==
            storage_v2::FixedSlotJournalEngine::State::Missing)
        {
            continue;
        }
        journal_found = true;
        if (inspection.state !=
                storage_v2::FixedSlotJournalEngine::State::Ready &&
            inspection.state !=
                storage_v2::FixedSlotJournalEngine::State::PartialTail)
        {
            rebuild_required = true;
            continue;
        }
        if (inspection.state ==
            storage_v2::FixedSlotJournalEngine::State::PartialTail)
        {
            rebuild_required = true;
        }
        const uint32_t start =
            inspection.slot_count > kSeenHotCapacity
                ? inspection.slot_count - static_cast<uint32_t>(kSeenHotCapacity)
                : 0U;
        for (uint32_t index = start; index < inspection.slot_count; ++index)
        {
            storage_v2::ReticulumSeenProjection projection{};
            if (!journal_.read(path,
                               MeshProtocol::Reticulum,
                               storage_v2::JournalKind::ReticulumSeen,
                               storage_v2::reticulumSeenSlotSize(),
                               index,
                               scratch_.data()) ||
                !storage_v2::decodeReticulumSeenSlot(
                    scratch_.data(),
                    storage_v2::reticulumSeenSlotSize(),
                    projection))
            {
                rebuild_required = true;
                continue;
            }
            if (seen_hot_.size() == kSeenHotCapacity)
            {
                seen_hot_.erase(seen_hot_.begin());
            }
            seen_hot_.push_back(projection);
        }
    }
    if (!journal_found)
    {
        for (const storage_v2::ChatCatalogProjection& projection : catalog_)
        {
            if (!projection.deleted &&
                sameProtocol(projection.conversation.protocol,
                             MeshProtocol::Reticulum) &&
                messageCountOnDisk(projection.conversation) > 0U)
            {
                rebuild_required = true;
                break;
            }
        }
    }
    return !rebuild_required || rebuildSeenJournalFromMessages();
}

bool SdStore::rebuildSeenJournalFromMessages()
{
    char final_path[128] = {};
    char temp_path[128] = {};
    char backup_path[128] = {};
    char delta_path[128] = {};
    buildProjectionPath(MeshProtocol::Reticulum,
                        "seen.snapshot",
                        final_path,
                        sizeof(final_path));
    buildProjectionPath(MeshProtocol::Reticulum,
                        "seen.snapshot.tmp",
                        temp_path,
                        sizeof(temp_path));
    buildProjectionPath(MeshProtocol::Reticulum,
                        "seen.snapshot.bak",
                        backup_path,
                        sizeof(backup_path));
    buildProjectionPath(MeshProtocol::Reticulum,
                        "seen.delta",
                        delta_path,
                        sizeof(delta_path));
    (void)storage_runtime::sd_remove(temp_path);
    const std::size_t slot_size = storage_v2::reticulumSeenSlotSize();
    if (!journal_.create(temp_path,
                         MeshProtocol::Reticulum,
                         storage_v2::JournalKind::ReticulumSeen,
                         slot_size))
    {
        return false;
    }

    seen_hot_.clear();
    uint32_t rebuilt = 0U;
    for (const storage_v2::ChatCatalogProjection& catalog : catalog_)
    {
        if (catalog.deleted ||
            !sameProtocol(catalog.conversation.protocol,
                          MeshProtocol::Reticulum))
        {
            continue;
        }
        const uint32_t message_count =
            messageCountOnDisk(catalog.conversation);
        for (uint32_t ordinal = 0U; ordinal < message_count; ++ordinal)
        {
            ChatMessage message{};
            if (!readMessageByOrdinal(catalog.conversation,
                                      ordinal,
                                      message) ||
                !chat::hasReticulumLxmfMessageHash(message))
            {
                continue;
            }
            storage_v2::ReticulumSeenProjection projection{};
            std::memcpy(projection.hash,
                        message.reticulum_lxmf_hash,
                        sizeof(projection.hash));
            if (!storage_v2::encodeReticulumSeenSlot(projection,
                                                     scratch_.data(),
                                                     slot_size) ||
                !journal_.append(temp_path,
                                 MeshProtocol::Reticulum,
                                 storage_v2::JournalKind::ReticulumSeen,
                                 slot_size,
                                 scratch_.data()))
            {
                (void)storage_runtime::sd_remove(temp_path);
                return false;
            }
            if (seen_hot_.size() == kSeenHotCapacity)
            {
                seen_hot_.erase(seen_hot_.begin());
            }
            seen_hot_.push_back(projection);
            ++rebuilt;
        }
    }
    if (!storage_v2::replaceFileAtomically(temp_path,
                                           final_path,
                                           backup_path))
    {
        return false;
    }
    (void)storage_runtime::sd_remove(delta_path);
    CHAT_STORE_LOG("[ChatStoreV2] seen rebuilt hashes=%lu authoritative=messages\n",
                   static_cast<unsigned long>(rebuilt));
    return true;
}

bool SdStore::recoverProjectionSnapshot(MeshProtocol protocol,
                                        const char* base_name)
{
    if (!base_name || base_name[0] == '\0')
    {
        return false;
    }
    char final_path[128] = {};
    char temp_path[128] = {};
    char backup_path[128] = {};
    char name[40] = {};
    std::snprintf(name, sizeof(name), "%s.snapshot", base_name);
    buildProjectionPath(protocol, name, final_path, sizeof(final_path));
    std::snprintf(name, sizeof(name), "%s.snapshot.tmp", base_name);
    buildProjectionPath(protocol, name, temp_path, sizeof(temp_path));
    std::snprintf(name, sizeof(name), "%s.snapshot.bak", base_name);
    buildProjectionPath(protocol, name, backup_path, sizeof(backup_path));
    return storage_v2::recoverAtomicFile(final_path,
                                         temp_path,
                                         backup_path);
}

bool SdStore::reconcileProtocolCatalog(MeshProtocol protocol)
{
    protocol = normalizeProtocol(protocol);
    for (storage_v2::ChatCatalogProjection& projection : catalog_)
    {
        if (sameProtocol(projection.conversation.protocol, protocol))
        {
            projection.deleted = true;
        }
    }

    char conversations_path[96]{};
    std::snprintf(conversations_path,
                  sizeof(conversations_path),
                  "%s/conversations",
                  protocolRoot(protocol));
    storage_runtime::SdRuntimeDir directory;
    if (!directory.open(conversations_path))
    {
        return false;
    }
    char name[80]{};
    bool is_directory = false;
    while (directory.read_next(name, sizeof(name), &is_directory))
    {
        if (is_directory && name[0] != '\0' &&
            !reconcileConversationDirectory(protocol, name))
        {
            projection_dirty_[protocolIndex(protocol)] = true;
        }
    }
    catalog_.erase(std::remove_if(catalog_.begin(),
                                  catalog_.end(),
                                  [&](const auto& value)
                                  {
                                      return sameProtocol(
                                                 value.conversation.protocol,
                                                 protocol) &&
                                             value.deleted;
                                  }),
                   catalog_.end());
    return true;
}

bool SdStore::reconcileConversationDirectory(MeshProtocol protocol,
                                             const char* directory_name)
{
    char directory_path[128]{};
    std::snprintf(directory_path,
                  sizeof(directory_path),
                  "%s/conversations/%s",
                  protocolRoot(protocol),
                  directory_name);
    const std::size_t slot_size = storage_v2::messageSlotSize(protocol);
    uint32_t total_count = 0;
    uint32_t last_segment = 0;
    uint32_t last_segment_count = 0;
    bool found_segment = false;
    for (uint32_t segment = 0; segment < 10000U; ++segment)
    {
        char path[160]{};
        std::snprintf(path,
                      sizeof(path),
                      "%s/%04lu.msg",
                      directory_path,
                      static_cast<unsigned long>(segment));
        auto inspection = journal_.inspect(
            path,
            protocol,
            storage_v2::JournalKind::MessageSegment,
            slot_size);
        if (inspection.state == storage_v2::FixedSlotJournalEngine::State::Missing)
        {
            break;
        }
        if (inspection.state ==
            storage_v2::FixedSlotJournalEngine::State::PartialTail)
        {
            if (!repairPartialJournal(path,
                                      protocol,
                                      storage_v2::JournalKind::MessageSegment,
                                      slot_size))
            {
                return false;
            }
            inspection = journal_.inspect(
                path,
                protocol,
                storage_v2::JournalKind::MessageSegment,
                slot_size);
        }
        if (inspection.state != storage_v2::FixedSlotJournalEngine::State::Ready)
        {
            return false;
        }
        found_segment = true;
        total_count += inspection.slot_count;
        last_segment = segment;
        last_segment_count = inspection.slot_count;
        if (inspection.slot_count < slotsPerMessageSegment(protocol))
        {
            break;
        }
    }
    if (!found_segment || total_count == 0U || last_segment_count == 0U)
    {
        return true;
    }

    char last_path[160]{};
    std::snprintf(last_path,
                  sizeof(last_path),
                  "%s/%04lu.msg",
                  directory_path,
                  static_cast<unsigned long>(last_segment));
    if (!journal_.read(last_path,
                       protocol,
                       storage_v2::JournalKind::MessageSegment,
                       slot_size,
                       last_segment_count - 1U,
                       scratch_.data()))
    {
        return false;
    }
    ChatMessage latest{};
    uint32_t sequence = 0;
    if (!storage_v2::decodeMessageSlot(protocol,
                                       scratch_.data(),
                                       slot_size,
                                       latest,
                                       &sequence))
    {
        return false;
    }
    applyStoredStatus(latest);
    const ConversationId conversation = conversationIdForMessage(latest);
    storage_v2::ChatCatalogProjection* projection = findCatalog(conversation);
    const bool catalog_current =
        projection && projection->message_count == total_count &&
        projection->last_message_id == latest.msg_id &&
        projection->last_sequence == sequence;
    if (!projection)
    {
        storage_v2::ChatCatalogProjection created{};
        created.conversation = conversation;
        catalog_.push_back(created);
        projection = &catalog_.back();
    }
    const uint32_t last_read =
        findReadState(conversation)
            ? findReadState(conversation)->last_read_sequence
            : 0U;
    projection->conversation = conversation;
    projection->message_count = total_count;
    projection->last_sequence = sequence;
    projection->last_message_id = latest.msg_id;
    projection->last_timestamp = latest.timestamp;
    projection->last_status = latest.status;
    projection->deleted = false;
    copyTextPreview(projection->preview,
                    sizeof(projection->preview),
                    latest.text);
    if (!catalog_current)
    {
        projection->unread = countUnreadAfter(conversation, last_read);
        projection_dirty_[protocolIndex(protocol)] = true;
    }
    return true;
}

std::size_t SdStore::slotsPerMessageSegment(MeshProtocol protocol) const
{
    const std::size_t slot_size = storage_v2::messageSlotSize(protocol);
    if (slot_size == 0U || kMessageSegmentBytes <=
                               storage_v2::FixedSlotJournalEngine::headerSize())
    {
        return 0U;
    }
    return (kMessageSegmentBytes -
            storage_v2::FixedSlotJournalEngine::headerSize()) /
           slot_size;
}

uint32_t SdStore::messageCountOnDisk(const ConversationId& conversation) const
{
    const std::size_t slot_size =
        storage_v2::messageSlotSize(conversation.protocol);
    uint32_t total_count = 0;
    for (uint32_t segment = 0; segment < 10000U; ++segment)
    {
        char path[128]{};
        buildMessageSegmentPath(conversation, segment, path, sizeof(path));
        const auto inspection = journal_.inspect(
            path,
            conversation.protocol,
            storage_v2::JournalKind::MessageSegment,
            slot_size);
        if (inspection.state == storage_v2::FixedSlotJournalEngine::State::Missing)
        {
            break;
        }
        if (inspection.state != storage_v2::FixedSlotJournalEngine::State::Ready &&
            inspection.state !=
                storage_v2::FixedSlotJournalEngine::State::PartialTail)
        {
            break;
        }
        total_count += inspection.slot_count;
        if (inspection.slot_count < slotsPerMessageSegment(conversation.protocol))
        {
            break;
        }
    }
    return total_count;
}

bool SdStore::readMessageByOrdinal(const ConversationId& input,
                                   uint32_t ordinal,
                                   ChatMessage& out_message,
                                   uint32_t* out_sequence) const
{
    ConversationId conversation = input;
    conversation.protocol = normalizeProtocol(conversation.protocol);
    const std::size_t capacity = slotsPerMessageSegment(conversation.protocol);
    const std::size_t slot_size =
        storage_v2::messageSlotSize(conversation.protocol);
    if (capacity == 0U || slot_size > scratch_.size())
    {
        return false;
    }
    const uint32_t segment = static_cast<uint32_t>(ordinal / capacity);
    const uint32_t slot = static_cast<uint32_t>(ordinal % capacity);
    char path[128]{};
    buildMessageSegmentPath(conversation, segment, path, sizeof(path));
    if (!journal_.read(path,
                       conversation.protocol,
                       storage_v2::JournalKind::MessageSegment,
                       slot_size,
                       slot,
                       scratch_.data()) ||
        !storage_v2::decodeMessageSlot(conversation.protocol,
                                       scratch_.data(),
                                       slot_size,
                                       out_message,
                                       out_sequence))
    {
        return false;
    }
    applyStoredStatus(out_message);
    return true;
}

bool SdStore::latestStoredMessage(const ConversationId& conversation,
                                  ChatMessage& out_message,
                                  uint32_t* out_count) const
{
    const uint32_t count = messageCountOnDisk(conversation);
    if (out_count)
    {
        *out_count = count;
    }
    return count != 0U &&
           readMessageByOrdinal(conversation, count - 1U, out_message);
}

bool SdStore::storedMessageMatches(const ChatMessage& message,
                                   bool* out_matches,
                                   uint32_t* out_count) const
{
    if (!out_matches || !out_count)
    {
        return false;
    }
    *out_matches = false;
    *out_count = 0U;
    const ConversationId conversation = conversationIdForMessage(message);
    ChatMessage latest{};
    if (!latestStoredMessage(conversation, latest, out_count))
    {
        return *out_count == 0U;
    }
    *out_matches = sameStoredMessage(latest, message);
    return true;
}

bool SdStore::appendMessageRecord(const ChatMessage& message,
                                  uint32_t sequence)
{
    const ConversationId conversation = conversationIdForMessage(message);
    char directory[128]{};
    buildConversationDirectory(conversation, directory, sizeof(directory));
    if (!ensureDirectory(directory))
    {
        return false;
    }
    const std::size_t capacity = slotsPerMessageSegment(message.protocol);
    const std::size_t slot_size = storage_v2::messageSlotSize(message.protocol);
    if (capacity == 0U || slot_size > scratch_.size() || sequence == 0U ||
        !storage_v2::encodeMessageSlot(message,
                                       sequence,
                                       scratch_.data(),
                                       slot_size))
    {
        return false;
    }
    const uint32_t ordinal = sequence - 1U;
    const uint32_t segment = static_cast<uint32_t>(ordinal / capacity);
    const uint32_t expected_slot = static_cast<uint32_t>(ordinal % capacity);
    char path[128]{};
    buildMessageSegmentPath(conversation, segment, path, sizeof(path));
    auto inspection = journal_.inspect(path,
                                       message.protocol,
                                       storage_v2::JournalKind::MessageSegment,
                                       slot_size);
    if (inspection.state == storage_v2::FixedSlotJournalEngine::State::PartialTail)
    {
        if (!repairPartialJournal(path,
                                  message.protocol,
                                  storage_v2::JournalKind::MessageSegment,
                                  slot_size))
        {
            return false;
        }
        inspection = journal_.inspect(path,
                                      message.protocol,
                                      storage_v2::JournalKind::MessageSegment,
                                      slot_size);
    }
    if (inspection.state != storage_v2::FixedSlotJournalEngine::State::Missing &&
        inspection.state != storage_v2::FixedSlotJournalEngine::State::Ready)
    {
        return false;
    }
    if (inspection.state == storage_v2::FixedSlotJournalEngine::State::Ready &&
        inspection.slot_count != expected_slot)
    {
        return false;
    }
    if (inspection.state == storage_v2::FixedSlotJournalEngine::State::Missing &&
        expected_slot != 0U)
    {
        return false;
    }
    // Tail recovery reuses scratch_, so restore the command record before the
    // authoritative append.
    if (!storage_v2::encodeMessageSlot(message,
                                       sequence,
                                       scratch_.data(),
                                       slot_size))
    {
        return false;
    }
    return journal_.append(path,
                           message.protocol,
                           storage_v2::JournalKind::MessageSegment,
                           slot_size,
                           scratch_.data());
}

bool SdStore::repairPartialJournal(const char* path,
                                   MeshProtocol protocol,
                                   storage_v2::JournalKind kind,
                                   std::size_t slot_size) const
{
    const auto inspection = journal_.inspect(path, protocol, kind, slot_size);
    if (inspection.state !=
        storage_v2::FixedSlotJournalEngine::State::PartialTail)
    {
        return inspection.state ==
               storage_v2::FixedSlotJournalEngine::State::Ready;
    }
    char temp[144]{};
    char backup[144]{};
    std::snprintf(temp, sizeof(temp), "%s.repair", path);
    std::snprintf(backup, sizeof(backup), "%s.partial", path);
    (void)storage_runtime::sd_remove(temp);
    (void)storage_runtime::sd_remove(backup);
    if (!journal_.create(temp, protocol, kind, slot_size))
    {
        return false;
    }
    for (uint32_t index = 0; index < inspection.slot_count; ++index)
    {
        if (!journal_.read(path,
                           protocol,
                           kind,
                           slot_size,
                           index,
                           scratch_.data()) ||
            !journal_.append(temp,
                             protocol,
                             kind,
                             slot_size,
                             scratch_.data()))
        {
            (void)storage_runtime::sd_remove(temp);
            return false;
        }
    }
    if (!storage_runtime::sd_rename(path, backup) ||
        !storage_runtime::sd_rename(temp, path))
    {
        if (!storage_runtime::sd_exists(path))
        {
            (void)storage_runtime::sd_rename(backup, path);
        }
        (void)storage_runtime::sd_remove(temp);
        return false;
    }
    (void)storage_runtime::sd_remove(backup);
    return true;
}

bool SdStore::appendCatalogProjection(
    const storage_v2::ChatCatalogProjection& projection)
{
    const MeshProtocol protocol =
        normalizeProtocol(projection.conversation.protocol);
    const std::size_t slot_size = storage_v2::catalogSlotSize(protocol);
    if (slot_size > scratch_.size() ||
        !storage_v2::encodeCatalogSlot(protocol,
                                       projection,
                                       scratch_.data(),
                                       slot_size))
    {
        return false;
    }
    char path[128]{};
    buildProjectionPath(protocol, "catalog.delta", path, sizeof(path));
    return journal_.append(path,
                           protocol,
                           storage_v2::JournalKind::CatalogDelta,
                           slot_size,
                           scratch_.data());
}

bool SdStore::appendReadProjection(
    const storage_v2::ChatReadProjection& projection)
{
    const MeshProtocol protocol =
        normalizeProtocol(projection.conversation.protocol);
    const std::size_t slot_size = storage_v2::readStateSlotSize(protocol);
    if (slot_size > scratch_.size() ||
        !storage_v2::encodeReadStateSlot(protocol,
                                         projection,
                                         scratch_.data(),
                                         slot_size))
    {
        return false;
    }
    char path[128]{};
    buildProjectionPath(protocol, "read.delta", path, sizeof(path));
    auto inspection = journal_.inspect(path,
                                       protocol,
                                       storage_v2::JournalKind::ReadStateDelta,
                                       slot_size);
    if (inspection.state == storage_v2::FixedSlotJournalEngine::State::PartialTail &&
        !repairPartialJournal(path,
                              protocol,
                              storage_v2::JournalKind::ReadStateDelta,
                              slot_size))
    {
        return false;
    }
    if (!storage_v2::encodeReadStateSlot(protocol,
                                         projection,
                                         scratch_.data(),
                                         slot_size))
    {
        return false;
    }
    return journal_.append(path,
                           protocol,
                           storage_v2::JournalKind::ReadStateDelta,
                           slot_size,
                           scratch_.data());
}

bool SdStore::appendStatusProjection(
    MeshProtocol protocol,
    const storage_v2::ChatStatusProjection& projection)
{
    protocol = normalizeProtocol(protocol);
    const std::size_t slot_size = storage_v2::statusSlotSize();
    if (slot_size > scratch_.size() ||
        !storage_v2::encodeStatusSlot(projection,
                                      scratch_.data(),
                                      slot_size))
    {
        return false;
    }
    char path[128]{};
    buildProjectionPath(protocol, "status.delta", path, sizeof(path));
    auto inspection = journal_.inspect(path,
                                       protocol,
                                       storage_v2::JournalKind::StatusDelta,
                                       slot_size);
    if (inspection.state == storage_v2::FixedSlotJournalEngine::State::PartialTail &&
        !repairPartialJournal(path,
                              protocol,
                              storage_v2::JournalKind::StatusDelta,
                              slot_size))
    {
        return false;
    }
    if (!storage_v2::encodeStatusSlot(projection,
                                      scratch_.data(),
                                      slot_size))
    {
        return false;
    }
    return journal_.append(path,
                           protocol,
                           storage_v2::JournalKind::StatusDelta,
                           slot_size,
                           scratch_.data());
}

bool SdStore::appendSeenProjection(
    const storage_v2::ReticulumSeenProjection& projection) const
{
    const std::size_t slot_size = storage_v2::reticulumSeenSlotSize();
    if (slot_size > scratch_.size() ||
        !storage_v2::encodeReticulumSeenSlot(projection,
                                             scratch_.data(),
                                             slot_size))
    {
        return false;
    }
    char path[128]{};
    buildProjectionPath(MeshProtocol::Reticulum,
                        "seen.delta",
                        path,
                        sizeof(path));
    auto inspection = journal_.inspect(
        path,
        MeshProtocol::Reticulum,
        storage_v2::JournalKind::ReticulumSeen,
        slot_size);
    if (inspection.state == storage_v2::FixedSlotJournalEngine::State::PartialTail &&
        !repairPartialJournal(path,
                              MeshProtocol::Reticulum,
                              storage_v2::JournalKind::ReticulumSeen,
                              slot_size))
    {
        return false;
    }
    if (!storage_v2::encodeReticulumSeenSlot(projection,
                                             scratch_.data(),
                                             slot_size))
    {
        return false;
    }
    return journal_.append(path,
                           MeshProtocol::Reticulum,
                           storage_v2::JournalKind::ReticulumSeen,
                           slot_size,
                           scratch_.data());
}

bool SdStore::rememberReticulumHash(const uint8_t* hash)
{
    if (hasReticulumLxmfMessageHash(hash))
    {
        return true;
    }
    storage_v2::ReticulumSeenProjection projection{};
    std::memcpy(projection.hash, hash, sizeof(projection.hash));
    if (!appendSeenProjection(projection))
    {
        return false;
    }
    if (seen_hot_.size() == kSeenHotCapacity)
    {
        seen_hot_.erase(seen_hot_.begin());
    }
    seen_hot_.push_back(projection);
    return true;
}

storage_v2::ChatCatalogProjection* SdStore::findCatalog(
    const ConversationId& conversation)
{
    for (storage_v2::ChatCatalogProjection& projection : catalog_)
    {
        if (sameConversationKey(projection.conversation, conversation))
        {
            return &projection;
        }
    }
    return nullptr;
}

const storage_v2::ChatCatalogProjection* SdStore::findCatalog(
    const ConversationId& conversation) const
{
    for (const storage_v2::ChatCatalogProjection& projection : catalog_)
    {
        if (sameConversationKey(projection.conversation, conversation))
        {
            return &projection;
        }
    }
    return nullptr;
}

storage_v2::ChatReadProjection* SdStore::findReadState(
    const ConversationId& conversation)
{
    for (storage_v2::ChatReadProjection& projection : read_state_)
    {
        if (sameConversationKey(projection.conversation, conversation))
        {
            return &projection;
        }
    }
    return nullptr;
}

const storage_v2::ChatReadProjection* SdStore::findReadState(
    const ConversationId& conversation) const
{
    for (const storage_v2::ChatReadProjection& projection : read_state_)
    {
        if (sameConversationKey(projection.conversation, conversation))
        {
            return &projection;
        }
    }
    return nullptr;
}

storage_v2::ChatStatusProjection* SdStore::findStatus(MessageId message_id,
                                                      MeshProtocol protocol)
{
    for (ProtocolStatusProjection& state : statuses_)
    {
        if (sameProtocol(state.protocol, protocol) &&
            state.value.message_id == message_id)
        {
            return &state.value;
        }
    }
    return nullptr;
}

const storage_v2::ChatStatusProjection* SdStore::findStatus(
    MessageId message_id,
    MeshProtocol protocol) const
{
    for (const ProtocolStatusProjection& state : statuses_)
    {
        if (sameProtocol(state.protocol, protocol) &&
            state.value.message_id == message_id)
        {
            return &state.value;
        }
    }
    return nullptr;
}

void SdStore::applyStoredStatus(ChatMessage& message) const
{
    if (message.from != 0U)
    {
        return;
    }
    if (const storage_v2::ChatStatusProjection* status =
            findStatus(message.msg_id, message.protocol))
    {
        message.status = status->status;
    }
}

uint32_t SdStore::countUnreadAfter(const ConversationId& conversation,
                                   uint32_t last_read_sequence) const
{
    const uint32_t count = messageCountOnDisk(conversation);
    uint32_t unread = 0;
    for (uint32_t ordinal = last_read_sequence; ordinal < count; ++ordinal)
    {
        ChatMessage message{};
        uint32_t sequence = 0;
        if (readMessageByOrdinal(conversation,
                                 ordinal,
                                 message,
                                 &sequence) &&
            sequence > last_read_sequence &&
            message.status == MessageStatus::Incoming)
        {
            ++unread;
        }
    }
    return unread;
}

uint32_t SdStore::sequenceForUnread(const ConversationId& conversation,
                                    uint32_t unread) const
{
    const uint32_t count = messageCountOnDisk(conversation);
    if (unread == 0U)
    {
        return count;
    }
    uint32_t incoming = 0;
    for (uint32_t ordinal = count; ordinal > 0U; --ordinal)
    {
        ChatMessage message{};
        uint32_t sequence = 0;
        if (!readMessageByOrdinal(conversation,
                                  ordinal - 1U,
                                  message,
                                  &sequence) ||
            message.status != MessageStatus::Incoming)
        {
            continue;
        }
        ++incoming;
        if (incoming == unread)
        {
            return sequence > 0U ? sequence - 1U : 0U;
        }
    }
    return 0U;
}

bool SdStore::rewriteCatalogSnapshot(MeshProtocol protocol)
{
    char final_path[128]{};
    char temp_path[128]{};
    buildProjectionPath(protocol,
                        "catalog.snapshot",
                        final_path,
                        sizeof(final_path));
    buildProjectionPath(protocol,
                        "catalog.snapshot.tmp",
                        temp_path,
                        sizeof(temp_path));
    return rewriteJournalFromCatalog(protocol, final_path, temp_path);
}

bool SdStore::rewriteReadSnapshot(MeshProtocol protocol)
{
    char final_path[128]{};
    char temp_path[128]{};
    buildProjectionPath(protocol,
                        "read.snapshot",
                        final_path,
                        sizeof(final_path));
    buildProjectionPath(protocol,
                        "read.snapshot.tmp",
                        temp_path,
                        sizeof(temp_path));
    return rewriteJournalFromReadState(protocol, final_path, temp_path);
}

bool SdStore::rewriteStatusSnapshot(MeshProtocol protocol)
{
    char final_path[128]{};
    char temp_path[128]{};
    buildProjectionPath(protocol,
                        "status.snapshot",
                        final_path,
                        sizeof(final_path));
    buildProjectionPath(protocol,
                        "status.snapshot.tmp",
                        temp_path,
                        sizeof(temp_path));
    return rewriteJournalFromStatus(protocol, final_path, temp_path);
}

bool SdStore::compactProtocolProjections(MeshProtocol protocol)
{
    protocol = normalizeProtocol(protocol);
    char catalog_delta[128]{};
    char read_delta[128]{};
    char status_delta[128]{};
    buildProjectionPath(protocol,
                        "catalog.delta",
                        catalog_delta,
                        sizeof(catalog_delta));
    buildProjectionPath(protocol,
                        "read.delta",
                        read_delta,
                        sizeof(read_delta));
    buildProjectionPath(protocol,
                        "status.delta",
                        status_delta,
                        sizeof(status_delta));
    const auto catalog_inspection = journal_.inspect(
        catalog_delta,
        protocol,
        storage_v2::JournalKind::CatalogDelta,
        storage_v2::catalogSlotSize(protocol));
    const auto read_inspection = journal_.inspect(
        read_delta,
        protocol,
        storage_v2::JournalKind::ReadStateDelta,
        storage_v2::readStateSlotSize(protocol));
    const auto status_inspection = journal_.inspect(
        status_delta,
        protocol,
        storage_v2::JournalKind::StatusDelta,
        storage_v2::statusSlotSize());
    const bool compact_catalog =
        projection_dirty_[protocolIndex(protocol)] ||
        catalog_inspection.slot_count >= kCatalogCompactThreshold;
    const bool compact_read =
        read_inspection.slot_count >= kReadCompactThreshold;
    const bool compact_status =
        status_inspection.slot_count >= kStatusCompactThreshold;
    if (compact_catalog && !rewriteCatalogSnapshot(protocol))
    {
        return false;
    }
    if (compact_read && !rewriteReadSnapshot(protocol))
    {
        return false;
    }
    if (compact_status && !rewriteStatusSnapshot(protocol))
    {
        return false;
    }
    if (compact_catalog)
    {
        (void)storage_runtime::sd_remove(catalog_delta);
        projection_dirty_[protocolIndex(protocol)] = false;
    }
    if (compact_read)
    {
        (void)storage_runtime::sd_remove(read_delta);
    }
    if (compact_status)
    {
        (void)storage_runtime::sd_remove(status_delta);
    }
    return true;
}

bool SdStore::rewriteJournalFromCatalog(MeshProtocol protocol,
                                        const char* final_path,
                                        const char* temp_path)
{
    const std::size_t slot_size = storage_v2::catalogSlotSize(protocol);
    (void)storage_runtime::sd_remove(temp_path);
    if (!journal_.create(temp_path,
                         protocol,
                         storage_v2::JournalKind::CatalogSnapshot,
                         slot_size))
    {
        return false;
    }
    for (const storage_v2::ChatCatalogProjection& projection : catalog_)
    {
        if (!sameProtocol(projection.conversation.protocol, protocol) ||
            projection.deleted)
        {
            continue;
        }
        if (!storage_v2::encodeCatalogSlot(protocol,
                                           projection,
                                           scratch_.data(),
                                           slot_size) ||
            !journal_.append(temp_path,
                             protocol,
                             storage_v2::JournalKind::CatalogSnapshot,
                             slot_size,
                             scratch_.data()))
        {
            (void)storage_runtime::sd_remove(temp_path);
            return false;
        }
    }
    return replaceSnapshot(temp_path, final_path);
}

bool SdStore::rewriteJournalFromReadState(MeshProtocol protocol,
                                          const char* final_path,
                                          const char* temp_path)
{
    const std::size_t slot_size = storage_v2::readStateSlotSize(protocol);
    (void)storage_runtime::sd_remove(temp_path);
    if (!journal_.create(temp_path,
                         protocol,
                         storage_v2::JournalKind::ReadStateSnapshot,
                         slot_size))
    {
        return false;
    }
    for (const storage_v2::ChatReadProjection& projection : read_state_)
    {
        if (!sameProtocol(projection.conversation.protocol, protocol) ||
            projection.deleted)
        {
            continue;
        }
        if (!storage_v2::encodeReadStateSlot(protocol,
                                             projection,
                                             scratch_.data(),
                                             slot_size) ||
            !journal_.append(temp_path,
                             protocol,
                             storage_v2::JournalKind::ReadStateSnapshot,
                             slot_size,
                             scratch_.data()))
        {
            (void)storage_runtime::sd_remove(temp_path);
            return false;
        }
    }
    return replaceSnapshot(temp_path, final_path);
}

bool SdStore::rewriteJournalFromStatus(MeshProtocol protocol,
                                       const char* final_path,
                                       const char* temp_path)
{
    const std::size_t slot_size = storage_v2::statusSlotSize();
    (void)storage_runtime::sd_remove(temp_path);
    if (!journal_.create(temp_path,
                         protocol,
                         storage_v2::JournalKind::StatusSnapshot,
                         slot_size))
    {
        return false;
    }
    for (const ProtocolStatusProjection& state : statuses_)
    {
        if (!sameProtocol(state.protocol, protocol))
        {
            continue;
        }
        if (!storage_v2::encodeStatusSlot(state.value,
                                          scratch_.data(),
                                          slot_size) ||
            !journal_.append(temp_path,
                             protocol,
                             storage_v2::JournalKind::StatusSnapshot,
                             slot_size,
                             scratch_.data()))
        {
            (void)storage_runtime::sd_remove(temp_path);
            return false;
        }
    }
    return replaceSnapshot(temp_path, final_path);
}

MeshProtocol SdStore::normalizeProtocol(MeshProtocol protocol)
{
    return protocol == MeshProtocol::RNode ? MeshProtocol::Reticulum : protocol;
}

const char* SdStore::protocolRoot(MeshProtocol protocol)
{
    protocol = normalizeProtocol(protocol);
    if (protocol == MeshProtocol::MeshCore)
    {
        return kMeshCoreRoot;
    }
    if (protocol == MeshProtocol::Reticulum)
    {
        return kReticulumRoot;
    }
    return kMeshtasticRoot;
}

const char* SdStore::protocolSlug(MeshProtocol protocol)
{
    protocol = normalizeProtocol(protocol);
    if (protocol == MeshProtocol::MeshCore)
    {
        return "mc";
    }
    if (protocol == MeshProtocol::Reticulum)
    {
        return "rt";
    }
    return "mt";
}

bool SdStore::sameProtocol(MeshProtocol lhs, MeshProtocol rhs)
{
    return normalizeProtocol(lhs) == normalizeProtocol(rhs);
}

bool SdStore::sameStoredMessage(const ChatMessage& lhs,
                                const ChatMessage& rhs)
{
    if (!sameProtocol(lhs.protocol, rhs.protocol) ||
        !sameConversationKey(conversationIdForMessage(lhs),
                             conversationIdForMessage(rhs)))
    {
        return false;
    }
    if (chat::hasReticulumLxmfMessageHash(lhs) &&
        chat::hasReticulumLxmfMessageHash(rhs))
    {
        return std::memcmp(lhs.reticulum_lxmf_hash,
                           rhs.reticulum_lxmf_hash,
                           kReticulumLxmfHashSize) == 0;
    }
    if (lhs.msg_id != 0U || rhs.msg_id != 0U)
    {
        return lhs.msg_id == rhs.msg_id && lhs.from == rhs.from;
    }
    return lhs.from == rhs.from && lhs.timestamp == rhs.timestamp &&
           lhs.text == rhs.text;
}

ConversationMeta SdStore::makeMeta(
    const storage_v2::ChatCatalogProjection& projection)
{
    ConversationMeta meta{};
    meta.id = projection.conversation;
    meta.preview = projection.preview;
    meta.last_timestamp = projection.last_timestamp;
    meta.unread = static_cast<int>(projection.unread);
    meta.reticulum_identity = projection.conversation.reticulum_identity;
    if (projection.conversation.peer == 0U &&
        projection.conversation.protocol != MeshProtocol::Reticulum)
    {
        meta.name = "Broadcast";
    }
    else if (projection.conversation.protocol == MeshProtocol::Reticulum &&
             hasReticulumDestinationIdentity(
                 projection.conversation.reticulum_identity))
    {
        char name[12]{};
        std::snprintf(name,
                      sizeof(name),
                      "%02X%02X%02X%02X",
                      projection.conversation.reticulum_identity
                          .destination_hash[0],
                      projection.conversation.reticulum_identity
                          .destination_hash[1],
                      projection.conversation.reticulum_identity
                          .destination_hash[2],
                      projection.conversation.reticulum_identity
                          .destination_hash[3]);
        meta.name = name;
    }
    else
    {
        char name[16]{};
        std::snprintf(name,
                      sizeof(name),
                      "%04lX",
                      static_cast<unsigned long>(projection.conversation.peer &
                                                 0xFFFFU));
        meta.name = name;
    }
    return meta;
}

void SdStore::buildConversationDirectory(const ConversationId& input,
                                         char* out,
                                         std::size_t out_len)
{
    if (!out || out_len == 0U)
    {
        return;
    }
    ConversationId conversation = input;
    conversation.protocol = normalizeProtocol(conversation.protocol);
    if (conversation.protocol == MeshProtocol::Reticulum &&
        hasReticulumDestinationIdentity(conversation.reticulum_identity))
    {
        char hash[2U * kReticulumPeerHashSize + 1U]{};
        hashToHex(conversation.reticulum_identity.destination_hash,
                  hash,
                  sizeof(hash));
        std::snprintf(out,
                      out_len,
                      "%s/conversations/d_%s",
                      protocolRoot(conversation.protocol),
                      hash);
        return;
    }
    std::snprintf(out,
                  out_len,
                  "%s/conversations/c%02X_p%08lX",
                  protocolRoot(conversation.protocol),
                  static_cast<unsigned>(conversation.channel),
                  static_cast<unsigned long>(conversation.peer));
}

void SdStore::buildMessageSegmentPath(const ConversationId& conversation,
                                      uint32_t segment,
                                      char* out,
                                      std::size_t out_len)
{
    char directory[112]{};
    buildConversationDirectory(conversation, directory, sizeof(directory));
    std::snprintf(out,
                  out_len,
                  "%s/%04lu.msg",
                  directory,
                  static_cast<unsigned long>(segment));
}

void SdStore::buildProjectionPath(MeshProtocol protocol,
                                  const char* name,
                                  char* out,
                                  std::size_t out_len)
{
    if (!out || out_len == 0U)
    {
        return;
    }
    std::snprintf(out, out_len, "%s/%s", protocolRoot(protocol), name);
}

bool SdStore::ensureDirectory(const char* path)
{
    return path && path[0] != '\0' &&
           (storage_runtime::sd_is_directory(path) ||
            storage_runtime::sd_mkdir(path));
}

bool SdStore::removeTree(const char* path)
{
    if (!path || std::strncmp(path, kRoot, std::strlen(kRoot)) != 0)
    {
        return false;
    }
    if (!storage_runtime::sd_exists(path))
    {
        return true;
    }
    if (!storage_runtime::sd_is_directory(path))
    {
        return storage_runtime::sd_remove(path);
    }
    storage_runtime::SdRuntimeDir directory;
    if (!directory.open(path))
    {
        return false;
    }
    char name[96]{};
    bool is_directory = false;
    bool ok = true;
    while (directory.read_next(name, sizeof(name), &is_directory))
    {
        char child[160]{};
        std::snprintf(child, sizeof(child), "%s/%s", path, name);
        ok = (is_directory ? removeTree(child)
                           : storage_runtime::sd_remove(child)) &&
             ok;
    }
    directory.close();
    return storage_runtime::sd_rmdir(path) && ok;
}

} // namespace chat
