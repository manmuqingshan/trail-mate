#include "platform/esp/arduino_common/map_poi/cjson_poi_parser.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

int main(int argc, char** argv)
{
    if (argc != 2) return 2;
    const std::filesystem::path root(argv[1]);
    platform::esp::arduino_common::map_poi::CJsonPoiParser parser;
    ui::map_poi::Policy policy;
    std::ifstream manifest(root / "manifest.json");
    std::stringstream source;
    source << manifest.rdbuf();
    const auto json = source.str();
    if (!parser.manifest(json.data(), json.size(), policy) || policy.schema_version != 3) return 3;
    for (int z = 1; z <= 18; ++z)
        if (!policy.enabled(z)) return 4;
    std::size_t files = 0, rows = 0, roads = 0, places = 0, pois = 0;
    ui::map_poi::Record record;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root / "index"))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".jsonl") continue;
        std::ifstream input(entry.path());
        std::string line;
        std::size_t count = 0;
        while (std::getline(input, line))
        {
            if (!parser.record(line.data(), line.size(), record) || !record.explicit_kind)
            {
                std::cerr << entry.path() << ':' << count + 1 << " invalid annotation\n";
                return 5;
            }
            ++count;
            ++rows;
            if (record.kind == ui::map::AnnotationKind::Road) ++roads;
            else if (record.kind == ui::map::AnnotationKind::Place) ++places;
            else ++pois;
        }
        if (count > ui::map_poi::TileHeader::kMaxRecords) return 6;
        ++files;
    }
    std::cout << "files=" << files << " rows=" << rows << " roads=" << roads << " places=" << places << " pois=" << pois << '\n';
    return roads && places && pois ? 0 : 7;
}
