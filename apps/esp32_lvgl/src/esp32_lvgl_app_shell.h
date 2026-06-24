#pragma once

#include "product_composition/target_profile.h"

namespace trailmate
{
namespace apps
{
namespace esp32_lvgl
{

struct Esp32LvglAppShellConfig
{
    const char* target_id = "tab5";
    const char* target_family = "esp32_lvgl";
    const char* default_ux_pack_id = "compatibility";
    const char* build_entrypoint = "builds/esp_idf";
    const char* component_sources = "builds/esp_idf/ESP_IDF_COMPONENT_SOURCES.cmake";
};

class Esp32LvglAppShell
{
  public:
    const Esp32LvglAppShellConfig& config() const;
    const char* targetId() const;
    const product_composition::TargetProfile* targetProfile() const;
    const char* activeUxPackId() const;
    bool validate() const;

  private:
    Esp32LvglAppShellConfig config_{};
};

} // namespace esp32_lvgl
} // namespace apps
} // namespace trailmate
