#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace chat::runtime
{

struct MeshtasticWaypointInput
{
    bool valid = false;
    double latitude_deg = 0.0;
    double longitude_deg = 0.0;
    uint32_t id = 0;
    uint32_t expire = 0;
    uint32_t locked_to = 0;
    uint32_t icon = 0;
    std::string name;
    std::string description;
};

enum class MeshtasticWaypointAvailabilityReason : uint8_t
{
    Available,
    InvalidFix,
    InvalidCoordinates,
};

struct MeshtasticWaypointAvailability
{
    bool available = false;
    MeshtasticWaypointAvailabilityReason reason =
        MeshtasticWaypointAvailabilityReason::InvalidFix;
};

class MeshtasticWaypointCore final
{
  public:
    static MeshtasticWaypointAvailability resolveAvailability(
        const MeshtasticWaypointInput& input);

    static bool buildWaypointPayload(const MeshtasticWaypointInput& input,
                                     uint8_t* out_buf,
                                     size_t* out_len);
};

} // namespace chat::runtime
