/**
 * @file sd_store.cpp
 * @brief SD-backed ESP Arduino chat storage implementation.
 */

#include "platform/esp/arduino_common/chat/infra/store/sd_store.h"

#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
#include "sys/clock.h"

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

constexpr const char* kTempSuffix = ".tmp";

bool readExact(storage::SdRuntimeFile& file, void* out, size_t len)
{
    return file.read(out, len) == static_cast<int>(len);
}

bool writeExact(storage::SdRuntimeFile& file, const void* data, size_t len)
{
    return file.write(data, len) == len;
}

std::string tempPathFor(const char* path)
{
    return std::string(path ? path : "") + kTempSuffix;
}
} // namespace

SdStore::SdStore(const char* path)
    : path_(path)
{
    ready_ = ensureFs();
    if (ready_)
    {
        (void)loadFromFs();
    }
}

void SdStore::append(const ChatMessage& msg)
{
    if (total_message_count_ >= kMaxMessagesTotal)
    {
        evictOldestMessage();
    }

    ConversationStorage& storage = getConversationStorage(ConversationId(msg.channel, msg.peer, msg.protocol));
    StoredMessageEntry entry;
    entry.message = msg;
    entry.sequence = next_sequence_++;
    storage.messages.push_back(entry);
    ++total_message_count_;
    if (msg.status == MessageStatus::Incoming)
    {
        ++storage.unread_count;
    }
    markDirty();
    maybeSave();
}

std::vector<ChatMessage> SdStore::loadRecent(const ConversationId& conv, size_t n)
{
    const ConversationStorage& storage = getConversationStorage(conv);
    std::vector<ChatMessage> result;
    const size_t count = storage.messages.size();
    const size_t start = (count > n) ? (count - n) : 0;
    result.reserve(count - start);
    for (size_t i = start; i < count; ++i)
    {
        result.push_back(storage.messages[i].message);
    }
    return result;
}

std::vector<ConversationMeta> SdStore::loadConversationPage(size_t offset,
                                                            size_t limit,
                                                            size_t* total)
{
    std::vector<ConversationMeta> list;
    list.reserve(conversations_.size());

    for (const auto& pair : conversations_)
    {
        const ConversationId& conv = pair.first;
        const ConversationStorage& storage = pair.second;
        if (storage.messages.empty())
        {
            continue;
        }

        ConversationMeta meta;
        meta.id = conv;
        meta.preview = storage.messages.back().message.text;
        meta.last_timestamp = storage.messages.back().message.timestamp;
        meta.unread = storage.unread_count;
        if (conv.peer == 0)
        {
            meta.name = "Broadcast";
        }
        else
        {
            char buf[16];
            std::snprintf(buf, sizeof(buf),
                          "%04lX",
                          static_cast<unsigned long>(conv.peer & 0xFFFFUL));
            meta.name = buf;
        }
        list.push_back(meta);
    }

    std::sort(list.begin(),
              list.end(),
              [](const ConversationMeta& a, const ConversationMeta& b)
              {
                  return a.last_timestamp > b.last_timestamp;
              });

    if (total)
    {
        *total = list.size();
    }
    if (offset >= list.size())
    {
        return {};
    }
    if (limit == 0)
    {
        return std::vector<ConversationMeta>(list.begin() + static_cast<long>(offset), list.end());
    }

    const size_t end = std::min(list.size(), offset + limit);
    return std::vector<ConversationMeta>(list.begin() + static_cast<long>(offset),
                                         list.begin() + static_cast<long>(end));
}

void SdStore::setUnread(const ConversationId& conv, int unread)
{
    getConversationStorage(conv).unread_count = unread;
    markDirty();
    maybeSave();
}

int SdStore::getUnread(const ConversationId& conv) const
{
    return getConversationStorage(conv).unread_count;
}

void SdStore::clearConversation(const ConversationId& conv)
{
    auto it = conversations_.find(conv);
    if (it == conversations_.end())
    {
        return;
    }

    total_message_count_ -= std::min(total_message_count_, it->second.messages.size());
    conversations_.erase(it);
    markDirty();
    maybeSave(true);
}

