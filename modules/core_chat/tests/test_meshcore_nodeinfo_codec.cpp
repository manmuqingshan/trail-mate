#include "chat/infra/meshcore/meshcore_payload_helpers.h"

#include <cassert>
#include <cstring>

int main()
{
    using chat::meshcore::DecodedNodeInfoControl;
    using chat::meshcore::MeshCoreNodeInfoBuildInfo;
    using chat::meshcore::MeshCoreNodeInfoControlType;

    {
        uint8_t payload[chat::meshcore::kMeshCoreNodeInfoInfoPayloadSize] = {};
        size_t payload_len = 0;
        assert(chat::meshcore::buildNodeInfoQueryControlPayload(true,
                                                                payload,
                                                                sizeof(payload),
                                                                &payload_len));
        assert(payload_len == chat::meshcore::kMeshCoreNodeInfoQueryPayloadSize);

        DecodedNodeInfoControl decoded{};
        assert(chat::meshcore::decodeNodeInfoControlPayload(payload, payload_len, &decoded));
        assert(decoded.valid);
        assert(decoded.complete);
        assert(decoded.type == MeshCoreNodeInfoControlType::Query);
        assert(decoded.request_reply);
    }

    {
        MeshCoreNodeInfoBuildInfo info{};
        info.role = 3;
        info.hops = 7;
        info.node_id = 0x12345678UL;
        info.timestamp = 1781258481UL;
        info.short_name = "shortname";
        info.long_name = "Trail Mate MeshCore";

        uint8_t payload[chat::meshcore::kMeshCoreNodeInfoInfoPayloadSize] = {};
        size_t payload_len = 0;
        assert(chat::meshcore::buildNodeInfoInfoControlPayload(info,
                                                               payload,
                                                               sizeof(payload),
                                                               &payload_len));
        assert(payload_len == chat::meshcore::kMeshCoreNodeInfoInfoPayloadSize);

        DecodedNodeInfoControl decoded{};
        assert(chat::meshcore::decodeNodeInfoControlPayload(payload, payload_len, &decoded));
        assert(decoded.valid);
        assert(decoded.complete);
        assert(decoded.type == MeshCoreNodeInfoControlType::Info);
        assert(decoded.info.role == info.role);
        assert(decoded.info.hops == info.hops);
        assert(decoded.info.node_id == info.node_id);
        assert(decoded.info.timestamp == info.timestamp);
        assert(std::strcmp(decoded.info.short_name, "shortname") == 0);
        assert(std::strcmp(decoded.info.long_name, "Trail Mate MeshCore") == 0);
    }

    {
        uint8_t payload[chat::meshcore::kMeshCoreNodeInfoQueryPayloadSize] = {};
        size_t payload_len = 0;
        assert(chat::meshcore::buildNodeInfoQueryControlPayload(false,
                                                                payload,
                                                                sizeof(payload),
                                                                &payload_len));

        DecodedNodeInfoControl decoded{};
        assert(chat::meshcore::decodeNodeInfoControlPayload(payload, payload_len, &decoded));
        assert(decoded.valid);
        assert(decoded.complete);
        assert(decoded.type == MeshCoreNodeInfoControlType::Query);
        assert(!decoded.request_reply);
    }

    {
        chat::meshcore::MeshCoreDiscoverRequestBuildInfo request{};
        request.prefix_only = true;
        request.type_filter = 0xAA;
        request.tag = 0x12345678UL;
        request.since = 1781258481UL;

        uint8_t payload[16] = {};
        size_t payload_len = 0;
        assert(chat::meshcore::buildDiscoverRequestControlPayload(request,
                                                                  payload,
                                                                  sizeof(payload),
                                                                  &payload_len));
        assert(payload_len == chat::meshcore::kMeshCoreDiscoverRequestBasePayloadSize + sizeof(uint32_t));

        chat::meshcore::DecodedDiscoverRequest decoded{};
        assert(chat::meshcore::decodeDiscoverRequest(payload, payload_len, &decoded));
        assert(decoded.valid);
        assert(decoded.prefix_only);
        assert(decoded.type_filter == request.type_filter);
        assert(decoded.tag == request.tag);
        assert(decoded.since == request.since);
    }

    {
        const uint8_t pubkey[] = {0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49};
        uint8_t payload[32] = {};
        size_t payload_len = 0;
        assert(chat::meshcore::buildDiscoverResponseControlPayload(
            chat::meshcore::kMeshCoreAdvertTypeRepeater,
            -4,
            0xAABBCCDDUL,
            pubkey,
            sizeof(pubkey),
            payload,
            sizeof(payload),
            &payload_len));
        assert(payload_len == chat::meshcore::kMeshCoreDiscoverResponseBasePayloadSize + sizeof(pubkey));

        chat::meshcore::DecodedDiscoverResponse decoded{};
        assert(chat::meshcore::decodeDiscoverResponse(payload, payload_len, &decoded));
        assert(decoded.valid);
        assert(decoded.node_type == chat::meshcore::kMeshCoreAdvertTypeRepeater);
        assert(decoded.snr_qdb == -4);
        assert(decoded.tag == 0xAABBCCDDUL);
        assert(decoded.pubkey_len == sizeof(pubkey));
        assert(std::memcmp(decoded.pubkey, pubkey, sizeof(pubkey)) == 0);
    }

    return 0;
}
