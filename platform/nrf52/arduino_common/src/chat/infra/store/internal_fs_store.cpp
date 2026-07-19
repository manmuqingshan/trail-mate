#include "platform/nrf52/arduino_common/chat/infra/store/internal_fs_store.h"

#include "platform/nrf52/arduino_common/internal_fs_utils.h"

#include <InternalFileSystem.h>

#include "sys/clock.h"
#include <algorithm>
#include <cstring>
#include <string>

namespace platform::nrf52::arduino_common::chat::infra::store
{
namespace
{
using Adafruit_LittleFS_Namespace::FILE_O_READ;

constexpr const char* kTempSuffix = ".tmp";
constexpr uint8_t kMessageHasGeoFlag = 0x01U;
constexpr uint8_t kMessageHasReticulumIdentityFlag = 0x02U;
constexpr uint16_t kConversationHasReticulumIdentityFlag = 0x0001U;

void copyReticulumIdentityToRecord(
    uint8_t* destination_hash,
    uint8_t* identity_hash,
    const ::chat::ReticulumPeerIdentity& identity)
{
    if (!destination_hash || !identity_hash)
    {
        return;
    }
    (void)::chat::copyReticulumIdentityHashes(destination_hash,
                                              identity_hash,
                                              identity);
}

void readReticulumIdentityFromRecord(
    ::chat::ReticulumPeerIdentity& identity,
    const uint8_t* destination_hash,
    const uint8_t* identity_hash)
{
    if (!destination_hash || !identity_hash)
    {
        return;
    }
    identity = ::chat::makeReticulumPeerIdentity(destination_hash,
                                                 identity_hash);
}
} // namespace

InternalFsStore::InternalFsStore(const char* path)
    : path_(path)
{
    (void)loadFromFs();
}

void InternalFsStore::append(const ::chat::ChatMessage& msg)
{
    if (total_message_count_ >= kMaxMessagesTotal)
    {
        evictOldestMessage();
    }

    ConversationStorage& storage = getConversationStorage(::chat::conversationIdForMessage(msg));
    StoredMessageEntry entry;
    entry.message = msg;
    entry.sequence = next_sequence_++;
    storage.messages.push_back(entry);
    total_message_count_++;
    if (msg.status == ::chat::MessageStatus::Incoming)
    {
        storage.unread_count++;
    }
    markDirty();
    maybeSave();
}

std::vector<::chat::ChatMessage> InternalFsStore::loadRecent(const ::chat::ConversationId& conv, size_t n)
{
    return loadPageFromLatest(conv, 0, n, nullptr);
}

std::vector<::chat::ChatMessage> InternalFsStore::loadPageFromLatest(
    const ::chat::ConversationId& conv,
    size_t offset_from_latest,
    size_t limit,
    size_t* total)
{
    const ConversationStorage& storage = getConversationStorage(conv);
    std::vector<::chat::ChatMessage> result;

    size_t count = storage.messages.size();
    if (total)
    {
        *total = count;
    }
    if (limit == 0 || offset_from_latest >= count)
    {
        return result;
    }

    const size_t available = count - offset_from_latest;
    const size_t to_read = std::min<size_t>(limit, available);
    size_t start = count - offset_from_latest - to_read;
    const size_t end = start + to_read;
    for (size_t i = start; i < end; ++i)
    {
        result.push_back(storage.messages[i].message);
    }
    return result;
}

std::vector<::chat::ConversationMeta> InternalFsStore::loadConversationPage(size_t offset,
                                                                            size_t limit,
                                                                            size_t* total)
{
    std::vector<::chat::ConversationMeta> list;
    list.reserve(conversations_.size());

    for (const auto& pair : conversations_)
    {
        const auto& conv = pair.first;
        const auto& storage = pair.second;
        const size_t count = storage.messages.size();
        if (count == 0)
        {
            continue;
        }

        ::chat::ConversationMeta meta;
        meta.id = conv;
        meta.preview = storage.messages.back().message.text;
        meta.last_timestamp = storage.messages.back().message.timestamp;
        meta.unread = storage.unread_count;
        meta.reticulum_identity = conv.reticulum_identity;
        if (conv.peer == 0)
        {
            meta.name = "Broadcast";
        }
        else
        {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%04lX", static_cast<unsigned long>(conv.peer & 0xFFFFUL));
            meta.name = buf;
        }
        list.push_back(meta);
    }

    std::sort(list.begin(), list.end(),
              [](const ::chat::ConversationMeta& a, const ::chat::ConversationMeta& b)
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
        return std::vector<::chat::ConversationMeta>(list.begin() + static_cast<long>(offset), list.end());
    }

    const size_t end = std::min(list.size(), offset + limit);
    return std::vector<::chat::ConversationMeta>(list.begin() + static_cast<long>(offset),
                                                 list.begin() + static_cast<long>(end));
}

bool InternalFsStore::setUnread(const ::chat::ConversationId& conv, int unread)
{
    getConversationStorage(conv).unread_count = unread;
    markDirty();
    maybeSave(true);
    return !dirty_;
}

int InternalFsStore::getUnread(const ::chat::ConversationId& conv) const
{
    return getConversationStorage(conv).unread_count;
}

void InternalFsStore::clearConversation(const ::chat::ConversationId& conv)
{
    auto it = conversations_.find(conv);
    if (it == conversations_.end())
    {
        return;
    }

    total_message_count_ -= std::min(total_message_count_, it->second.messages.size());
    it->second.messages.clear();
    it->second.unread_count = 0;
    conversations_.erase(it);
    markDirty();
    maybeSave(true);
}

void InternalFsStore::clearAll()
{
    conversations_.clear();
    total_message_count_ = 0;
    next_sequence_ = 1;
    dirty_ = false;
    pending_write_count_ = 0;
    dirty_since_ms_ = 0;
    if (ensureFs() && path_ && InternalFS.exists(path_))
    {
        InternalFS.remove(path_);
    }
}

bool InternalFsStore::updateMessageStatus(::chat::MessageId msg_id, ::chat::MessageStatus status)
{
    if (msg_id == 0)
    {
        return false;
    }

    for (auto& pair : conversations_)
    {
        auto& storage = pair.second;
        const size_t count = storage.messages.size();
        for (size_t i = 0; i < count; ++i)
        {
            ::chat::ChatMessage* msg = &storage.messages[i].message;
            if (!msg || msg->msg_id != msg_id || msg->from != 0)
            {
                continue;
            }
            msg->status = status;
            markDirty();
            maybeSave();
            return true;
        }
    }

    return false;
}

bool InternalFsStore::updateMessageStatusForProtocol(
    ::chat::MessageId msg_id,
    ::chat::MeshProtocol protocol,
    ::chat::MessageStatus status)
{
    if (msg_id == 0)
    {
        return false;
    }

    for (auto& pair : conversations_)
    {
        auto& storage = pair.second;
        const size_t count = storage.messages.size();
        for (size_t i = 0; i < count; ++i)
        {
            ::chat::ChatMessage* msg = &storage.messages[i].message;
            if (!msg || msg->msg_id != msg_id || msg->protocol != protocol ||
                msg->from != 0)
            {
                continue;
            }
            msg->status = status;
            markDirty();
            maybeSave();
            return true;
        }
    }

    return false;
}

bool InternalFsStore::getMessage(::chat::MessageId msg_id, ::chat::ChatMessage* out) const
{
    if (msg_id == 0)
    {
        return false;
    }

    for (const auto& pair : conversations_)
    {
        const auto& storage = pair.second;
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

bool InternalFsStore::getMessageForProtocol(
    ::chat::MessageId msg_id,
    ::chat::MeshProtocol protocol,
    ::chat::ChatMessage* out) const
{
    if (msg_id == 0)
    {
        return false;
    }

    for (const auto& pair : conversations_)
    {
        const auto& storage = pair.second;
        for (const auto& entry : storage.messages)
        {
            if (entry.message.msg_id != msg_id ||
                entry.message.protocol != protocol)
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

bool InternalFsStore::ensureFs() const
{
    return path_ && InternalFS.begin();
}

bool InternalFsStore::loadFromFs()
{
    conversations_.clear();
    total_message_count_ = 0;
    next_sequence_ = 1;
    if (!ensureFs() || !InternalFS.exists(path_))
    {
        return false;
    }

    auto file = InternalFS.open(path_, FILE_O_READ);
    if (!file)
    {
        return false;
    }

    FileHeader header{};
    if (file.read(&header, sizeof(header)) != sizeof(header) || header.magic != kMagic)
    {
        file.close();
        InternalFS.remove(path_);
        return false;
    }

    if (header.version != kVersion &&
        header.version != kReticulumIdentityVersion &&
        header.version != kLegacyVersion)
    {
        file.close();
        InternalFS.remove(path_);
        return false;
    }

    const uint16_t conversation_count = header.conversation_count;
    next_sequence_ = std::max<uint32_t>(1U, header.next_sequence);
    const bool legacy_format = header.version == kLegacyVersion;
    const bool reticulum_identity_format =
        header.version == kReticulumIdentityVersion;

    for (uint16_t index = 0; index < conversation_count; ++index)
    {
        ::chat::ConversationId conv{};
        int32_t unread_count = 0;
        uint16_t message_count = 0;

        if (legacy_format)
        {
            ConversationRecordV2 conv_record{};
            if (file.read(&conv_record, sizeof(conv_record)) != sizeof(conv_record))
            {
                file.close();
                InternalFS.remove(path_);
                conversations_.clear();
                return false;
            }
            conv = ::chat::ConversationId(
                static_cast<::chat::ChannelId>(conv_record.channel),
                conv_record.peer,
                static_cast<::chat::MeshProtocol>(conv_record.protocol));
            unread_count = conv_record.unread_count;
            message_count = conv_record.message_count;
        }
        else
        {
            ConversationRecord conv_record{};
            if (file.read(&conv_record, sizeof(conv_record)) != sizeof(conv_record))
            {
                file.close();
                InternalFS.remove(path_);
                conversations_.clear();
                return false;
            }
            conv = ::chat::ConversationId(
                static_cast<::chat::ChannelId>(conv_record.channel),
                conv_record.peer,
                static_cast<::chat::MeshProtocol>(conv_record.protocol));
            if ((conv_record.flags & kConversationHasReticulumIdentityFlag) != 0)
            {
                readReticulumIdentityFromRecord(
                    conv.reticulum_identity,
                    conv_record.reticulum_destination_hash,
                    conv_record.reticulum_identity_hash);
            }
            unread_count = conv_record.unread_count;
            message_count = conv_record.message_count;
        }

        ConversationStorage& storage = getConversationStorage(conv);
        storage.unread_count = unread_count;

        for (uint16_t message_index = 0; message_index < message_count; ++message_index)
        {
            MessageRecord rec{};
            MessageRecordV3 rec_v3{};
            MessageRecordV2 legacy_rec{};
            if (legacy_format)
            {
                if (file.read(&legacy_rec, sizeof(legacy_rec)) != sizeof(legacy_rec))
                {
                    file.close();
                    InternalFS.remove(path_);
                    conversations_.clear();
                    return false;
                }
            }
            else if (reticulum_identity_format)
            {
                if (file.read(&rec_v3, sizeof(rec_v3)) != sizeof(rec_v3))
                {
                    file.close();
                    InternalFS.remove(path_);
                    conversations_.clear();
                    return false;
                }
            }
            else if (file.read(&rec, sizeof(rec)) != sizeof(rec))
            {
                file.close();
                InternalFS.remove(path_);
                conversations_.clear();
                return false;
            }

            ::chat::ChatMessage msg;
            uint32_t sequence = 0;
            if (legacy_format)
            {
                msg.protocol = static_cast<::chat::MeshProtocol>(legacy_rec.protocol);
                msg.channel = static_cast<::chat::ChannelId>(legacy_rec.channel);
                msg.from = legacy_rec.from;
                msg.peer = legacy_rec.peer;
                msg.msg_id = legacy_rec.msg_id;
                msg.timestamp = legacy_rec.timestamp;
                msg.team_location_icon = legacy_rec.team_location_icon;
                msg.has_geo = (legacy_rec.flags & kMessageHasGeoFlag) != 0;
                msg.geo_lat_e7 = legacy_rec.geo_lat_e7;
                msg.geo_lon_e7 = legacy_rec.geo_lon_e7;
                msg.status = static_cast<::chat::MessageStatus>(legacy_rec.status);
                msg.text.assign(legacy_rec.text,
                                std::min<size_t>(legacy_rec.text_len,
                                                 sizeof(legacy_rec.text)));
                sequence = legacy_rec.sequence;
            }
            else if (reticulum_identity_format)
            {
                msg.protocol = static_cast<::chat::MeshProtocol>(rec_v3.protocol);
                msg.channel = static_cast<::chat::ChannelId>(rec_v3.channel);
                msg.from = rec_v3.from;
                msg.peer = rec_v3.peer;
                msg.msg_id = rec_v3.msg_id;
                msg.timestamp = rec_v3.timestamp;
                msg.team_location_icon = rec_v3.team_location_icon;
                msg.has_geo = (rec_v3.flags & kMessageHasGeoFlag) != 0;
                msg.geo_lat_e7 = rec_v3.geo_lat_e7;
                msg.geo_lon_e7 = rec_v3.geo_lon_e7;
                msg.status = static_cast<::chat::MessageStatus>(rec_v3.status);
                if ((rec_v3.flags & kMessageHasReticulumIdentityFlag) != 0)
                {
                    readReticulumIdentityFromRecord(
                        msg.reticulum_identity,
                        rec_v3.reticulum_destination_hash,
                        rec_v3.reticulum_identity_hash);
                }
                msg.text.assign(rec_v3.text,
                                std::min<size_t>(rec_v3.text_len,
                                                 sizeof(rec_v3.text)));
                sequence = rec_v3.sequence;
            }
            else
            {
                msg.protocol = static_cast<::chat::MeshProtocol>(rec.protocol);
                msg.channel = static_cast<::chat::ChannelId>(rec.channel);
                msg.from = rec.from;
                msg.peer = rec.peer;
                msg.msg_id = rec.msg_id;
                msg.timestamp = rec.timestamp;
                msg.team_location_icon = rec.team_location_icon;
                msg.has_geo = (rec.flags & kMessageHasGeoFlag) != 0;
                msg.geo_lat_e7 = rec.geo_lat_e7;
                msg.geo_lon_e7 = rec.geo_lon_e7;
                msg.status = static_cast<::chat::MessageStatus>(rec.status);
                msg.rx_origin = static_cast<::chat::RxOrigin>(rec.rx_origin);
                if ((rec.flags & kMessageHasReticulumIdentityFlag) != 0)
                {
                    readReticulumIdentityFromRecord(
                        msg.reticulum_identity,
                        rec.reticulum_destination_hash,
                        rec.reticulum_identity_hash);
                }
                msg.text.assign(rec.text,
                                std::min<size_t>(rec.text_len, sizeof(rec.text)));
                sequence = rec.sequence;
            }
            StoredMessageEntry entry;
            entry.message = msg;
            entry.sequence = sequence;
            storage.messages.push_back(entry);
            total_message_count_++;
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
    return true;
}

bool InternalFsStore::saveToFs() const
{
    if (!ensureFs())
    {
        return false;
    }

    std::string temp_path = std::string(path_) + kTempSuffix;
    Adafruit_LittleFS_Namespace::File file(InternalFS);
    if (!::platform::nrf52::arduino_common::internal_fs::openTempForReplace(temp_path.c_str(), &file))
    {
        return false;
    }

    FileHeader header{};
    header.magic = kMagic;
    header.version = kVersion;
    header.conversation_count = static_cast<uint16_t>(std::min<size_t>(conversations_.size(), 0xFFFF));
    header.next_sequence = next_sequence_;
    if (file.write(reinterpret_cast<const uint8_t*>(&header), sizeof(header)) != sizeof(header))
    {
        file.close();
        ::platform::nrf52::arduino_common::internal_fs::removeIfExists(temp_path.c_str());
        return false;
    }

    for (const auto& pair : conversations_)
    {
        const auto& conv = pair.first;
        const auto& storage = pair.second;
        ConversationRecord conv_record{};
        conv_record.protocol = static_cast<uint8_t>(conv.protocol);
        conv_record.channel = static_cast<uint8_t>(conv.channel);
        conv_record.peer = conv.peer;
        conv_record.unread_count = storage.unread_count;
        conv_record.message_count = static_cast<uint16_t>(std::min<size_t>(storage.messages.size(), 0xFFFF));
        if (conv.protocol == ::chat::MeshProtocol::Reticulum &&
            ::chat::hasReticulumDestinationIdentity(conv.reticulum_identity))
        {
            conv_record.flags |= kConversationHasReticulumIdentityFlag;
            copyReticulumIdentityToRecord(
                conv_record.reticulum_destination_hash,
                conv_record.reticulum_identity_hash,
                conv.reticulum_identity);
        }
        if (file.write(reinterpret_cast<const uint8_t*>(&conv_record), sizeof(conv_record)) != sizeof(conv_record))
        {
            file.close();
            ::platform::nrf52::arduino_common::internal_fs::removeIfExists(temp_path.c_str());
            return false;
        }

        for (size_t i = 0; i < storage.messages.size(); ++i)
        {
            const StoredMessageEntry& entry = storage.messages[i];
            const ::chat::ChatMessage* msg = &entry.message;

            MessageRecord rec{};
            rec.protocol = static_cast<uint8_t>(msg->protocol);
            rec.channel = static_cast<uint8_t>(msg->channel);
            rec.status = static_cast<uint8_t>(msg->status);
            rec.flags = msg->has_geo ? kMessageHasGeoFlag : 0x00U;
            rec.rx_origin = static_cast<uint8_t>(msg->rx_origin);
            if (msg->protocol == ::chat::MeshProtocol::Reticulum &&
                ::chat::hasReticulumDestinationIdentity(msg->reticulum_identity))
            {
                rec.flags |= kMessageHasReticulumIdentityFlag;
                copyReticulumIdentityToRecord(
                    rec.reticulum_destination_hash,
                    rec.reticulum_identity_hash,
                    msg->reticulum_identity);
            }
            rec.from = msg->from;
            rec.peer = msg->peer;
            rec.msg_id = msg->msg_id;
            rec.timestamp = msg->timestamp;
            rec.sequence = entry.sequence;
            rec.team_location_icon = msg->team_location_icon;
            rec.geo_lat_e7 = msg->geo_lat_e7;
            rec.geo_lon_e7 = msg->geo_lon_e7;
            rec.text_len = static_cast<uint16_t>(std::min<size_t>(msg->text.size(), sizeof(rec.text)));
            if (rec.text_len > 0)
            {
                std::memcpy(rec.text, msg->text.data(), rec.text_len);
            }

            if (file.write(reinterpret_cast<const uint8_t*>(&rec), sizeof(rec)) != sizeof(rec))
            {
                file.close();
                ::platform::nrf52::arduino_common::internal_fs::removeIfExists(temp_path.c_str());
                return false;
            }
        }
    }

    file.flush();
    file.close();

    const bool renamed = ::platform::nrf52::arduino_common::internal_fs::commitTempReplace(path_, temp_path.c_str());
    if (renamed)
    {
        dirty_ = false;
        pending_write_count_ = 0;
        dirty_since_ms_ = 0;
        last_save_ms_ = sys::millis_now();
    }
    return renamed;
}

void InternalFsStore::flush()
{
    maybeSave(true);
}

void InternalFsStore::markDirty()
{
    if (!dirty_)
    {
        dirty_ = true;
        dirty_since_ms_ = sys::millis_now();
    }
    ++pending_write_count_;
}

void InternalFsStore::maybeSave(bool force)
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

    (void)saveToFs();
}

void InternalFsStore::evictOldestMessage()
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

    auto& storage = oldest_it->second;
    const ::chat::ChatMessage removed = storage.messages[oldest_index].message;
    storage.messages.erase(storage.messages.begin() + static_cast<long>(oldest_index));
    if (removed.status == ::chat::MessageStatus::Incoming && storage.unread_count > 0)
    {
        storage.unread_count--;
    }
    if (storage.messages.empty())
    {
        conversations_.erase(oldest_it);
    }
    if (total_message_count_ > 0)
    {
        total_message_count_--;
    }
}

InternalFsStore::ConversationStorage& InternalFsStore::getConversationStorage(const ::chat::ConversationId& conv)
{
    auto it = conversations_.find(conv);
    if (it == conversations_.end())
    {
        auto result = conversations_.emplace(conv, ConversationStorage{});
        return result.first->second;
    }
    return it->second;
}

const InternalFsStore::ConversationStorage& InternalFsStore::getConversationStorage(const ::chat::ConversationId& conv) const
{
    auto it = conversations_.find(conv);
    if (it == conversations_.end())
    {
        static ConversationStorage empty;
        return empty;
    }
    return it->second;
}

} // namespace platform::nrf52::arduino_common::chat::infra::store
