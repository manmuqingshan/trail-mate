#include "ui_presentation/chat/chat_workspace_snapshot.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace
{

void directPeerAndTeamAreDifferent()
{
    ui::chat::ConversationId direct;
    direct.kind = ui::chat::ConversationKind::DirectPeer;
    direct.protocol = ui::chat::ChatProtocolKind::Meshtastic;
    direct.primary = 1234;
    direct.secondary = 0;

    ui::chat::ConversationId team;
    team.kind = ui::chat::ConversationKind::Team;
    team.protocol = ui::chat::ChatProtocolKind::TrailMate;
    team.primary = 1234;
    team.secondary = 0;

    assert(direct.isValid());
    assert(team.isValid());
    assert(direct != team);
}

void reticulumDestinationHashDefinesPresentationIdentity()
{
    ui::chat::ConversationId first;
    first.kind = ui::chat::ConversationKind::DirectPeer;
    first.protocol = ui::chat::ChatProtocolKind::Reticulum;
    first.primary = 0x1234ABCDU;
    first.secondary = 0;
    first.has_reticulum_destination_hash = true;

    ui::chat::ConversationId second = first;
    second.primary = 0x87654321U;

    for (std::size_t index = 0;
         index < ui::chat::kReticulumDestinationHashSize;
         ++index)
    {
        first.reticulum_destination_hash[index] =
            static_cast<std::uint8_t>(0x30U + index);
        second.reticulum_destination_hash[index] =
            static_cast<std::uint8_t>(0x30U + index);
    }

    assert(first == second);

    second.reticulum_destination_hash[0] ^= 0x01U;
    assert(first != second);
}

void workspaceSnapshotUsesPresentationIds()
{
    ui::chat::ChatWorkspaceSnapshot snapshot;
    snapshot.header.valid = true;
    snapshot.conversation_count = 2;
    snapshot.conversations[0].id.kind = ui::chat::ConversationKind::DirectPeer;
    snapshot.conversations[0].id.protocol = ui::chat::ChatProtocolKind::MeshCore;
    snapshot.conversations[0].id.primary = 42;
    snapshot.conversations[0].id.secondary = 1;
    snapshot.conversations[0].kind = snapshot.conversations[0].id.kind;
    snapshot.conversations[0].protocol = snapshot.conversations[0].id.protocol;
    ui::copyText(snapshot.conversations[0].title, "Node 42");

    snapshot.conversations[1].id.kind = ui::chat::ConversationKind::System;
    snapshot.conversations[1].id.protocol = ui::chat::ChatProtocolKind::TrailMate;
    snapshot.conversations[1].id.primary = 7;
    snapshot.conversations[1].kind = snapshot.conversations[1].id.kind;
    snapshot.conversations[1].protocol = snapshot.conversations[1].id.protocol;
    ui::copyText(snapshot.conversations[1].title, "Diagnostics");

    snapshot.selected_conversation = snapshot.conversations[0].id;
    snapshot.message_count = 1;
    snapshot.messages[0].conversation = snapshot.conversations[0].id;
    snapshot.messages[0].ref.origin = ui::chat::MessageOrigin::LocalStored;
    snapshot.messages[0].ref.local_id = 99;
    snapshot.messages[0].delivery = ui::chat::MessageDeliveryState::Sent;
    ui::copyText(snapshot.messages[0].text, "hello");

    assert(snapshot.header.valid);
    assert(snapshot.conversations[0].id.isValid());
    assert(snapshot.conversations[1].id.isValid());
    assert(snapshot.conversations[0].id != snapshot.conversations[1].id);
    assert(snapshot.messages[0].ref.isValid());
    assert(snapshot.selected_conversation == snapshot.conversations[0].id);
    assert(std::strcmp(snapshot.conversations[0].title.c_str(), "Node 42") == 0);
    assert(std::strcmp(snapshot.messages[0].text.c_str(), "hello") == 0);
}

void sendViewUsesConversationToken()
{
    ui::chat::SendMessageView view;
    view.conversation.kind = ui::chat::ConversationKind::Channel;
    view.conversation.protocol = ui::chat::ChatProtocolKind::Meshtastic;
    view.conversation.primary = 0;
    view.text = "broadcast";
    view.text_len = 9;

    assert(view.conversation.isValid());
    assert(view.conversation.kind == ui::chat::ConversationKind::Channel);
    assert(view.text_len == 9);
}

} // namespace

int main()
{
    directPeerAndTeamAreDifferent();
    reticulumDestinationHashDefinesPresentationIdentity();
    workspaceSnapshotUsesPresentationIds();
    sendViewUsesConversationToken();
    return 0;
}
