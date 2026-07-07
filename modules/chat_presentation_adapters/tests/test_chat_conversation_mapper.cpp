#include "chat_presentation_adapters/chat_conversation_mapper.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace
{

chat::ReticulumPeerIdentity makeReticulumIdentity(uint8_t seed)
{
    uint8_t destination_hash[chat::kReticulumPeerHashSize] = {};
    uint8_t identity_hash[chat::kReticulumPeerHashSize] = {};
    for (std::size_t index = 0; index < chat::kReticulumPeerHashSize; ++index)
    {
        destination_hash[index] = static_cast<uint8_t>(seed + index);
        identity_hash[index] = static_cast<uint8_t>(seed + 0x40U + index);
    }
    return chat::makeReticulumPeerIdentity(destination_hash, identity_hash);
}

void peerConversationMapsToDirectPeer()
{
    chat::ConversationId core(
        chat::ChannelId::PRIMARY,
        1234,
        chat::MeshProtocol::Meshtastic);

    const ui::chat::ConversationId ui =
        chat_presentation_adapters::toUiConversationId(core);

    assert(ui.kind == ui::chat::ConversationKind::DirectPeer);
    assert(ui.protocol == ui::chat::ChatProtocolKind::Meshtastic);
    assert(ui.primary == 1234);
    assert(ui.secondary == 0);
}

void channelConversationMapsToChannel()
{
    chat::ConversationId core(
        chat::ChannelId::SECONDARY,
        0,
        chat::MeshProtocol::MeshCore);

    const ui::chat::ConversationId ui =
        chat_presentation_adapters::toUiConversationId(core);

    assert(ui.kind == ui::chat::ConversationKind::Channel);
    assert(ui.protocol == ui::chat::ChatProtocolKind::MeshCore);
    assert(ui.primary == static_cast<uint32_t>(chat::ChannelId::SECONDARY));
    assert(ui.secondary == 0);
}

void protocolsMapOneToOne()
{
    assert(chat_presentation_adapters::mapProtocol(chat::MeshProtocol::Meshtastic) ==
           ui::chat::ChatProtocolKind::Meshtastic);
    assert(chat_presentation_adapters::mapProtocol(chat::MeshProtocol::MeshCore) ==
           ui::chat::ChatProtocolKind::MeshCore);
    assert(chat_presentation_adapters::mapProtocol(chat::MeshProtocol::RNode) ==
           ui::chat::ChatProtocolKind::Reticulum);
    assert(chat_presentation_adapters::mapProtocol(chat::MeshProtocol::Reticulum) ==
           ui::chat::ChatProtocolKind::Reticulum);

    chat::MeshProtocol core_protocol = chat::MeshProtocol::Reticulum;
    assert(chat_presentation_adapters::tryMapProtocol(
        ui::chat::ChatProtocolKind::Meshtastic,
        core_protocol));
    assert(core_protocol == chat::MeshProtocol::Meshtastic);
    assert(chat_presentation_adapters::tryMapProtocol(
        ui::chat::ChatProtocolKind::MeshCore,
        core_protocol));
    assert(core_protocol == chat::MeshProtocol::MeshCore);
    assert(chat_presentation_adapters::tryMapProtocol(
        ui::chat::ChatProtocolKind::RNode,
        core_protocol));
    assert(core_protocol == chat::MeshProtocol::Reticulum);
    assert(chat_presentation_adapters::tryMapProtocol(
        ui::chat::ChatProtocolKind::Reticulum,
        core_protocol));
    assert(core_protocol == chat::MeshProtocol::Reticulum);
}

void unsupportedPresentationProtocolsDoNotMapToCore()
{
    chat::MeshProtocol core_protocol = chat::MeshProtocol::Meshtastic;
    assert(!chat_presentation_adapters::tryMapProtocol(
        ui::chat::ChatProtocolKind::None,
        core_protocol));
    assert(!chat_presentation_adapters::tryMapProtocol(
        ui::chat::ChatProtocolKind::TrailMate,
        core_protocol));
    assert(!chat_presentation_adapters::tryMapProtocol(
        ui::chat::ChatProtocolKind::Mixed,
        core_protocol));
}

void directPeerMapsBackToCoreConversation()
{
    ui::chat::ConversationId ui;
    ui.kind = ui::chat::ConversationKind::DirectPeer;
    ui.protocol = ui::chat::ChatProtocolKind::Meshtastic;
    ui.primary = 1234;
    ui.secondary = static_cast<uint32_t>(chat::ChannelId::SECONDARY);

    chat::ConversationId core;
    assert(chat_presentation_adapters::toCoreConversationId(ui, core));
    assert(core.protocol == chat::MeshProtocol::Meshtastic);
    assert(core.channel == chat::ChannelId::SECONDARY);
    assert(core.peer == 1234);
}

void reticulumDestinationHashSurvivesRoundTrip()
{
    chat::ConversationId core(
        chat::ChannelId::SECONDARY,
        0x1234ABCDU,
        chat::MeshProtocol::Reticulum);
    core.reticulum_identity = makeReticulumIdentity(0x20U);

    const ui::chat::ConversationId ui =
        chat_presentation_adapters::toUiConversationId(core);

    assert(ui.kind == ui::chat::ConversationKind::DirectPeer);
    assert(ui.protocol == ui::chat::ChatProtocolKind::Reticulum);
    assert(ui.primary == 0x1234ABCDU);
    assert(ui.secondary == static_cast<uint32_t>(chat::ChannelId::SECONDARY));
    assert(ui.hasReticulumDestinationHash());
    assert(std::memcmp(ui.reticulum_destination_hash,
                       core.reticulum_identity.destination_hash,
                       chat::kReticulumPeerHashSize) == 0);

    chat::ConversationId round_trip;
    assert(chat_presentation_adapters::toCoreConversationId(ui, round_trip));
    assert(round_trip == core);
}

void reticulumIdentityFactoryRejectsMissingHashes()
{
    uint8_t hash[chat::kReticulumPeerHashSize] = {};
    uint8_t zero_hash[chat::kReticulumPeerHashSize] = {};
    for (std::size_t index = 0; index < chat::kReticulumPeerHashSize; ++index)
    {
        hash[index] = static_cast<uint8_t>(0x80U + index);
    }

    assert(!chat::makeReticulumPeerIdentity(nullptr, hash).valid);
    assert(!chat::makeReticulumPeerIdentity(hash, nullptr).valid);
    assert(!chat::makeReticulumDestinationIdentity(nullptr).valid);

    const chat::ReticulumPeerIdentity destination_only =
        chat::makeReticulumDestinationIdentity(hash);
    assert(destination_only.valid);
    assert(std::memcmp(destination_only.destination_hash,
                       hash,
                       chat::kReticulumPeerHashSize) == 0);
    assert(std::memcmp(destination_only.identity_hash,
                       zero_hash,
                       chat::kReticulumPeerHashSize) == 0);
}

void reticulumDestinationHashCanRoundTripWithoutProjectedPeer()
{
    chat::ConversationId core(
        chat::ChannelId::PRIMARY,
        0,
        chat::MeshProtocol::Reticulum);
    core.reticulum_identity = makeReticulumIdentity(0x50U);

    const ui::chat::ConversationId ui =
        chat_presentation_adapters::toUiConversationId(core);

    assert(ui.kind == ui::chat::ConversationKind::DirectPeer);
    assert(ui.primary == 0);
    assert(ui.hasReticulumDestinationHash());

    chat::ConversationId round_trip;
    assert(chat_presentation_adapters::toCoreConversationId(ui, round_trip));
    assert(round_trip == core);
    assert(std::memcmp(round_trip.reticulum_identity.identity_hash,
                       core.reticulum_identity.identity_hash,
                       chat::kReticulumPeerHashSize) != 0);
}

void teamDoesNotMapBackToCoreConversation()
{
    ui::chat::ConversationId ui;
    ui.kind = ui::chat::ConversationKind::Team;
    ui.protocol = ui::chat::ChatProtocolKind::TrailMate;
    ui.primary = 1234;

    chat::ConversationId core;
    assert(!chat_presentation_adapters::toCoreConversationId(ui, core));
}

void trailMateDirectPeerDoesNotMapBackToCoreConversation()
{
    ui::chat::ConversationId ui;
    ui.kind = ui::chat::ConversationKind::DirectPeer;
    ui.protocol = ui::chat::ChatProtocolKind::TrailMate;
    ui.primary = 1234;
    ui.secondary = static_cast<uint32_t>(chat::ChannelId::PRIMARY);

    chat::ConversationId core;
    assert(!chat_presentation_adapters::toCoreConversationId(ui, core));
}

} // namespace

int main()
{
    peerConversationMapsToDirectPeer();
    channelConversationMapsToChannel();
    protocolsMapOneToOne();
    unsupportedPresentationProtocolsDoNotMapToCore();
    directPeerMapsBackToCoreConversation();
    reticulumDestinationHashSurvivesRoundTrip();
    reticulumIdentityFactoryRejectsMissingHashes();
    reticulumDestinationHashCanRoundTripWithoutProjectedPeer();
    teamDoesNotMapBackToCoreConversation();
    trailMateDirectPeerDoesNotMapBackToCoreConversation();
    return 0;
}
