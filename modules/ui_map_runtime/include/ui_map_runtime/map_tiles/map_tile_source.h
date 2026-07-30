#pragma once

#include "ui_map_runtime/map_tiles/map_tile_types.h"

#include <cstddef>
#include <cstdint>

namespace ui
{
namespace map_tiles
{

enum class MapTileReadStatus : uint8_t
{
    Ready,
    Missing,
    RetryLater,
    Error,
    Invalid,
};

struct MapTileReadResult
{
    MapTileReadStatus status = MapTileReadStatus::Error;
    std::size_t size = 0;
    int32_t error = -1;
    MapTileFormat format = MapTileFormat::Unknown;
};

class IMapTileSource
{
  public:
    virtual ~IMapTileSource() = default;

    virtual MapTileLookupResult lookup(const MapTileRef& ref) const = 0;

    virtual MapTileReadResult read(const MapTileRef& ref,
                                   uint8_t* buffer,
                                   std::size_t capacity) const = 0;
};

class IMapTileFileSystem
{
  public:
    virtual ~IMapTileFileSystem() = default;

    virtual bool exists(const char* path) const = 0;
    virtual bool isDirectory(const char* path) const = 0;
    virtual MapTileReadResult readFile(const char* path,
                                       uint8_t* buffer,
                                       std::size_t capacity) const = 0;
};

} // namespace map_tiles
} // namespace ui
