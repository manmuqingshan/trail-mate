#include "chat/runtime/meshtastic_app_action_runtime.h"

#include "pb_encode.h"

#include <cassert>
#include <vector>

namespace
{

std::vector<uint8_t> encodeRouting(meshtastic_Routing_Error reason)
{
    meshtastic_Routing routing = meshtastic_Routing_init_zero;
    routing.which_variant = meshtastic_Routing_error_reason_tag;
    routing.error_reason = reason;

    uint8_t buffer[32] = {};
    pb_ostream_t stream = pb_ostream_from_buffer(buffer, sizeof(buffer));
    assert(pb_encode(&stream, meshtastic_Routing_fields, &routing));
    return std::vector<uint8_t>(buffer, buffer + stream.bytes_written);
}

chat::MeshIncomingData makeRouting(uint32_t request_id, meshtastic_Routing_Error reason)
{
    chat::MeshIncomingData data{};
    data.portnum = meshtastic_PortNum_ROUTING_APP;
    data.request_id = request_id;
    data.payload = encodeRouting(reason);
    return data;
}

chat::MeshIncomingData makeTraceResponse(uint32_t request_id)
{
    chat::MeshIncomingData data{};
    data.portnum = meshtastic_PortNum_TRACEROUTE_APP;
    data.request_id = request_id;
    data.payload.push_back(0);
    return data;
}

chat::MeshIncomingData makePositionResponse(uint32_t request_id)
{
    chat::MeshIncomingData data{};
    data.portnum = meshtastic_PortNum_POSITION_APP;
    data.request_id = request_id;
    data.payload.push_back(0);
    return data;
}

} // namespace

int main()
{
    using chat::runtime::MeshtasticAppActionReason;
    using chat::runtime::MeshtasticAppActionRuntime;
    using chat::runtime::MeshtasticAppActionSnapshot;
    using chat::runtime::MeshtasticAppActionState;

    {
        MeshtasticAppActionRuntime runtime;
        MeshtasticAppActionSnapshot snapshot{};
        runtime.startTraceRoute(0x1001, 0xAA, 1000);

        assert(runtime.consumeIncomingData(
            makeRouting(0x1001, meshtastic_Routing_Error_NONE), 1200, &snapshot));
        assert(snapshot.state == MeshtasticAppActionState::Delivered);
        assert(snapshot.reason == MeshtasticAppActionReason::RoutingAck);
        assert(runtime.active());

        assert(runtime.consumeIncomingData(makeTraceResponse(0x1001), 1500, &snapshot));
        assert(snapshot.state == MeshtasticAppActionState::Completed);
        assert(snapshot.reason == MeshtasticAppActionReason::AppResponse);
        assert(!runtime.active());
    }

    {
        MeshtasticAppActionRuntime runtime;
        MeshtasticAppActionSnapshot snapshot{};
        runtime.startTraceRoute(0x2002, 0xBB, 1000);

        assert(runtime.consumeIncomingData(
            makeRouting(0x2002, meshtastic_Routing_Error_NO_ROUTE), 1300, &snapshot));
        assert(snapshot.state == MeshtasticAppActionState::Failed);
        assert(snapshot.reason == MeshtasticAppActionReason::RoutingError);
        assert(snapshot.routing_error == meshtastic_Routing_Error_NO_ROUTE);
        assert(!runtime.active());
    }

    {
        MeshtasticAppActionRuntime runtime;
        MeshtasticAppActionSnapshot snapshot{};
        runtime.startTraceRoute(0x3003, 0xCC, 1000, 500);

        assert(!runtime.tick(1499, &snapshot));
        assert(runtime.tick(1500, &snapshot));
        assert(snapshot.state == MeshtasticAppActionState::TimedOut);
        assert(snapshot.reason == MeshtasticAppActionReason::Timeout);
        assert(!runtime.active());
    }

    {
        MeshtasticAppActionRuntime runtime;
        MeshtasticAppActionSnapshot snapshot{};
        runtime.startTraceRoute(0x4004, 0xDD, 1000);

        assert(!runtime.consumeIncomingData(
            makeRouting(0x9999, meshtastic_Routing_Error_NONE), 1200, &snapshot));
        assert(runtime.active());
    }

    {
        MeshtasticAppActionRuntime runtime;
        MeshtasticAppActionSnapshot snapshot{};
        runtime.startPositionExchange(0x5005, 0xEE, 1000);

        assert(!runtime.consumeIncomingData(makePositionResponse(0x9999), 1200, &snapshot));
        assert(runtime.consumeIncomingData(makePositionResponse(0x5005), 1300, &snapshot));
        assert(snapshot.state == MeshtasticAppActionState::Completed);
        assert(snapshot.reason == MeshtasticAppActionReason::AppResponse);
        assert(!runtime.active());
    }

    return 0;
}
