#include "chat_presentation_adapters/chat_conversation_mapper.h"

namespace chat_presentation_adapters
{

static_assert(ui::chat::kReticulumDestinationHashSize ==
              chat::kReticulumPeerHashSize);

bool hasReticulumConversationKey(const chat::ConversationId& id)
{
    return id.protocol == chat::MeshProtocol::Reticulum &&
           chat::hasReticulumDestinationIdentity(id.reticulum_identity);
}

void copyReticulumDestinationHash(
    ui::chat::ConversationId& out,
    const chat::ReticulumPeerIdentity& identity)
{
    out.has_reticulum_destination_hash =
        chat::copyReticulumDestinationHash(out.reticulum_destination_hash,
                                           identity);
}

void copyReticulumDestinationHash(
    chat::ConversationId& out,
    const ui::chat::ConversationId& id)
{
    if (out.protocol != chat::MeshProtocol::Reticulum ||
        !id.hasReticulumDestinationHash())
    {
        return;
    }

    out.reticulum_identity =
        chat::makeReticulumDestinationIdentity(id.reticulum_destination_hash);
}

ui::chat::ChatProtocolKind mapProtocol(chat::MeshProtocol protocol)
{
    switch (protocol)
    {
    case chat::MeshProtocol::Meshtastic:
        return ui::chat::ChatProtocolKind::Meshtastic;
    case chat::MeshProtocol::MeshCore:
        return ui::chat::ChatProtocolKind::MeshCore;
    case chat::MeshProtocol::RNode:
    case chat::MeshProtocol::Reticulum:
        return ui::chat::ChatProtocolKind::Reticulum;
    }
    return ui::chat::ChatProtocolKind::None;
}

bool tryMapProtocol(ui::chat::ChatProtocolKind protocol,
                    chat::MeshProtocol& out)
{
    switch (protocol)
    {
    case ui::chat::ChatProtocolKind::Meshtastic:
        out = chat::MeshProtocol::Meshtastic;
        return true;
    case ui::chat::ChatProtocolKind::MeshCore:
        out = chat::MeshProtocol::MeshCore;
        return true;
    case ui::chat::ChatProtocolKind::RNode:
    case ui::chat::ChatProtocolKind::Reticulum:
        out = chat::MeshProtocol::Reticulum;
        return true;
    case ui::chat::ChatProtocolKind::None:
    case ui::chat::ChatProtocolKind::TrailMate:
    case ui::chat::ChatProtocolKind::Mixed:
        break;
    }
    return false;
}

ui::chat::ConversationId toUiConversationId(const chat::ConversationId& id)
{
    ui::chat::ConversationId out;
    out.protocol = mapProtocol(id.protocol);
    const uint32_t channel = static_cast<uint32_t>(id.channel);
    const bool has_reticulum_key = hasReticulumConversationKey(id);
    if (has_reticulum_key)
    {
        copyReticulumDestinationHash(out, id.reticulum_identity);
    }

    if (id.peer != 0 || has_reticulum_key)
    {
        out.kind = ui::chat::ConversationKind::DirectPeer;
        out.primary = id.peer;
        out.secondary = channel;
        return out;
    }

    out.kind = ui::chat::ConversationKind::Channel;
    out.primary = channel;
    out.secondary = 0;
    return out;
}

bool toCoreConversationId(const ui::chat::ConversationId& id,
                          chat::ConversationId& out)
{
    if (!id.isValid())
    {
        return false;
    }

    chat::MeshProtocol protocol;
    if (!tryMapProtocol(id.protocol, protocol))
    {
        return false;
    }

    if (id.kind == ui::chat::ConversationKind::DirectPeer)
    {
        out = chat::ConversationId(static_cast<chat::ChannelId>(id.secondary),
                                   id.primary,
                                   protocol);
        copyReticulumDestinationHash(out, id);
        return id.primary != 0 ||
               (protocol == chat::MeshProtocol::Reticulum &&
                id.hasReticulumDestinationHash());
    }

    if (id.kind == ui::chat::ConversationKind::Channel)
    {
        out = chat::ConversationId(static_cast<chat::ChannelId>(id.primary),
                                   0,
                                   protocol);
        copyReticulumDestinationHash(out, id);
        return true;
    }

    return false;
}

} // namespace chat_presentation_adapters
