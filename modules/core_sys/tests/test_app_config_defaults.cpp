#include "app/app_config.h"

#include <cassert>
#include <cstring>

int main()
{
    const app::AppConfig config{};
    assert(config.mesh_protocol == chat::MeshProtocol::Reticulum);
    assert(config.reticulumConfig().reticulum_wifi_gateway_enabled);
    assert(config.reticulumConfig().reticulum_wifi_gateway_port == 4242);
    assert(!config.reticulumConfig().reticulum_allow_location_requests);
    assert(!config.meshcore_config.meshcore_mqtt_enabled);
    assert(config.meshcore_config.meshcore_mqtt_uplink_enabled);
    assert(config.meshcore_config.meshcore_mqtt_downlink_enabled);
    assert(std::strcmp(config.meshcore_config.meshcore_mqtt_host,
                       app::AppConfig::kDefaultMeshCoreMqttHost) == 0);
    assert(config.meshcore_config.meshcore_mqtt_port == 1883);
    assert(std::strcmp(config.meshcore_config.meshcore_mqtt_root,
                       app::AppConfig::kDefaultMeshCoreMqttRoot) == 0);
    assert(config.meshcore_config.meshcore_mqtt_username[0] == '\0');
    assert(config.meshcore_config.meshcore_mqtt_password[0] == '\0');
    return 0;
}
