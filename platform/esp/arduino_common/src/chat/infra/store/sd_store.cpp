/**
 * @file sd_store.cpp
 * @brief SD-backed ESP Arduino chat storage using per-conversation log files.
 */

#include "platform/esp/arduino_common/chat/infra/store/sd_store.h"

#include "chat/infra/mesh_protocol_utils.h"
#include "platform/esp/arduino_common/storage/sd_card_runtime.h"

#if defined(ARDUINO)
#include <Arduino.h>
#endif

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

namespace chat
{
namespace
{
namespace storage = ::platform::esp::arduino_common::storage;

#if defined(ARDUINO)
#define CHAT_STORE_LOG(...) Serial.printf(__VA_ARGS__)
#else
#define CHAT_STORE_LOG(...) std::printf(__VA_ARGS__)
#endif

constexpr uint8_t kRecordHasReticulumIdentityFlag = 0x01U;
constexpr uint8_t kRecordSourceUnverifiedFlag = 0x02U;
constexpr uint8_t kRecordHasReticulumLxmfHashFlag = 0x04U;
constexpr uint8_t kIndexHasReticulumIdentityFlag = 0x01U;
constexpr uint8_t kReadStateHasReticulumIdentityFlag = 0x01U;
// FileHeader::reserved is a durable unread count once the valid bit is set.
constexpr uint16_t kUnreadStateValidMask = 0x8000U;
constexpr uint16_t kUnreadStateCountMask = 0x7FFFU;
constexpr const char* kTempIndexFile = "/chat/index.tmp";
constexpr const char* kBackupIndexFile = "/chat/index.bak";
constexpr const char* kTempReadStateFile = "/chat/read_state.tmp";
constexpr const char* kBackupReadStateFile = "/chat/read_state.bak";

static_assert(SdStore::kMaxMessagesPerConv <= kUnreadStateCountMask,
              "Unread state must cover a full conversation ring");

uint16_t encodeUnreadState(uint16_t unread)
{
    return static_cast<uint16_t>(kUnreadStateValidMask |
                                 std::min<uint16_t>(unread, kUnreadStateCountMask));
}

bool decodeUnreadState(uint16_t state, uint16_t* unread)
{
    if ((state & kUnreadStateValidMask) == 0)
    {
        return false;
    }
    if (unread)
    {
        *unread = static_cast<uint16_t>(state & kUnreadStateCountMask);
    }
    return true;
}

bool readExact(storage::SdRuntimeFile& file, void* out, size_t len)
{
    return file.read(out, len) == static_cast<int>(len);
}

bool writeExact(storage::SdRuntimeFile& file, const void* data, size_t len)
{
    return file.write(data, len) == len;
}

const char* protocolTag(MeshProtocol protocol)
{
    return chat::infra::meshProtocolSlug(protocol);
}

bool hasReticulumConversationKey(const ConversationId& conv)
{
    return conv.protocol == MeshProtocol::Reticulum &&
           hasReticulumDestinationIdentity(conv.reticulum_identity);
}

bool sameReticulumDestination(const ReticulumPeerIdentity& identity,
                              const uint8_t* destination_hash)
{
    return sameReticulumDestinationHash(identity, destination_hash);
}

bool validLxmfMessageHash(const uint8_t* lxmf_hash)
{
    return lxmf_hash &&
           !isAllZeroKeyBytes(lxmf_hash, kReticulumLxmfHashSize);
}

void copyReticulumIdentityToStorage(uint8_t* destination_hash,
                                    uint8_t* identity_hash,
                                    const ReticulumPeerIdentity& identity)
{
    if (!destination_hash || !identity_hash)
    {
        return;
    }
    (void)copyReticulumIdentityHashes(destination_hash, identity_hash, identity);
}

void readReticulumIdentityFromStorage(ReticulumPeerIdentity& identity,
                                      const uint8_t* destination_hash,
                                      const uint8_t* identity_hash)
{
    if (!destination_hash || !identity_hash)
    {
        return;
    }
    identity = makeReticulumPeerIdentity(destination_hash, identity_hash);
}

void hashToHex(const uint8_t* hash, char* out, size_t out_len)
{
    static constexpr char kHex[] = "0123456789abcdef";
    if (!hash || !out || out_len < (kReticulumPeerHashSize * 2U + 1U))
    {
        return;
    }
    for (size_t index = 0; index < kReticulumPeerHashSize; ++index)
    {
        out[index * 2U] = kHex[(hash[index] >> 4U) & 0x0FU];
        out[index * 2U + 1U] = kHex[hash[index] & 0x0FU];
    }
    out[kReticulumPeerHashSize * 2U] = '\0';
}

std::string pathInChatDir(const char* name)
{
    if (!name || name[0] == '\0')
    {
        return {};
    }
    if (name[0] == '/')
    {
        return std::string(name);
    }
    return std::string(SdStore::kDir) + "/" + name;
}
} // namespace

SdStore::SdStore()
{
    ready_ = ensureFs() && ensureDir();
    if (!ready_)
    {
        CHAT_STORE_LOG("[AppContext] chat store=SdStore layout=logs ready=0 root=%s\n", kDir);
        return;
    }

    std::vector<IndexEntry> entries;
    if (!readIndex(entries) &&
        (!recoverIndex() || !readIndex(entries)))
    {
        rebuildIndex();
        (void)readIndex(entries);
    }
    if (reconcileIndexUnread(entries))
    {
        unread_reconcile_pending_ = !writeIndex(entries);
    }
    CHAT_STORE_LOG("[AppContext] chat store=SdStore layout=logs root=%s index=%u\n",
                   kDir,
                   static_cast<unsigned>(entries.size()));
}

void SdStore::append(const ChatMessage& msg)
{
    (void)appendInternal(msg);
}

bool SdStore::appendIncomingDurably(const ChatMessage& msg)
{
    return appendInternal(msg);
}

bool SdStore::appendInternal(const ChatMessage& msg)
{
    if (!ready_ || !ensureDir())
    {
        return false;
    }

    const ConversationId conv = conversationIdForMessage(msg);
    storage::SdRuntimeFile file;
    FileHeader header{};
    if (!openConversationForUpdate(conv, file, header))
    {
        return false;
    }

    if (chat::hasReticulumLxmfMessageHash(msg))
    {
        bool already_committed = false;
        if (!conversationContainsReticulumLxmfHash(file,
                                                   header,
                                                   msg.reticulum_lxmf_hash,
                                                   &already_committed))
        {
            file.close();
            return false;
        }
        if (already_committed)
        {
            uint16_t committed_unread = 0;
            if (!readStateUnreadOrLegacy(conv, &committed_unread))
            {
                committed_unread = 0;
            }
            file.close();
            updateIndexForMessage(msg, committed_unread);
            return rememberReticulumLxmfMessageHash(msg.reticulum_lxmf_hash);
        }
    }

    uint16_t unread = 0;
    if (!readStateUnreadOrLegacy(conv, &unread))
    {
        unread = 0;
    }
    if (msg.status == MessageStatus::Incoming && unread < kUnreadStateCountMask)
    {
        unread = static_cast<uint16_t>(unread + 1U);
    }

    const Record rec = recordFromMessage(msg);
    if (!writeRecord(file, header.head, rec))
    {
        file.close();
        return false;
    }

    header.head = static_cast<uint16_t>((header.head + 1U) % kMaxMessagesPerConv);
    if (header.count < kMaxMessagesPerConv)
    {
        header.count = static_cast<uint16_t>(header.count + 1U);
    }

    header.reserved = encodeUnreadState(unread);
    if (!file.seek(0) || !writeExact(file, &header, sizeof(header)))
    {
        file.close();
        return false;
    }
    if (!file.flush())
    {
        file.close();
        return false;
    }
    file.close();

    if (!writeReadStateUnread(conv, unread))
    {
        CHAT_STORE_LOG("[AppContext] chat unread persist failed stage=read_state unread=%u\n",
                       static_cast<unsigned>(unread));
        unread_reconcile_pending_ = true;
        if (!removeReadStateEntry(conv))
        {
            return false;
        }
    }
    updateIndexForMessage(msg, unread);
    if (chat::hasReticulumLxmfMessageHash(msg))
    {
        return rememberReticulumLxmfMessageHash(msg.reticulum_lxmf_hash);
    }
    return true;
}

std::vector<ChatMessage> SdStore::loadRecent(const ConversationId& conv, size_t n)
{
    return loadPageFromLatest(conv, 0, n, nullptr);
}

std::vector<ChatMessage> SdStore::loadPageFromLatest(const ConversationId& conv,
                                                     size_t offset_from_latest,
                                                     size_t limit,
                                                     size_t* total)
{
    std::vector<ChatMessage> out;
    if (total)
    {
        *total = 0;
    }
    if (!ready_ || limit == 0)
    {
        return out;
    }

    char path[96]{};
    buildConversationPath(conv, path, sizeof(path));
    if (!storage::sd_exists(path))
    {
        return out;
    }

    storage::SdRuntimeFile file;
    if (!file.open(path, "r"))
    {
        return out;
    }

    FileHeader header{};
    if (!loadFileHeader(file, header) || header.count == 0)
    {
        file.close();
        return out;
    }

    if (total)
    {
        *total = header.count;
    }
    if (offset_from_latest >= header.count)
    {
        file.close();
        return out;
    }

    const size_t available = header.count - offset_from_latest;
    const size_t to_read = std::min<size_t>(limit, available);
    const size_t logical_start = header.count - offset_from_latest - to_read;
    const uint16_t start = static_cast<uint16_t>(
        (header.head + kMaxMessagesPerConv - header.count + logical_start) %
        kMaxMessagesPerConv);

    out.reserve(to_read);
    for (size_t index = 0; index < to_read; ++index)
    {
        const uint16_t slot = static_cast<uint16_t>((start + index) % kMaxMessagesPerConv);
        Record rec{};
        if (!readRecord(file, header, slot, rec) || rec.text_len == 0)
        {
            continue;
        }
        out.push_back(messageFromRecord(rec));
    }
    file.close();
    return out;
}

std::vector<ConversationMeta> SdStore::loadConversationPage(size_t offset,
                                                            size_t limit,
                                                            size_t* total)
{
    std::vector<IndexEntry> entries;
    if (!ready_ || !ensureIndex(entries))
    {
        if (total)
        {
            *total = 0;
        }
        return {};
    }

    std::sort(entries.begin(),
              entries.end(),
              [](const IndexEntry& a, const IndexEntry& b)
              {
                  return a.last_timestamp > b.last_timestamp;
              });

    if (total)
    {
        *total = entries.size();
    }
    if (offset >= entries.size())
    {
        return {};
    }

    size_t end = entries.size();
    if (limit != 0 && offset + limit < end)
    {
        end = offset + limit;
    }

    std::vector<ConversationMeta> list;
    list.reserve(end - offset);
    for (size_t index = offset; index < end; ++index)
    {
        list.push_back(metaFromIndexEntry(entries[index]));
    }
    return list;
}

bool SdStore::setUnread(const ConversationId& conv, int unread)
{
    if (!ready_)
    {
        return false;
    }

    const uint16_t unread_count = static_cast<uint16_t>(
        std::min<int>(std::max(0, unread), kUnreadStateCountMask));
    if (!writeReadStateUnread(conv, unread_count))
    {
        CHAT_STORE_LOG("[AppContext] chat unread persist failed stage=read_state unread=%u\n",
                       static_cast<unsigned>(unread_count));
        return false;
    }

    const bool header_written = writeConversationUnread(conv, unread_count);
    if (!header_written)
    {
        CHAT_STORE_LOG("[AppContext] chat unread persist failed stage=conversation_header unread=%u\n",
                       static_cast<unsigned>(unread_count));
    }

    std::vector<IndexEntry> entries;
    if (!ensureIndex(entries))
    {
        unread_reconcile_pending_ = true;
        return true;
    }

    size_t index = 0;
    if (!findIndexEntry(conv, entries, &index))
    {
        return true;
    }
    entries[index].unread = unread_count;
    const bool index_written = writeIndex(entries);
    if (!index_written)
    {
        CHAT_STORE_LOG("[AppContext] chat unread persist failed stage=index unread=%u\n",
                       static_cast<unsigned>(unread_count));
        unread_reconcile_pending_ = true;
        rebuildIndex();
    }
    return true;
}

int SdStore::getUnread(const ConversationId& conv) const
{
    if (!ready_)
    {
        return 0;
    }

    uint16_t unread = 0;
    if (readStateUnreadOrLegacy(conv, &unread))
    {
        return unread;
    }
    return 0;
}

void SdStore::clearConversation(const ConversationId& conv)
{
    if (!ready_)
    {
        return;
    }

    (void)removeReadStateEntry(conv);

    char path[96]{};
    buildConversationPath(conv, path, sizeof(path));
    if (storage::sd_exists(path))
    {
        storage::sd_remove(path);
    }

    std::vector<IndexEntry> entries;
    if (!readIndex(entries))
    {
        return;
    }
    entries.erase(std::remove_if(entries.begin(),
                                 entries.end(),
                                 [&](const IndexEntry& entry)
                                 {
                                     return indexEntryMatchesConversation(entry, conv);
                                 }),
                  entries.end());
    (void)writeIndex(entries);
}

void SdStore::clearAll()
{
    if (!ready_)
    {
        return;
    }

    if (storage::sd_exists(kIndexFile))
    {
        storage::sd_remove(kIndexFile);
    }
    if (storage::sd_exists(kTempIndexFile))
    {
        storage::sd_remove(kTempIndexFile);
    }
    if (storage::sd_exists(kBackupIndexFile))
    {
        storage::sd_remove(kBackupIndexFile);
    }
    if (storage::sd_exists(kReadStateFile))
    {
        storage::sd_remove(kReadStateFile);
    }
    if (storage::sd_exists(kTempReadStateFile))
    {
        storage::sd_remove(kTempReadStateFile);
    }
    if (storage::sd_exists(kBackupReadStateFile))
    {
        storage::sd_remove(kBackupReadStateFile);
    }

    std::vector<std::string> log_paths;
    storage::SdRuntimeDir dir;
    if (dir.open(kDir))
    {
        char name[96]{};
        bool is_dir = false;
        while (dir.read_next(name, sizeof(name), &is_dir))
        {
            if (!is_dir && hasLogSuffix(name))
            {
                log_paths.push_back(pathInChatDir(name));
            }
        }
        dir.close();
    }

    for (const auto& path : log_paths)
    {
        if (!path.empty() && storage::sd_exists(path.c_str()))
        {
            storage::sd_remove(path.c_str());
        }
    }
    unread_reconcile_pending_ = false;
}

bool SdStore::updateMessageStatus(MessageId msg_id, MessageStatus status)
{
    if (!ready_ || msg_id == 0)
    {
        return false;
    }

    std::vector<IndexEntry> entries;
    if (!readIndex(entries))
    {
        return false;
    }

    bool updated = false;
    for (auto& entry : entries)
    {
        const ConversationId conv = conversationFromIndexEntry(entry);
        char path[96]{};
        buildConversationPath(conv, path, sizeof(path));
        if (!storage::sd_exists(path))
        {
            continue;
        }

        storage::SdRuntimeFile file;
        if (!file.open(path, "r+"))
        {
            continue;
        }

        FileHeader header{};
        if (!loadFileHeader(file, header) ||
            !upgradeConversationFile(file, path, header))
        {
            file.close();
            continue;
        }

        for (uint16_t index = 0; index < header.count; ++index)
        {
            const uint16_t slot =
                static_cast<uint16_t>((header.head + kMaxMessagesPerConv - header.count + index) %
                                      kMaxMessagesPerConv);
            Record rec{};
            if (!readRecord(file, header, slot, rec))
            {
                continue;
            }
            if (rec.msg_id != msg_id || rec.from != 0)
            {
                continue;
            }

            rec.status = static_cast<uint8_t>(status);
            updated = writeRecord(file, slot, rec);
            if (updated)
            {
                file.flush();
                if (entry.last_msg_id == msg_id)
                {
                    entry.status = static_cast<uint8_t>(status);
                }
            }
            break;
        }

        file.close();
        if (updated)
        {
            break;
        }
    }

    if (updated)
    {
        (void)writeIndex(entries);
    }
    return updated;
}

bool SdStore::updateMessageStatusForProtocol(MessageId msg_id,
                                             MeshProtocol protocol,
                                             MessageStatus status)
{
    if (!ready_ || msg_id == 0)
    {
        return false;
    }

    std::vector<IndexEntry> entries;
    if (!readIndex(entries))
    {
        return false;
    }

    bool updated = false;
    for (auto& entry : entries)
    {
        if (static_cast<MeshProtocol>(entry.protocol) != protocol)
        {
            continue;
        }
        const ConversationId conv = conversationFromIndexEntry(entry);
        char path[96]{};
        buildConversationPath(conv, path, sizeof(path));
        if (!storage::sd_exists(path))
        {
            continue;
        }

        storage::SdRuntimeFile file;
        if (!file.open(path, "r+"))
        {
            continue;
        }

        FileHeader header{};
        if (!loadFileHeader(file, header) ||
            !upgradeConversationFile(file, path, header))
        {
            file.close();
            continue;
        }

        for (uint16_t index = 0; index < header.count; ++index)
        {
            const uint16_t slot =
                static_cast<uint16_t>((header.head + kMaxMessagesPerConv - header.count + index) %
                                      kMaxMessagesPerConv);
            Record rec{};
            if (!readRecord(file, header, slot, rec))
            {
                continue;
            }
            if (rec.msg_id != msg_id || rec.from != 0 ||
                static_cast<MeshProtocol>(rec.protocol) != protocol)
            {
                continue;
            }

            rec.status = static_cast<uint8_t>(status);
            updated = writeRecord(file, slot, rec);
            if (updated)
            {
                file.flush();
                if (entry.last_msg_id == msg_id)
                {
                    entry.status = static_cast<uint8_t>(status);
                }
            }
            break;
        }

        file.close();
        if (updated)
        {
            break;
        }
    }

    if (updated)
    {
        (void)writeIndex(entries);
    }
    return updated;
}

bool SdStore::getMessage(MessageId msg_id, ChatMessage* out) const
{
    if (!ready_ || msg_id == 0)
    {
        return false;
    }

    std::vector<IndexEntry> entries;
    if (!readIndex(entries))
    {
        return false;
    }

    for (const auto& entry : entries)
    {
        const ConversationId conv = conversationFromIndexEntry(entry);
        char path[96]{};
        buildConversationPath(conv, path, sizeof(path));
        if (!storage::sd_exists(path))
        {
            continue;
        }

        storage::SdRuntimeFile file;
        if (!file.open(path, "r"))
        {
            continue;
        }

        FileHeader header{};
        if (!loadFileHeader(file, header))
        {
            file.close();
            continue;
        }

        for (uint16_t index = 0; index < header.count; ++index)
        {
            const uint16_t slot =
                static_cast<uint16_t>((header.head + kMaxMessagesPerConv - header.count + index) %
                                      kMaxMessagesPerConv);
            Record rec{};
            if (!readRecord(file, header, slot, rec) || rec.text_len == 0 || rec.msg_id != msg_id)
            {
                continue;
            }
            if (out)
            {
                *out = messageFromRecord(rec);
            }
            file.close();
            return true;
        }
        file.close();
    }
    return false;
}

bool SdStore::getMessageForProtocol(MessageId msg_id,
                                    MeshProtocol protocol,
                                    ChatMessage* out) const
{
    if (!ready_ || msg_id == 0)
    {
        return false;
    }

    std::vector<IndexEntry> entries;
    if (!readIndex(entries))
    {
        return false;
    }

    for (const auto& entry : entries)
    {
        if (static_cast<MeshProtocol>(entry.protocol) != protocol)
        {
            continue;
        }
        const ConversationId conv = conversationFromIndexEntry(entry);
        char path[96]{};
        buildConversationPath(conv, path, sizeof(path));
        if (!storage::sd_exists(path))
        {
            continue;
        }

        storage::SdRuntimeFile file;
        if (!file.open(path, "r"))
        {
            continue;
        }

        FileHeader header{};
        if (!loadFileHeader(file, header))
        {
            file.close();
            continue;
        }

        for (uint16_t index = 0; index < header.count; ++index)
        {
            const uint16_t slot =
                static_cast<uint16_t>((header.head + kMaxMessagesPerConv - header.count + index) %
                                      kMaxMessagesPerConv);
            Record rec{};
            if (!readRecord(file, header, slot, rec) || rec.text_len == 0 ||
                rec.msg_id != msg_id ||
                static_cast<MeshProtocol>(rec.protocol) != protocol)
            {
                continue;
            }
            if (out)
            {
                *out = messageFromRecord(rec);
            }
            file.close();
            return true;
        }
        file.close();
    }
    return false;
}

bool SdStore::hasReticulumLxmfMessageHash(const uint8_t* lxmf_hash) const
{
    if (!ready_ || !validLxmfMessageHash(lxmf_hash))
    {
        return false;
    }

    storage::SdRuntimeFile file;
    if (!file.open(kReticulumLxmfSeenFile, "r"))
    {
        return false;
    }

    LxmfSeenHeader header{};
    if (!loadLxmfSeenHeader(file, header))
    {
        file.close();
        return false;
    }

    for (uint16_t index = 0; index < header.count; ++index)
    {
        const uint16_t slot =
            static_cast<uint16_t>((header.head + kMaxReticulumLxmfSeen - header.count + index) %
                                  kMaxReticulumLxmfSeen);
        LxmfSeenRecord rec{};
        if (!readLxmfSeenRecord(file, slot, rec))
        {
            continue;
        }
        if (std::memcmp(rec.hash, lxmf_hash, kReticulumLxmfHashSize) == 0)
        {
            file.close();
            return true;
        }
    }
    file.close();
    return false;
}

void SdStore::flush()
{
}

bool SdStore::ensureFs() const
{
    return storage::sd_card_ready();
}

bool SdStore::ensureDir() const
{
    if (!ensureFs())
    {
        return false;
    }
    if (storage::sd_is_directory(kDir))
    {
        return true;
    }
    return storage::sd_mkdir(kDir);
}

bool SdStore::recoverIndex() const
{
    if (storage::sd_exists(kIndexFile))
    {
        return false;
    }
    const auto restore_candidate = [this](const char* path, const char* source)
    {
        if (!storage::sd_exists(path) || !storage::sd_rename(path, kIndexFile))
        {
            return false;
        }
        std::vector<IndexEntry> recovered;
        if (readIndex(recovered))
        {
            CHAT_STORE_LOG("[AppContext] chat store=SdStore recovered index source=%s\n",
                           source);
            return true;
        }
        (void)storage::sd_remove(kIndexFile);
        return false;
    };

    if (restore_candidate(kTempIndexFile, "temp"))
    {
        return true;
    }
    return restore_candidate(kBackupIndexFile, "backup");
}

bool SdStore::reconcileIndexUnread(std::vector<IndexEntry>& entries) const
{
    bool changed = false;
    for (auto& entry : entries)
    {
        const ConversationId conv = conversationFromIndexEntry(entry);
        uint16_t durable_unread = 0;
        if (!readStateUnreadOrLegacy(conv, &durable_unread))
        {
            durable_unread = entry.unread;
            (void)writeReadStateUnread(conv, durable_unread);
        }
        if (entry.unread != durable_unread)
        {
            entry.unread = durable_unread;
            changed = true;
        }
    }
    return changed;
}

bool SdStore::readIndex(std::vector<IndexEntry>& entries) const
{
    entries.clear();
    if (!ensureFs() || !storage::sd_exists(kIndexFile))
    {
        return false;
    }

    storage::SdRuntimeFile file;
    if (!file.open(kIndexFile, "r"))
    {
        return false;
    }

    IndexHeader header{};
    if (!readExact(file, &header, sizeof(header)) ||
        header.magic != kIndexMagic ||
        (header.version != kIndexVersion && header.version != kLegacyVersion))
    {
        file.close();
        return false;
    }

    entries.reserve(header.count);
    bool ok = true;
    for (uint16_t index = 0; ok && index < header.count; ++index)
    {
        IndexEntry entry{};
        if (header.version == kLegacyVersion)
        {
            IndexEntryV2 legacy_entry{};
            ok = readExact(file, &legacy_entry, sizeof(legacy_entry));
            if (ok)
            {
                entry.protocol = legacy_entry.protocol;
                entry.channel = legacy_entry.channel;
                entry.status = legacy_entry.status;
                entry.unread = legacy_entry.unread;
                entry.peer = legacy_entry.peer;
                entry.last_msg_id = legacy_entry.last_msg_id;
                entry.last_timestamp = legacy_entry.last_timestamp;
                entry.last_from = legacy_entry.last_from;
                entry.preview_len = legacy_entry.preview_len;
                std::memcpy(entry.preview,
                            legacy_entry.preview,
                            sizeof(entry.preview));
            }
        }
        else
        {
            ok = readExact(file, &entry, sizeof(entry));
        }
        if (ok)
        {
            entries.push_back(entry);
        }
    }
    file.close();
    if (!ok)
    {
        entries.clear();
        return false;
    }
    return true;
}

bool SdStore::writeIndex(const std::vector<IndexEntry>& entries) const
{
    if (!ensureDir())
    {
        return false;
    }

    if (storage::sd_exists(kTempIndexFile))
    {
        storage::sd_remove(kTempIndexFile);
    }

    storage::SdRuntimeFile file;
    if (!file.open(kTempIndexFile, "w"))
    {
        return false;
    }

    IndexHeader header{};
    header.magic = kIndexMagic;
    header.version = kIndexVersion;
    header.count = static_cast<uint16_t>(std::min<size_t>(entries.size(), 0xFFFFU));
    bool ok = writeExact(file, &header, sizeof(header));
    for (size_t index = 0; ok && index < header.count; ++index)
    {
        ok = writeExact(file, &entries[index], sizeof(IndexEntry));
    }
    file.flush();
    file.close();

    if (!ok)
    {
        storage::sd_remove(kTempIndexFile);
        return false;
    }

    const bool had_index = storage::sd_exists(kIndexFile);
    if (had_index)
    {
        if (storage::sd_exists(kBackupIndexFile) &&
            !storage::sd_remove(kBackupIndexFile))
        {
            storage::sd_remove(kTempIndexFile);
            return false;
        }
        if (!storage::sd_rename(kIndexFile, kBackupIndexFile))
        {
            storage::sd_remove(kTempIndexFile);
            return false;
        }
    }
    if (!storage::sd_rename(kTempIndexFile, kIndexFile))
    {
        if (had_index && !storage::sd_exists(kIndexFile))
        {
            (void)storage::sd_rename(kBackupIndexFile, kIndexFile);
        }
        storage::sd_remove(kTempIndexFile);
        return false;
    }
    if (storage::sd_exists(kBackupIndexFile))
    {
        (void)storage::sd_remove(kBackupIndexFile);
    }
    return true;
}

bool SdStore::readReadState(std::vector<ReadStateEntry>& entries) const
{
    entries.clear();
    if (!ensureFs() || !storage::sd_exists(kReadStateFile))
    {
        return false;
    }

    storage::SdRuntimeFile file;
    if (!file.open(kReadStateFile, "r"))
    {
        return false;
    }

    ReadStateHeader header{};
    if (!readExact(file, &header, sizeof(header)) ||
        header.magic != kReadStateMagic ||
        header.version != kReadStateVersion)
    {
        file.close();
        return false;
    }

    entries.reserve(header.count);
    bool ok = true;
    for (uint16_t index = 0; ok && index < header.count; ++index)
    {
        ReadStateEntry entry{};
        ok = readExact(file, &entry, sizeof(entry));
        if (ok)
        {
            entries.push_back(entry);
        }
    }
    file.close();
    if (!ok)
    {
        entries.clear();
        return false;
    }
    return true;
}

bool SdStore::writeReadState(const std::vector<ReadStateEntry>& entries) const
{
    if (!ensureDir())
    {
        return false;
    }

    if (storage::sd_exists(kTempReadStateFile))
    {
        storage::sd_remove(kTempReadStateFile);
    }

    storage::SdRuntimeFile file;
    if (!file.open(kTempReadStateFile, "w"))
    {
        return false;
    }

    ReadStateHeader header{};
    header.magic = kReadStateMagic;
    header.version = kReadStateVersion;
    header.count = static_cast<uint16_t>(std::min<size_t>(entries.size(), 0xFFFFU));
    bool ok = writeExact(file, &header, sizeof(header));
    for (size_t index = 0; ok && index < header.count; ++index)
    {
        ok = writeExact(file, &entries[index], sizeof(ReadStateEntry));
    }
    file.flush();
    file.close();

    if (!ok)
    {
        storage::sd_remove(kTempReadStateFile);
        return false;
    }

    const bool had_state = storage::sd_exists(kReadStateFile);
    if (had_state)
    {
        if (storage::sd_exists(kBackupReadStateFile) &&
            !storage::sd_remove(kBackupReadStateFile))
        {
            storage::sd_remove(kTempReadStateFile);
            return false;
        }
        if (!storage::sd_rename(kReadStateFile, kBackupReadStateFile))
        {
            storage::sd_remove(kTempReadStateFile);
            return false;
        }
    }
    if (!storage::sd_rename(kTempReadStateFile, kReadStateFile))
    {
        if (had_state && !storage::sd_exists(kReadStateFile))
        {
            (void)storage::sd_rename(kBackupReadStateFile, kReadStateFile);
        }
        storage::sd_remove(kTempReadStateFile);
        return false;
    }
    if (storage::sd_exists(kBackupReadStateFile))
    {
        (void)storage::sd_remove(kBackupReadStateFile);
    }
    return true;
}

bool SdStore::findReadStateEntry(const ConversationId& conv,
                                 std::vector<ReadStateEntry>& entries,
                                 size_t* out_idx) const
{
    return findReadStateEntry(
        conv,
        static_cast<const std::vector<ReadStateEntry>&>(entries),
        out_idx);
}

bool SdStore::findReadStateEntry(const ConversationId& conv,
                                 const std::vector<ReadStateEntry>& entries,
                                 size_t* out_idx) const
{
    for (size_t index = 0; index < entries.size(); ++index)
    {
        if (readStateEntryMatchesConversation(entries[index], conv))
        {
            if (out_idx)
            {
                *out_idx = index;
            }
            return true;
        }
    }
    return false;
}

bool SdStore::readStateUnreadOnly(const ConversationId& conv, uint16_t* unread) const
{
    if (!unread)
    {
        return false;
    }
    std::vector<ReadStateEntry> entries;
    if (!readReadState(entries))
    {
        return false;
    }
    size_t index = 0;
    if (!findReadStateEntry(conv, entries, &index))
    {
        return false;
    }
    *unread = entries[index].unread;
    return true;
}

bool SdStore::readStateUnreadOrLegacy(const ConversationId& conv, uint16_t* unread) const
{
    if (!unread)
    {
        return false;
    }
    if (readStateUnreadOnly(conv, unread))
    {
        return true;
    }
    if (readConversationUnread(conv, unread))
    {
        (void)writeReadStateUnread(conv, *unread);
        return true;
    }
    std::vector<IndexEntry> entries;
    size_t index = 0;
    if (readIndex(entries) && findIndexEntry(conv, entries, &index))
    {
        *unread = entries[index].unread;
        (void)writeReadStateUnread(conv, *unread);
        return true;
    }
    return false;
}

bool SdStore::writeReadStateUnread(const ConversationId& conv, uint16_t unread) const
{
    std::vector<ReadStateEntry> entries;
    if (!readReadState(entries))
    {
        entries.clear();
    }

    size_t index = 0;
    if (findReadStateEntry(conv, entries, &index))
    {
        entries[index] = readStateEntryFromConversation(conv, unread);
    }
    else
    {
        entries.push_back(readStateEntryFromConversation(conv, unread));
    }
    return writeReadState(entries);
}

bool SdStore::removeReadStateEntry(const ConversationId& conv) const
{
    std::vector<ReadStateEntry> entries;
    if (!readReadState(entries))
    {
        return true;
    }
    const size_t before = entries.size();
    entries.erase(std::remove_if(entries.begin(),
                                 entries.end(),
                                 [&](const ReadStateEntry& entry)
                                 {
                                     return readStateEntryMatchesConversation(entry, conv);
                                 }),
                  entries.end());
    if (entries.size() == before)
    {
        return true;
    }
    return writeReadState(entries);
}

bool SdStore::ensureIndex(std::vector<IndexEntry>& entries)
{
    bool loaded = readIndex(entries);
    if (!loaded && recoverIndex())
    {
        loaded = readIndex(entries);
    }
    if (!loaded)
    {
        rebuildIndex();
        loaded = readIndex(entries);
    }
    if (!loaded)
    {
        return false;
    }
    if (unread_reconcile_pending_)
    {
        const bool changed = reconcileIndexUnread(entries);
        if (!changed || writeIndex(entries))
        {
            unread_reconcile_pending_ = false;
        }
    }
    return true;
}

bool SdStore::findIndexEntry(const ConversationId& conv,
                             std::vector<IndexEntry>& entries,
                             size_t* out_idx) const
{
    return findIndexEntry(conv,
                          static_cast<const std::vector<IndexEntry>&>(entries),
                          out_idx);
}

bool SdStore::findIndexEntry(const ConversationId& conv,
                             const std::vector<IndexEntry>& entries,
                             size_t* out_idx) const
{
    for (size_t index = 0; index < entries.size(); ++index)
    {
        const IndexEntry& entry = entries[index];
        if (indexEntryMatchesConversation(entry, conv))
        {
            if (out_idx)
            {
                *out_idx = index;
            }
            return true;
        }
    }
    return false;
}

void SdStore::updateIndexForMessage(const ChatMessage& msg, uint16_t unread)
{
    std::vector<IndexEntry> entries;
    if (!ensureIndex(entries))
    {
        entries.clear();
    }

    const ConversationId conv = conversationIdForMessage(msg);
    size_t index = 0;
    if (!findIndexEntry(conv, entries, &index))
    {
        IndexEntry entry{};
        entry.protocol = static_cast<uint8_t>(msg.protocol);
        entry.channel = static_cast<uint8_t>(msg.channel);
        entry.peer = msg.peer;
        if (hasReticulumConversationKey(conv))
        {
            entry.flags |= kIndexHasReticulumIdentityFlag;
            copyReticulumIdentityToStorage(entry.reticulum_destination_hash,
                                           entry.reticulum_identity_hash,
                                           conv.reticulum_identity);
        }
        entries.push_back(entry);
        index = entries.size() - 1;
    }

    IndexEntry& entry = entries[index];
    entry.unread = std::min<uint16_t>(unread, kUnreadStateCountMask);
    entry.protocol = static_cast<uint8_t>(msg.protocol);
    entry.channel = static_cast<uint8_t>(msg.channel);
    entry.status = static_cast<uint8_t>(msg.status);
    entry.peer = msg.peer;
    entry.flags &= static_cast<uint8_t>(~kIndexHasReticulumIdentityFlag);
    std::memset(entry.reticulum_destination_hash, 0, sizeof(entry.reticulum_destination_hash));
    std::memset(entry.reticulum_identity_hash, 0, sizeof(entry.reticulum_identity_hash));
    if (hasReticulumConversationKey(conv))
    {
        entry.flags |= kIndexHasReticulumIdentityFlag;
        copyReticulumIdentityToStorage(entry.reticulum_destination_hash,
                                       entry.reticulum_identity_hash,
                                       conv.reticulum_identity);
    }
    entry.last_msg_id = msg.msg_id;
    entry.last_timestamp = msg.timestamp;
    entry.last_from = msg.from;
    entry.preview_len = static_cast<uint16_t>(std::min<size_t>(msg.text.size(), kPreviewLen));
    std::memset(entry.preview, 0, sizeof(entry.preview));
    if (entry.preview_len > 0)
    {
        std::memcpy(entry.preview, msg.text.data(), entry.preview_len);
    }
    const bool index_written = writeIndex(entries);
    if (!index_written)
    {
        unread_reconcile_pending_ = true;
        rebuildIndex();
    }
}

void SdStore::rebuildIndex()
{
    if (!ensureDir())
    {
        return;
    }

    std::vector<IndexEntry> entries;
    storage::SdRuntimeDir dir;
    if (!dir.open(kDir))
    {
        return;
    }

    char name[96]{};
    bool is_dir = false;
    while (dir.read_next(name, sizeof(name), &is_dir))
    {
        if (is_dir || !hasLogSuffix(name))
        {
            continue;
        }

        const std::string path = pathInChatDir(name);
        storage::SdRuntimeFile file;
        if (path.empty() || !file.open(path.c_str(), "r"))
        {
            continue;
        }

        FileHeader header{};
        if (!loadFileHeader(file, header))
        {
            file.close();
            continue;
        }

        ChatMessage last_msg;
        bool have_last = false;
        uint16_t unread = 0;
        const bool has_header_unread = decodeUnreadState(header.reserved, &unread);
        for (uint16_t index = 0; index < header.count; ++index)
        {
            const uint16_t slot =
                static_cast<uint16_t>((header.head + kMaxMessagesPerConv - header.count + index) %
                                      kMaxMessagesPerConv);
            Record rec{};
            if (!readRecord(file, header, slot, rec) || rec.text_len == 0)
            {
                continue;
            }
            ChatMessage msg = messageFromRecord(rec);
            if (!has_header_unread &&
                msg.status == MessageStatus::Incoming &&
                unread < kUnreadStateCountMask)
            {
                ++unread;
            }
            if (!have_last || msg.timestamp >= last_msg.timestamp)
            {
                last_msg = msg;
                have_last = true;
            }
        }
        file.close();

        if (!have_last)
        {
            continue;
        }

        const ConversationId conv = conversationIdForMessage(last_msg);
        uint16_t ledger_unread = 0;
        if (readStateUnreadOrLegacy(conv, &ledger_unread))
        {
            unread = ledger_unread;
        }

        IndexEntry entry{};
        entry.protocol = static_cast<uint8_t>(last_msg.protocol);
        entry.channel = static_cast<uint8_t>(last_msg.channel);
        entry.status = static_cast<uint8_t>(last_msg.status);
        entry.unread = unread;
        entry.peer = last_msg.peer;
        if (last_msg.protocol == MeshProtocol::Reticulum &&
            hasReticulumDestinationIdentity(last_msg.reticulum_identity))
        {
            entry.flags |= kIndexHasReticulumIdentityFlag;
            copyReticulumIdentityToStorage(entry.reticulum_destination_hash,
                                           entry.reticulum_identity_hash,
                                           last_msg.reticulum_identity);
        }
        entry.last_msg_id = last_msg.msg_id;
        entry.last_timestamp = last_msg.timestamp;
        entry.last_from = last_msg.from;
        entry.preview_len = static_cast<uint16_t>(std::min<size_t>(last_msg.text.size(), kPreviewLen));
        if (entry.preview_len > 0)
        {
            std::memcpy(entry.preview, last_msg.text.data(), entry.preview_len);
        }
        entries.push_back(entry);
        if (!readStateUnreadOnly(conv, &ledger_unread))
        {
            (void)writeReadStateUnread(conv, unread);
        }
        if (!has_header_unread)
        {
            (void)writeConversationUnread(conv, unread);
        }
    }
    dir.close();

    unread_reconcile_pending_ = !writeIndex(entries);
    CHAT_STORE_LOG("[AppContext] chat store=SdStore rebuild index entries=%u\n",
                   static_cast<unsigned>(entries.size()));
}

bool SdStore::loadFileHeader(storage::SdRuntimeFile& file, FileHeader& header) const
{
    if (!file.is_open() || file.size() < sizeof(FileHeader))
    {
        return false;
    }
    if (!file.seek(0) || !readExact(file, &header, sizeof(header)))
    {
        return false;
    }
    return header.magic == kFileMagic &&
           (header.version == kFileVersion ||
            header.version == kRxOriginVersion ||
            header.version == kReticulumIdentityVersion ||
            header.version == kLegacyVersion) &&
           header.head < kMaxMessagesPerConv && header.count <= kMaxMessagesPerConv;
}

bool SdStore::initFileHeader(storage::SdRuntimeFile& file) const
{
    FileHeader header{};
    header.magic = kFileMagic;
    header.version = kFileVersion;
    if (!file.seek(0) || !writeExact(file, &header, sizeof(header)))
    {
        return false;
    }
    return file.flush();
}

bool SdStore::upgradeConversationFile(storage::SdRuntimeFile& file,
                                      const char* path,
                                      FileHeader& header) const
{
    if (header.version == kFileVersion)
    {
        return true;
    }
    if ((header.version != kLegacyVersion &&
         header.version != kReticulumIdentityVersion &&
         header.version != kRxOriginVersion) ||
        !path || path[0] == '\0')
    {
        return false;
    }

    std::vector<Record> records;
    records.reserve(header.count);
    for (uint16_t index = 0; index < header.count; ++index)
    {
        const uint16_t slot =
            static_cast<uint16_t>((header.head + kMaxMessagesPerConv - header.count + index) %
                                  kMaxMessagesPerConv);
        Record rec{};
        if (!readRecord(file, header, slot, rec))
        {
            return false;
        }
        records.push_back(rec);
    }

    file.close();
    const std::string temp_path = std::string(path) + ".upgrade";
    const std::string backup_path = std::string(path) + ".bak";
    if (storage::sd_exists(temp_path.c_str()))
    {
        (void)storage::sd_remove(temp_path.c_str());
    }
    if (storage::sd_exists(backup_path.c_str()) &&
        !storage::sd_remove(backup_path.c_str()))
    {
        return false;
    }
    if (!file.open(temp_path.c_str(), "w+"))
    {
        return false;
    }

    FileHeader upgraded{};
    upgraded.magic = kFileMagic;
    upgraded.version = kFileVersion;
    upgraded.reserved = header.reserved;
    upgraded.count = static_cast<uint16_t>(std::min<size_t>(records.size(), kMaxMessagesPerConv));
    upgraded.head = static_cast<uint16_t>(upgraded.count % kMaxMessagesPerConv);
    if (!file.seek(0) || !writeExact(file, &upgraded, sizeof(upgraded)))
    {
        file.close();
        (void)storage::sd_remove(temp_path.c_str());
        return false;
    }
    for (uint16_t index = 0; index < upgraded.count; ++index)
    {
        if (!writeRecord(file, index, records[index]))
        {
            file.close();
            (void)storage::sd_remove(temp_path.c_str());
            return false;
        }
    }
    if (!file.flush())
    {
        file.close();
        (void)storage::sd_remove(temp_path.c_str());
        return false;
    }
    file.close();

    if (!storage::sd_rename(path, backup_path.c_str()))
    {
        (void)storage::sd_remove(temp_path.c_str());
        return false;
    }
    if (!storage::sd_rename(temp_path.c_str(), path))
    {
        (void)storage::sd_rename(backup_path.c_str(), path);
        (void)storage::sd_remove(temp_path.c_str());
        return false;
    }
    if (!file.open(path, "r+") || !loadFileHeader(file, header) ||
        header.version != kFileVersion)
    {
        file.close();
        (void)storage::sd_remove(path);
        (void)storage::sd_rename(backup_path.c_str(), path);
        return false;
    }
    (void)storage::sd_remove(backup_path.c_str());
    return true;
}

bool SdStore::readRecord(storage::SdRuntimeFile& file,
                         const FileHeader& header,
                         uint16_t slot,
                         Record& rec) const
{
    if (slot >= kMaxMessagesPerConv)
    {
        return false;
    }
    size_t record_size = sizeof(Record);
    if (header.version == kLegacyVersion)
    {
        record_size = sizeof(RecordV2);
    }
    else if (header.version == kReticulumIdentityVersion)
    {
        record_size = sizeof(RecordV3);
    }
    else if (header.version == kRxOriginVersion)
    {
        record_size = sizeof(RecordV4);
    }
    const uint64_t offset = sizeof(FileHeader) + static_cast<uint64_t>(slot) * record_size;
    if (file.size() < offset + record_size)
    {
        return false;
    }
    if (!file.seek(offset))
    {
        return false;
    }
    if (header.version == kFileVersion)
    {
        return readExact(file, &rec, sizeof(rec));
    }

    if (header.version == kRxOriginVersion)
    {
        RecordV4 rec_v4{};
        if (!readExact(file, &rec_v4, sizeof(rec_v4)))
        {
            return false;
        }
        rec.protocol = rec_v4.protocol;
        rec.channel = rec_v4.channel;
        rec.status = rec_v4.status;
        rec.flags = rec_v4.flags;
        rec.rx_origin = rec_v4.rx_origin;
        rec.text_len = rec_v4.text_len;
        rec.from = rec_v4.from;
        rec.peer = rec_v4.peer;
        rec.msg_id = rec_v4.msg_id;
        rec.timestamp = rec_v4.timestamp;
        std::memcpy(rec.reticulum_destination_hash,
                    rec_v4.reticulum_destination_hash,
                    sizeof(rec.reticulum_destination_hash));
        std::memcpy(rec.reticulum_identity_hash,
                    rec_v4.reticulum_identity_hash,
                    sizeof(rec.reticulum_identity_hash));
        std::memcpy(rec.text, rec_v4.text, sizeof(rec.text));
        return true;
    }

    if (header.version == kReticulumIdentityVersion)
    {
        RecordV3 rec_v3{};
        if (!readExact(file, &rec_v3, sizeof(rec_v3)))
        {
            return false;
        }
        rec.protocol = rec_v3.protocol;
        rec.channel = rec_v3.channel;
        rec.status = rec_v3.status;
        rec.flags = rec_v3.flags;
        rec.text_len = rec_v3.text_len;
        rec.from = rec_v3.from;
        rec.peer = rec_v3.peer;
        rec.msg_id = rec_v3.msg_id;
        rec.timestamp = rec_v3.timestamp;
        std::memcpy(rec.reticulum_destination_hash,
                    rec_v3.reticulum_destination_hash,
                    sizeof(rec.reticulum_destination_hash));
        std::memcpy(rec.reticulum_identity_hash,
                    rec_v3.reticulum_identity_hash,
                    sizeof(rec.reticulum_identity_hash));
        std::memcpy(rec.text, rec_v3.text, sizeof(rec.text));
        return true;
    }

    RecordV2 legacy_rec{};
    if (header.version != kLegacyVersion ||
        !readExact(file, &legacy_rec, sizeof(legacy_rec)))
    {
        return false;
    }
    rec.protocol = legacy_rec.protocol;
    rec.channel = legacy_rec.channel;
    rec.status = legacy_rec.status;
    rec.text_len = legacy_rec.text_len;
    rec.from = legacy_rec.from;
    rec.peer = legacy_rec.peer;
    rec.msg_id = legacy_rec.msg_id;
    rec.timestamp = legacy_rec.timestamp;
    std::memcpy(rec.text, legacy_rec.text, sizeof(rec.text));
    return true;
}

bool SdStore::writeRecord(storage::SdRuntimeFile& file, uint16_t slot, const Record& rec) const
{
    if (slot >= kMaxMessagesPerConv)
    {
        return false;
    }
    const uint64_t offset = sizeof(FileHeader) + static_cast<uint64_t>(slot) * sizeof(Record);
    return file.seek(offset) && writeExact(file, &rec, sizeof(rec));
}

bool SdStore::initLxmfSeenFile(storage::SdRuntimeFile& file) const
{
    LxmfSeenHeader header{};
    header.magic = kLxmfSeenMagic;
    header.version = kLxmfSeenVersion;
    if (!file.seek(0) || !writeExact(file, &header, sizeof(header)))
    {
        return false;
    }
    return file.flush();
}

bool SdStore::loadLxmfSeenHeader(storage::SdRuntimeFile& file,
                                 LxmfSeenHeader& header) const
{
    if (!file.is_open() || file.size() < sizeof(LxmfSeenHeader))
    {
        return false;
    }
    if (!file.seek(0) || !readExact(file, &header, sizeof(header)))
    {
        return false;
    }
    return header.magic == kLxmfSeenMagic &&
           header.version == kLxmfSeenVersion &&
           header.head < kMaxReticulumLxmfSeen &&
           header.count <= kMaxReticulumLxmfSeen;
}

bool SdStore::readLxmfSeenRecord(storage::SdRuntimeFile& file,
                                 uint16_t slot,
                                 LxmfSeenRecord& rec) const
{
    if (slot >= kMaxReticulumLxmfSeen)
    {
        return false;
    }
    const uint64_t offset =
        sizeof(LxmfSeenHeader) + static_cast<uint64_t>(slot) * sizeof(LxmfSeenRecord);
    if (file.size() < offset + sizeof(LxmfSeenRecord))
    {
        return false;
    }
    return file.seek(offset) && readExact(file, &rec, sizeof(rec));
}

bool SdStore::writeLxmfSeenRecord(storage::SdRuntimeFile& file,
                                  uint16_t slot,
                                  const LxmfSeenRecord& rec) const
{
    if (slot >= kMaxReticulumLxmfSeen)
    {
        return false;
    }
    const uint64_t offset =
        sizeof(LxmfSeenHeader) + static_cast<uint64_t>(slot) * sizeof(LxmfSeenRecord);
    return file.seek(offset) && writeExact(file, &rec, sizeof(rec));
}

bool SdStore::rememberReticulumLxmfMessageHash(const uint8_t* lxmf_hash) const
{
    if (!ready_ || !validLxmfMessageHash(lxmf_hash) || !ensureDir())
    {
        return false;
    }
    if (hasReticulumLxmfMessageHash(lxmf_hash))
    {
        return true;
    }

    storage::SdRuntimeFile file;
    if (storage::sd_exists(kReticulumLxmfSeenFile))
    {
        if (!file.open(kReticulumLxmfSeenFile, "r+"))
        {
            return false;
        }
    }
    else if (!file.open(kReticulumLxmfSeenFile, "w+"))
    {
        return false;
    }

    LxmfSeenHeader header{};
    if (!loadLxmfSeenHeader(file, header))
    {
        if (!initLxmfSeenFile(file) || !loadLxmfSeenHeader(file, header))
        {
            file.close();
            return false;
        }
    }

    LxmfSeenRecord rec{};
    std::memcpy(rec.hash, lxmf_hash, sizeof(rec.hash));
    if (!writeLxmfSeenRecord(file, header.head, rec))
    {
        file.close();
        return false;
    }

    header.head = static_cast<uint16_t>((header.head + 1U) % kMaxReticulumLxmfSeen);
    if (header.count < kMaxReticulumLxmfSeen)
    {
        header.count = static_cast<uint16_t>(header.count + 1U);
    }
    if (!file.seek(0) || !writeExact(file, &header, sizeof(header)) || !file.flush())
    {
        file.close();
        return false;
    }
    file.close();
    return true;
}

bool SdStore::conversationContainsReticulumLxmfHash(
    storage::SdRuntimeFile& file,
    const FileHeader& header,
    const uint8_t* lxmf_hash,
    bool* found) const
{
    if (!file.is_open() || !validLxmfMessageHash(lxmf_hash) || !found)
    {
        return false;
    }
    *found = false;
    for (uint16_t index = 0; index < header.count; ++index)
    {
        const uint16_t slot = static_cast<uint16_t>(
            (header.head + kMaxMessagesPerConv - header.count + index) %
            kMaxMessagesPerConv);
        Record rec{};
        if (!readRecord(file, header, slot, rec))
        {
            return false;
        }
        if ((rec.flags & kRecordHasReticulumLxmfHashFlag) != 0 &&
            std::memcmp(rec.reticulum_lxmf_hash,
                        lxmf_hash,
                        kReticulumLxmfHashSize) == 0)
        {
            *found = true;
            return true;
        }
    }
    return true;
}

bool SdStore::openConversationForUpdate(const ConversationId& conv,
                                        storage::SdRuntimeFile& file,
                                        FileHeader& header) const
{
    char path[96]{};
    buildConversationPath(conv, path, sizeof(path));

    if (storage::sd_exists(path))
    {
        if (!file.open(path, "r+"))
        {
            return false;
        }
        if (loadFileHeader(file, header) && upgradeConversationFile(file, path, header))
        {
            return true;
        }
        file.close();
        return false;
    }

    if (!file.open(path, "w+"))
    {
        return false;
    }
    if (!initFileHeader(file))
    {
        file.close();
        return false;
    }
    return loadFileHeader(file, header);
}

bool SdStore::readConversationUnread(const ConversationId& conv, uint16_t* unread) const
{
    if (!unread)
    {
        return false;
    }

    char path[96]{};
    buildConversationPath(conv, path, sizeof(path));
    storage::SdRuntimeFile file;
    if (!storage::sd_exists(path) || !file.open(path, "r"))
    {
        return false;
    }

    FileHeader header{};
    const bool ok = loadFileHeader(file, header) &&
                    decodeUnreadState(header.reserved, unread);
    file.close();
    return ok;
}

bool SdStore::writeConversationUnread(const ConversationId& conv, uint16_t unread) const
{
    char path[96]{};
    buildConversationPath(conv, path, sizeof(path));
    storage::SdRuntimeFile file;
    if (!storage::sd_exists(path) || !file.open(path, "r+"))
    {
        return false;
    }

    FileHeader header{};
    if (!loadFileHeader(file, header) ||
        !upgradeConversationFile(file, path, header))
    {
        file.close();
        return false;
    }
    header.reserved = encodeUnreadState(unread);
    const bool ok = file.seek(0) &&
                    writeExact(file, &header, sizeof(header)) &&
                    file.flush();
    file.close();
    return ok;
}

void SdStore::buildConversationPath(const ConversationId& conv, char* out, size_t out_len) const
{
    if (!out || out_len == 0)
    {
        return;
    }
    if (hasReticulumConversationKey(conv))
    {
        uint8_t destination_hash[kReticulumPeerHashSize] = {};
        char destination_key[kReticulumPeerHashSize * 2U + 1U]{};
        (void)copyReticulumDestinationHash(destination_hash,
                                           conv.reticulum_identity);
        hashToHex(destination_hash, destination_key, sizeof(destination_key));
        std::snprintf(out,
                      out_len,
                      "%s/%s_r_%s.log",
                      kDir,
                      protocolTag(conv.protocol),
                      destination_key);
        return;
    }
    if (conv.peer == 0)
    {
        std::snprintf(out,
                      out_len,
                      "%s/%s_broadcast_%s.log",
                      kDir,
                      protocolTag(conv.protocol),
                      channelName(conv.channel));
        return;
    }

    std::snprintf(out,
                  out_len,
                  "%s/%s_n_%08lX.log",
                  kDir,
                  protocolTag(conv.protocol),
                  static_cast<unsigned long>(conv.peer));
}

const char* SdStore::channelName(ChannelId channel) const
{
    switch (channel)
    {
    case ChannelId::PRIMARY:
        return "LongFast";
    case ChannelId::SECONDARY:
        return "Squad";
    default:
        return "Unknown";
    }
}

ChatMessage SdStore::messageFromRecord(const Record& rec)
{
    ChatMessage msg;
    msg.protocol = static_cast<MeshProtocol>(rec.protocol);
    msg.channel = static_cast<ChannelId>(rec.channel);
    msg.from = rec.from;
    msg.peer = rec.peer;
    msg.msg_id = rec.msg_id;
    msg.timestamp = rec.timestamp;
    msg.text.assign(rec.text, std::min<size_t>(rec.text_len, sizeof(rec.text)));
    msg.status = static_cast<MessageStatus>(rec.status);
    msg.rx_origin = static_cast<RxOrigin>(rec.rx_origin);
    msg.source_unverified = (rec.flags & kRecordSourceUnverifiedFlag) != 0;
    if ((rec.flags & kRecordHasReticulumIdentityFlag) != 0)
    {
        readReticulumIdentityFromStorage(msg.reticulum_identity,
                                         rec.reticulum_destination_hash,
                                         rec.reticulum_identity_hash);
    }
    if ((rec.flags & kRecordHasReticulumLxmfHashFlag) != 0)
    {
        std::memcpy(msg.reticulum_lxmf_hash,
                    rec.reticulum_lxmf_hash,
                    sizeof(msg.reticulum_lxmf_hash));
    }
    return msg;
}

SdStore::Record SdStore::recordFromMessage(const ChatMessage& msg)
{
    Record rec{};
    rec.protocol = static_cast<uint8_t>(msg.protocol);
    rec.channel = static_cast<uint8_t>(msg.channel);
    rec.status = static_cast<uint8_t>(msg.status);
    rec.text_len = static_cast<uint16_t>(std::min<size_t>(msg.text.size(), sizeof(rec.text)));
    rec.from = msg.from;
    rec.peer = msg.peer;
    rec.msg_id = msg.msg_id;
    rec.timestamp = msg.timestamp;
    rec.rx_origin = static_cast<uint8_t>(msg.rx_origin);
    if (msg.source_unverified)
    {
        rec.flags |= kRecordSourceUnverifiedFlag;
    }
    if (msg.protocol == MeshProtocol::Reticulum &&
        hasReticulumDestinationIdentity(msg.reticulum_identity))
    {
        rec.flags |= kRecordHasReticulumIdentityFlag;
        copyReticulumIdentityToStorage(rec.reticulum_destination_hash,
                                       rec.reticulum_identity_hash,
                                       msg.reticulum_identity);
    }
    if (chat::hasReticulumLxmfMessageHash(msg))
    {
        rec.flags |= kRecordHasReticulumLxmfHashFlag;
        std::memcpy(rec.reticulum_lxmf_hash,
                    msg.reticulum_lxmf_hash,
                    sizeof(rec.reticulum_lxmf_hash));
    }
    if (rec.text_len > 0)
    {
        std::memcpy(rec.text, msg.text.data(), rec.text_len);
    }
    return rec;
}

bool SdStore::indexEntryHasReticulumIdentity(const IndexEntry& entry)
{
    return static_cast<MeshProtocol>(entry.protocol) == MeshProtocol::Reticulum &&
           (entry.flags & kIndexHasReticulumIdentityFlag) != 0;
}

bool SdStore::indexEntryMatchesConversation(const IndexEntry& entry,
                                            const ConversationId& conv)
{
    if (entry.protocol != static_cast<uint8_t>(conv.protocol) ||
        entry.channel != static_cast<uint8_t>(conv.channel))
    {
        return false;
    }

    const bool conv_has_reticulum_key = hasReticulumConversationKey(conv);
    const bool entry_has_reticulum_key = indexEntryHasReticulumIdentity(entry);
    if (conv_has_reticulum_key || entry_has_reticulum_key)
    {
        return conv_has_reticulum_key && entry_has_reticulum_key &&
               sameReticulumDestination(conv.reticulum_identity,
                                        entry.reticulum_destination_hash);
    }
    return entry.peer == conv.peer;
}

bool SdStore::readStateEntryHasReticulumIdentity(const ReadStateEntry& entry)
{
    return static_cast<MeshProtocol>(entry.protocol) == MeshProtocol::Reticulum &&
           (entry.flags & kReadStateHasReticulumIdentityFlag) != 0;
}

bool SdStore::readStateEntryMatchesConversation(const ReadStateEntry& entry,
                                                const ConversationId& conv)
{
    if (entry.protocol != static_cast<uint8_t>(conv.protocol) ||
        entry.channel != static_cast<uint8_t>(conv.channel))
    {
        return false;
    }

    const bool conv_has_reticulum_key = hasReticulumConversationKey(conv);
    const bool entry_has_reticulum_key = readStateEntryHasReticulumIdentity(entry);
    if (conv_has_reticulum_key || entry_has_reticulum_key)
    {
        return conv_has_reticulum_key && entry_has_reticulum_key &&
               sameReticulumDestination(conv.reticulum_identity,
                                        entry.reticulum_destination_hash);
    }
    return entry.peer == conv.peer;
}

SdStore::ReadStateEntry SdStore::readStateEntryFromConversation(
    const ConversationId& conv,
    uint16_t unread)
{
    ReadStateEntry entry{};
    entry.protocol = static_cast<uint8_t>(conv.protocol);
    entry.channel = static_cast<uint8_t>(conv.channel);
    entry.unread = static_cast<uint16_t>(
        std::min<uint16_t>(unread, kUnreadStateCountMask));
    entry.peer = conv.peer;
    if (hasReticulumConversationKey(conv))
    {
        entry.flags |= kReadStateHasReticulumIdentityFlag;
        copyReticulumIdentityToStorage(entry.reticulum_destination_hash,
                                       entry.reticulum_identity_hash,
                                       conv.reticulum_identity);
    }
    return entry;
}

ConversationId SdStore::conversationFromIndexEntry(const IndexEntry& entry)
{
    ConversationId conv(static_cast<ChannelId>(entry.channel),
                        entry.peer,
                        static_cast<MeshProtocol>(entry.protocol));
    if (indexEntryHasReticulumIdentity(entry))
    {
        readReticulumIdentityFromStorage(conv.reticulum_identity,
                                         entry.reticulum_destination_hash,
                                         entry.reticulum_identity_hash);
    }
    return conv;
}

ConversationMeta SdStore::metaFromIndexEntry(const IndexEntry& entry)
{
    ConversationMeta meta;
    meta.id = conversationFromIndexEntry(entry);
    meta.preview.assign(entry.preview, std::min<size_t>(entry.preview_len, sizeof(entry.preview)));
    meta.last_timestamp = entry.last_timestamp;
    meta.unread = entry.unread;
    meta.reticulum_identity = meta.id.reticulum_identity;
    if (entry.peer == 0 && !indexEntryHasReticulumIdentity(entry))
    {
        meta.name = "Broadcast";
    }
    else
    {
        char buf[16];
        std::snprintf(buf,
                      sizeof(buf),
                      "%04lX",
                      static_cast<unsigned long>(entry.peer & 0xFFFFUL));
        meta.name = buf;
    }
    return meta;
}

bool SdStore::hasLogSuffix(const char* name)
{
    if (!name)
    {
        return false;
    }
    const char* suffix = std::strstr(name, ".log");
    return suffix != nullptr && suffix[4] == '\0';
}

} // namespace chat
