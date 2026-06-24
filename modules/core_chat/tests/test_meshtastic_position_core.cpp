#include "chat/runtime/meshtastic_position_core.h"
#include "meshtastic/mesh.pb.h"
#include "pb_decode.h"

#include <cassert>
#include <cstdint>
#include <limits>

int main()
{
    {
        chat::runtime::MeshtasticPositionInput input{};
        input.valid = true;
        input.latitude_deg = 26.67773;
        input.longitude_deg = 107.28225;
        input.has_altitude = true;
        input.altitude_m = 1903.6;
        input.has_speed = true;
        input.speed_mps = 3.6;
        input.has_course = true;
        input.course_deg = 360.0;
        input.satellites = 11;
        input.timestamp_s = 1710000000U;
        const auto available =
            chat::runtime::MeshtasticPositionCore::resolveAvailability(input);
        assert(available.available);
        assert(available.reason == chat::runtime::MeshtasticPositionAvailabilityReason::Available);

        uint8_t payload[96] = {};
        size_t payload_len = sizeof(payload);
        assert(chat::runtime::MeshtasticPositionCore::buildPositionPayload(input,
                                                                           payload,
                                                                           &payload_len));
        assert(payload_len > 0);

        meshtastic_Position pos = meshtastic_Position_init_zero;
        pb_istream_t stream = pb_istream_from_buffer(payload, payload_len);
        assert(pb_decode(&stream, meshtastic_Position_fields, &pos));
        assert(pos.has_latitude_i);
        assert(pos.latitude_i == static_cast<int32_t>(input.latitude_deg * 1e7));
        assert(pos.has_longitude_i);
        assert(pos.longitude_i == static_cast<int32_t>(input.longitude_deg * 1e7));
        assert(pos.location_source == meshtastic_Position_LocSource_LOC_INTERNAL);
        assert(pos.has_altitude);
        assert(pos.altitude == 1904);
        assert(pos.altitude_source == meshtastic_Position_AltSource_ALT_INTERNAL);
        assert(pos.has_ground_speed);
        assert(pos.ground_speed == 4U);
        assert(pos.has_ground_track);
        assert(pos.ground_track == 35999U);
        assert(pos.sats_in_view == 11U);
        assert(pos.timestamp == input.timestamp_s);
    }

    {
        chat::runtime::MeshtasticPositionInput input{};
        input.valid = false;
        input.latitude_deg = 26.0;
        input.longitude_deg = 107.0;
        const auto available =
            chat::runtime::MeshtasticPositionCore::resolveAvailability(input);
        assert(!available.available);
        assert(available.reason == chat::runtime::MeshtasticPositionAvailabilityReason::InvalidFix);

        uint8_t payload[96] = {};
        size_t payload_len = sizeof(payload);
        assert(!chat::runtime::MeshtasticPositionCore::buildPositionPayload(input,
                                                                            payload,
                                                                            &payload_len));
    }

    {
        chat::runtime::MeshtasticPositionInput input{};
        input.valid = true;
        input.latitude_deg = std::numeric_limits<double>::quiet_NaN();
        input.longitude_deg = 107.0;

        const auto available =
            chat::runtime::MeshtasticPositionCore::resolveAvailability(input);
        assert(!available.available);
        assert(available.reason ==
               chat::runtime::MeshtasticPositionAvailabilityReason::InvalidCoordinates);
    }

    return 0;
}
