#include "screen_app_internal.h"

#include "ui/mono/screens/screen_240x320/runtime_feature_port.h"

namespace ui::mono::screens::screen_240x320::detail
{
namespace
{

::ui::mono::screens::screen_240x320::SstvView s_sstv_view;

const char* sstv_state_text(::ui::mono::screens::screen_240x320::SstvViewState state)
{
    using ::ui::mono::screens::screen_240x320::SstvViewState;
    switch (state)
    {
    case SstvViewState::Waiting:
        return "WAITING";
    case SstvViewState::Receiving:
        return "RECEIVING";
    case SstvViewState::Complete:
        return "COMPLETE";
    case SstvViewState::Error:
        return "ERROR";
    case SstvViewState::Idle:
    default:
        return "IDLE";
    }
}

} // namespace

void render_sstv()
{
    ::ui::mono::screens::screen_240x320::RuntimeFeaturePort* const port =
        ::ui::mono::screens::screen_240x320::runtimeFeaturePort();
    s_sstv_view = {};
    if (port != nullptr)
    {
        port->readSstv(s_sstv_view);
    }

    set_linef(0, "SSTV %s",
              s_sstv_view.supported
                  ? (s_sstv_view.state ==
                             ::ui::mono::screens::screen_240x320::SstvViewState::Idle
                         ? "READY"
                         : "ACTIVE")
                  : "UNAVAILABLE");
    set_linef(1, "STATE %s", sstv_state_text(s_sstv_view.state));
    set_linef(2, "MODE %s", s_sstv_view.mode[0] != '\0' ? s_sstv_view.mode : "--");
    set_linef(3, "PROGRESS %.0f%%  LINE %u",
              static_cast<double>(s_sstv_view.progress * 100.0F),
              static_cast<unsigned>(s_sstv_view.line));
    set_linef(4, "IMAGE %s", s_sstv_view.has_image ? "READY" : "--");
    set_linef(5, "PATH %s",
              s_sstv_view.last_saved_path[0] != '\0' ? s_sstv_view.last_saved_path : "--");
    set_line(6, s_state.notice[0] != '\0' ? s_state.notice : "START / STOP RECEIVER");
    clear_lines_from(7);
}

void add_sstv_actions()
{
    add_action("START/STOP", Action::ToggleSstv, kMargin, kActionTop, 112);
}

bool handle_sstv_action(Action action)
{
    if (action != Action::ToggleSstv)
    {
        return false;
    }

    ::ui::mono::screens::screen_240x320::RuntimeFeaturePort* const port =
        ::ui::mono::screens::screen_240x320::runtimeFeaturePort();
    if (port == nullptr)
    {
        set_notice("SSTV UNAVAILABLE");
        return true;
    }
    port->readSstv(s_sstv_view);
    if (!s_sstv_view.supported)
    {
        set_notice("SSTV UNAVAILABLE");
    }
    else if (s_sstv_view.state !=
             ::ui::mono::screens::screen_240x320::SstvViewState::Idle)
    {
        port->stopSstv();
        set_notice("SSTV STOPPED");
    }
    else
    {
        set_notice(port->startSstv() ? "SSTV STARTED" : "SSTV START FAILED");
    }
    return true;
}

} // namespace ui::mono::screens::screen_240x320::detail
