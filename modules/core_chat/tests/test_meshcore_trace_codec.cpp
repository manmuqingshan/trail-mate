#include "chat/infra/meshcore/meshcore_protocol_helpers.h"

#include <cassert>
#include <cstring>

int main()
{
    {
        uint8_t payload[chat::meshcore::kMeshCoreTraceBasePayloadSize] = {};
        size_t payload_len = 0;
        assert(chat::meshcore::buildTracePayload(0x12345678UL,
                                                 0xAABBCCDDUL,
                                                 0x00,
                                                 payload,
                                                 sizeof(payload),
                                                 &payload_len));
        assert(payload_len == chat::meshcore::kMeshCoreTraceBasePayloadSize);

        chat::meshcore::DecodedTracePayload decoded{};
        assert(chat::meshcore::decodeTracePayload(payload, payload_len, 0, &decoded));
        assert(decoded.valid);
        assert(decoded.terminal);
        assert(decoded.tag == 0x12345678UL);
        assert(decoded.auth == 0xAABBCCDDUL);
        assert(decoded.flags == 0x00);
        assert(decoded.path_hash_size == 1);
        assert(decoded.trace_hashes_len == 0);
    }

    {
        const uint8_t path_hashes[] = {0x11, 0x22, 0x33};
        uint8_t payload[chat::meshcore::kMeshCoreTraceBasePayloadSize + sizeof(path_hashes)] = {};
        size_t payload_len = 0;
        assert(chat::meshcore::isValidTracePathHashBytes(0x00, sizeof(path_hashes), 64));
        assert(chat::meshcore::buildTracePayload(1, 2, 0x00,
                                                 path_hashes,
                                                 sizeof(path_hashes),
                                                 payload,
                                                 sizeof(payload),
                                                 &payload_len));
        assert(payload_len == chat::meshcore::kMeshCoreTraceBasePayloadSize + sizeof(path_hashes));

        chat::meshcore::DecodedTracePayload decoded{};
        assert(chat::meshcore::decodeTracePayload(payload, payload_len, 1, &decoded));
        assert(decoded.valid);
        assert(!decoded.terminal);
        assert(decoded.offset == 1);
        assert(decoded.next_hash == 0x22);
        assert(decoded.trace_hashes_len == 3);

        assert(chat::meshcore::decodeTracePayload(payload, payload_len, 3, &decoded));
        assert(decoded.valid);
        assert(decoded.terminal);
    }

    {
        const uint8_t path_hashes[] = {0xAA, 0xBB, 0xCC, 0xDD};
        uint8_t payload[chat::meshcore::kMeshCoreTraceBasePayloadSize + sizeof(path_hashes)] = {};
        size_t payload_len = 0;
        assert(chat::meshcore::isValidTracePathHashBytes(0x01, sizeof(path_hashes), 64));
        assert(!chat::meshcore::isValidTracePathHashBytes(0x01, 3, 64));
        assert(chat::meshcore::buildTracePayload(1, 2, 0x01,
                                                 path_hashes,
                                                 sizeof(path_hashes),
                                                 payload,
                                                 sizeof(payload),
                                                 &payload_len));

        chat::meshcore::DecodedTracePayload decoded{};
        assert(chat::meshcore::decodeTracePayload(payload, payload_len, 1, &decoded));
        assert(decoded.path_hash_size == 2);
        assert(decoded.offset == 2);
        assert(decoded.next_hash == 0xCC);
    }

    return 0;
}