void SdStore::clearAll()
{
    conversations_.clear();
    total_message_count_ = 0;
    next_sequence_ = 1;
    dirty_ = false;
    pending_write_count_ = 0;
    dirty_since_ms_ = 0;

    if (ensureFs() && path_ && storage::sd_exists(path_))
    {
        storage::sd_remove(path_);
    }
    const std::string temp_path = tempPathFor(path_);
    if (!temp_path.empty() && storage::sd_exists(temp_path.c_str()))
    {
        storage::sd_remove(temp_path.c_str());
    }
}

bool SdStore::updateMessageStatus(MessageId msg_id, MessageStatus status)
{
    if (msg_id == 0)
    {
        return false;
    }

    for (auto& pair : conversations_)
    {
        ConversationStorage& storage = pair.second;
        for (auto& entry : storage.messages)
        {
            ChatMessage& msg = entry.message;
            if (msg.msg_id != msg_id || msg.from != 0)
            {
                continue;
            }
            msg.status = status;
            markDirty();
            maybeSave();
            return true;
        }
    }
    return false;
}

bool SdStore::getMessage(MessageId msg_id, ChatMessage* out) const
{
    if (msg_id == 0)
    {
        return false;
    }

    for (const auto& pair : conversations_)
    {
        const ConversationStorage& storage = pair.second;
        for (const auto& entry : storage.messages)
        {
            if (entry.message.msg_id != msg_id)
            {
                continue;
            }
            if (out)
            {
                *out = entry.message;
            }
            return true;
        }
    }
    return false;
}

void SdStore::flush()
{
    maybeSave(true);
}

bool SdStore::ensureFs() const
{
    return path_ && path_[0] != '\0' && storage::sd_card_ready();
}

bool SdStore::loadFromFs()
{
    conversations_.clear();
    total_message_count_ = 0;
    next_sequence_ = 1;
    if (!ensureFs() || !storage::sd_exists(path_))
    {
        return false;
    }

    storage::SdRuntimeFile file;
    if (!file.open(path_, "r"))
    {
        return false;
    }

    LegacyFileHeader legacy_header{};
    if (!readExact(file, &legacy_header, sizeof(legacy_header)) || legacy_header.magic != kMagic)
    {
        file.close();
        return false;
    }

    const bool legacy_v1 = legacy_header.version == 1;
    const bool current_v2 = legacy_header.version == kVersion;
    if (!legacy_v1 && !current_v2)
    {
        file.close();
        return false;
    }

    uint16_t conversation_count = legacy_header.conversation_count;
    if (current_v2)
    {
        uint32_t persisted_next_sequence = 1U;
        if (!readExact(file, &persisted_next_sequence, sizeof(persisted_next_sequence)))
        {
            file.close();
            return false;
        }
        next_sequence_ = std::max<uint32_t>(1U, persisted_next_sequence);
    }
    uint32_t recovered_sequence = 1U;

    for (uint16_t index = 0; index < conversation_count; ++index)
    {
        ConversationRecord conv_record{};
        if (!readExact(file, &conv_record, sizeof(conv_record)))
        {
            file.close();
            conversations_.clear();
            total_message_count_ = 0;
            return false;
        }

        ConversationId conv(static_cast<ChannelId>(conv_record.channel),
                            conv_record.peer,
                            static_cast<MeshProtocol>(conv_record.protocol));
        ConversationStorage& storage = getConversationStorage(conv);
        storage.unread_count = conv_record.unread_count;

        for (uint16_t message_index = 0; message_index < conv_record.message_count; ++message_index)
        {
            MessageRecord rec{};
            if (current_v2)
            {
                if (!readExact(file, &rec, sizeof(rec)))
                {
                    file.close();
                    conversations_.clear();
                    total_message_count_ = 0;
                    return false;
                }
            }
            else
            {
                LegacyMessageRecord legacy_rec{};
                if (!readExact(file, &legacy_rec, sizeof(legacy_rec)))
                {
                    file.close();
                    conversations_.clear();
                    total_message_count_ = 0;
                    return false;
                }
                rec.protocol = legacy_rec.protocol;
                rec.channel = legacy_rec.channel;
                rec.status = legacy_rec.status;
                rec.flags = legacy_rec.flags;
                rec.from = legacy_rec.from;
                rec.peer = legacy_rec.peer;
                rec.msg_id = legacy_rec.msg_id;
                rec.timestamp = legacy_rec.timestamp;
                rec.team_location_icon = legacy_rec.team_location_icon;
                rec.geo_lat_e7 = legacy_rec.geo_lat_e7;
                rec.geo_lon_e7 = legacy_rec.geo_lon_e7;
                rec.text_len = legacy_rec.text_len;
                std::memcpy(rec.text, legacy_rec.text, sizeof(rec.text));
            }

            ChatMessage msg;
            msg.protocol = static_cast<MeshProtocol>(rec.protocol);
            msg.channel = static_cast<ChannelId>(rec.channel);
            msg.from = rec.from;
            msg.peer = rec.peer;
            msg.msg_id = rec.msg_id;
            msg.timestamp = rec.timestamp;
            msg.team_location_icon = rec.team_location_icon;
            msg.has_geo = (rec.flags & 0x01U) != 0;
            msg.geo_lat_e7 = rec.geo_lat_e7;
            msg.geo_lon_e7 = rec.geo_lon_e7;
            msg.status = static_cast<MessageStatus>(rec.status);
            msg.text.assign(rec.text, std::min<size_t>(rec.text_len, sizeof(rec.text)));

            const uint32_t sequence = current_v2 ? rec.sequence : recovered_sequence++;
            StoredMessageEntry entry;
            entry.message = msg;
            entry.sequence = sequence;
            storage.messages.push_back(entry);
            ++total_message_count_;
            if (sequence >= next_sequence_)
            {
                next_sequence_ = sequence + 1U;
            }
        }
    }

    file.close();
    while (total_message_count_ > kMaxMessagesTotal)
    {
        evictOldestMessage();
    }
    dirty_ = false;
    pending_write_count_ = 0;
    dirty_since_ms_ = 0;
    last_save_ms_ = sys::millis_now();
    Serial.printf("[AppContext] chat store=SdStore load path=%s conversations=%u messages=%u\n",
                  path_,
                  static_cast<unsigned>(conversations_.size()),
                  static_cast<unsigned>(total_message_count_));
    return true;
}

