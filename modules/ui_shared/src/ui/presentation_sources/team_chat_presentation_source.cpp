#include "ui/presentation_sources/team_chat_presentation_source.h"

#include "platform/ui/team_ui_chat_log_store.h"
#include "platform/ui/team_ui_snapshot_store.h"
#include "platform/ui/team_ui_store_runtime.h"
#include "ui/team_presentation/team_member_label.h"
#include "ui/team_presentation/team_rich_payload_projector.h"
#include "ui_presentation/common/fixed_text.h"

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

namespace ui::presentation_sources
{
namespace
{

constexpr std::size_t kMaxMessageRows = ::ui::chat::ChatWorkspaceSnapshot::kMaxMessages;
constexpr double kCoordinateScale = 10000000.0;

uint32_t foldTeamId(const ::team::TeamId& team_id)
{
    uint32_t hash = 2166136261UL;
    for (const uint8_t byte : team_id)
    {
        hash ^= byte;
        hash *= 16777619UL;
    }
    return hash == 0 ? 1U : hash;
}

ui::chat::ConversationId teamConversationId(const ::team::ui::TeamUiSnapshot& snap)
{
    ui::chat::ConversationId id;
    id.kind = ui::chat::ConversationKind::Team;
    id.protocol = ui::chat::ChatProtocolKind::TrailMate;
    id.primary = snap.has_team_id ? foldTeamId(snap.team_id) : 1U;
    id.secondary = 0;
    return id;
}

std::string teamTitle(const ::team::ui::TeamUiSnapshot& snap)
{
    return snap.team_name.empty() ? "Team" : snap.team_name;
}

void copyTimeLabel(ui::FixedText<24>& out, uint32_t timestamp)
{
    if (timestamp == 0)
    {
        out.clear();
        return;
    }

    char buffer[24]{};
    std::snprintf(buffer, sizeof(buffer), "%lu",
                  static_cast<unsigned long>(timestamp));
    ui::copyText(out, buffer);
}

void copySenderLabel(ui::FixedText<32>& out,
                     const ::team::ui::TeamUiSnapshot& snap,
                     const ::team::ui::TeamChatLogEntry& entry)
{
    if (!entry.incoming)
    {
        ui::copyText(out, "Me");
        return;
    }

    for (const auto& member : snap.members)
    {
        if (member.node_id == entry.peer_id && !member.name.empty())
        {
            ui::copyText(out, member.name.c_str());
            return;
        }
    }

    char buffer[16]{};
    std::snprintf(buffer,
                  sizeof(buffer),
                  "%04lX",
                  static_cast<unsigned long>(entry.peer_id & 0xFFFFU));
    ui::copyText(out, buffer);
}

bool hasCoordinate(const ::team::ui::TeamPosSample& sample)
{
    return sample.lat_e7 != 0 || sample.lon_e7 != 0;
}

bool validCoordinate(double lat, double lon)
{
    return std::isfinite(lat) && std::isfinite(lon) && lat >= -90.0 &&
           lat <= 90.0 && lon >= -180.0 && lon <= 180.0;
}

void appendTeamLocationParticipants(ui::chat::ChatWorkspaceSnapshot& out,
                                    const ::team::ui::TeamUiSnapshot& snap)
{
    if (!snap.in_team || !snap.has_team_id)
    {
        return;
    }

    std::vector<::team::ui::TeamPosSample> samples;
    if (!::team::ui::team_ui_posring_load_latest(snap.team_id, samples))
    {
        return;
    }

    for (const auto& sample : samples)
    {
        if (!hasCoordinate(sample))
        {
            continue;
        }

        const double lat = static_cast<double>(sample.lat_e7) / kCoordinateScale;
        const double lon = static_cast<double>(sample.lon_e7) / kCoordinateScale;
        if (!validCoordinate(lat, lon))
        {
            continue;
        }

        if (out.location_participant_count >=
            ui::chat::ChatWorkspaceSnapshot::kMaxLocationParticipants)
        {
            out.location_participants_truncated = true;
            break;
        }

        auto& participant =
            out.location_participants[out.location_participant_count++];
        participant.node_id = sample.member_id;
        participant.lat = lat;
        participant.lon = lon;
        participant.timestamp = sample.ts;
        participant.valid = true;
        participant.self = false;
        const std::string label =
            ::ui::team_presentation::shortTeamMemberLabel(sample.member_id);
        ui::copyText(participant.label, label.c_str());
    }
}

uint64_t messageLocalId(const ::team::ui::TeamChatLogEntry& entry,
                        std::size_t index)
{
    uint64_t id = 1469598103934665603ULL;
    id ^= entry.ts;
    id *= 1099511628211ULL;
    id ^= entry.peer_id;
    id *= 1099511628211ULL;
    id ^= static_cast<uint64_t>(index + 1U);
    id *= 1099511628211ULL;
    return id == 0 ? static_cast<uint64_t>(index + 1U) : id;
}

ui::chat::TeamMessageRichPayloadKind toMessageKind(
    ::ui::team_presentation::TeamRichPayloadKind kind)
{
    switch (kind)
    {
    case ::ui::team_presentation::TeamRichPayloadKind::None:
        return ui::chat::TeamMessageRichPayloadKind::None;
    case ::ui::team_presentation::TeamRichPayloadKind::Text:
        return ui::chat::TeamMessageRichPayloadKind::Text;
    case ::ui::team_presentation::TeamRichPayloadKind::Location:
        return ui::chat::TeamMessageRichPayloadKind::Location;
    case ::ui::team_presentation::TeamRichPayloadKind::Command:
        return ui::chat::TeamMessageRichPayloadKind::Command;
    case ::ui::team_presentation::TeamRichPayloadKind::Unsupported:
        return ui::chat::TeamMessageRichPayloadKind::Unsupported;
    }
    return ui::chat::TeamMessageRichPayloadKind::Unsupported;
}

ui::chat::TeamMessageCommandKind toMessageCommandKind(
    ::ui::team_presentation::TeamCommandDisplayKind kind)
{
    switch (kind)
    {
    case ::ui::team_presentation::TeamCommandDisplayKind::Unknown:
        return ui::chat::TeamMessageCommandKind::Unknown;
    case ::ui::team_presentation::TeamCommandDisplayKind::MoveTo:
        return ui::chat::TeamMessageCommandKind::MoveTo;
    case ::ui::team_presentation::TeamCommandDisplayKind::RallyPoint:
        return ui::chat::TeamMessageCommandKind::RallyPoint;
    case ::ui::team_presentation::TeamCommandDisplayKind::Hold:
        return ui::chat::TeamMessageCommandKind::Hold;
    case ::ui::team_presentation::TeamCommandDisplayKind::Help:
        return ui::chat::TeamMessageCommandKind::Help;
    case ::ui::team_presentation::TeamCommandDisplayKind::CheckIn:
        return ui::chat::TeamMessageCommandKind::CheckIn;
    case ::ui::team_presentation::TeamCommandDisplayKind::Custom:
        return ui::chat::TeamMessageCommandKind::Custom;
    }
    return ui::chat::TeamMessageCommandKind::Unknown;
}

void copyRichPayload(
    ui::chat::TeamMessageRichPayload& out,
    const ::ui::team_presentation::TeamRichPayloadDisplay& display)
{
    out = ui::chat::TeamMessageRichPayload{};
    out.kind = toMessageKind(display.kind);
    ui::copyText(out.title, display.title.c_str());
    ui::copyText(out.summary, display.summary.c_str());
    ui::copyText(out.badge, display.badge.c_str());
    out.location.lat = display.location.lat;
    out.location.lon = display.location.lon;
    out.location.has_altitude = display.location.has_altitude;
    out.location.altitude_m = display.location.altitude_m;
    out.location.marker_icon = display.location.marker_icon;
    out.command.kind = toMessageCommandKind(display.command.kind);
    out.command.lat = display.command.lat;
    out.command.lon = display.command.lon;
    out.command.radius_m = display.command.radius_m;
    out.command.priority = display.command.priority;
}

} // namespace

TeamChatPresentationSource::TeamChatPresentationSource(
    ::team::ui::ITeamUiSnapshotStore& snapshot_store,
    ::team::ui::ITeamUiChatLogStore& chat_log_store)
    : snapshot_store_(snapshot_store),
      chat_log_store_(chat_log_store)
{
}

bool TeamChatPresentationSource::buildChatWorkspaceSnapshot(
    const ui::chat::ChatWorkspaceRequest& request,
    ui::chat::ChatWorkspaceSnapshot& out) const
{
    (void)request.conversation_offset;
    (void)request.message_offset;

    ui::chat::resetChatWorkspaceSnapshot(out);
    out.header.valid = true;
    out.header.version = 1;
    ui::copyText(out.workspace_title, "Team");
    ui::copyText(out.composer_placeholder, "Team message");

    ::team::ui::TeamUiSnapshot snap;
    if (!snapshot_store_.load(snap) || !snap.has_team_id)
    {
        return true;
    }

    const ui::chat::ConversationId team_id = teamConversationId(snap);
    out.selected_conversation = request.selected;

    out.conversation_count = 1;
    ui::chat::ConversationRow& conversation = out.conversations[0];
    conversation.id = team_id;
    conversation.kind = ui::chat::ConversationKind::Team;
    conversation.protocol = ui::chat::ChatProtocolKind::TrailMate;
    conversation.unread_count = static_cast<uint16_t>(
        snap.team_chat_unread > 0xFFFFU ? 0xFFFFU : snap.team_chat_unread);
    conversation.selected = request.selected == team_id;
    ui::copyText(conversation.title, teamTitle(snap).c_str());
    ui::copyText(conversation.subtitle, "No messages");

    std::vector<::team::ui::TeamChatLogEntry> entries;
    if (chat_log_store_.loadRecent(snap.team_id,
                                   kMaxMessageRows,
                                   entries) &&
        !entries.empty())
    {
        ::ui::team_presentation::TeamRichPayloadProjector projector;
        ::ui::team_presentation::TeamRichPayloadDisplay display;
        (void)projector.project(entries.back(), display);
        ui::copyText(conversation.subtitle,
                     display.summary.c_str());
        conversation.last_timestamp = entries.back().ts;
        conversation.last_delivery = entries.back().incoming
                                         ? ui::chat::MessageDeliveryState::Received
                                         : ui::chat::MessageDeliveryState::Sent;
    }

    if (request.selected != team_id)
    {
        return true;
    }

    out.can_send = snap.has_team_psk;
    out.composer_enabled = out.can_send;
    appendTeamLocationParticipants(out, snap);
    out.message_count =
        entries.size() < kMaxMessageRows ? entries.size() : kMaxMessageRows;

    ::ui::team_presentation::TeamRichPayloadProjector projector;
    for (std::size_t i = 0; i < out.message_count; ++i)
    {
        const ::team::ui::TeamChatLogEntry& entry = entries[i];
        ::ui::team_presentation::TeamRichPayloadDisplay display;
        (void)projector.project(entry, display);
        ui::chat::MessageRow& row = out.messages[i];
        row.conversation = team_id;
        row.outgoing = !entry.incoming;
        row.sender_node_id = entry.incoming ? entry.peer_id : 0;
        row.delivery = entry.incoming ? ui::chat::MessageDeliveryState::Received
                                      : ui::chat::MessageDeliveryState::Sent;
        row.failure = ui::chat::MessageFailureKind::None;
        row.ref.origin = entry.incoming ? ui::chat::MessageOrigin::RemoteStored
                                        : ui::chat::MessageOrigin::LocalStored;
        row.ref.local_id = messageLocalId(entry, i);
        ui::copyText(row.text, display.summary.c_str());
        row.has_team_rich_payload = true;
        copyRichPayload(row.team_rich_payload, display);
        copyTimeLabel(row.time_label, entry.ts);
        copySenderLabel(row.sender_label, snap, entry);
    }

    return true;
}

} // namespace ui::presentation_sources
