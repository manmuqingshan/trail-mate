#include "ui/presentation_sources/chat_presentation_source.h"

#include "chat/delivery/chat_delivery_message_projection.h"
#include "chat/time_utils.h"
#include "chat_presentation_adapters/chat_conversation_mapper.h"
#include "chat_presentation_adapters/chat_message_mapper.h"
#include "ui_presentation/common/fixed_text.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <string>

#ifndef CHAT_UI_SEND_TRACE_ENABLE
#define CHAT_UI_SEND_TRACE_ENABLE 0
#endif

#if CHAT_UI_SEND_TRACE_ENABLE
#define CHAT_SNAPSHOT_TRACE(...) std::printf(__VA_ARGS__)
#else
#define CHAT_SNAPSHOT_TRACE(...)
#endif

namespace ui::presentation_sources
{
namespace
{

constexpr std::size_t kMaxConversationRows = 16;
constexpr std::size_t kMaxMessageRows = ::ui::chat::ChatWorkspaceSnapshot::kMaxMessages;
constexpr double kCoordinateScale = 10000000.0;

template <std::size_t N>
void copyString(ui::FixedText<N>& out, const std::string& text)
{
    ui::copyText(out, text.c_str());
}

std::string formatNodeLabel(::chat::NodeId node_id)
{
    if (node_id == 0)
    {
        return std::string("Unknown");
    }

    char buffer[16]{};
    std::snprintf(buffer, sizeof(buffer), "%04lX",
                  static_cast<unsigned long>(node_id & 0xFFFFU));
    return std::string(buffer);
}

void copyNodeLabel(ui::FixedText<32>& out, ::chat::NodeId node_id)
{
    const std::string label = formatNodeLabel(node_id);
    ui::copyText(out, label.c_str());
}

std::string contactDisplayName(const ::chat::contacts::ContactService* contacts,
                               ::chat::NodeId node_id)
{
    if (node_id == 0)
    {
        return std::string("Unknown");
    }

    if (contacts != nullptr)
    {
        const auto* node = contacts->getPeerByNodeId(node_id);
        if (node != nullptr)
        {
            if (!node->display_name.empty())
            {
                return node->display_name;
            }
            if (node->long_name[0] != '\0')
            {
                return std::string(node->long_name);
            }
            if (node->short_name[0] != '\0')
            {
                return std::string(node->short_name);
            }
        }

        const std::string contact = contacts->getContactName(node_id);
        if (!contact.empty())
        {
            return contact;
        }
    }

    return formatNodeLabel(node_id);
}

std::string conversationTitle(const ::chat::ConversationMeta& meta,
                              const ::chat::contacts::ContactService* contacts)
{
    if (meta.id.peer != 0)
    {
        return contactDisplayName(contacts, meta.id.peer);
    }

    ::chat::ReticulumPeerIdentity identity = meta.reticulum_identity;
    if (!::chat::hasReticulumDestinationIdentity(identity))
    {
        identity = meta.id.reticulum_identity;
    }
    if (contacts != nullptr &&
        ::chat::hasReticulumDestinationIdentity(identity))
    {
        ::chat::NodeId node_id = 0;
        if (contacts->findNodeIdByReticulumDestinationHash(
                identity.destination_hash,
                &node_id))
        {
            return contactDisplayName(contacts, node_id);
        }
    }

    if (!meta.name.empty())
    {
        return meta.name;
    }
    return formatNodeLabel(meta.id.peer);
}

bool isValidCoordinate(double lat, double lon)
{
    return std::isfinite(lat) && std::isfinite(lon) &&
           lat >= -90.0 && lat <= 90.0 &&
           lon >= -180.0 && lon <= 180.0;
}

void copyParticipantLabel(ui::FixedText<32>& out,
                          const ::chat::contacts::ContactService* contacts,
                          ::chat::NodeId node_id,
                          bool self)
{
    if (self)
    {
        ui::copyText(out, "Me");
        return;
    }
    if (contacts != nullptr)
    {
        const std::string contact = contactDisplayName(contacts, node_id);
        ui::copyText(out, contact.c_str());
        return;
    }
    copyNodeLabel(out, node_id);
}

bool nodePositionToLocation(const ::chat::contacts::ContactService* contacts,
                            ::chat::NodeId node_id,
                            double& out_lat,
                            double& out_lon,
                            uint32_t& out_timestamp)
{
    if (contacts == nullptr || node_id == 0)
    {
        return false;
    }

    const auto* node = contacts->getPeerByNodeId(node_id);
    if (node == nullptr || !node->position.valid)
    {
        return false;
    }

    const double lat = static_cast<double>(node->position.latitude_i) / kCoordinateScale;
    const double lon = static_cast<double>(node->position.longitude_i) / kCoordinateScale;
    if (!isValidCoordinate(lat, lon))
    {
        return false;
    }

    out_lat = lat;
    out_lon = lon;
    out_timestamp = node->position.timestamp;
    return true;
}

bool messageGeoToLocation(const ::chat::ChatMessage& message,
                          double& out_lat,
                          double& out_lon,
                          uint32_t& out_timestamp)
{
    if (!message.has_geo)
    {
        return false;
    }

    const double lat = static_cast<double>(message.geo_lat_e7) / kCoordinateScale;
    const double lon = static_cast<double>(message.geo_lon_e7) / kCoordinateScale;
    if (!isValidCoordinate(lat, lon))
    {
        return false;
    }

    out_lat = lat;
    out_lon = lon;
    out_timestamp = message.timestamp;
    return true;
}

::ui::chat::ConversationLocationParticipant* findLocationParticipant(
    ::ui::chat::ChatWorkspaceSnapshot& snapshot,
    ::chat::NodeId node_id)
{
    if (node_id == 0)
    {
        return nullptr;
    }

    for (std::size_t i = 0; i < snapshot.location_participant_count; ++i)
    {
        auto& participant = snapshot.location_participants[i];
        if (participant.node_id == node_id)
        {
            return &participant;
        }
    }
    return nullptr;
}

void appendLocationParticipant(::ui::chat::ChatWorkspaceSnapshot& snapshot,
                               const ::chat::contacts::ContactService* contacts,
                               ::chat::NodeId node_id,
                               double lat,
                               double lon,
                               uint32_t timestamp,
                               bool self)
{
    if (node_id == 0 || !isValidCoordinate(lat, lon))
    {
        return;
    }

    if (auto* existing = findLocationParticipant(snapshot, node_id))
    {
        existing->lat = lat;
        existing->lon = lon;
        existing->timestamp = timestamp;
        existing->valid = true;
        existing->self = existing->self || self;
        if (existing->label.empty())
        {
            copyParticipantLabel(existing->label, contacts, node_id, existing->self);
        }
        return;
    }

    if (snapshot.location_participant_count >=
        ::ui::chat::ChatWorkspaceSnapshot::kMaxLocationParticipants)
    {
        snapshot.location_participants_truncated = true;
        return;
    }

    auto& participant =
        snapshot.location_participants[snapshot.location_participant_count++];
    participant.node_id = node_id;
    participant.lat = lat;
    participant.lon = lon;
    participant.timestamp = timestamp;
    participant.valid = true;
    participant.self = self;
    copyParticipantLabel(participant.label, contacts, node_id, self);
}

void appendNodePositionParticipant(
    ::ui::chat::ChatWorkspaceSnapshot& snapshot,
    const ::chat::contacts::ContactService* contacts,
    ::chat::NodeId node_id,
    bool self)
{
    double lat = 0.0;
    double lon = 0.0;
    uint32_t timestamp = 0;
    if (!nodePositionToLocation(contacts, node_id, lat, lon, timestamp))
    {
        return;
    }
    appendLocationParticipant(snapshot, contacts, node_id, lat, lon, timestamp, self);
}

void appendMessageLocationParticipant(
    ::ui::chat::ChatWorkspaceSnapshot& snapshot,
    const ::chat::contacts::ContactService* contacts,
    const ::chat::ChatMessage& message,
    ::chat::NodeId self_node)
{
    const bool outgoing = message.status != ::chat::MessageStatus::Incoming;
    const ::chat::NodeId node_id = outgoing ? self_node : message.from;
    if (node_id == 0 || findLocationParticipant(snapshot, node_id) != nullptr)
    {
        return;
    }

    double lat = 0.0;
    double lon = 0.0;
    uint32_t timestamp = 0;
    if (nodePositionToLocation(contacts, node_id, lat, lon, timestamp) ||
        messageGeoToLocation(message, lat, lon, timestamp))
    {
        appendLocationParticipant(
            snapshot, contacts, node_id, lat, lon, timestamp, outgoing);
    }
}

void copyTimeLabel(ui::FixedText<24>& out, uint32_t timestamp)
{
    if (!::chat::is_valid_epoch(timestamp))
    {
        out.clear();
        return;
    }

    char buffer[24]{};
    std::snprintf(buffer, sizeof(buffer), "%lu",
                  static_cast<unsigned long>(timestamp));
    ui::copyText(out, buffer);
}

ui::chat::MessageDeliveryState mapDeliveryState(
    ::chat::delivery::DeliveryState state)
{
    switch (state)
    {
    case ::chat::delivery::DeliveryState::Queued:
        return ui::chat::MessageDeliveryState::Queued;
    case ::chat::delivery::DeliveryState::Sending:
        return ui::chat::MessageDeliveryState::Sending;
    case ::chat::delivery::DeliveryState::Sent:
        return ui::chat::MessageDeliveryState::Sent;
    case ::chat::delivery::DeliveryState::Delivered:
        return ui::chat::MessageDeliveryState::Delivered;
    case ::chat::delivery::DeliveryState::Failed:
        return ui::chat::MessageDeliveryState::Failed;
    case ::chat::delivery::DeliveryState::Received:
        return ui::chat::MessageDeliveryState::Received;
    case ::chat::delivery::DeliveryState::Unknown:
        return ui::chat::MessageDeliveryState::Unknown;
    }
    return ui::chat::MessageDeliveryState::Unknown;
}

ui::chat::MessageFailureKind mapDeliveryFailure(
    ::chat::delivery::DeliveryFailureKind failure)
{
    switch (failure)
    {
    case ::chat::delivery::DeliveryFailureKind::None:
        return ui::chat::MessageFailureKind::None;
    case ::chat::delivery::DeliveryFailureKind::PeerKeyMissing:
        return ui::chat::MessageFailureKind::PeerKeyMissing;
    case ::chat::delivery::DeliveryFailureKind::ChannelKeyMissing:
        return ui::chat::MessageFailureKind::ChannelKeyMissing;
    case ::chat::delivery::DeliveryFailureKind::LocalIdentityMissing:
        return ui::chat::MessageFailureKind::LocalIdentityMissing;
    case ::chat::delivery::DeliveryFailureKind::RadioSendFailed:
        return ui::chat::MessageFailureKind::RadioSendFailed;
    case ::chat::delivery::DeliveryFailureKind::AckTimeout:
        return ui::chat::MessageFailureKind::AckTimeout;
    case ::chat::delivery::DeliveryFailureKind::UnsupportedProtocol:
        return ui::chat::MessageFailureKind::UnsupportedProtocol;
    case ::chat::delivery::DeliveryFailureKind::Rejected:
        return ui::chat::MessageFailureKind::Rejected;
    case ::chat::delivery::DeliveryFailureKind::Unknown:
        return ui::chat::MessageFailureKind::Unknown;
    }
    return ui::chat::MessageFailureKind::Unknown;
}

} // namespace

ChatPresentationSource::ChatPresentationSource(
    ::chat::ChatService& chat_service,
    ::chat::contacts::ContactService* contact_service,
    const ::chat::delivery::ChatDeliveryReadModel* delivery_read_model,
    const ::chat::IMeshAdapter* mesh_adapter)
    : chat_service_(chat_service),
      contact_service_(contact_service),
      delivery_read_model_(delivery_read_model),
      mesh_adapter_(mesh_adapter)
{
}

bool ChatPresentationSource::buildChatWorkspaceSnapshot(
    const ui::chat::ChatWorkspaceRequest& request,
    ui::chat::ChatWorkspaceSnapshot& out) const
{
    const auto started = std::chrono::steady_clock::now();
    CHAT_SNAPSHOT_TRACE("[ChatUiTrace] stage=snapshot_source begin kind=%u protocol=%u primary=%lu secondary=%lu conv_offset=%u msg_offset=%u\n",
                        static_cast<unsigned>(request.selected.kind),
                        static_cast<unsigned>(request.selected.protocol),
                        static_cast<unsigned long>(request.selected.primary),
                        static_cast<unsigned long>(request.selected.secondary),
                        static_cast<unsigned>(request.conversation_offset),
                        static_cast<unsigned>(request.message_offset));
    ui::chat::resetChatWorkspaceSnapshot(out);
    if (!chat_service_.isDataReady())
    {
        CHAT_SNAPSHOT_TRACE(
            "[ChatUiTrace] stage=snapshot_source reject reason=data_not_ready elapsed_ms=%lld\n",
            static_cast<long long>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - started)
                    .count()));
        return false;
    }
    out.header.valid = true;
    out.header.version = 1;
    ui::copyText(out.workspace_title, "Chat");
    ui::copyText(out.composer_placeholder, "Message");

    std::size_t total = 0;
    CHAT_SNAPSHOT_TRACE("[ChatUiTrace] stage=snapshot_source conversations begin elapsed_ms=%lld\n",
                        static_cast<long long>(
                            std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - started)
                                .count()));
    const auto conversations = chat_service_.getConversations(
        request.conversation_offset,
        kMaxConversationRows,
        &total);
    CHAT_SNAPSHOT_TRACE("[ChatUiTrace] stage=snapshot_source conversations done loaded=%u total=%u elapsed_ms=%lld\n",
                        static_cast<unsigned>(conversations.size()),
                        static_cast<unsigned>(total),
                        static_cast<long long>(
                            std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - started)
                                .count()));

    out.conversation_count = conversations.size() < kMaxConversationRows
                                 ? conversations.size()
                                 : kMaxConversationRows;
    for (std::size_t i = 0; i < out.conversation_count; ++i)
    {
        const ::chat::ConversationMeta& meta = conversations[i];
        ui::chat::ConversationRow& row = out.conversations[i];
        row.id = chat_presentation_adapters::toUiConversationId(meta.id);
        row.kind = row.id.kind;
        row.protocol = row.id.protocol;
        row.unread_count = meta.unread < 0 ? 0U : static_cast<uint16_t>(meta.unread);
        row.last_timestamp = meta.last_timestamp;
        row.selected = row.id == request.selected;
        copyString(row.title, conversationTitle(meta, contact_service_));
        copyString(row.subtitle, meta.preview);
    }
    CHAT_SNAPSHOT_TRACE("[ChatUiTrace] stage=snapshot_source conversation_rows done count=%u elapsed_ms=%lld\n",
                        static_cast<unsigned>(out.conversation_count),
                        static_cast<long long>(
                            std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - started)
                                .count()));

    out.selected_conversation = request.selected;

    ::chat::ConversationId core_selected;
    if (chat_presentation_adapters::toCoreConversationId(request.selected,
                                                         core_selected))
    {
        CHAT_SNAPSHOT_TRACE("[ChatUiTrace] stage=snapshot_source selected mapped protocol=%u channel=%u peer=%08lX elapsed_ms=%lld\n",
                            static_cast<unsigned>(core_selected.protocol),
                            static_cast<unsigned>(core_selected.channel),
                            static_cast<unsigned long>(core_selected.peer),
                            static_cast<long long>(
                                std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::steady_clock::now() - started)
                                    .count()));
        std::size_t total_messages = 0;
        CHAT_SNAPSHOT_TRACE("[ChatUiTrace] stage=snapshot_source messages begin offset=%u limit=%u elapsed_ms=%lld\n",
                            static_cast<unsigned>(request.message_offset),
                            static_cast<unsigned>(kMaxMessageRows),
                            static_cast<long long>(
                                std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::steady_clock::now() - started)
                                    .count()));
        const auto messages =
            chat_service_.getMessagePageFromLatest(core_selected,
                                                   request.message_offset,
                                                   kMaxMessageRows,
                                                   &total_messages);
        CHAT_SNAPSHOT_TRACE("[ChatUiTrace] stage=snapshot_source messages done loaded=%u total=%u elapsed_ms=%lld\n",
                            static_cast<unsigned>(messages.size()),
                            static_cast<unsigned>(total_messages),
                            static_cast<long long>(
                                std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::steady_clock::now() - started)
                                    .count()));
        const ::chat::NodeId self_node =
            mesh_adapter_ != nullptr ? mesh_adapter_->getNodeId() : 0;
        if (self_node != 0)
        {
            appendNodePositionParticipant(
                out, contact_service_, self_node, true);
        }
        if (core_selected.peer != 0)
        {
            appendNodePositionParticipant(
                out, contact_service_, core_selected.peer, false);
        }
        out.message_offset = request.message_offset;
        out.message_total_count = static_cast<uint16_t>(
            std::min<std::size_t>(total_messages, 0xFFFFU));
        out.has_newer_messages = request.message_offset > 0;
        out.has_older_messages =
            request.message_offset + messages.size() < total_messages;
        out.message_count = messages.size() < kMaxMessageRows
                                ? messages.size()
                                : kMaxMessageRows;
        for (std::size_t i = 0; i < out.message_count; ++i)
        {
            const ::chat::ChatMessage& message = messages[i];
            ui::chat::MessageRow& row = out.messages[i];
            const ::chat::ConversationId core_conversation(
                message.channel,
                message.peer,
                message.protocol);
            row.conversation =
                chat_presentation_adapters::toUiConversationId(core_conversation);
            row.ref = chat_presentation_adapters::toUiMessageRef(message);
            row.delivery =
                chat_presentation_adapters::mapMessageStatus(message.status);
            row.failure =
                chat_presentation_adapters::mapMessageFailure(message.status);
            row.ingress_transport =
                message.status == ::chat::MessageStatus::Incoming
                    ? chat_presentation_adapters::mapMessageIngressTransport(
                          message.rx_origin)
                    : ui::chat::MessageIngressTransport::Unknown;
            if (delivery_read_model_ != nullptr)
            {
                ::chat::delivery::ChatDeliveryRecord delivery{};
                if (delivery_read_model_->find(
                        ::chat::delivery::toDeliveryRef(message),
                        delivery))
                {
                    row.delivery = mapDeliveryState(delivery.state);
                    row.failure = mapDeliveryFailure(delivery.failure);
                }
            }
            row.outgoing = message.status != ::chat::MessageStatus::Incoming;
            row.source_unverified = !row.outgoing && message.source_unverified;
            row.sender_node_id = row.outgoing ? 0 : message.from;
            copyString(row.text, message.text);
            copyTimeLabel(row.time_label, message.timestamp);

            if (row.outgoing)
            {
                ui::copyText(row.sender_label, "Me");
            }
            else if (contact_service_ != nullptr)
            {
                const std::string contact =
                    contactDisplayName(contact_service_, message.from);
                ui::copyText(row.sender_label, contact.c_str());
            }
            else
            {
                copyNodeLabel(row.sender_label, message.from);
            }
            appendMessageLocationParticipant(
                out, contact_service_, message, self_node);
        }
        CHAT_SNAPSHOT_TRACE("[ChatUiTrace] stage=snapshot_source message_rows done count=%u participants=%u elapsed_ms=%lld\n",
                            static_cast<unsigned>(out.message_count),
                            static_cast<unsigned>(out.location_participant_count),
                            static_cast<long long>(
                                std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::steady_clock::now() - started)
                                    .count()));
    }
    else
    {
        CHAT_SNAPSHOT_TRACE("[ChatUiTrace] stage=snapshot_source selected reject reason=conversation_map elapsed_ms=%lld\n",
                            static_cast<long long>(
                                std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::steady_clock::now() - started)
                                    .count()));
    }

    const bool selected_supported =
        request.selected.kind == ui::chat::ConversationKind::DirectPeer ||
        request.selected.kind == ui::chat::ConversationKind::Channel;
    out.can_send = request.selected.isValid() && selected_supported &&
                   chat_presentation_adapters::toCoreConversationId(request.selected,
                                                                    core_selected) &&
                   chat_service_.canSendToConversation(core_selected);
    out.composer_enabled = out.can_send;
    CHAT_SNAPSHOT_TRACE("[ChatUiTrace] stage=snapshot_source end can_send=%u conversations=%u messages=%u total=%u elapsed_ms=%lld\n",
                        out.can_send ? 1U : 0U,
                        static_cast<unsigned>(out.conversation_count),
                        static_cast<unsigned>(out.message_count),
                        static_cast<unsigned>(out.message_total_count),
                        static_cast<long long>(
                            std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - started)
                                .count()));
    return true;
}

} // namespace ui::presentation_sources
