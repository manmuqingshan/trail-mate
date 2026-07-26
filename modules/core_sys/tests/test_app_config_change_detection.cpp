#include "app/app_config_change_detection.h"

#include <cassert>
#include <cstring>

int main()
{
    const app::AppConfig baseline{};

    app::AppConfig map_changed = baseline;
    map_changed.map_source = 2;
    app::AppConfigChangeSet changes =
        app::detectAppConfigChanges(baseline, map_changed);
    assert(changes.contains(app::AppConfigChangeDomain::Map));
    assert(!changes.contains(app::AppConfigChangeDomain::Mesh));
    assert(!changes.contains(app::AppConfigChangeDomain::Gps));

    app::AppConfig protocol_changed = baseline;
    protocol_changed.mesh_protocol = chat::MeshProtocol::MeshCore;
    changes = app::detectAppConfigChanges(baseline, protocol_changed);
    assert(changes.contains(app::AppConfigChangeDomain::Mesh));
    assert(!changes.contains(app::AppConfigChangeDomain::Map));

    app::AppConfig channel_changed = baseline;
    channel_changed.primary_channel_position_precision = 16;
    changes = app::detectAppConfigChanges(baseline, channel_changed);
    assert(changes.contains(app::AppConfigChangeDomain::Channels));
    assert(!changes.contains(app::AppConfigChangeDomain::Mesh));

    app::AppConfig reticulum_runtime_only = baseline;
    const char runtime_only_name[] = "runtime-only";
    std::memset(reticulum_runtime_only.reticulumConfig().reticulum_groups[0].name,
                0,
                sizeof(reticulum_runtime_only.reticulumConfig().reticulum_groups[0].name));
    std::memcpy(reticulum_runtime_only.reticulumConfig().reticulum_groups[0].name,
                runtime_only_name,
                sizeof(runtime_only_name) - 1);
    changes = app::detectAppConfigChanges(baseline, reticulum_runtime_only);
    assert(changes.empty());

    app::AppConfig aprs_changed = baseline;
    aprs_changed.aprs.enabled = true;
    changes = app::detectAppConfigChanges(baseline, aprs_changed);
    assert(changes.contains(app::AppConfigChangeDomain::Aprs));
    assert(!changes.contains(app::AppConfigChangeDomain::Map));

    return 0;
}
