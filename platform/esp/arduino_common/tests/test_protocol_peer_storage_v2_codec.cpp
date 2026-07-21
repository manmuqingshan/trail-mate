#include "platform/esp/arduino_common/chat/infra/store/protocol_peer_codec.h"

#include <cassert>
#include <cstring>
#include <vector>

namespace storage = chat::storage::v2;

namespace
{

void fillKey(uint8_t* key, std::size_t len, uint8_t seed)
{
    for (std::size_t index = 0U; index < len; ++index)
    {
        key[index] = static_cast<uint8_t>(seed + index);
    }
}

storage::PeerProjection meshtasticPeer()
{
    storage::PeerProjection projection{};
    chat::MeshPeerRecord& peer = projection.record;
    peer.valid = true;
    peer.identity = chat::makeMeshPeerNodeIdentity(
        chat::MeshProtocol::Meshtastic,
        0x11223344U);
    peer.source = chat::MeshPeerSource::RuntimeRx;
    peer.first_seen_s = 10U;
    peer.last_seen_s = 20U;
    chat::copyMeshPeerText(peer.display_name,
                           sizeof(peer.display_name),
                           "Meshtastic peer");
    chat::copyMeshPeerText(peer.user_alias,
                           sizeof(peer.user_alias),
                           "not-in-peer");
    peer.flags.favorite = true;
    chat::copyMeshPeerText(peer.meshtastic.node.short_name,
                           sizeof(peer.meshtastic.node.short_name),
                           "MT");
    chat::copyMeshPeerText(peer.meshtastic.node.long_name,
                           sizeof(peer.meshtastic.node.long_name),
                           "Meshtastic Node");
    peer.meshtastic.node.channel = 1U;
    peer.meshtastic.node.hops_away = 2U;
    peer.meshtastic.has_next_hop = true;
    peer.meshtastic.next_hop = 7U;
    peer.meshtastic.has_public_key = true;
    peer.meshtastic.key_manually_verified = true;
    fillKey(peer.meshtastic.public_key,
            sizeof(peer.meshtastic.public_key),
            1U);
    peer.observations.has_snr = true;
    peer.observations.snr = 4.5F;
    peer.observations.has_position = true;
    peer.observations.position.valid = true;
    peer.observations.position.latitude_i = 123;
    return projection;
}

void roundTripPeer(chat::MeshProtocol protocol,
                   const storage::PeerProjection& input)
{
    std::vector<uint8_t> slot(storage::peerSlotSize(protocol));
    assert(storage::encodePeerSlot(protocol,
                                   input,
                                   slot.data(),
                                   slot.size()));
    storage::PeerProjection output{};
    assert(storage::decodePeerSlot(protocol,
                                   slot.data(),
                                   slot.size(),
                                   output));
    assert(chat::sameMeshPeerIdentity(input.record.identity,
                                      output.record.identity));
    assert(output.record.last_seen_s == input.record.last_seen_s);
    assert(std::strcmp(output.record.display_name,
                       input.record.display_name) == 0);
    assert(output.record.user_alias[0] == '\0');
    assert(!output.record.flags.favorite);
    slot.back() ^= 0x80U;
    assert(!storage::decodePeerSlot(protocol,
                                    slot.data(),
                                    slot.size(),
                                    output));
}

void unresolvedIdentityRoundTrips()
{
    storage::PeerProjection mc{};
    mc.record.valid = true;
    mc.record.identity = chat::makeMeshPeerNodeIdentity(
        chat::MeshProtocol::MeshCore,
        0xAABBCCDDU);
    mc.record.meshcore.node_id_hint = 0xAABBCCDDU;
    roundTripPeer(chat::MeshProtocol::MeshCore, mc);

    storage::PeerProjection rt{};
    rt.record.valid = true;
    rt.record.identity = chat::makeMeshPeerNodeIdentity(
        chat::MeshProtocol::Reticulum,
        0x01020304U);
    roundTripPeer(chat::MeshProtocol::Reticulum, rt);
}

void stableIdentityAndContactRoundTrips()
{
    uint8_t public_key[chat::kMeshPeerMeshCorePublicKeyLen]{};
    fillKey(public_key, sizeof(public_key), 9U);
    storage::PeerProjection mc{};
    mc.record.valid = true;
    assert(chat::makeMeshPeerPublicKeyIdentity(
        chat::MeshProtocol::MeshCore,
        public_key,
        sizeof(public_key),
        mc.record.identity));
    mc.record.meshcore.has_public_key = true;
    mc.record.meshcore.node_id_hint = 0x55667788U;
    std::memcpy(mc.record.meshcore.public_key,
                public_key,
                sizeof(public_key));
    roundTripPeer(chat::MeshProtocol::MeshCore, mc);

    storage::ContactProjection contact{};
    contact.identity = mc.record.identity;
    contact.node_id_hint = mc.record.meshcore.node_id_hint;
    contact.flags.favorite = true;
    contact.flags.trusted = true;
    chat::copyMeshPeerText(contact.alias,
                           sizeof(contact.alias),
                           "base");
    std::vector<uint8_t> slot(
        storage::contactSlotSize(chat::MeshProtocol::MeshCore));
    assert(storage::encodeContactSlot(chat::MeshProtocol::MeshCore,
                                      contact,
                                      slot.data(),
                                      slot.size()));
    storage::ContactProjection decoded{};
    assert(storage::decodeContactSlot(chat::MeshProtocol::MeshCore,
                                      slot.data(),
                                      slot.size(),
                                      decoded));
    assert(decoded.node_id_hint == contact.node_id_hint);
    assert(decoded.flags.favorite);
    assert(decoded.flags.trusted);
    assert(std::strcmp(decoded.alias, contact.alias) == 0);

    uint8_t destination[chat::kReticulumPeerHashSize]{};
    uint8_t identity[chat::kReticulumPeerHashSize]{};
    fillKey(destination, sizeof(destination), 3U);
    fillKey(identity, sizeof(identity), 33U);
    storage::PeerProjection rt{};
    rt.record.valid = true;
    rt.record.identity = chat::makeMeshPeerReticulumIdentity(
        chat::makeReticulumPeerIdentity(destination, identity));
    rt.record.reticulum.identity = rt.record.identity.reticulum;
    roundTripPeer(chat::MeshProtocol::Reticulum, rt);
}

} // namespace

int main()
{
    const storage::PeerProjection mt = meshtasticPeer();
    roundTripPeer(chat::MeshProtocol::Meshtastic, mt);
    unresolvedIdentityRoundTrips();
    stableIdentityAndContactRoundTrips();

    assert(storage::peerSlotSize(chat::MeshProtocol::Meshtastic) <
           sizeof(chat::MeshPeerRecord));
    assert(storage::peerSlotSize(chat::MeshProtocol::MeshCore) <
           sizeof(chat::MeshPeerRecord));
    assert(storage::peerSlotSize(chat::MeshProtocol::Reticulum) <
           sizeof(chat::MeshPeerRecord));
    assert(storage::contactSlotSize(chat::MeshProtocol::Meshtastic) < 64U);
    assert(storage::contactSlotSize(chat::MeshProtocol::MeshCore) < 80U);
    assert(storage::contactSlotSize(chat::MeshProtocol::Reticulum) < 64U);
    return 0;
}
