#pragma once

#include <cstddef>
#include <cstdint>

#include "bsp/trail_mate_t_display_p4_runtime.h"

namespace boards::t_display_p4
{

/**
 * Adapts the P4 board-owned ES8311 session API to the existing ESP audio
 * service contract used by Walkie and SSTV. The board runtime remains the
 * single owner of the codec/I2S lifecycle and enforces cross-feature
 * exclusion through trail_mate_t_display_p4_audio_owner_t.
 */
class CodecCompat
{
  public:
    explicit CodecCompat(trail_mate_t_display_p4_audio_owner_t owner);
    ~CodecCompat();

    CodecCompat(const CodecCompat&) = delete;
    CodecCompat& operator=(const CodecCompat&) = delete;

    int open(uint8_t bits_per_sample, uint8_t channels, uint32_t sample_rate);
    void close();

    int write(uint8_t* buffer, size_t size);
    int read(uint8_t* buffer, size_t size);

    void setMute(bool enable);
    bool getMute() const;
    void setOutMute(bool enable);
    bool getOutMute() const;
    void setVolume(uint8_t level);
    int getVolume() const;
    void setGain(float db_value);
    float getGain() const;
    bool ready() const;

  private:
    void apply_output_level();

    trail_mate_t_display_p4_audio_owner_t owner_;
    bool open_ = false;
    bool mute_ = false;
    bool out_mute_ = false;
    uint8_t volume_ = 10;
    float gain_db_ = 36.0f;
};

} // namespace boards::t_display_p4
