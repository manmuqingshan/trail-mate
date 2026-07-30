#include "ui_map_runtime/map_tiles/filesystem_map_tile_source.h"

#include <cassert>
#include <cstring>
#include <string>
#include <vector>

namespace
{

class FakeFileSystem final : public ui::map_tiles::IMapTileFileSystem
{
  public:
    std::vector<std::string> files;
    std::vector<std::string> dirs;

    bool exists(const char* path) const override
    {
        return contains(files, path);
    }

    bool isDirectory(const char* path) const override
    {
        return contains(dirs, path);
    }

    ui::map_tiles::MapTileReadResult readFile(
        const char* path,
        uint8_t* buffer,
        std::size_t capacity) const override
    {
        if (!exists(path) || capacity < 3 || buffer == nullptr)
        {
            return {ui::map_tiles::MapTileReadStatus::Missing, 0, -1};
        }
        buffer[0] = 1;
        buffer[1] = 2;
        buffer[2] = 3;
        return {ui::map_tiles::MapTileReadStatus::Ready, 3, 0};
    }

  private:
    static bool contains(const std::vector<std::string>& values, const char* path)
    {
        if (path == nullptr)
        {
            return false;
        }
        for (const auto& value : values)
        {
            if (value == path)
            {
                return true;
            }
        }
        return false;
    }
};

void test_lookup_and_read()
{
    FakeFileSystem fs;
    fs.files.push_back("/sd/maps/base/osm/4/8/6.png");

    ui::map_tiles::FilesystemMapTileSource source(fs, "/sd");
    ui::map_tiles::MapTileRef ref{};
    ref.layer = ui::map_tiles::MapTileLayer::Osm;
    ref.z = 4;
    ref.x = 8;
    ref.y = 6;

    const auto hit = source.lookup(ref);
    assert(hit.status == ui::map_tiles::MapTileStatus::Available);
    assert(hit.format == ui::map_tiles::MapTileFormat::Png);

    uint8_t buffer[4]{};
    const auto read_result = source.read(ref, buffer, sizeof(buffer));
    assert(read_result.status == ui::map_tiles::MapTileReadStatus::Ready);
    assert(read_result.size == 3);
    assert(read_result.format == ui::map_tiles::MapTileFormat::Png);
    assert(buffer[0] == 1 && buffer[1] == 2 && buffer[2] == 3);

    ref.y = 7;
    const auto miss = source.lookup(ref);
    assert(miss.status == ui::map_tiles::MapTileStatus::Missing);
    const auto missing_read = source.read(ref, buffer, sizeof(buffer));
    assert(missing_read.status == ui::map_tiles::MapTileReadStatus::Missing);
}

void test_directories()
{
    FakeFileSystem fs;
    fs.dirs.push_back("A:/maps/base/terrain");
    fs.dirs.push_back("A:/maps/contour/minor-20");

    ui::map_tiles::FilesystemMapTileSource source(fs, "A:");
    assert(source.layerDirectoryAvailable(ui::map_tiles::MapTileLayer::Terrain));
    assert(!source.layerDirectoryAvailable(ui::map_tiles::MapTileLayer::Satellite));
    assert(source.layerDirectoryAvailable(ui::map_tiles::MapTileLayer::ContourMinor20));
    assert(source.anyContourDirectoryAvailable());
}

void test_resolve_path()
{
    FakeFileSystem fs;
    ui::map_tiles::FilesystemMapTileSource source(fs, "A:");

    ui::map_tiles::MapTileRef ref{};
    ref.layer = ui::map_tiles::MapTileLayer::Satellite;
    ref.z = 9;
    ref.x = 82;
    ref.y = 190;

    char path[160]{};
    assert(source.resolvePath(ref, path, sizeof(path)));
    assert(std::strcmp(path, "A:/maps/base/satellite/9/82/190.png") == 0);
}

} // namespace

int main()
{
    test_lookup_and_read();
    test_directories();
    test_resolve_path();
    return 0;
}
