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

    const ConversationId conv(msg.channel, msg.peer, msg.protocol);
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
        if (!readRecord(file, slot, rec) || rec.text_len == 0)
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
                                     return entry.peer == conv.peer &&
                                            entry.channel == static_cast<uint8_t>(conv.channel) &&
                                            entry.protocol == static_cast<uint8_t>(conv.protocol);
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
        ConversationId conv(static_cast<ChannelId>(entry.channel),
                            entry.peer,
                            static_cast<MeshProtocol>(entry.protocol));
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
            if (!readRecord(file, slot, rec))
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
        ConversationId conv(static_cast<ChannelId>(entry.channel),
                            entry.peer,
                            static_cast<MeshProtocol>(entry.protocol));
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
            if (!readRecord(file, slot, rec) || rec.text_len == 0 || rec.msg_id != msg_id)
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
        header.version != kVersion)
    {
        file.close();
        return false;
    }

    entries.resize(header.count);
    const size_t expected = static_cast<size_t>(header.count) * sizeof(IndexEntry);
    const bool ok = expected == 0 || readExact(file, entries.data(), expected);
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
    header.version = kVersion;
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
        if (entry.peer == conv.peer &&
            entry.channel == static_cast<uint8_t>(conv.channel) &&
            entry.protocol == static_cast<uint8_t>(conv.protocol))
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

    const ConversationId conv(msg.channel, msg.peer, msg.protocol);
    size_t index = 0;
    if (!findIndexEntry(conv, entries, &index))
    {
        IndexEntry entry{};
        entry.protocol = static_cast<uint8_t>(msg.protocol);
        entry.channel = static_cast<uint8_t>(msg.channel);
        entry.peer = msg.peer;
        entries.push_back(entry);
        index = entries.size() - 1;
    }

    IndexEntry& entry = entries[index];
    entry.protocol = static_cast<uint8_t>(msg.protocol);
    entry.channel = static_cast<uint8_t>(msg.channel);
    entry.status = static_cast<uint8_t>(msg.status);
    entry.peer = msg.peer;
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
            if (!readRecord(file, slot, rec) || rec.text_len == 0)
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
    return header.magic == kFileMagic && header.version == kVersion &&
           header.head < kMaxMessagesPerConv && header.count <= kMaxMessagesPerConv;
}

bool SdStore::initFileHeader(storage::SdRuntimeFile& file) const
{
    FileHeader header{};
    header.magic = kFileMagic;
    header.version = kVersion;
    if (!file.seek(0) || !writeExact(file, &header, sizeof(header)))
    {
        return false;
    }
    return file.flush();
}

bool SdStore::readRecord(storage::SdRuntimeFile& file, uint16_t slot, Record& rec) const
{
    if (slot >= kMaxMessagesPerConv)
    {
        return false;
    }
    const uint64_t offset = sizeof(FileHeader) + static_cast<uint64_t>(slot) * sizeof(Record);
    if (file.size() < offset + sizeof(Record))
    {
        return false;
    }
    return file.seek(offset) && readExact(file, &rec, sizeof(rec));
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
        if (loadFileHeader(file, header))
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
    if (rec.text_len > 0)
    {
        std::memcpy(rec.text, msg.text.data(), rec.text_len);
    }
    return rec;
}

ConversationMeta SdStore::metaFromIndexEntry(const IndexEntry& entry)
{
    ConversationMeta meta;
    meta.id.protocol = static_cast<MeshProtocol>(entry.protocol);
    meta.id.channel = static_cast<ChannelId>(entry.channel);
    meta.id.peer = entry.peer;
    meta.preview.assign(entry.preview, std::min<size_t>(entry.preview_len, sizeof(entry.preview)));
    meta.last_timestamp = entry.last_timestamp;
    meta.unread = entry.unread;
    if (entry.peer == 0)
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
