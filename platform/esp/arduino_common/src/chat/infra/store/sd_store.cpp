/**
 * @file sd_store.cpp
 * @brief SD-backed ESP Arduino chat storage using per-conversation log files.
 */

#include "platform/esp/arduino_common/chat/infra/store/sd_store.h"

#include "chat/infra/mesh_protocol_utils.h"
#include "platform/esp/arduino_common/storage/sd_card_runtime.h"

#include <Arduino.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

namespace chat
{
namespace
{
namespace storage = ::platform::esp::arduino_common::storage;

constexpr uint8_t kRecordHasReticulumIdentityFlag = 0x01U;
constexpr uint8_t kIndexHasReticulumIdentityFlag = 0x01U;

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
        Serial.printf("[AppContext] chat store=SdStore layout=logs ready=0 root=%s\n", kDir);
        return;
    }

    std::vector<IndexEntry> entries;
    if (!readIndex(entries))
    {
        rebuildIndex();
        (void)readIndex(entries);
    }
    Serial.printf("[AppContext] chat store=SdStore layout=logs root=%s index=%u\n",
                  kDir,
                  static_cast<unsigned>(entries.size()));
}

void SdStore::append(const ChatMessage& msg)
{
    if (!ready_ || !ensureDir())
    {
        return;
    }

    const ConversationId conv = conversationIdForMessage(msg);
    storage::SdRuntimeFile file;
    FileHeader header{};
    if (!openConversationForUpdate(conv, file, header))
    {
        return;
    }

    const Record rec = recordFromMessage(msg);
    if (!writeRecord(file, header.head, rec))
    {
        file.close();
        return;
    }

    header.head = static_cast<uint16_t>((header.head + 1U) % kMaxMessagesPerConv);
    if (header.count < kMaxMessagesPerConv)
    {
        header.count = static_cast<uint16_t>(header.count + 1U);
    }

    if (!file.seek(0) || !writeExact(file, &header, sizeof(header)))
    {
        file.close();
        return;
    }
    file.flush();
    file.close();

    updateIndexForMessage(msg);
}

std::vector<ChatMessage> SdStore::loadRecent(const ConversationId& conv, size_t n)
{
    std::vector<ChatMessage> out;
    if (!ready_ || n == 0)
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

    const size_t to_read = std::min<size_t>(n, header.count);
    const uint16_t start = static_cast<uint16_t>(
        (header.head + kMaxMessagesPerConv - to_read) % kMaxMessagesPerConv);

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

void SdStore::setUnread(const ConversationId& conv, int unread)
{
    std::vector<IndexEntry> entries;
    if (!ready_ || !ensureIndex(entries))
    {
        return;
    }

    size_t index = 0;
    if (!findIndexEntry(conv, entries, &index))
    {
        return;
    }
    entries[index].unread = static_cast<uint16_t>(std::max(0, unread));
    (void)writeIndex(entries);
}

int SdStore::getUnread(const ConversationId& conv) const
{
    std::vector<IndexEntry> entries;
    if (!ready_ || !readIndex(entries))
    {
        return 0;
    }

    size_t index = 0;
    if (!findIndexEntry(conv, entries, &index))
    {
        return 0;
    }
    return entries[index].unread;
}

void SdStore::clearConversation(const ConversationId& conv)
{
    if (!ready_)
    {
        return;
    }

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

    static constexpr const char* kTempIndexFile = "/chat/index.tmp";
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

    if (storage::sd_exists(kIndexFile))
    {
        storage::sd_remove(kIndexFile);
    }
    if (!storage::sd_rename(kTempIndexFile, kIndexFile))
    {
        storage::sd_remove(kTempIndexFile);
        return false;
    }
    return true;
}

bool SdStore::ensureIndex(std::vector<IndexEntry>& entries)
{
    if (readIndex(entries))
    {
        return true;
    }
    rebuildIndex();
    return readIndex(entries);
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

void SdStore::updateIndexForMessage(const ChatMessage& msg)
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
    if (msg.status == MessageStatus::Incoming && entry.unread < 0xFFFFU)
    {
        entry.unread = static_cast<uint16_t>(entry.unread + 1U);
    }

    (void)writeIndex(entries);
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
            if (msg.status == MessageStatus::Incoming && unread < 0xFFFFU)
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
    }
    dir.close();

    (void)writeIndex(entries);
    Serial.printf("[AppContext] chat store=SdStore rebuild index entries=%u\n",
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
         header.version != kReticulumIdentityVersion) ||
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
        if (readRecord(file, header, slot, rec) && rec.text_len != 0)
        {
            records.push_back(rec);
        }
    }

    file.close();
    if (!file.open(path, "w+"))
    {
        return false;
    }

    FileHeader upgraded{};
    upgraded.magic = kFileMagic;
    upgraded.version = kFileVersion;
    upgraded.count = static_cast<uint16_t>(std::min<size_t>(records.size(), kMaxMessagesPerConv));
    upgraded.head = static_cast<uint16_t>(upgraded.count % kMaxMessagesPerConv);
    if (!file.seek(0) || !writeExact(file, &upgraded, sizeof(upgraded)))
    {
        file.close();
        return false;
    }
    for (uint16_t index = 0; index < upgraded.count; ++index)
    {
        if (!writeRecord(file, index, records[index]))
        {
            file.close();
            return false;
        }
    }
    if (!file.flush())
    {
        file.close();
        return false;
    }
    return loadFileHeader(file, header) && header.version == kFileVersion;
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

bool SdStore::openConversationForUpdate(const ConversationId& conv,
                                        storage::SdRuntimeFile& file,
                                        FileHeader& header) const
{
    char path[96]{};
    buildConversationPath(conv, path, sizeof(path));

    if (storage::sd_exists(path) && file.open(path, "r+"))
    {
        if (loadFileHeader(file, header) && upgradeConversationFile(file, path, header))
        {
            return true;
        }
        file.close();
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
    if ((rec.flags & kRecordHasReticulumIdentityFlag) != 0)
    {
        readReticulumIdentityFromStorage(msg.reticulum_identity,
                                         rec.reticulum_destination_hash,
                                         rec.reticulum_identity_hash);
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
    if (msg.protocol == MeshProtocol::Reticulum &&
        hasReticulumDestinationIdentity(msg.reticulum_identity))
    {
        rec.flags |= kRecordHasReticulumIdentityFlag;
        copyReticulumIdentityToStorage(rec.reticulum_destination_hash,
                                       rec.reticulum_identity_hash,
                                       msg.reticulum_identity);
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
