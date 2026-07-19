#include "chat/delivery/chat_delivery_message_projection.h"
#include "chat/delivery/chat_delivery_read_model.h"
#include "chat/domain/chat_model.h"
#include "chat/infra/store/ram_store.h"
#include "chat/ports/i_mesh_adapter.h"
#include "chat/usecase/chat_service.h"
#include "chat/usecase/contact_service.h"
#include "sys/clock.h"
#include "ui/presentation_sources/chat_presentation_source.h"
#include "ui/presentation_sources/runtime_chat_action_sink.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

namespace
{

class FakeMeshAdapter final : public ::chat::IMeshAdapter
{
  public:
    bool sendText(::chat::ChannelId channel,
                  const std::string& text,
                  ::chat::MessageId* out_msg_id,
                  ::chat::NodeId peer = 0) override
    {
        ++send_count;
        last_channel = channel;
        last_text = text;
        last_peer = peer;
        if (out_msg_id)
        {
            *out_msg_id = next_id++;
        }
        return send_ok;
    }

    ::chat::MeshSendResult sendTextDetailed(::chat::ChannelId channel,
                                            const std::string& text,
                                            ::chat::MessageId forced_msg_id = 0,
                                            ::chat::NodeId peer = 0) override
    {
        ::chat::MessageId msg_id = forced_msg_id;
        const bool ok = sendText(channel, text, &msg_id, peer);
        if (ok)
        {
            return ::chat::MeshSendResult::success(msg_id);
        }
        return ::chat::MeshSendResult::fail(send_failure, fail_returns_msg_id ? msg_id : 0);
    }

    bool pollIncomingText(::chat::MeshIncomingText* out) override
    {
        if (!out || incoming.empty())
        {
            return false;
        }
        *out = incoming.front();
        incoming.pop_front();
        return true;
    }
    bool sendAppData(::chat::ChannelId,
                     uint32_t,
                     const uint8_t*,
                     size_t,
                     ::chat::NodeId = 0,
                     bool = false,
                     ::chat::MessageId = 0,
                     bool = false) override
    {
        return false;
    }
    bool pollIncomingData(::chat::MeshIncomingData*) override { return false; }
    void applyConfig(const ::chat::MeshConfig&) override {}
    bool isReady() const override { return true; }
    bool pollIncomingRawPacket(uint8_t*, size_t&, size_t) override { return false; }
    ::chat::NodeId getNodeId() const override { return self_node_id; }

    int send_count = 0;
    bool send_ok = true;
    bool fail_returns_msg_id = true;
    ::chat::MeshOperationFailure send_failure = ::chat::MeshOperationFailure::Unknown;
    ::chat::MessageId next_id = 100;
    ::chat::ChannelId last_channel = ::chat::ChannelId::PRIMARY;
    std::string last_text;
    ::chat::NodeId last_peer = 0;
    ::chat::NodeId self_node_id = 0;
    std::deque<::chat::MeshIncomingText> incoming;
};

class FakeNodeStore final : public ::chat::contacts::INodeStore
{
  public:
    void begin() override {}

    void applyUpdate(uint32_t node_id,
                     const ::chat::contacts::NodeUpdate& update) override
    {
        auto* entry = findOrCreate(node_id);
        if (update.short_name != nullptr)
        {
            std::snprintf(entry->short_name,
                          sizeof(entry->short_name),
                          "%s",
                          update.short_name);
        }
        if (update.long_name != nullptr)
        {
            std::snprintf(entry->long_name,
                          sizeof(entry->long_name),
                          "%s",
                          update.long_name);
        }
        if (update.has_last_seen)
        {
            entry->last_seen = update.last_seen;
        }
        if (update.has_protocol)
        {
            entry->protocol = update.protocol;
        }
        if (update.reticulum_identity.valid)
        {
            entry->reticulum_identity = update.reticulum_identity;
        }
        if (update.has_position)
        {
            updatePosition(node_id, update.position);
        }
    }

