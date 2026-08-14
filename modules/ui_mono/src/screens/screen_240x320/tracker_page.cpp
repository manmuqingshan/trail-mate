#include "screen_app_internal.h"

#include "ui/mono/screens/screen_240x320/runtime_feature_port.h"

namespace ui::mono::screens::screen_240x320::detail
{
namespace
{

::ui::mono::screens::screen_240x320::TrackerView s_tracker_view;

} // namespace

void render_tracker()
{
    ::ui::mono::screens::screen_240x320::RuntimeFeaturePort* const port =
        ::ui::mono::screens::screen_240x320::runtimeFeaturePort();
    s_tracker_view = {};
    if (port != nullptr)
    {
        port->readTracker(s_tracker_view);
    }

    set_linef(0, "TRACKER %s", s_tracker_view.supported ? "READY" : "UNAVAILABLE");
    set_linef(1, "RECORDING %s", s_tracker_view.recording ? "ON" : "OFF");
    set_line(2, "FORMAT GPX / CSV / BIN");
    set_linef(3, "PATH %s", s_tracker_view.has_path ? s_tracker_view.path : "--");
    set_line(4, s_state.notice[0] != '\0' ? s_state.notice : "SELECT TO START OR STOP");
    clear_lines_from(5);
}

void add_tracker_actions()
{
    add_action("START/STOP", Action::ToggleTracker, kMargin, kActionTop, 112);
}

bool handle_tracker_action(Action action)
{
    if (action != Action::ToggleTracker)
    {
        return false;
    }

    ::ui::mono::screens::screen_240x320::RuntimeFeaturePort* const port =
        ::ui::mono::screens::screen_240x320::runtimeFeaturePort();
    if (port == nullptr)
    {
        set_notice("TRACKER UNAVAILABLE");
    }
    else
    {
        port->readTracker(s_tracker_view);
        if (!s_tracker_view.supported)
        {
            set_notice("TRACKER UNAVAILABLE");
        }
        else if (s_tracker_view.recording)
        {
            port->stopTracker();
            set_notice("TRACK RECORDING STOPPED");
        }
        else
        {
            set_notice(port->startTracker() ? "TRACK RECORDING STARTED"
                                            : "TRACKER START FAILED");
        }
    }
    return true;
}

} // namespace ui::mono::screens::screen_240x320::detail