bool SdStore::saveToFs() const
{
    if (!ensureFs())
    {
        return false;
    }

    const std::string temp_path = tempPathFor(path_);
    if (temp_path.empty())
    {
        return false;
    }
    if (storage::sd_exists(temp_path.c_str()))
    {
        storage::sd_remove(temp_path.c_str());
    }

    storage::SdRuntimeFile file;
    if (!file.open(temp_path.c_str(), "w"))
    {
        return false;
    }

    FileHeader header{};
    header.magic = kMagic;
    header.version = kVersion;
    header.conversation_count = static_cast<uint16_t>(std::min<size_t>(conversations_.size(), 0xFFFFU));
    header.next_sequence = next_sequence_;
    if (!writeExact(file, &header, sizeof(header)))
    {
        file.close();
        storage::sd_remove(temp_path.c_str());
        return false;
    }

    for (const auto& pair : conversations_)
    {
        const ConversationId& conv = pair.first;
        const ConversationStorage& storage_entry = pair.second;

        ConversationRecord conv_record{};
        conv_record.protocol = static_cast<uint8_t>(conv.protocol);
        conv_record.channel = static_cast<uint8_t>(conv.channel);
        conv_record.peer = conv.peer;
        conv_record.unread_count = storage_entry.unread_count;
        conv_record.message_count = static_cast<uint16_t>(std::min<size_t>(storage_entry.messages.size(), 0xFFFFU));
        if (!writeExact(file, &conv_record, sizeof(conv_record)))
        {
            file.close();
            storage::sd_remove(temp_path.c_str());
            return false;
        }

        for (const StoredMessageEntry& entry : storage_entry.messages)
        {
            const ChatMessage& msg = entry.message;

            MessageRecord rec{};
            rec.protocol = static_cast<uint8_t>(msg.protocol);
            rec.channel = static_cast<uint8_t>(msg.channel);
            rec.status = static_cast<uint8_t>(msg.status);
            rec.flags = msg.has_geo ? 0x01U : 0x00U;
            rec.from = msg.from;
            rec.peer = msg.peer;
            rec.msg_id = msg.msg_id;
            rec.timestamp = msg.timestamp;
            rec.sequence = entry.sequence;
            rec.team_location_icon = msg.team_location_icon;
            rec.geo_lat_e7 = msg.geo_lat_e7;
            rec.geo_lon_e7 = msg.geo_lon_e7;
            rec.text_len = static_cast<uint16_t>(std::min<size_t>(msg.text.size(), sizeof(rec.text)));
            if (rec.text_len > 0)
            {
                std::memcpy(rec.text, msg.text.data(), rec.text_len);
            }

            if (!writeExact(file, &rec, sizeof(rec)))
            {
                file.close();
                storage::sd_remove(temp_path.c_str());
                return false;
            }
        }
    }

    file.flush();
    file.close();

    if (storage::sd_exists(path_))
    {
        storage::sd_remove(path_);
    }
    const bool renamed = storage::sd_rename(temp_path.c_str(), path_);
    if (renamed)
    {
        dirty_ = false;
        pending_write_count_ = 0;
        dirty_since_ms_ = 0;
        last_save_ms_ = sys::millis_now();
    }
    return renamed;
}

