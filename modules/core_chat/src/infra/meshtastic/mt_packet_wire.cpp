/**
 * @file mt_packet_wire.cpp
 * @brief Meshtastic wire packet format implementation.
 */

#include "chat/infra/meshtastic/mt_packet_wire.h"

#include "chat/infra/meshtastic/mt_aes_ctr.h"

#include <cstring>

namespace chat
{
namespace meshtastic
{
namespace
{

void makePacketNonce(uint32_t packet_id, uint32_t from_node, uint8_t nonce[16])
{
    memset(nonce, 0, 16);
    const uint64_t packet_id64 = static_cast<uint64_t>(packet_id);
    memcpy(nonce, &packet_id64, sizeof(packet_id64));
    memcpy(nonce + sizeof(packet_id64), &from_node, sizeof(from_node));
}

} // namespace

bool buildWirePacket(const uint8_t* data_payload, size_t data_len,
                     uint32_t from_node, uint32_t packet_id,
                     uint32_t dest_node, uint8_t channel_hash,
                     uint8_t hop_limit, bool want_ack,
                     const uint8_t* psk, size_t psk_len,
                     uint8_t* out_buffer, size_t* out_size)
{
    if (!data_payload || !out_buffer || !out_size || data_len == 0)
    {
        return false;
    }

    PacketHeaderWire hdr{};
    hdr.to = dest_node;
    hdr.from = from_node;
    hdr.id = packet_id;

    const uint8_t hop_start = hop_limit;
    uint8_t flags = (hop_limit & PACKET_FLAGS_HOP_LIMIT_MASK) |
                    ((hop_start << PACKET_FLAGS_HOP_START_SHIFT) & PACKET_FLAGS_HOP_START_MASK);
    if (want_ack)
    {
        flags |= PACKET_FLAGS_WANT_ACK_MASK;
    }
    hdr.flags = flags;
    hdr.channel = channel_hash;
    hdr.next_hop = 0;
    hdr.relay_node = static_cast<uint8_t>(from_node & 0xFF);

    const size_t required_size = sizeof(hdr) + data_len;
    if (*out_size < required_size)
    {
        *out_size = required_size;
        return false;
    }

    // The caller may intentionally use the same buffer for the source and
    // destination. Move the payload behind the header before encrypting it
    // in place so no protocol-sized automatic scratch buffer is needed.
    memmove(out_buffer + sizeof(hdr), data_payload, data_len);
    memcpy(out_buffer, &hdr, sizeof(hdr));

    if (psk && psk_len > 0)
    {
        uint8_t nonce[16];
        makePacketNonce(packet_id, from_node, nonce);
        if (!aesCtrCryptInPlace(psk, psk_len, nonce, out_buffer + sizeof(hdr), data_len))
        {
            return false;
        }
    }

    *out_size = required_size;
    return true;
}

bool parseWirePacket(const uint8_t* buffer, size_t size,
                     PacketHeaderWire* out_header,
                     uint8_t* out_payload, size_t* out_payload_size)
{
    if (!buffer || !out_header || !out_payload || !out_payload_size || size < sizeof(PacketHeaderWire))
    {
        return false;
    }

    memcpy(out_header, buffer, sizeof(PacketHeaderWire));

    const size_t payload_len = size - sizeof(PacketHeaderWire);
    if (payload_len > *out_payload_size)
    {
        *out_payload_size = payload_len;
        return false;
    }

    memcpy(out_payload, buffer + sizeof(PacketHeaderWire), payload_len);
    *out_payload_size = payload_len;
    return true;
}

bool decryptPayload(const PacketHeaderWire& header,
                    const uint8_t* cipher, size_t cipher_len,
                    const uint8_t* psk, size_t psk_len,
                    uint8_t* out_plaintext, size_t* out_plain_len)
{
    if (!cipher || !psk || !out_plaintext || !out_plain_len || cipher_len == 0)
    {
        return false;
    }

    if (*out_plain_len < cipher_len)
    {
        *out_plain_len = cipher_len;
        return false;
    }

    memcpy(out_plaintext, cipher, cipher_len);
    uint8_t nonce[16];
    makePacketNonce(header.id, header.from, nonce);
    if (!aesCtrCryptInPlace(psk, psk_len, nonce, out_plaintext, cipher_len))
    {
        return false;
    }

    *out_plain_len = cipher_len;
    return true;
}

} // namespace meshtastic
} // namespace chat
