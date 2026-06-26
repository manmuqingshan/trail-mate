/**
 * @file settings_state.cpp
 * @brief Settings UI state storage
 */

#include "ui/screens/settings/settings_state.h"

#if defined(ESP_PLATFORM)
#include "esp_attr.h"
#define UI_SETTINGS_STATE_RAM_ATTR EXT_RAM_ATTR
#else
#define UI_SETTINGS_STATE_RAM_ATTR
#endif

namespace settings::ui
{

UI_SETTINGS_STATE_RAM_ATTR SettingsData g_settings{};
UI_SETTINGS_STATE_RAM_ATTR UiState g_state{};

} // namespace settings::ui
