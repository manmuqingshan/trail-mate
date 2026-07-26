#include "app/app_config_change_detection.h"
#include "app/app_config_save_plan.h"

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

    app::AppConfig contour_changed = baseline;
    contour_changed.map_contour_enabled = !baseline.map_contour_enabled;
    changes = app::detectAppConfigChanges(baseline, contour_changed);
    assert(changes.contains(app::AppConfigChangeDomain::Map));
    assert(!changes.contains(app::AppConfigChangeDomain::Channels));

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

    app::AppConfig max_channels_changed = baseline;
    max_channels_changed.chat_policy.max_channels = 4;
    changes = app::detectAppConfigChanges(baseline, max_channels_changed);
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

    app::AppConfigChangeSet merged = app::AppConfigChangeSet::map();
    merged.mergeIn(app::AppConfigChangeSet::gps());
    assert(merged.containsAll(app::AppConfigChangeSet::map()));
    assert(merged.containsAll(app::AppConfigChangeSet::gps()));
    assert(!merged.containsAll(app::AppConfigChangeSet::mesh()));

    app::AppConfigSavePlan unchanged_plan =
        app::planAppConfigSave(baseline,
                               true,
                               baseline,
                               false,
                               baseline,
                               app::AppConfigChangeSet::none(),
                               app::AppConfigChangeSet::none());
    assert(!unchanged_plan.queue);
    assert(unchanged_plan.changes.empty());

    app::AppConfigSavePlan busy_follow_up_plan =
        app::planAppConfigSave(protocol_changed,
                               true,
                               baseline,
                               true,
                               protocol_changed,
                               app::AppConfigChangeSet::mesh(),
                               app::AppConfigChangeSet::none());
    assert(busy_follow_up_plan.queue);
    assert(busy_follow_up_plan.changes.contains(app::AppConfigChangeDomain::Mesh));

    app::AppConfigSavePlan changed_while_busy_plan =
        app::planAppConfigSave(protocol_changed,
                               true,
                               map_changed,
                               true,
                               protocol_changed,
                               app::AppConfigChangeSet::mesh(),
                               app::AppConfigChangeSet::none());
    assert(changed_while_busy_plan.queue);
    assert(changed_while_busy_plan.changes.contains(app::AppConfigChangeDomain::Map));

    app::AppConfigSavePlan invalid_baseline_plan =
        app::planAppConfigSave(baseline,
                               false,
                               baseline,
                               false,
                               baseline,
                               app::AppConfigChangeSet::none(),
                               app::AppConfigChangeSet::none());
    assert(invalid_baseline_plan.queue);
    assert(invalid_baseline_plan.changes.containsAll(
        app::AppConfigChangeSet::allPersisted()));

    return 0;
}
