#pragma once

#include <vector>

namespace ui::map_tiles
{

struct MapTileCoordinate
{
    int x = 0;
    int y = 0;
};

/**
 * Canonical Web-Mercator tile geometry shared by every map renderer.
 *
 * The ESP32 map widget is the source of truth for longitude wrapping,
 * latitude clamping, and neighbour generation. Desktop renderers must use
 * these helpers instead of reimplementing tile math locally.
 */
void normalizeTile(int zoom, int& x, int& y) noexcept;
void latLngToTile(double lat,
                  double lon,
                  int zoom,
                  int& tile_x,
                  int& tile_y) noexcept;

std::vector<MapTileCoordinate> tilesAround(double lat,
                                           double lon,
                                           int zoom,
                                           int radius_x,
                                           int radius_y);

} // namespace ui::map_tiles