    void upsert(uint32_t node_id,
                const char* short_name,
                const char* long_name,
                uint32_t now_secs,
                float snr = 0.0f,
                float rssi = 0.0f,
                uint8_t protocol = 0,
                uint8_t role = ::chat::contacts::kNodeRoleUnknown,
                uint8_t hops_away = 0xFF,
                uint8_t hw_model = 0,
                uint8_t channel = 0xFF) override
    {
        (void)snr;
        (void)rssi;
        (void)protocol;
        (void)role;
        (void)hops_away;
        (void)hw_model;
        (void)channel;
        auto* entry = findOrCreate(node_id);
        std::snprintf(entry->short_name,
                      sizeof(entry->short_name),
                      "%s",
                      short_name ? short_name : "");
        std::snprintf(entry->long_name,
                      sizeof(entry->long_name),
                      "%s",
                      long_name ? long_name : "");
        entry->last_seen = now_secs;
    }

    void updateProtocol(uint32_t, uint8_t, uint32_t) override {}

    void updatePosition(uint32_t node_id,
                        const ::chat::contacts::NodePosition& position) override
    {
        auto* entry = findOrCreate(node_id);
        entry->position_valid = position.valid;
        entry->position_latitude_i = position.latitude_i;
        entry->position_longitude_i = position.longitude_i;
        entry->position_has_altitude = position.has_altitude;
        entry->position_altitude = position.altitude;
        entry->position_timestamp = position.timestamp;
    }

    bool remove(uint32_t node_id) override
    {
        for (auto it = entries.begin(); it != entries.end(); ++it)
        {
            if (it->node_id == node_id)
            {
                entries.erase(it);
                return true;
            }
        }
        return false;
    }

    const std::vector<::chat::contacts::NodeEntry>& getEntries() const override
    {
        return entries;
    }

    void clear() override { entries.clear(); }
    bool flush() override { return true; }

  private:
    ::chat::contacts::NodeEntry* findOrCreate(uint32_t node_id)
    {
        for (auto& entry : entries)
        {
            if (entry.node_id == node_id)
            {
                return &entry;
            }
        }
        ::chat::contacts::NodeEntry entry{};
        entry.node_id = node_id;
        entries.push_back(entry);
        return &entries.back();
    }

    std::vector<::chat::contacts::NodeEntry> entries;
};

class FakeContactStore final : public ::chat::contacts::IContactStore
{
  public:
    void begin() override {}
    std::string getNickname(uint32_t) const override { return {}; }
    bool setNickname(uint32_t, const char*) override { return false; }
    bool removeNickname(uint32_t) override { return false; }
    bool hasNickname(const char*) const override { return false; }
    std::vector<uint32_t> getAllContactIds() const override { return {}; }
    size_t getCount() const override { return 0; }
};

class PagingStore final : public ::chat::IChatStore
{
  public:
    void append(const ::chat::ChatMessage& msg) override
    {
        messages_.push_back(msg);
    }

    std::vector<::chat::ChatMessage> loadRecent(const ::chat::ConversationId& conv,
                                                size_t n) override
    {
        return loadPageFromLatest(conv, 0, n, nullptr);
    }

    std::vector<::chat::ChatMessage> loadPageFromLatest(
        const ::chat::ConversationId& conv,
        size_t offset_from_latest,
        size_t limit,
        size_t* total) override
    {
        size_t count = 0;
        for (const auto& msg : messages_)
        {
            if (::chat::conversationIdForMessage(msg) == conv)
            {
                ++count;
            }
        }
        if (total)
        {
            *total = count;
        }
        if (limit == 0 || offset_from_latest >= count)
        {
            return {};
        }

        const size_t available = count - offset_from_latest;
        const size_t to_read = available < limit ? available : limit;
        const size_t start = count - offset_from_latest - to_read;
        const size_t end = start + to_read;

        std::vector<::chat::ChatMessage> out;
        out.reserve(to_read);
        size_t index = 0;
        for (const auto& msg : messages_)
        {
            if (!(::chat::conversationIdForMessage(msg) == conv))
            {
                continue;
            }
            if (index >= start && index < end)
            {
                out.push_back(msg);
            }
            ++index;
        }
        return out;
    }

