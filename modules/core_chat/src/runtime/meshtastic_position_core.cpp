#include "chat/runtime/meshtastic_position_core.h"

#include "meshtastic/mesh.pb.h"
#include "pb_encode.h"

#include <cmath>

namespace chat::runtime
{
namespace
{

constexpr uint32_t kMinimumValidEpochSeconds = 1577836800U;

} // namespace

MeshtasticPositionAvailability MeshtasticPositionCore::resolveAvailability(
    const MeshtasticPositionInput& input)
{
    if (!input.valid)
    {
        MeshtasticPositionAvailability availability{};
        availability.available = false;
        availability.reason = MeshtasticPositionAvailabilityReason::InvalidFix;
        return availability;
    }
    if (!std::isfinite(input.latitude_deg) || !std::isfinite(input.longitude_deg))
    {
        MeshtasticPositionAvailability availability{};
        availability.available = false;
        availability.reason = MeshtasticPositionAvailabilityReason::InvalidCoordinates;
        return availability;
    }
    MeshtasticPositionAvailability availability{};
    availability.available = true;
    availability.reason = MeshtasticPositionAvailabilityReason::Available;
    return availability;
}

bool MeshtasticPositionCore::buildPositionPayload(const MeshtasticPositionInput& input,
                                                  uint8_t* out_buf,
                                                  size_t* out_len)
{
    const auto availability = resolveAvailability(input);
    if (!availability.available || !out_buf || !out_len || *out_len == 0)
    {
        return false;
    }

    meshtastic_Position pos = meshtastic_Position_init_zero;
    pos.has_latitude_i = true;
    pos.latitude_i = static_cast<int32_t>(input.latitude_deg * 1e7);
    pos.has_longitude_i = true;
    pos.longitude_i = static_cast<int32_t>(input.longitude_deg * 1e7);
    pos.location_source = meshtastic_Position_LocSource_LOC_INTERNAL;

    if (input.has_altitude && std::isfinite(input.altitude_m))
    {
        pos.has_altitude = true;
        pos.altitude = static_cast<int32_t>(std::lround(input.altitude_m));
        pos.altitude_source = meshtastic_Position_AltSource_ALT_INTERNAL;
    }
    if (input.has_speed && std::isfinite(input.speed_mps))
    {
        pos.has_ground_speed = true;
        pos.ground_speed = static_cast<uint32_t>(std::lround(input.speed_mps));
    }
    if (input.has_course && std::isfinite(input.course_deg))
    {
        double course = input.course_deg;
        if (course < 0.0)
        {
            course = 0.0;
        }
        uint32_t cdeg = static_cast<uint32_t>(std::lround(course * 100.0));
        if (cdeg >= 36000U)
        {
            cdeg = 35999U;
        }
        pos.has_ground_track = true;
        pos.ground_track = cdeg;
    }
    if (input.satellites > 0)
    {
        pos.sats_in_view = static_cast<uint32_t>(input.satellites);
    }

    if (input.timestamp_s >= kMinimumValidEpochSeconds)
    {
        pos.timestamp = input.timestamp_s;
    }

    pb_ostream_t stream = pb_ostream_from_buffer(out_buf, *out_len);
    if (!pb_encode(&stream, meshtastic_Position_fields, &pos))
    {
        return false;
    }

    *out_len = stream.bytes_written;
    return true;
}

} // namespace chat::runtime
