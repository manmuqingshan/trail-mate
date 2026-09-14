#include "platform/esp/arduino_common/map_poi/cjson_poi_parser.h"
#include "ui_map_runtime/map_poi/poi_snapshot_builder.h"
#include "ui_map_runtime/map_poi/poi_tile_source.h"
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <vector>

using namespace ui::map_poi;
using namespace ui::map_tiles;
using platform::esp::arduino_common::map_poi::CJsonPoiParser;
const std::string manifest = R"({"version":2,"index":{"scheme":"web-mercator-xyz","format":"jsonl","enabled_zoom_levels":[16,17,18],"include_labels":true}})";
const std::string row = u8R"({"id":"node/123","type":"water","name":"昆明饮水点","lat":25.04,"lon":102.7,"priority":90})";

struct Files final : IMapTileFileSystem
{
    std::map<std::string, std::string> data;
    mutable std::vector<std::string> reads;
    bool busy = false;
    bool exists(const char* p) const override { return data.count(p) != 0; }
    bool isDirectory(const char*) const override { return false; }
    MapTileReadResult readFile(const char* p, uint8_t* out, std::size_t cap) const override
    {
        reads.emplace_back(p);
        if (busy) return {MapTileReadStatus::RetryLater};
        auto it = data.find(p);
        if (it == data.end()) return {MapTileReadStatus::Missing};
        if (it->second.size() > cap) return {MapTileReadStatus::Invalid};
        std::memcpy(out, it->second.data(), it->second.size());
        return {MapTileReadStatus::Ready, it->second.size(), 0};
    }
};

void parser_contract()
{
    CJsonPoiParser parser;
    Policy policy;
    assert(parser.manifest(manifest.data(), manifest.size(), policy));
    assert(policy.enabled(16) && policy.enabled(18) && !policy.enabled(15));
    for (const auto& levels : {std::string("[]"), std::string("[10,12,15]")})
    {
        auto json = manifest;
        json.replace(json.find("[16,17,18]"), 10, levels);
        assert(parser.manifest(json.data(), json.size(), policy));
        assert(!policy.enabled(16));
    }
    const std::string legacy = R"({"version":1,"index":{"scheme":"web-mercator-xyz","format":"jsonl","min_zoom":15,"max_zoom":18}})";
    assert(parser.manifest(legacy.data(), legacy.size(), policy) && policy.enabled(15));
    auto bad = legacy;
    bad[bad.find(":1") + 1] = '2';
    assert(!parser.manifest(bad.data(), bad.size(), policy));
    Record record;
    assert(parser.record(row.data(), row.size(), record));
    assert(std::strcmp(record.name, u8"昆明饮水点") == 0 && record.priority == 90);
    const std::string compact = R"({"id":"n1","t":"camp","n":"camp","lat":25,"lon":102,"p":80})";
    assert(parser.record(compact.data(), compact.size(), record) && record.priority == 80);
    for (const auto& invalid : {row + "garbage", std::string("{\"x\":[[[[[]]]]]}"), std::string("{\"id\":\"n\",\"type\":\"water\",\"lat\":91,\"lon\":1}")})
        assert(!parser.record(invalid.data(), invalid.size(), record));
}

