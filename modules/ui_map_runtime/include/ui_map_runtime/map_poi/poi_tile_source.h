#pragma once

#include "ui_map_runtime/map_poi/poi_types.h"
#include "ui_map_runtime/map_tiles/map_tile_resolver.h"
#include "ui_map_runtime/map_tiles/map_tile_source.h"

namespace ui::map_poi
{
// Worker-owned source. Uses the same filesystem boundary as raster tiles;
// returns typed POI data so the UI does no JSON parsing or file access.
class PoiTileSource final : public map_tiles::IMapTileSource
{
  public:
    PoiTileSource(map_tiles::IMapTileFileSystem& files, const Parser& parser, const char* root = "/");
    map_tiles::MapTileLookupResult lookup(const map_tiles::MapTileRef& ref) const override;
    map_tiles::MapTileReadResult read(const map_tiles::MapTileRef& ref, uint8_t* buffer, std::size_t capacity) const override;
    void reset();

  private:
    map_tiles::IMapTileFileSystem& files_;
    const Parser& parser_;
    const char* root_; // Borrowed; the root string must outlive this source.
    mutable bool policy_checked_ = false;
    mutable bool policy_valid_ = false;
    mutable Policy policy_{};
};
static_assert(sizeof(PoiTileSource) <= 64, "POI source must not retain path or JSON buffers");
} // namespace ui::map_poi
