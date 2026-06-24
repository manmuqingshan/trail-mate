#include "chat/runtime/meshtastic_waypoint_core.h"
#include "meshtastic/mesh.pb.h"
#include "pb_decode.h"

#include <cassert>
#include <cstdint>
#include <limits>
#include <string>

int main()
{
    {
        chat::runtime::MeshtasticWaypointInput input{};
        input.valid = true;
        input.latitude_deg = 26.67773;
        input.longitude_deg = 107.28225;
        input.id = 1710000000U;
        input.expire = input.id + 86400U;
        input.name = "Trail Mate POI";
        input.description = "Shared from uConsole current GPS fix";
        const auto available =
            chat::runtime::MeshtasticWaypointCore::resolveAvailability(input);
        assert(available.available);
        assert(available.reason == chat::runtime::MeshtasticWaypointAvailabilityReason::Available);

        uint8_t payload[meshtastic_Waypoint_size] = {};
        size_t payload_len = sizeof(payload);
        assert(chat::runtime::MeshtasticWaypointCore::buildWaypointPayload(input,
                                                                           payload,
                                                                           &payload_len));
        assert(payload_len > 0);

        meshtastic_Waypoint waypoint = meshtastic_Waypoint_init_zero;
        pb_istream_t stream = pb_istream_from_buffer(payload, payload_len);
        assert(pb_decode(&stream, meshtastic_Waypoint_fields, &waypoint));
        assert(waypoint.id == input.id);
        assert(waypoint.has_latitude_i);
        assert(waypoint.latitude_i == static_cast<int32_t>(input.latitude_deg * 1e7));
        assert(waypoint.has_longitude_i);
        assert(waypoint.longitude_i == static_cast<int32_t>(input.longitude_deg * 1e7));
        assert(waypoint.expire == input.expire);
        assert(std::string(waypoint.name) == input.name);
        assert(std::string(waypoint.description) == input.description);
    }

    {
        chat::runtime::MeshtasticWaypointInput input{};
        input.valid = false;
        input.latitude_deg = 26.0;
        input.longitude_deg = 107.0;
        const auto available =
            chat::runtime::MeshtasticWaypointCore::resolveAvailability(input);
        assert(!available.available);
        assert(available.reason == chat::runtime::MeshtasticWaypointAvailabilityReason::InvalidFix);

        uint8_t payload[meshtastic_Waypoint_size] = {};
        size_t payload_len = sizeof(payload);
        assert(!chat::runtime::MeshtasticWaypointCore::buildWaypointPayload(input,
                                                                            payload,
                                                                            &payload_len));
    }

    {
        chat::runtime::MeshtasticWaypointInput input{};
        input.valid = true;
        input.latitude_deg = std::numeric_limits<double>::quiet_NaN();
        input.longitude_deg = 107.0;

        const auto available =
            chat::runtime::MeshtasticWaypointCore::resolveAvailability(input);
        assert(!available.available);
        assert(available.reason ==
               chat::runtime::MeshtasticWaypointAvailabilityReason::InvalidCoordinates);
    }

    return 0;
}
