#pragma once

#include "app/app_config.h"
#include "app/app_config_changes.h"

namespace app
{

bool loadAppConfig(AppConfig& config);
bool saveAppConfig(const AppConfig& config,
                   AppConfigChangeSet changes = AppConfigChangeSet::allPersisted());
uint8_t loadMessageToneVolume();

} // namespace app
