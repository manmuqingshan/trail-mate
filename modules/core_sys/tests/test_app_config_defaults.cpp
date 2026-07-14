#include "app/app_config.h"

#include <cassert>

int main()
{
    const app::AppConfig config{};
    assert(config.mesh_protocol == chat::MeshProtocol::Reticulum);
    assert(config.reticulumConfig().reticulum_wifi_gateway_enabled);
    assert(config.reticulumConfig().reticulum_wifi_gateway_port == 4242);
    assert(config.reticulumConfig().reticulum_call_wire_profile ==
           chat::ReticulumCallWireProfile::SidebandLxst);
    assert(!config.reticulumConfig().reticulum_allow_location_requests);
    return 0;
}