    std::vector<::chat::ConversationMeta> loadConversationPage(size_t offset,
                                                               size_t limit,
                                                               size_t* total) override
    {
        const size_t count = messages_.empty() ? 0U : 1U;
        if (total)
        {
            *total = count;
        }
        if (messages_.empty() || offset > 0)
        {
            return {};
        }

        const auto& latest = messages_.back();
        ::chat::ConversationMeta meta;
        meta.id = ::chat::conversationIdForMessage(latest);
        meta.preview = latest.text;
        meta.last_timestamp = latest.timestamp;
        return {meta};
    }

    void setUnread(const ::chat::ConversationId&, int unread) override
    {
        unread_ = unread;
    }

    int getUnread(const ::chat::ConversationId&) const override
    {
        return unread_;
    }

    void clearConversation(const ::chat::ConversationId& conv) override
    {
        std::vector<::chat::ChatMessage> kept;
        kept.reserve(messages_.size());
        for (const auto& msg : messages_)
        {
            if (!(::chat::conversationIdForMessage(msg) == conv))
            {
                kept.push_back(msg);
            }
        }
        messages_ = kept;
    }

    void clearAll() override
    {
        messages_.clear();
        unread_ = 0;
    }

    bool updateMessageStatus(::chat::MessageId msg_id,
                             ::chat::MessageStatus status) override
    {
        for (auto& msg : messages_)
        {
            if (msg.msg_id == msg_id)
            {
                msg.status = status;
                return true;
            }
        }
        return false;
    }

    bool updateMessageStatusForProtocol(::chat::MessageId msg_id,
                                        ::chat::MeshProtocol protocol,
                                        ::chat::MessageStatus status) override
    {
        for (auto& msg : messages_)
        {
            if (msg.msg_id == msg_id && msg.protocol == protocol)
            {
                msg.status = status;
                return true;
            }
        }
        return false;
    }

    bool getMessage(::chat::MessageId msg_id,
                    ::chat::ChatMessage* out) const override
    {
        for (const auto& msg : messages_)
        {
            if (msg.msg_id == msg_id)
            {
                if (out)
                {
                    *out = msg;
                }
                return true;
            }
        }
        return false;
    }

    bool getMessageForProtocol(::chat::MessageId msg_id,
                               ::chat::MeshProtocol protocol,
                               ::chat::ChatMessage* out) const override
    {
        for (const auto& msg : messages_)
        {
            if (msg.msg_id == msg_id && msg.protocol == protocol)
            {
                if (out)
                {
                    *out = msg;
                }
                return true;
            }
        }
        return false;
    }

