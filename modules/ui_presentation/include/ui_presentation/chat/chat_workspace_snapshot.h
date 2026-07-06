#pragma once

#include "ui_presentation/chat/chat_identity.h"
#include "ui_presentation/chat/chat_message_ref.h"
#include "ui_presentation/common/fixed_text.h"
#include "ui_presentation/common/snapshot_header.h"

#include <stddef.h>
#include <stdint.h>

namespace ui::chat
{

struct ConversationRow
{
    ConversationId id;

    ui::FixedText<32> title;
    ui::FixedText<64> subtitle;

    uint16_t unread_count = 0;
    uint32_t last_timestamp = 0;
    bool selected = false;
    bool muted = false;

    ConversationKind kind = ConversationKind::None;
    ChatProtocolKind protocol = ChatProtocolKind::None;

    MessageDeliveryState last_delivery = MessageDeliveryState::Unknown;
};

enum class TeamMessageRichPayloadKind : uint8_t
{
    None,
    Text,
    Location,
    Command,
    Unsupported,
};

enum class TeamMessageCommandKind : uint8_t
{
    Unknown,
    MoveTo,
    RallyPoint,
    Hold,
    Help,
    CheckIn,
    Custom,
};

struct TeamMessageLocationPayload
{
    double lat = 0.0;
    double lon = 0.0;
    bool has_altitude = false;
    float altitude_m = 0.0f;
    uint8_t marker_icon = 0;
};

struct TeamMessageCommandPayload
{
    TeamMessageCommandKind kind = TeamMessageCommandKind::Unknown;
    double lat = 0.0;
    double lon = 0.0;
    float radius_m = 0.0f;
    uint8_t priority = 0;
};

struct TeamMessageRichPayload
{
    TeamMessageRichPayloadKind kind = TeamMessageRichPayloadKind::None;

    ui::FixedText<32> title;
    ui::FixedText<96> summary;
    ui::FixedText<32> badge;

    TeamMessageLocationPayload location;
    TeamMessageCommandPayload command;
};

struct MessageRow
{
    MessageRef ref;
    ConversationId conversation;

    bool outgoing = false;
    MessageDeliveryState delivery = MessageDeliveryState::Unknown;
    MessageFailureKind failure = MessageFailureKind::None;

    uint32_t sender_node_id = 0;
    ui::FixedText<160> text;
    ui::FixedText<24> time_label;
    ui::FixedText<32> sender_label;

    bool has_team_rich_payload = false;
    TeamMessageRichPayload team_rich_payload;
};

struct ConversationLocationParticipant
{
    uint32_t node_id = 0;
    ui::FixedText<32> label;
    double lat = 0.0;
    double lon = 0.0;
    uint32_t timestamp = 0;
    bool valid = false;
    bool self = false;
};

struct ChatWorkspaceSnapshot
{
    ui::SnapshotHeader header;

    static constexpr size_t kMaxLocationParticipants = 16;

    ConversationRow conversations[16]{};
    size_t conversation_count = 0;

    MessageRow messages[24]{};
    size_t message_count = 0;

    ConversationLocationParticipant location_participants[kMaxLocationParticipants]{};
    size_t location_participant_count = 0;
    bool location_participants_truncated = false;

    ConversationId selected_conversation;

    bool can_send = false;
    bool composer_enabled = false;

    ui::FixedText<64> composer_placeholder;
    ui::FixedText<64> workspace_title;
};

struct SendMessageView
{
    ConversationId conversation;
    const char* text = nullptr;
    size_t text_len = 0;
};

inline void resetConversationRow(ConversationRow& row)
{
    row.id = ConversationId{};
    row.title.clear();
    row.subtitle.clear();
    row.unread_count = 0;
    row.last_timestamp = 0;
    row.selected = false;
    row.muted = false;
    row.kind = ConversationKind::None;
    row.protocol = ChatProtocolKind::None;
    row.last_delivery = MessageDeliveryState::Unknown;
}

inline void resetMessageRow(MessageRow& row)
{
    row.ref = MessageRef{};
    row.conversation = ConversationId{};
    row.outgoing = false;
    row.delivery = MessageDeliveryState::Unknown;
    row.failure = MessageFailureKind::None;
    row.sender_node_id = 0;
    row.text.clear();
    row.time_label.clear();
    row.sender_label.clear();
    row.has_team_rich_payload = false;
    row.team_rich_payload = TeamMessageRichPayload{};
}

inline void resetConversationLocationParticipant(
    ConversationLocationParticipant& participant)
{
    participant.node_id = 0;
    participant.label.clear();
    participant.lat = 0.0;
    participant.lon = 0.0;
    participant.timestamp = 0;
    participant.valid = false;
    participant.self = false;
}

inline void resetChatWorkspaceSnapshot(ChatWorkspaceSnapshot& out)
{
    out.header = ui::SnapshotHeader{};

    for (size_t i = 0; i < 16; ++i)
    {
        resetConversationRow(out.conversations[i]);
    }
    out.conversation_count = 0;

    for (size_t i = 0; i < 24; ++i)
    {
        resetMessageRow(out.messages[i]);
    }
    out.message_count = 0;

    for (size_t i = 0; i < ChatWorkspaceSnapshot::kMaxLocationParticipants; ++i)
    {
        resetConversationLocationParticipant(out.location_participants[i]);
    }
    out.location_participant_count = 0;
    out.location_participants_truncated = false;

    out.selected_conversation = ConversationId{};
    out.can_send = false;
    out.composer_enabled = false;
    out.composer_placeholder.clear();
    out.workspace_title.clear();
}

} // namespace ui::chat
