#pragma once

#include <cstdint>

namespace ui::mono::screens::screen_240x320
{

// Radio probing is an external capability. The screen receives a compact
// projection and emits commands, without learning the radio worker or target.
struct ProtocolProbeView
{
    bool available = false;
    bool scanning = false;
    bool radio_error = false;
    bool applied = false;
    bool has_selection = false;
    uint32_t candidate_index = 0;
    uint32_t candidate_count = 0;
    uint32_t completed_passes = 0;
    uint32_t observation_count = 0;
    uint32_t evidence_count = 0;
    uint32_t crc_frame_count = 0;
    char status[48]{};
    char current_profile[40]{};
    char selected_profile[40]{};
};

class ProtocolProbePort
{
  public:
    virtual ~ProtocolProbePort() = default;

    virtual bool read(ProtocolProbeView& out) const = 0;
    virtual bool start() = 0;
    virtual void stop() = 0;
    virtual bool selectObservationDelta(int delta) = 0;
    virtual bool applySelected() = 0;
};

void installProtocolProbePort(ProtocolProbePort* port);
ProtocolProbePort* protocolProbePort();

} // namespace ui::mono::screens::screen_240x320
