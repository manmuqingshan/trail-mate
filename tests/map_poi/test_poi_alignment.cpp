#include "platform/esp/arduino_common/map_poi/cjson_poi_parser.h"
#include "ui_map_runtime/map_poi/poi_tile_source.h"
#include <cassert>
#include <cstring>
#include <vector>

struct Files : ui::map_tiles::IMapTileFileSystem
{
    mutable unsigned reads = 0;
    bool exists(const char*) const override { return true; }
    bool isDirectory(const char*) const override { return false; }
    ui::map_tiles::MapTileReadResult readFile(const char*, uint8_t* out, std::size_t) const override
    {
        ++reads;
        const char* manifest = "{\"version\":2,\"index\":{\"scheme\":\"web-mercator-xyz\",\"format\":\"jsonl\",\"enabled_zoom_levels\":[]}}";
        std::memcpy(out, manifest, std::strlen(manifest));
        return {ui::map_tiles::MapTileReadStatus::Ready, std::strlen(manifest), 0};
    }
};
int main()
{
    Files files;
    platform::esp::arduino_common::map_poi::CJsonPoiParser parser;
    ui::map_poi::PoiTileSource source(files, parser);
    std::vector<uint64_t> storage(192 * 1024 / 8 + 1);
    auto* aligned = reinterpret_cast<uint8_t*>(storage.data());
    auto* fourAligned = aligned + 4;
    const ui::map_tiles::MapTileRef ref{ui::map_tiles::MapTileLayer::Poi, 12, 3216, 1753};
    auto rejected = source.read(ref, fourAligned, 192 * 1024);
    assert(rejected.status == ui::map_tiles::MapTileReadStatus::Invalid && rejected.error == -1);
    assert(files.reads == 0); // Same failure happens before reading the SD card.
    auto accepted = source.read(ref, aligned, 192 * 1024);
    assert(accepted.status == ui::map_tiles::MapTileReadStatus::Ready && files.reads == 1);
    assert(ui::map_poi::validPayload(aligned, accepted.size));
}
