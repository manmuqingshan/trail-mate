#pragma once
#include "ui_map_runtime/map_poi/poi_types.h"

namespace platform::esp::arduino_common::map_poi
{
class CJsonPoiParser final : public ::ui::map_poi::Parser
{
  public:
    bool manifest(const char* json, std::size_t size, ::ui::map_poi::Policy& out) const override;
    bool record(const char* json, std::size_t size, ::ui::map_poi::Record& out) const override;
};
} // namespace platform::esp::arduino_common::map_poi
