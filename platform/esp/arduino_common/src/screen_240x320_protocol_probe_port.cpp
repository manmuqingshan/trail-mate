#include "platform/ui/screen_240x320_protocol_probe_port.h"

#include "ui/mono/screens/screen_240x320/protocol_probe_port.h"
#include "ui/screens/energy_sweep/energy_sweep_page_runtime.h"

#include <cstring>

namespace platform::ui
{
namespace
{

class EspProtocolProbePort final
    : public ::ui::mono::screens::screen_240x320::ProtocolProbePort
{
  public:
    bool read(::ui::mono::screens::screen_240x320::ProtocolProbeView& out) const override
    {
        if (!::energy_sweep::ui::runtime::text_snapshot(snapshot_scratch_))
        {
            out = {};
            return false;
        }

        out.available = snapshot_scratch_.available;
        out.scanning = snapshot_scratch_.scanning;
        out.radio_error = snapshot_scratch_.radio_error;
        out.applied = snapshot_scratch_.applied;
        out.has_selection = snapshot_scratch_.has_selection;
        out.candidate_index = snapshot_scratch_.candidate_index;
        out.candidate_count = snapshot_scratch_.candidate_count;
        out.completed_passes = snapshot_scratch_.completed_passes;
        out.observation_count = snapshot_scratch_.observation_count;
        out.evidence_count = snapshot_scratch_.evidence_count;
        out.crc_frame_count = snapshot_scratch_.crc_frame_count;
        std::memcpy(out.status, snapshot_scratch_.status, sizeof(out.status));
        std::memcpy(out.current_profile,
                    snapshot_scratch_.current_profile,
                    sizeof(out.current_profile));
        std::memcpy(out.selected_profile,
                    snapshot_scratch_.selected_profile,
                    sizeof(out.selected_profile));
        return true;
    }

    bool start() override
    {
        return ::energy_sweep::ui::runtime::text_start();
    }

    void stop() override
    {
        ::energy_sweep::ui::runtime::text_stop();
    }

    bool selectObservationDelta(int delta) override
    {
        return ::energy_sweep::ui::runtime::text_select_observation_delta(delta);
    }

    bool applySelected() override
    {
        return ::energy_sweep::ui::runtime::text_apply_selected();
    }

  private:
    // Snapshot buffer is owned by the adapter, never allocated on an ESP
    // task stack while a radio worker is active.
    mutable ::energy_sweep::ui::runtime::TextSnapshot snapshot_scratch_{};
};

EspProtocolProbePort s_protocol_probe_port;

} // namespace

void install_screen_240x320_protocol_probe_port()
{
    ::ui::mono::screens::screen_240x320::installProtocolProbePort(
        &s_protocol_probe_port);
}

} // namespace platform::ui
