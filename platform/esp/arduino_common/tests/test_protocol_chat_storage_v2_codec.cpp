#include "platform/esp/arduino_common/chat/infra/store/protocol_chat_codec.h"

#include <cassert>
#include <cstring>
#include <string>
#include <vector>

namespace storage = chat::storage::v2;

namespace
{

chat::ChatMessage messageFor(chat::MeshProtocol protocol,
                             std::size_t text_length)
{
    chat::ChatMessage message{};
    message.protocol = protocol;
    message.channel = chat::ChannelId::SECONDARY;
    message.from = 0x11223344U;
    message.peer = 0x55667788U;
    message.msg_id = 0xAABBCCDDU;
    message.timestamp = 123456U;
    message.text.assign(text_length, 'x');
    message.has_geo = true;
    message.geo_lat_e7 = 123;
    message.geo_lon_e7 = -456;
    message.source_unverified = true;
    message.rx_origin = chat::RxOrigin::WiFi;
    message.status = chat::MessageStatus::Incoming;
    return message;
}

void roundTrip(chat::MeshProtocol protocol, std::size_t text_length)
{
    chat::ChatMessage input = messageFor(protocol, text_length);
    if (protocol == chat::MeshProtocol::Reticulum)
    {
        uint8_t destination[chat::kReticulumPeerHashSize]{};
        uint8_t identity[chat::kReticulumPeerHashSize]{};
        destination[0] = 0x42U;
        identity[0] = 0x24U;
        input.reticulum_identity =
            chat::makeReticulumPeerIdentity(destination, identity);
        input.has_reticulum_lxmf_hash = true;
        input.reticulum_lxmf_hash[0] = 0x99U;
    }

    std::vector<uint8_t> slot(storage::messageSlotSize(protocol));
    assert(storage::encodeMessageSlot(input, 17U, slot.data(), slot.size()));

    chat::ChatMessage output{};
    uint32_t sequence = 0;
    assert(storage::decodeMessageSlot(protocol,
                                      slot.data(),
                                      slot.size(),
                                      output,
                                      &sequence));
    assert(sequence == 17U);
    assert(output.protocol == protocol);
    assert(output.msg_id == input.msg_id);
    assert(output.text == input.text);
    assert(output.has_geo);
    assert(output.geo_lon_e7 == input.geo_lon_e7);
    assert(output.source_unverified);
    if (protocol == chat::MeshProtocol::Reticulum)
    {
        assert(chat::sameReticulumDestinationHash(output.reticulum_identity,
                                                  input.reticulum_identity));
        assert(output.has_reticulum_lxmf_hash);
        assert(std::memcmp(output.reticulum_lxmf_hash,
                           input.reticulum_lxmf_hash,
                           chat::kReticulumLxmfHashSize) == 0);
    }

    slot.back() ^= 0x80U;
    assert(!storage::decodeMessageSlot(protocol,
                                       slot.data(),
                                       slot.size(),
                                       output));
}

} // namespace

int main()
{
    roundTrip(chat::MeshProtocol::Meshtastic,
              storage::kMeshtasticTextMax);
    roundTrip(chat::MeshProtocol::MeshCore, storage::kMeshCoreTextMax);
    roundTrip(chat::MeshProtocol::Reticulum,
              storage::kReticulumTextMax);

    chat::ChatMessage too_long = messageFor(
        chat::MeshProtocol::Meshtastic,
        storage::kMeshtasticTextMax + 1U);
    std::vector<uint8_t> slot(
        storage::messageSlotSize(chat::MeshProtocol::Meshtastic));
    assert(!storage::encodeMessageSlot(too_long,
                                       1U,
                                       slot.data(),
                                       slot.size()));
    assert(storage::messageSlotSize(chat::MeshProtocol::Meshtastic) <
           storage::messageSlotSize(chat::MeshProtocol::Reticulum));
    return 0;
}
