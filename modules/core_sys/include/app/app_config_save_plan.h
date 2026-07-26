#pragma once

#include "app/app_config_change_detection.h"

namespace app
{

struct AppConfigSavePlan
{
    AppConfigChangeSet changes = AppConfigChangeSet::none();
    bool queue = false;
};

inline AppConfigSavePlan planAppConfigSave(const AppConfig& baseline,
                                           bool baseline_valid,
                                           const AppConfig& desired,
                                           bool save_busy,
                                           const AppConfig& active_save,
                                           AppConfigChangeSet active_changes,
                                           AppConfigChangeSet requested_changes)
{
    AppConfigSavePlan plan;
    if (!save_busy)
    {
        plan.changes = baseline_valid
                           ? detectAppConfigChanges(baseline, desired)
                           : AppConfigChangeSet::allPersisted();
        plan.changes.mergeIn(requested_changes);
        plan.queue = !plan.changes.empty();
        return plan;
    }

    plan.changes = detectAppConfigChanges(active_save, desired);
    if (!active_changes.containsAll(requested_changes))
    {
        plan.changes.mergeIn(requested_changes);
    }
    plan.queue = !plan.changes.empty();
    return plan;
}

} // namespace app
