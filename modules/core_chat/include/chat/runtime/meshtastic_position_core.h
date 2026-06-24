#pragma once

#include <cstddef>
#include <cstdint>

namespace chat::runtime
{

struct MeshtasticPositionInput
{
    bool valid = false;
    double latitude_deg = 0.0;
    double longitude_deg = 0.0;
    bool has_altitude = false;
    double altitude_m = 0.0;
    bool has_speed = false;
    double speed_mps = 0.0;
    bool has_course = false;
    double course_deg = 0.0;
    uint32_t satellites = 0;
    uint32_t timestamp_s = 0;
};

enum class MeshtasticPositionAvailabilityReason : uint8_t
{
    Available,
    InvalidFix,
    InvalidCoordinates,
};

struct MeshtasticPositionAvailability
{
    bool available = false;
    MeshtasticPositionAvailabilityReason reason =
        MeshtasticPositionAvailabilityReason::InvalidFix;
};

class MeshtasticPositionCore final
{
  public:
    static MeshtasticPositionAvailability resolveAvailability(
        const MeshtasticPositionInput& input);

    static bool buildPositionPayload(const MeshtasticPositionInput& input,
                                     uint8_t* out_buf,
                                     size_t* out_len);
};

} // namespace chat::runtime
