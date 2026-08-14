#include "screen_app_internal.h"

#include "ui/mono/screens/screen_240x320/protocol_probe_port.h"

namespace ui::mono::screens::screen_240x320::detail
{
namespace
{

struct ProtocolProbePageState
{
    enum class Route : unsigned char
    {
        Overview,
        CandidateDetail,
        ApplyConfirm,
    };

    ::ui::mono::screens::screen_240x320::ProtocolProbeView snapshot{};
    Route route = Route::Overview;
};

ProtocolProbePageState s_protocol_probe_page_state;

} // namespace

void reset_protocol_probe_page_state()
{
    s_protocol_probe_page_state = ProtocolProbePageState{};
}

void render_protocol_probe()
{
    ::ui::mono::screens::screen_240x320::ProtocolProbePort* const port =
        ::ui::mono::screens::screen_240x320::protocolProbePort();
    if (port == nullptr || !port->read(s_protocol_probe_page_state.snapshot))
    {
        set_line(0, "PROBE RUNTIME UNAVAILABLE");
        set_line(1, "CHECK RADIO SUPPORT");
        set_line(2, s_state.notice[0] != '\0' ? s_state.notice : "BACK TO RETURN");
        clear_lines_from(3);
        return;
    }

    const auto& probe = s_protocol_probe_page_state.snapshot;
    if (s_protocol_probe_page_state.route == ProtocolProbePageState::Route::CandidateDetail)
    {
        set_text(s_state.title, "PROBE");
        set_text(s_state.subtitle, "CANDIDATE");
        set_linef(0, "PROFILE %s", probe.selected_profile[0] != '\0' ? probe.selected_profile : "--");
        set_linef(1, "STEP %lu / %lu", static_cast<unsigned long>(probe.candidate_index + 1U), static_cast<unsigned long>(probe.candidate_count));
        set_linef(2, "OBSERVATIONS %lu", static_cast<unsigned long>(probe.observation_count));
        set_linef(3, "EVIDENCE %lu", static_cast<unsigned long>(probe.evidence_count));
        set_linef(4, "CRC FRAMES %lu", static_cast<unsigned long>(probe.crc_frame_count));
        set_line(5, probe.scanning ? "SCAN ACTIVE" : "SCAN STOPPED");
        set_line(6, probe.has_selection ? "APPLY OPENS CONFIRMATION" : "NO CANDIDATE SELECTED");
        set_line(7, s_state.notice[0] != '\0' ? s_state.notice : "PREV/NEXT SELECT CANDIDATE");
        clear_lines_from(8);
        return;
    }

    if (s_protocol_probe_page_state.route == ProtocolProbePageState::Route::ApplyConfirm)
    {
        set_text(s_state.title, "PROBE");
        set_text(s_state.subtitle, "CONFIRM");
        set_line(0, "APPLY RADIO PROFILE?");
        set_linef(1, "%s", probe.selected_profile[0] != '\0' ? probe.selected_profile : "--");
        set_line(2, "THIS CHANGES ACTIVE RADIO SETTINGS");
        set_line(3, "CANCEL KEEPS CURRENT PROFILE");
        set_line(4, s_state.notice[0] != '\0' ? s_state.notice : "CANCEL OR CONFIRM");
        clear_lines_from(5);
        return;
    }

    set_text(s_state.title, "PROBE");
    set_text(s_state.subtitle, "OVERVIEW");
    set_linef(0, "RADIO %s", probe.available ? "READY" : "UNAVAILABLE");
    set_linef(1, "STATE %s", probe.status);
    set_linef(2, "PROFILE %s", probe.current_profile[0] != '\0' ? probe.current_profile : "--");
    set_linef(3,
              "STEP %lu / %lu  PASS %lu",
              static_cast<unsigned long>(probe.candidate_index + 1U),
              static_cast<unsigned long>(probe.candidate_count),
              static_cast<unsigned long>(probe.completed_passes));
    set_linef(4,
              "FOUND %lu  EVIDENCE %lu",
              static_cast<unsigned long>(probe.observation_count),
              static_cast<unsigned long>(probe.evidence_count));
    set_linef(5, "CRC FRAMES %lu", static_cast<unsigned long>(probe.crc_frame_count));
    set_linef(6,
              "SELECTED %s",
              probe.selected_profile[0] != '\0' ? probe.selected_profile : "--");
    set_line(7, probe.scanning ? "RADIO SCANS; DISPLAY STAYS QUIET"
                               : "START TO PROBE KNOWN PROFILES");
    set_line(8, "SYNC UPDATES THIS PAPER VIEW");
    set_line(9,
             s_state.notice[0] != '\0'
                 ? s_state.notice
                 : (probe.has_selection ? "DETAIL OPENS SELECTED PROFILE"
                                        : "WAIT FOR EVIDENCE"));
}

void configure_protocol_probe_actions()
{
    if (s_state.adapter == nullptr || s_state.adapter->page_kind() != PageKind::ProtocolProbe ||
        s_state.action_count < 6)
    {
        return;
    }

    if (s_protocol_probe_page_state.route == ProtocolProbePageState::Route::ApplyConfirm)
    {
        set_action(0, "CANCEL", Action::ProbeCancel);
        set_action(1, "CONFIRM", Action::ProbeConfirm);
        for (size_t index = 0; index < 2; ++index)
        {
            set_action_visible(index, true);
        }
        for (size_t index = 2; index < s_state.action_count; ++index)
        {
            set_action_visible(index, false);
        }
        return;
    }
    if (s_protocol_probe_page_state.route == ProtocolProbePageState::Route::CandidateDetail)
    {
        set_action(0, "PREV", Action::ProbePrevious);
        set_action(1, "NEXT", Action::ProbeNext);
        set_action(2, "APPLY", Action::ProbeApply);
        set_action(3, "BACK", Action::Back);
        for (size_t index = 0; index < 4; ++index)
        {
            set_action_visible(index, true);
        }
        for (size_t index = 4; index < s_state.action_count; ++index)
        {
            set_action_visible(index, false);
        }
        return;
    }

    set_action(0, s_protocol_probe_page_state.snapshot.scanning ? "STOP" : "START", Action::ProbeStartStop);
    set_action(1, "PREV", Action::ProbePrevious);
    set_action(2, "NEXT", Action::ProbeNext);
    set_action(3, "DETAIL", Action::ProbeDetails);
    set_action(4, "SYNC", Action::ProbeSync);
    set_action(5, "BACK", Action::Back);
    for (size_t index = 0; index < 6; ++index)
    {
        set_action_visible(index, true);
    }
}

void add_protocol_probe_actions()
{
    add_action("START", Action::ProbeStartStop, kMargin, kActionTop, 70);
    add_action("PREV", Action::ProbePrevious, 84, kActionTop, 48);
    add_action("NEXT", Action::ProbeNext, 138, kActionTop, 48);
    add_action("DETAIL", Action::ProbeDetails, kMargin, kActionTop + 22, 70);
    add_action("SYNC", Action::ProbeSync, 84, kActionTop + 22, 48);
    add_action("BACK", Action::Back, 138, kActionTop + 22, 48);
}

bool handle_protocol_probe_action(Action action)
{
    ::ui::mono::screens::screen_240x320::ProtocolProbePort* const port =
        ::ui::mono::screens::screen_240x320::protocolProbePort();
    if (port == nullptr)
    {
        if (action == Action::ProbeStartStop || action == Action::ProbePrevious ||
            action == Action::ProbeNext || action == Action::ProbeApply ||
            action == Action::ProbeConfirm || action == Action::ProbeSync ||
            action == Action::ProbeDetails)
        {
            set_notice("PROBE RUNTIME UNAVAILABLE");
            return true;
        }
        return false;
    }

    if (s_protocol_probe_page_state.route == ProtocolProbePageState::Route::ApplyConfirm)
    {
        if (action == Action::Back || action == Action::ProbeCancel)
        {
            s_protocol_probe_page_state.route = ProtocolProbePageState::Route::CandidateDetail;
            set_notice("PROFILE APPLY CANCELLED");
            return true;
        }
        if (action == Action::ProbeConfirm)
        {
            s_protocol_probe_page_state.route = ProtocolProbePageState::Route::CandidateDetail;
            set_notice(port->applySelected() ? "PROFILE APPLIED" : "APPLY FAILED");
            return true;
        }
        return false;
    }

    if (s_protocol_probe_page_state.route == ProtocolProbePageState::Route::CandidateDetail &&
        action == Action::Back)
    {
        s_protocol_probe_page_state.route = ProtocolProbePageState::Route::Overview;
        set_notice("PROBE OVERVIEW");
        return true;
    }

    switch (action)
    {
    case Action::ProbeStartStop:
        if (s_protocol_probe_page_state.snapshot.scanning)
        {
            port->stop();
            set_notice("PROBE STOPPED");
        }
        else
        {
            set_notice(port->start() ? "PROBE STARTED" : "PROBE START FAILED");
        }
        return true;
    case Action::ProbePrevious:
    case Action::ProbeNext:
        if (port->selectObservationDelta(action == Action::ProbePrevious ? -1 : 1))
        {
            set_notice("PROFILE SELECTED");
        }
        else
        {
            set_notice("NO PROFILE EVIDENCE");
        }
        return true;
    case Action::ProbeApply:
        if (!s_protocol_probe_page_state.snapshot.has_selection)
        {
            set_notice("NO PROFILE SELECTED");
        }
        else
        {
            s_protocol_probe_page_state.route = ProtocolProbePageState::Route::ApplyConfirm;
            set_notice("CONFIRM PROFILE APPLY");
        }
        return true;
    case Action::ProbeDetails:
        if (!s_protocol_probe_page_state.snapshot.has_selection)
        {
            set_notice("NO PROFILE SELECTED");
            return true;
        }
        s_protocol_probe_page_state.route = ProtocolProbePageState::Route::CandidateDetail;
        set_notice("PROFILE DETAIL");
        return true;
    case Action::ProbeSync:
        set_notice("SNAPSHOT UPDATED");
        return true;
    default:
        return false;
    }
}

} // namespace ui::mono::screens::screen_240x320::detail