void source_contract()
{
    Files files;
    CJsonPoiParser parser;
    files.data["/maps/poi/manifest.json"] = manifest;
    files.data["/maps/poi/index/16/1/2.jsonl"] = row + "\r\nnot-json\n" + row;
    PoiTileSource source(files, parser);
    std::vector<uint64_t> storage(192 * 1024 / 8);
    auto* bytes = reinterpret_cast<uint8_t*>(storage.data());
    auto size = storage.size() * 8;
    auto result = source.read({MapTileLayer::Poi, 15, 1, 2}, bytes, size);
    assert(validPayload(bytes, result.size) && files.reads.size() == 1);
    assert(reinterpret_cast<TileHeader*>(bytes)->count == 0);
    result = source.read({MapTileLayer::Poi, 16, 1, 2}, bytes, size);
    assert(validPayload(bytes, result.size));
    assert(result.size == sizeof(TileHeader) + 2 * sizeof(Record));
    assert(reinterpret_cast<TileHeader*>(bytes)->invalid_rows == 1);
    assert(std::strcmp(payloadRecords(bytes)[0].name, u8"昆明饮水点") == 0);
    files.data["/maps/poi/index/16/1/2.jsonl"].clear();
    for (int i = 0; i < 205; ++i) files.data["/maps/poi/index/16/1/2.jsonl"] += row + "\n";
    result = source.read({MapTileLayer::Poi, 16, 1, 2}, bytes, size);
    assert(validPayload(bytes, result.size));
    assert(reinterpret_cast<TileHeader*>(bytes)->count == 200 && reinterpret_cast<TileHeader*>(bytes)->truncated);
    files.busy = true;
    source.reset();
    assert(source.read({MapTileLayer::Poi, 16, 1, 2}, bytes, size).status == MapTileReadStatus::RetryLater);
    files.busy = false;
    files.data.erase("/maps/poi/manifest.json");
    files.reads.clear();
    result = source.read({MapTileLayer::Poi, 16, 1, 2}, bytes, size);
    assert(validPayload(bytes, result.size) && files.reads.size() == 1 && !reinterpret_cast<TileHeader*>(bytes)->manifest_valid);
    assert(source.read({MapTileLayer::Osm, 16, 1, 2}, bytes, size).status == MapTileReadStatus::Invalid);
}

void snapshot_contract()
{
    std::vector<ui::map::MapPoiItem> items(2);
    ui::map::MapPoiSnapshot out;
    out.items = items.data();
    out.capacity = items.size();
    SnapshotBuilder builder(out, 240, 135);
    Record r;
    std::strcpy(r.id, "a");
    r.priority = 50;
    builder.add(r, 10, 10);
    builder.add(r, 100, 100); // ID deduplication
    std::strcpy(r.id, "b");
    r.priority = 70;
    builder.add(r, 100, 100);
    std::strcpy(r.id, "c");
    r.priority = 90;
    builder.add(r, 200, 100);
    std::strcpy(r.id, "d");
    r.priority = 100;
    builder.add(r, -1, 100);
    builder.finish();
    assert(out.item_count == 2 && out.truncated);
    assert(items[0].priority == 90 && items[1].priority == 70);
    SnapshotBuilder collision(out, 240, 135);
    std::strcpy(r.id, "a");
    collision.add(r, 50, 50);
    std::strcpy(r.id, "b");
    collision.add(r, 55, 55);
    collision.finish();
    assert(out.item_count == 1);
}

int main(int argc, char** argv)
{
    parser_contract();
    source_contract();
    snapshot_contract();
    if (argc > 1)
    {
        CJsonPoiParser parser;
        Policy policy;
        Record record;
        const std::filesystem::path root(argv[1]);
        std::ifstream input(root / "manifest.json");
        std::stringstream json;
        json << input.rdbuf();
        auto text = json.str();
        assert(parser.manifest(text.data(), text.size(), policy));
        assert(!policy.enabled(15) && policy.enabled(16) && policy.enabled(17) && policy.enabled(18));
        std::size_t rows = 0, files = 0;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(root / "index"))
        {
            if (entry.path().extension() != ".jsonl") continue;
            std::ifstream stream(entry.path());
            std::string line;
            std::size_t count = 0;
            while (std::getline(stream, line))
            {
                assert(parser.record(line.data(), line.size(), record));
                ++rows;
                ++count;
            }
            assert(count <= TileHeader::kMaxRecords);
            ++files;
        }
        assert(files > 0);
        std::cout << "Package files=" << files << " rows=" << rows << '\n';
    }
    std::cout << "POI parser, source, bounds and snapshot contracts passed\n";
}
