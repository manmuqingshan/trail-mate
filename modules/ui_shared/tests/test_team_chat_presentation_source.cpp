#include "platform/ui/team_ui_chat_log_store.h"
#include "platform/ui/team_ui_snapshot_store.h"
#include "platform/ui/team_ui_store_runtime.h"
#include "team/protocol/team_chat.h"
#include "team/protocol/team_location_marker.h"
#include "ui/presentation_sources/team_chat_presentation_source.h"

#include <cassert>
#include <cstring>
#include <vector>

namespace
{

team::TeamId testTeamId()
{
    team::TeamId id{};
    id[0] = 0xA1;
    id[1] = 0xB2;
    id[2] = 0xC3;
    id[3] = 0xD4;
    return id;
}

team::ui::TeamUiSnapshot makeSnapshot()
{
    team::ui::TeamUiSnapshot snap;
    snap.in_team = true;
    snap.has_team_id = true;
    snap.team_id = testTeamId();
    snap.team_name = "Alpha";
    snap.has_team_psk = true;
    snap.team_chat_unread = 2;

    team::ui::TeamMemberUi member;
    member.node_id = 0x12345678;
    member.name = "Ada";
    snap.members.push_back(member);
    return snap;
}

ui::chat::ChatWorkspaceSnapshot buildSnapshot(
    ui::presentation_sources::TeamChatPresentationSource& source,
    ui::chat::ConversationId selected = {})
{
    ui::chat::ChatWorkspaceRequest request;
    request.selected = selected;

    ui::chat::ChatWorkspaceSnapshot snapshot;
    assert(source.buildChatWorkspaceSnapshot(request, snapshot));
    assert(snapshot.header.valid);
    return snapshot;
}

bool contains(const ui::FixedText<160>& text, const char* needle)
{
    return std::strstr(text.c_str(), needle) != nullptr;
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

} // namespace

int main()
{
    team::ui::ITeamUiSnapshotStore& store = team::ui::team_ui_snapshot_store();
    store.clear();

    const team::ui::TeamUiSnapshot snap = makeSnapshot();
    store.save(snap);
    assert(team::ui::team_ui_posring_append(
        snap.team_id,
        0x12345678,
        312345678,
        1219876543,
        42,
        0,
        100));

    assert(team::ui::team_ui_chat_log_store().appendText(
        snap.team_id, 0x12345678, true, 100, "hello team"));

    team::proto::TeamChatLocation loc;
    loc.lat_e7 = 312345678;
    loc.lon_e7 = 1219876543;
    loc.source = static_cast<uint8_t>(team::proto::TeamLocationMarkerIcon::Rally);
    std::vector<uint8_t> location_payload;
    assert(team::proto::encodeTeamChatLocation(loc, location_payload));
    assert(team::ui::team_ui_chat_log_store().appendStructured(
        snap.team_id,
        0,
        false,
        101,
        team::proto::TeamChatType::Location,
        location_payload));

    team::proto::TeamChatCommand command;
    command.cmd_type = team::proto::TeamCommandType::Hold;
    command.note = "wait";
    std::vector<uint8_t> command_payload;
    assert(team::proto::encodeTeamChatCommand(command, command_payload));
    assert(team::ui::team_ui_chat_log_store().appendStructured(
        snap.team_id,
        0x12345678,
        true,
        102,
        team::proto::TeamChatType::Command,
        command_payload));

    ui::presentation_sources::TeamChatPresentationSource source(
        store,
        team::ui::team_ui_chat_log_store());

    ui::chat::ChatWorkspaceSnapshot overview = buildSnapshot(source);
    assert(overview.conversation_count == 1);
    assert(overview.conversations[0].kind == ui::chat::ConversationKind::Team);
    assert(overview.conversations[0].protocol ==
           ui::chat::ChatProtocolKind::TrailMate);
    assert(std::strcmp(overview.conversations[0].title.c_str(), "Alpha") == 0);
    assert(overview.conversations[0].unread_count == 2);
    assert(overview.conversations[0].last_timestamp == 102);
    assert(overview.message_count == 0);

    const ui::chat::ConversationId team_id = overview.conversations[0].id;
    ui::chat::ChatWorkspaceSnapshot selected = buildSnapshot(source, team_id);
    assert(selected.selected_conversation == team_id);
    assert(selected.conversation_count == 1);
    assert(selected.conversations[0].selected);
    assert(selected.message_count == 3);
    assert(selected.can_send);
    assert(selected.composer_enabled);
    assert(selected.location_participant_count == 1);
    const auto* member_location =
        findLocationParticipant(selected, 0x12345678);
    assert(member_location != nullptr);
    assert(member_location->valid);
    assert(member_location->lat > 31.23 && member_location->lat < 31.24);
    assert(member_location->lon > 121.98 && member_location->lon < 121.99);

    assert(selected.messages[0].conversation.kind ==
           ui::chat::ConversationKind::Team);
    assert(selected.messages[0].delivery ==
           ui::chat::MessageDeliveryState::Received);
    assert(selected.messages[0].ref.origin ==
           ui::chat::MessageOrigin::RemoteStored);
    assert(selected.messages[0].sender_node_id == 0x12345678);
    assert(std::strcmp(selected.messages[0].sender_label.c_str(), "Ada") == 0);
    assert(std::strcmp(selected.messages[0].text.c_str(), "hello team") == 0);
    assert(selected.messages[0].has_team_rich_payload);
    assert(selected.messages[0].team_rich_payload.kind ==
           ui::chat::TeamMessageRichPayloadKind::Text);
    assert(std::strcmp(selected.messages[0].team_rich_payload.summary.c_str(),
                       "hello team") == 0);

    assert(selected.messages[1].outgoing);
    assert(selected.messages[1].sender_node_id == 0);
    assert(selected.messages[1].delivery ==
           ui::chat::MessageDeliveryState::Sent);
    assert(selected.messages[1].ref.origin ==
           ui::chat::MessageOrigin::LocalStored);
    assert(contains(selected.messages[1].text, "Rally"));
    assert(contains(selected.messages[1].text, "31.23457"));
    assert(selected.messages[1].has_team_rich_payload);
    assert(selected.messages[1].team_rich_payload.kind ==
           ui::chat::TeamMessageRichPayloadKind::Location);
    assert(std::strcmp(selected.messages[1].team_rich_payload.badge.c_str(),
                       "Marker") == 0);
    assert(selected.messages[1].team_rich_payload.location.marker_icon ==
           static_cast<uint8_t>(team::proto::TeamLocationMarkerIcon::Rally));

    assert(!selected.messages[2].outgoing);
    assert(contains(selected.messages[2].text, "Command: Hold"));
    assert(contains(selected.messages[2].text, "wait"));
    assert(selected.messages[2].has_team_rich_payload);
    assert(selected.messages[2].team_rich_payload.kind ==
           ui::chat::TeamMessageRichPayloadKind::Command);
    assert(selected.messages[2].team_rich_payload.command.kind ==
           ui::chat::TeamMessageCommandKind::Hold);

    store.clear();
    return 0;
}
