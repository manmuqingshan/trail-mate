#include "screen_app_internal.h"

namespace ui::mono::screens::screen_240x320::detail
{

const char* state_text(ui::device::CapabilityDisplayState state)
{
    switch (state)
    {
    case ui::device::CapabilityDisplayState::Off:
        return "OFF";
    case ui::device::CapabilityDisplayState::Starting:
        return "STARTING";
    case ui::device::CapabilityDisplayState::Ready:
        return "READY";
    case ui::device::CapabilityDisplayState::Degraded:
        return "DEGRADED";
    case ui::device::CapabilityDisplayState::Error:
        return "ERROR";
    case ui::device::CapabilityDisplayState::Simulated:
        return "SIMULATED";
    case ui::device::CapabilityDisplayState::Unsupported:
    default:
        return "--";
    }
}

void render_network()
{
    ::ui::device::DeviceStatusSnapshot device{};
    ::ui::mesh::MeshStatusSnapshot mesh{};
    (void)::ui::presentation_sources::runtime_device_status_source().buildDeviceStatusSnapshot(device);
    (void)::ui::presentation_sources::runtime_mesh_status_source().buildMeshStatusSnapshot(mesh);

    set_linef(0, "PROTOCOL %s", device.active_protocol.c_str());
    set_linef(1, "REGION %s", device.region.c_str());
    set_linef(2, "MODEM %s", device.modem_preset.c_str());
    set_linef(3, "LORA %s", state_text(device.lora));
    set_linef(4, "RADIO %s", mesh.radio_label.c_str());
    set_linef(5, "NODES %lu", static_cast<unsigned long>(mesh.known_nodes));
    set_linef(6, "UNREAD %lu", static_cast<unsigned long>(mesh.unread_messages));
    set_linef(7, "TEAM %s", mesh.team_linked ? "LINKED" : "--");
    set_line(8, s_state.notice[0] != '\0' ? s_state.notice : mesh.status_line.c_str());
    clear_lines_from(9);
}

void add_network_actions()
{
    add_action("REFRESH", Action::Refresh, kMargin, kActionTop, 86);
}

} // namespace ui::mono::screens::screen_240x320::detail
