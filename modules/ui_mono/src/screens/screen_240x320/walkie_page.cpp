#include "screen_app_internal.h"

#include "ui/mono/screens/screen_240x320/runtime_feature_port.h"

namespace ui::mono::screens::screen_240x320::detail
{
namespace
{

::ui::mono::screens::screen_240x320::WalkieView s_walkie_view;

} // namespace

void render_walkie()
{
    ::ui::mono::screens::screen_240x320::RuntimeFeaturePort* const port =
        ::ui::mono::screens::screen_240x320::runtimeFeaturePort();
    s_walkie_view = {};
    if (port != nullptr)
    {
        port->readWalkie(s_walkie_view);
    }

    set_linef(0, "WALKIE %s",
              s_walkie_view.supported
                  ? (s_walkie_view.active ? "ACTIVE" : "READY")
                  : "UNAVAILABLE");
    set_linef(1, "MONITOR %s", s_walkie_view.monitor_enabled ? "ON" : "OFF");
    set_linef(2, "TX %s  RX %u", s_walkie_view.tx ? "ON" : "OFF",
              static_cast<unsigned>(s_walkie_view.rx_level));
    set_linef(3, "FREQ %.3fMHz", static_cast<double>(s_walkie_view.frequency_mhz));
    set_line(4, s_state.notice[0] != '\0' ? s_state.notice : "START / MONITOR CONTROLS");
    clear_lines_from(5);
}

void add_walkie_actions()
{
    add_action("START/STOP", Action::ToggleWalkie, kMargin, kActionTop, 112);
    add_action("MONITOR", Action::ToggleWalkieMonitor, 126, kActionTop, 106);
}

bool handle_walkie_action(Action action)
{
    ::ui::mono::screens::screen_240x320::RuntimeFeaturePort* const port =
        ::ui::mono::screens::screen_240x320::runtimeFeaturePort();
    if (port == nullptr)
    {
        if (action == Action::ToggleWalkie || action == Action::ToggleWalkieMonitor)
        {
            set_notice("WALKIE UNAVAILABLE");
            return true;
        }
        return false;
    }

    port->readWalkie(s_walkie_view);
    if (action == Action::ToggleWalkie)
    {
        if (!s_walkie_view.supported)
        {
            set_notice("WALKIE UNAVAILABLE");
        }
        else if (s_walkie_view.active)
        {
            port->stopWalkie();
            set_notice("WALKIE STOPPED");
        }
        else
        {
            set_notice(port->startWalkie() ? "WALKIE STARTED" : "WALKIE START FAILED");
        }
        return true;
    }
    if (action == Action::ToggleWalkieMonitor)
    {
        set_notice(s_walkie_view.supported &&
                           port->setWalkieMonitorEnabled(!s_walkie_view.monitor_enabled)
                       ? (!s_walkie_view.monitor_enabled ? "MONITOR ON" : "MONITOR OFF")
                       : "MONITOR CHANGE FAILED");
        return true;
    }
    return false;
}

} // namespace ui::mono::screens::screen_240x320::detail