  private:
    std::vector<::chat::ChatMessage> messages_;
    int unread_ = 0;
};

ui::chat::ConversationId directPeer(uint32_t peer)
{
    ui::chat::ConversationId id;
    id.kind = ui::chat::ConversationKind::DirectPeer;
    id.protocol = ui::chat::ChatProtocolKind::Meshtastic;
    id.primary = peer;
    id.secondary = static_cast<uint32_t>(::chat::ChannelId::PRIMARY);
    return id;
}

ui::chat::ConversationId teamConversation()
{
    ui::chat::ConversationId id;
    id.kind = ui::chat::ConversationKind::Team;
    id.protocol = ui::chat::ChatProtocolKind::TrailMate;
    id.primary = 7;
    return id;
}

ui::chat::ConversationId broadcastConversation()
{
    ui::chat::ConversationId id;
    id.kind = ui::chat::ConversationKind::Channel;
    id.protocol = ui::chat::ChatProtocolKind::Meshtastic;
    id.primary = static_cast<uint32_t>(::chat::ChannelId::PRIMARY);
    return id;
}

ui::chat::ConversationId meshCoreBroadcastConversation()
{
    ui::chat::ConversationId id;
    id.kind = ui::chat::ConversationKind::Channel;
    id.protocol = ui::chat::ChatProtocolKind::MeshCore;
    id.primary = static_cast<uint32_t>(::chat::ChannelId::PRIMARY);
    return id;
}

ui::chat::ConversationId systemConversation()
{
    ui::chat::ConversationId id;
    id.kind = ui::chat::ConversationKind::System;
    id.protocol = ui::chat::ChatProtocolKind::TrailMate;
    id.primary = 1;
    return id;
}

const ui::chat::ConversationLocationParticipant* findLocationParticipant(
    const ui::chat::ChatWorkspaceSnapshot& snapshot,
    uint32_t node_id)
{
    for (size_t i = 0; i < snapshot.location_participant_count; ++i)
    {
        if (snapshot.location_participants[i].node_id == node_id)
        {
            return &snapshot.location_participants[i];
        }
    }
    return nullptr;
}

uint16_t unreadForConversation(const ui::chat::ChatWorkspaceSnapshot& snapshot,
                               const ui::chat::ConversationId& id)
{
    for (size_t i = 0; i < snapshot.conversation_count; ++i)
    {
        if (snapshot.conversations[i].id == id)
        {
            return snapshot.conversations[i].unread_count;
        }
    }
    assert(false);
    return 0;
}

void setNodePosition(::chat::contacts::ContactService& contacts,
                     uint32_t node_id,
                     int32_t lat_e7,
                     int32_t lon_e7)
{
    ::chat::contacts::NodePosition pos{};
    pos.valid = true;
    pos.latitude_i = lat_e7;
    pos.longitude_i = lon_e7;
    pos.timestamp = 1700000000U;
    contacts.updateNodePosition(node_id, pos);
}

} // namespace

