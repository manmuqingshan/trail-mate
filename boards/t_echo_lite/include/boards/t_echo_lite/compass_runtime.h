#pragma once

#include "platform/ui/compass_runtime.h"

#include <cstdint>

namespace boards::t_echo_lite
{

struct CompassRawSample
{
    int16_t ax = 0;
    int16_t ay = 0;
    int16_t az = 0;
    int16_t mx = 0;
    int16_t my = 0;
    int16_t mz = 0;
};

class CompassRuntime
{
  public:
    platform::ui::compass::CompassState state();

  private:
    bool probe(uint32_t now_ms, bool force_log);
    bool readSample(CompassRawSample* out);
    void updateCalibration(const CompassRawSample& sample);
    void updateHeading(const CompassRawSample& sample, uint32_t now_ms);

    uint8_t imu_address_ = 0;
    bool probe_done_ = false;
    bool available_ = false;
    bool heading_valid_ = false;
    bool magnetic_ok_ = false;
    bool flat_ = false;
    bool calibration_initialized_ = false;
    uint8_t calibration_percent_ = 0;
    uint8_t read_fail_count_ = 0;
    int16_t mag_min_[3] = {};
    int16_t mag_max_[3] = {};
    float heading_deg_ = 0.0F;
    uint32_t last_probe_ms_ = 0;
    uint32_t last_sample_ms_ = 0;
};

} // namespace boards::t_echo_lite
