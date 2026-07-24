#include "ui_map_runtime/map_tiles/map_tile_geometry.h"

#include <algorithm>
#include <cmath>

namespace ui::map_tiles
{
namespace
{

constexpr double kPi = 3.14159265358979323846;
constexpr double kMaxMercatorLat = 85.05112878;
constexpr int kMaxZoom = 19;

double clampLatitude(double lat) noexcept
{
    return std::clamp(lat, -kMaxMercatorLat, kMaxMercatorLat);
}

} // namespace

void normalizeTile(int zoom, int& x, int& y) noexcept
{
    zoom = std::clamp(zoom, 0, kMaxZoom);
    const int world_tiles = 1 << zoom;
    if (world_tiles <= 0)
    {
        x = 0;
        y = 0;
        return;
    }

    x %= world_tiles;
    if (x < 0)
    {
        x += world_tiles;
    }
    y = std::clamp(y, 0, world_tiles - 1);
}

void latLngToTile(double lat,
                  double lon,
                  int zoom,
                  int& tile_x,
                  int& tile_y) noexcept
{
    zoom = std::clamp(zoom, 0, kMaxZoom);
    const double world_tiles = static_cast<double>(1U << zoom);
    const double clamped_lat = clampLatitude(lat);
    const double lat_rad = clamped_lat * kPi / 180.0;
    const double x = (lon + 180.0) / 360.0 * world_tiles;
    const double y =
        (1.0 - std::log(std::tan(lat_rad) + 1.0 / std::cos(lat_rad)) / kPi) /
        2.0 * world_tiles;

    tile_x = static_cast<int>(std::floor(x));
    tile_y = static_cast<int>(std::floor(y));
    normalizeTile(zoom, tile_x, tile_y);
}

std::vector<MapTileCoordinate> tilesAround(double lat,
                                           double lon,
                                           int zoom,
                                           int radius_x,
                                           int radius_y)
{
    zoom = std::clamp(zoom, 0, kMaxZoom);
    radius_x = std::clamp(radius_x, 0, 5);
    radius_y = std::clamp(radius_y, 0, 5);

    int center_x = 0;
    int center_y = 0;
    latLngToTile(lat, lon, zoom, center_x, center_y);

    std::vector<MapTileCoordinate> out;
    out.reserve(static_cast<std::size_t>((radius_x * 2 + 1) *
                                         (radius_y * 2 + 1)));
    for (int dy = -radius_y; dy <= radius_y; ++dy)
    {
        for (int dx = -radius_x; dx <= radius_x; ++dx)
        {
            MapTileCoordinate tile{center_x + dx, center_y + dy};
            normalizeTile(zoom, tile.x, tile.y);
            out.push_back(tile);
        }
    }
    return out;
}

} // namespace ui::map_tiles