int main()
{
    sys::set_epoch_seconds_provider([]() -> uint32_t
                                    { return 1700000000U; });
    sys::set_millis_provider([]() -> uint32_t
                             { return 5000U; });

    ::chat::ChatModel model;
    FakeMeshAdapter mesh;
    mesh.self_node_id = 0x01020304;
    ::chat::RamStore store;
    ::chat::ChatService service(model, mesh, store);
    ::chat::delivery::ChatDeliveryReadModel delivery_read_model;
    FakeNodeStore node_store;
    FakeContactStore contact_store;
    ::chat::contacts::ContactService contacts(node_store, contact_store);
    setNodePosition(contacts, mesh.self_node_id, 312345678, 1219876543);
    setNodePosition(contacts, 1234, 313000000, 1220000000);
    contacts.updateNodeInfo(1234,
                            "04D2",
                            "Ada Mesh",
                            0.0f,
                            0.0f,
                            1700000000U,
                            0,
                            ::chat::contacts::kNodeRoleUnknown,
                            0xFF,
                            0,
                            0xFF);

    ui::presentation_sources::RuntimeChatActionSink sink(service);
    ui::presentation_sources::ChatPresentationSource source(
        service, &contacts, &delivery_read_model, &mesh);

    const ui::chat::ConversationId ada = directPeer(1234);
    ui::chat::SendMessageView send;
    send.conversation = ada;
    send.text = "hello";
    send.text_len = 5;

    const auto send_result = sink.sendMessage(send);
    assert(send_result.ok);
    assert(mesh.send_count == 1);
    assert(mesh.last_peer == 1234);
    assert(mesh.last_text == "hello");

    const ui::chat::ConversationId meshcore_channel = meshCoreBroadcastConversation();
    const int send_count_before_mismatch = mesh.send_count;
    const ui::chat::SendMessageView mismatched_send{meshcore_channel, "mc", 2};
    const auto mismatched_result = sink.sendMessage(mismatched_send);
    assert(!mismatched_result.ok);
    assert(mismatched_result.failure == ui::UiActionFailure::Unsupported);
    assert(mesh.send_count == send_count_before_mismatch);

    ui::chat::ChatWorkspaceRequest mismatched_request;
    mismatched_request.selected = meshcore_channel;
    ui::chat::ChatWorkspaceSnapshot mismatched_snapshot;
    assert(source.buildChatWorkspaceSnapshot(mismatched_request, mismatched_snapshot));
    assert(!mismatched_snapshot.can_send);
    assert(!mismatched_snapshot.composer_enabled);

    ::chat::delivery::ChatDeliveryRecord delivered{};
    delivered.ref.protocol_id = 100;
    delivered.ref.protocol =
        static_cast<uint8_t>(::chat::MeshProtocol::Meshtastic);
    delivered.state = ::chat::delivery::DeliveryState::Delivered;
    delivered.failure = ::chat::delivery::DeliveryFailureKind::None;
    assert(delivery_read_model.upsert(delivered));

    ui::chat::ChatWorkspaceRequest request;
    request.selected = ada;
    ui::chat::ChatWorkspaceSnapshot snapshot;
    assert(source.buildChatWorkspaceSnapshot(request, snapshot));
    assert(snapshot.header.valid);
    assert(snapshot.conversation_count == 1);
    assert(snapshot.conversations[0].id == ada);
    assert(snapshot.conversations[0].selected);
    assert(snapshot.conversations[0].last_timestamp != 0);
    assert(std::strcmp(snapshot.conversations[0].title.c_str(), "Ada Mesh") == 0);
    assert(snapshot.message_count == 1);
    assert(snapshot.messages[0].conversation == ada);
    assert(snapshot.messages[0].outgoing);
    assert(std::strcmp(snapshot.messages[0].time_label.c_str(), "") != 0);
    assert(std::strtoul(snapshot.messages[0].time_label.c_str(), nullptr, 10) ==
           snapshot.conversations[0].last_timestamp);
    assert(snapshot.messages[0].delivery ==
           ui::chat::MessageDeliveryState::Delivered);
    assert(snapshot.messages[0].failure == ui::chat::MessageFailureKind::None);
    assert(std::strcmp(snapshot.messages[0].text.c_str(), "hello") == 0);
    assert(snapshot.can_send);
    assert(snapshot.composer_enabled);
    assert(snapshot.location_participant_count == 2);
    const auto* self_location =
        findLocationParticipant(snapshot, mesh.self_node_id);
    assert(self_location != nullptr);
    assert(self_location->self);
    assert(self_location->valid);
    assert(self_location->lat > 31.23 && self_location->lat < 31.24);
    const auto* peer_location = findLocationParticipant(snapshot, 1234);
    assert(peer_location != nullptr);
    assert(!peer_location->self);
    assert(peer_location->valid);
    assert(peer_location->lon > 121.99 && peer_location->lon < 122.01);

    mesh.send_ok = false;
    mesh.send_failure = ::chat::MeshOperationFailure::PeerKeyMissing;
    const ui::chat::SendMessageView failed_send{ada, "fail", 4};
    const auto rejected_send = sink.sendMessage(failed_send);
    assert(!rejected_send.ok);
    assert(rejected_send.failure == ui::UiActionFailure::PeerKeyMissing);

    mesh.fail_returns_msg_id = false;
    mesh.send_failure = ::chat::MeshOperationFailure::ChannelKeyMissing;
    const auto channel_key_send = sink.sendMessage(failed_send);
    assert(!channel_key_send.ok);
    assert(channel_key_send.failure == ui::UiActionFailure::ChannelKeyMissing);

    mesh.send_failure = ::chat::MeshOperationFailure::RadioOffline;
    const auto radio_offline_send = sink.sendMessage(failed_send);
    assert(!radio_offline_send.ok);
    assert(radio_offline_send.failure == ui::UiActionFailure::RadioOffline);
    mesh.fail_returns_msg_id = true;
    assert(delivery_read_model.upsert(::chat::delivery::toFailedDeliveryRecord(
        ::chat::delivery::ChatDeliveryRef{
            0,
            101,
            0,
            static_cast<uint8_t>(::chat::MeshProtocol::Meshtastic)},
        ::chat::delivery::SendFailureKind::PeerKeyMissing)));
    assert(source.buildChatWorkspaceSnapshot(request, snapshot));
    assert(snapshot.message_count == 2);
    assert(snapshot.messages[1].delivery == ui::chat::MessageDeliveryState::Failed);
    assert(snapshot.messages[1].failure ==
           ui::chat::MessageFailureKind::PeerKeyMissing);

    assert(delivery_read_model.upsert(::chat::delivery::toFailedDeliveryRecord(
        ::chat::delivery::ChatDeliveryRef{
            0,
            101,
            0,
            static_cast<uint8_t>(::chat::MeshProtocol::Meshtastic)},
        ::chat::delivery::SendFailureKind::ChannelKeyMissing)));
    assert(source.buildChatWorkspaceSnapshot(request, snapshot));
    assert(snapshot.message_count == 2);
    assert(snapshot.messages[1].failure ==
           ui::chat::MessageFailureKind::ChannelKeyMissing);

    assert(sink.markRead(ada).ok);

    ::chat::MeshIncomingText incoming{};
    incoming.channel = ::chat::ChannelId::PRIMARY;
    incoming.from = 0x648144D4;
    incoming.to = 0xFFFFFFFFUL;
    incoming.msg_id = 900;
    incoming.text = "broadcast hello";
    incoming.source_unverified = true;
    incoming.rx_meta.origin = ::chat::RxOrigin::LoRa;
    contacts.updateNodeInfo(0x648144D4,
                            "44D4",
                            "Mother",
                            0.0f,
                            0.0f,
                            1700000000U,
                            0,
                            ::chat::contacts::kNodeRoleUnknown,
                            0xFF,
                            0,
                            0xFF);
    mesh.incoming.push_back(incoming);
    service.processIncoming();

    const ui::chat::ConversationId broadcast = broadcastConversation();
    request.selected = broadcast;
    assert(source.buildChatWorkspaceSnapshot(request, snapshot));
    assert(snapshot.header.valid);
    assert(snapshot.message_count == 1);
    assert(snapshot.messages[0].conversation == broadcast);
    assert(!snapshot.messages[0].outgoing);
    assert(snapshot.messages[0].ingress_transport ==
           ui::chat::MessageIngressTransport::LoRa);
    assert(snapshot.messages[0].source_unverified);
    assert(snapshot.messages[0].sender_node_id == 0x648144D4);
    assert(std::strcmp(snapshot.messages[0].sender_label.c_str(), "Mother") == 0);
    assert(unreadForConversation(snapshot, broadcast) == 1);
    assert(sink.markRead(broadcast).ok);
    assert(source.buildChatWorkspaceSnapshot(request, snapshot));
    assert(unreadForConversation(snapshot, broadcast) == 0);

    const uint32_t paging_peer = 0x00ABCDEF;
    contacts.updateNodeInfo(paging_peer,
                            "CDEF",
                            "Pager",
                            0.0f,
                            0.0f,
                            1700000000U,
                            0,
                            ::chat::contacts::kNodeRoleUnknown,
                            0xFF,
                            0,
                            0xFF);
    ::chat::ChatModel paging_model;
    FakeMeshAdapter paging_mesh;
    paging_mesh.self_node_id = mesh.self_node_id;
    PagingStore paging_store;
    ::chat::ChatService paging_service(paging_model, paging_mesh, paging_store);
    ui::presentation_sources::ChatPresentationSource paging_source(
        paging_service, &contacts, &delivery_read_model, &paging_mesh);
    const ui::chat::ConversationId paging = directPeer(paging_peer);
    for (::chat::MessageId id = 1; id <= 25; ++id)
    {
        ::chat::ChatMessage page_msg;
        page_msg.protocol = ::chat::MeshProtocol::Meshtastic;
        page_msg.channel = ::chat::ChannelId::PRIMARY;
        page_msg.from = paging_peer;
        page_msg.peer = paging_peer;
        page_msg.msg_id = 2000 + id;
        page_msg.timestamp = 1700000000U + id;
        page_msg.text = "page-" + std::to_string(id);
        page_msg.status = ::chat::MessageStatus::Incoming;
        paging_store.append(page_msg);
    }

    ui::chat::ChatWorkspaceRequest paging_request;
    paging_request.selected = paging;
    paging_request.message_offset = 0;
    assert(paging_source.buildChatWorkspaceSnapshot(paging_request, snapshot));
    assert(snapshot.message_count == ui::chat::ChatWorkspaceSnapshot::kMaxMessages);
    assert(snapshot.message_total_count == 25);
    assert(!snapshot.has_newer_messages);
    assert(snapshot.has_older_messages);
    assert(std::strcmp(snapshot.messages[0].text.c_str(), "page-6") == 0);
    assert(std::strcmp(snapshot.messages[19].text.c_str(), "page-25") == 0);

    paging_request.message_offset = ui::chat::ChatWorkspaceSnapshot::kMaxMessages;
    assert(paging_source.buildChatWorkspaceSnapshot(paging_request, snapshot));
    assert(snapshot.message_count == 5);
    assert(snapshot.message_total_count == 25);
    assert(snapshot.has_newer_messages);
    assert(!snapshot.has_older_messages);
    assert(std::strcmp(snapshot.messages[0].text.c_str(), "page-1") == 0);
    assert(std::strcmp(snapshot.messages[4].text.c_str(), "page-5") == 0);

    service.setActiveProtocol(::chat::MeshProtocol::MeshCore);
    ::chat::MeshIncomingText unknown_meshcore_incoming{};
    unknown_meshcore_incoming.channel = ::chat::ChannelId::PRIMARY;
    unknown_meshcore_incoming.from = 0;
    unknown_meshcore_incoming.to = 0xFFFFFFFFUL;
    unknown_meshcore_incoming.msg_id = 901;
    unknown_meshcore_incoming.text = "mc sender unknown";
    mesh.incoming.push_back(unknown_meshcore_incoming);
    service.processIncoming();

    const ui::chat::ConversationId meshcore_broadcast = meshCoreBroadcastConversation();
    request.selected = meshcore_broadcast;
    assert(source.buildChatWorkspaceSnapshot(request, snapshot));
    assert(snapshot.header.valid);
    assert(snapshot.message_count == 1);
    assert(snapshot.messages[0].conversation == meshcore_broadcast);
    assert(!snapshot.messages[0].outgoing);
    assert(snapshot.messages[0].sender_node_id == 0);
    assert(std::strcmp(snapshot.messages[0].sender_label.c_str(), "Unknown") == 0);

    const ui::chat::ConversationId team = teamConversation();
    assert(!sink.selectConversation(team).ok);
    assert(!sink.markRead(team).ok);
    send.conversation = team;
    const auto team_send = sink.sendMessage(send);
    assert(!team_send.ok);
    assert(team_send.failure == ui::UiActionFailure::Unsupported);

    request.selected = team;
    assert(source.buildChatWorkspaceSnapshot(request, snapshot));
    assert(!snapshot.can_send);
    assert(!snapshot.composer_enabled);

    send.conversation = broadcast;
    service.setActiveProtocol(::chat::MeshProtocol::Meshtastic);
    mesh.send_ok = true;
    const auto broadcast_send = sink.sendMessage(send);
    assert(broadcast_send.ok);

    const ui::chat::ConversationId system = systemConversation();
    send.conversation = system;
    const auto system_send = sink.sendMessage(send);
    assert(!system_send.ok);
    assert(system_send.failure == ui::UiActionFailure::Unsupported);

    sys::set_epoch_seconds_provider(nullptr);
    sys::set_millis_provider(nullptr);
    return 0;
}