void SdStore::markDirty()
{
    if (!dirty_)
    {
        dirty_ = true;
        dirty_since_ms_ = sys::millis_now();
    }
    ++pending_write_count_;
}

void SdStore::maybeSave(bool force)
{
    if (!dirty_)
    {
        return;
    }

    const uint32_t now_ms = sys::millis_now();
    const bool interval_elapsed =
        (dirty_since_ms_ != 0) && ((now_ms - dirty_since_ms_) >= kSaveIntervalMs);
    const bool too_many_pending = pending_write_count_ >= kMaxPendingWrites;
    if (!force && !interval_elapsed && !too_many_pending)
    {
        return;
    }

    if (!saveToFs())
    {
        Serial.printf("[AppContext] chat store=SdStore save_failed path=%s dirty=%u pending=%u\n",
                      path_ ? path_ : "",
                      dirty_ ? 1U : 0U,
                      static_cast<unsigned>(pending_write_count_));
    }
}

void SdStore::evictOldestMessage()
{
    auto oldest_it = conversations_.end();
    size_t oldest_index = 0;
    uint32_t oldest_sequence = 0;
    bool found = false;

    for (auto it = conversations_.begin(); it != conversations_.end(); ++it)
    {
        auto& messages = it->second.messages;
        for (size_t index = 0; index < messages.size(); ++index)
        {
            if (!found || messages[index].sequence < oldest_sequence)
            {
                oldest_it = it;
                oldest_index = index;
                oldest_sequence = messages[index].sequence;
                found = true;
            }
        }
    }

    if (!found)
    {
        total_message_count_ = 0;
        return;
    }

    ConversationStorage& storage_entry = oldest_it->second;
    const ChatMessage removed = storage_entry.messages[oldest_index].message;
    storage_entry.messages.erase(storage_entry.messages.begin() + static_cast<long>(oldest_index));
    if (removed.status == MessageStatus::Incoming && storage_entry.unread_count > 0)
    {
        --storage_entry.unread_count;
    }
    if (storage_entry.messages.empty())
    {
        conversations_.erase(oldest_it);
    }
    if (total_message_count_ > 0)
    {
        --total_message_count_;
    }
}

SdStore::ConversationStorage& SdStore::getConversationStorage(const ConversationId& conv)
{
    auto it = conversations_.find(conv);
    if (it == conversations_.end())
    {
        auto result = conversations_.emplace(conv, ConversationStorage{});
        return result.first->second;
    }
    return it->second;
}

const SdStore::ConversationStorage& SdStore::getConversationStorage(const ConversationId& conv) const
{
    auto it = conversations_.find(conv);
    if (it == conversations_.end())
    {
        static ConversationStorage empty;
        return empty;
    }
    return it->second;
}

} // namespace chat
