#pragma once
#include "ui_map_runtime/map_poi/poi_types.h"
#include "ui_presentation/map/map_poi_snapshot.h"

namespace ui::map_poi
{
class SnapshotBuilder
{
  public:
    SnapshotBuilder(ui::map::MapPoiSnapshot& out, int width, int height);
    void add(const Record& record, int x, int y);
    void finish();

  private:
    ui::map::MapPoiSnapshot& out_;
    int width_;
    int height_;
    bool better(uint8_t priority, int x, int y, const char* id, const ui::map::MapPoiItem& other) const;
};
} // namespace ui::map_poi
