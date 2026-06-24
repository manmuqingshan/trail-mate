#include "chat/runtime/meshtastic_waypoint_core.h"

#include "meshtastic/mesh.pb.h"
#include "pb_encode.h"

#include <cmath>
#include <cstdio>

namespace chat::runtime
{
namespace
{

void copyFixedString(char* out, size_t out_len, const std::string& value)
{
    if (out == nullptr || out_len == 0)
    {
        return;
    }
    std::snprintf(out, out_len, "%s", value.c_str());
}

} // namespace

MeshtasticWaypointAvailability MeshtasticWaypointCore::resolveAvailability(
    const MeshtasticWaypointInput& input)
{
    if (!input.valid)
    {
        MeshtasticWaypointAvailability availability{};
        availability.available = false;
        availability.reason = MeshtasticWaypointAvailabilityReason::InvalidFix;
        return availability;
    }
    if (!std::isfinite(input.latitude_deg) || !std::isfinite(input.longitude_deg))
    {
        MeshtasticWaypointAvailability availability{};
        availability.available = false;
        availability.reason = MeshtasticWaypointAvailabilityReason::InvalidCoordinates;
        return availability;
    }
    MeshtasticWaypointAvailability availability{};
    availability.available = true;
    availability.reason = MeshtasticWaypointAvailabilityReason::Available;
    return availability;
}

bool MeshtasticWaypointCore::buildWaypointPayload(const MeshtasticWaypointInput& input,
                                                  uint8_t* out_buf,
                                                  size_t* out_len)
{
    const auto availability = resolveAvailability(input);
    if (!availability.available || !out_buf || !out_len || *out_len == 0)
    {
        return false;
    }

    meshtastic_Waypoint waypoint = meshtastic_Waypoint_init_zero;
    waypoint.id = input.id;
    waypoint.has_latitude_i = true;
    waypoint.latitude_i = static_cast<int32_t>(std::lround(input.latitude_deg * 1e7));
    waypoint.has_longitude_i = true;
    waypoint.longitude_i = static_cast<int32_t>(std::lround(input.longitude_deg * 1e7));
    waypoint.expire = input.expire;
    waypoint.locked_to = input.locked_to;
    waypoint.icon = input.icon;
    copyFixedString(waypoint.name, sizeof(waypoint.name), input.name);
    copyFixedString(waypoint.description, sizeof(waypoint.description), input.description);

    pb_ostream_t stream = pb_ostream_from_buffer(out_buf, *out_len);
    if (!pb_encode(&stream, meshtastic_Waypoint_fields, &waypoint))
    {
        return false;
    }

    *out_len = stream.bytes_written;
    return true;
}

} // namespace chat::runtime
